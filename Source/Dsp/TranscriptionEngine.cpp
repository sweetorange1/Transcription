#include "TranscriptionEngine.h"
#include <algorithm>
#include <cmath>

namespace transcription
{
namespace
{
constexpr double pi = 3.14159265358979323846;
bool validChannel(int channel) noexcept { return channel >= 1 && channel <= 16; }
float smoothstep(float a, float b, float x) noexcept
{
    if (b <= a) return x >= b ? 1.0f : 0.0f;
    const float t = std::clamp((x - a) / (b - a), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}
}

float Engine::Voice::nextEnvelope() noexcept
{
    switch (stage)
    {
        case Stage::attack:
            envelope += attackStep;
            if (envelope >= 1.0f) { envelope = 1.0f; stage = Stage::decay; }
            break;
        case Stage::decay:
            envelope = sustain + (envelope - sustain) * decayMultiplier;
            if (std::abs(envelope - sustain) < 0.0001f) stage = Stage::sustain;
            break;
        case Stage::sustain:
            envelope += (sustain - envelope) * 0.002f;
            break;
        case Stage::release:
            envelope *= releaseMultiplier;
            if (envelope < 0.00001f) { envelope = 0.0f; stage = Stage::idle; }
            break;
        default: envelope = 0.0f; break;
    }
    return envelope;
}
float Engine::Voice::noise() noexcept
{
    random ^= random << 13; random ^= random >> 17; random ^= random << 5;
    const float white = static_cast<float>(random >> 8) / 8388608.0f - 1.0f;
    // 一阶泄漏积分 → 布朗噪声（-6 dB/oct）。极点 0.995 在 48 kHz 下约 38 Hz 转折，
    // 既保留低频堆积，又不会让次声漂移；输出再归一化到接近原白噪声的 RMS。
    brown = brown * 0.995f + white * 0.05f;
    return std::clamp(brown * 1.5f, -1.0f, 1.0f);
}
void Engine::Voice::release() noexcept
{
    keyDown = false;
    if (stage != Stage::idle) stage = Stage::release;
}

void Engine::prepare(double rate) noexcept
{
    sampleRate = std::isfinite(rate) && rate >= 8000.0 && rate <= 384000.0 ? rate : 44100.0;
    smoothing = static_cast<float>(1.0 - std::exp(-1.0 / (sampleRate * 0.004)));
    dcPole = static_cast<float>(std::exp(-2.0 * pi * 2.0 / sampleRate));
    stealSamples = static_cast<int>(sampleRate * 0.005);
    agcAttack = static_cast<float>(1.0 - std::exp(-1.0 / (sampleRate * 0.2)));
    agcRelease = static_cast<float>(1.0 - std::exp(-1.0 / (sampleRate * 0.2)));   // 对称慢速，追踪平均能量而非窄带峰值
    agcSmooth = static_cast<float>(1.0 - std::exp(-1.0 / (sampleRate * 0.05)));
    for (auto& voice : voices) voice.core.prepare(sampleRate);
    body.prepare(sampleRate);
    reset();
}
void Engine::reset() noexcept
{
    voices = {};
    for (auto& voice : voices) voice.core.prepare(sampleRate);
    body.reset();
    pedal.fill(false); bend.fill(0.0f); wheel.fill(0.0f);
    volume.fill(1.0f); expression.fill(1.0f);
    dcInput.fill(0.0f); dcOutput.fill(0.0f);
    refreshCountdown = 0; serial = 0; lastPeak = 0.0f;
    // AGC 从中性能量起步，避免下一次触发时 gain 从 targetRms/√0 顶到上限、起音爆出大音量。
    rmsFollower = 2.0f * targetRms * targetRms; autoGain = 1.0f;
}
void Engine::setTranscript(const Transcript& next) noexcept
{
    transcript = next;
    body.configure(transcript);
    refreshCountdown = 0;
}
void Engine::setScanPhase(float phase) noexcept
{
    scanPhase = std::isfinite(phase) ? std::clamp(phase, 0.0f, 1.0f) : 1.0f;
}
void Engine::refresh(Voice& voice, bool snap) noexcept
{
    const auto ch = static_cast<size_t>(voice.channel);
    voice.f0 = 440.0f * std::pow(2.0f, (static_cast<float>(voice.note - 69) + bend[ch] * 2.0f) / 12.0f);
    const auto& env = transcript.envelope;
    voice.attackStep = 1.0f / (std::max(0.002f, env.attack) * static_cast<float>(sampleRate));
    voice.decayMultiplier = static_cast<float>(std::exp(-7.0 / (std::max(0.01f, env.decay) * sampleRate)));
    voice.releaseMultiplier = static_cast<float>(std::exp(-11.512925465 / (std::max(0.03f, env.release) * sampleRate)));
    voice.sustain = std::clamp(env.sustain, 0.0f, 1.0f);
    voice.targetChannelGain = volume[ch] * expression[ch];

    CoreParams core;
    core.harmonics = transcript.coreHarmonics;
    core.resonance = transcript.coreResonance;
    core.cutoff = transcript.coreCutoff;
    core.slope = transcript.coreSlope;
    core.formant = transcript.coreFormant;
    core.formantAmt = transcript.coreFormantAmt;
    core.oddEven = transcript.coreOddEven;
    core.drive = transcript.coreDrive;
    // snap 只在 noteOn 时为 true：新音符必须从正确的带通系数起步（见 SubtractiveCore::setParameters）。
    voice.core.setParameters(sampleRate, voice.f0, core, snap);

    // 激活调度：核心 4 级 ∈ [0, coreCount/M]（body 效果链 4 级见 updateBodyActivation）。
    const int M = transcript.coreCount + transcript.bodyCount;
    const float coreWindow = M > 0 ? static_cast<float>(transcript.coreCount) / static_cast<float>(M) : 0.0f;
    if (transcript.coreCount > 0)
    {
        for (int k = 0; k < 4; ++k)
        {
            const float start = coreWindow * static_cast<float>(k) / 4.0f;
            const float end = coreWindow * static_cast<float>(k + 1) / 4.0f;
            voice.coreActivation[static_cast<size_t>(k)] = smoothstep(start, end, scanPhase);
        }
    }
    else voice.coreActivation.fill(0.0f);

    if (snap) voice.channelGain = voice.targetChannelGain;
}
void Engine::updateBodyActivation() noexcept
{
    // 效果器完全按“密码子位置”推进，而不是按 core/body 比例：
    //   第 4 个密码子（1 起算，C[3]）= 0，终止密码子（C[N-1]）= 1。
    // 四级激活与整体湿音共用同一条位置轴，因此到终止密码子时两者同时到 1，
    // 不会出现“读完前几个密码子效果就到位”，也不会拖到转录结束之后才满。
    fx::Activation activation {};
    float wet = 1.0f;
    if (transcript.bodyCount > 0)
    {
        const int codons = transcript.coreCount + transcript.bodyCount + 2;
        constexpr float firstWetCodon = 3.0f;                       // C[3] = 第 4 个密码子
        const float lastWetCodon = std::max(firstWetCodon + 1.0f, static_cast<float>(codons - 1));
        const float position = scanPhase * static_cast<float>(codons);
        const float span = lastWetCodon - firstWetCodon;
        for (int k = 0; k < 4; ++k)
        {
            const float start = firstWetCodon + span * static_cast<float>(k) / 4.0f;
            const float end = firstWetCodon + span * static_cast<float>(k + 1) / 4.0f;
            activation[static_cast<size_t>(k)] = smoothstep(start, end, position);
        }
        wet = smoothstep(firstWetCodon, lastWetCodon, position);
    }
    body.setActivation(activation);
    body.setWet(wet);
}
void Engine::noteOn(int channel, int note, float velocity) noexcept
{
    if (!validChannel(channel) || note < 0 || note > 127 || !std::isfinite(velocity)) return;
    if (velocity <= 0.0f) { noteOff(channel, note); return; }
    Voice* chosen = nullptr;
    for (auto& voice : voices)
        if (voice.stage == Stage::idle) { chosen = &voice; break; }
    if (chosen == nullptr)
    {
        chosen = &voices.front();
        for (auto& voice : voices)
        {
            const bool releasing = voice.stage == Stage::release;
            const bool chosenReleasing = chosen->stage == Stage::release;
            if ((releasing && !chosenReleasing)
                || (releasing == chosenReleasing && voice.age < chosen->age)) chosen = &voice;
        }
    }
    const bool stealing = chosen->stage != Stage::idle;
    const float oldL = chosen->lastL, oldR = chosen->lastR;
    *chosen = Voice {};
    chosen->core.prepare(sampleRate);
    chosen->channel = channel - 1; chosen->note = note; chosen->keyDown = true;
    chosen->age = ++serial;
    chosen->random = 0x9e3779b9u ^ static_cast<std::uint32_t>(serial * 747796405u) ^ static_cast<std::uint32_t>(note * 2891336453u);
    if (chosen->random == 0) chosen->random = 1;
    chosen->velocity = std::pow(std::clamp(velocity, 0.0f, 1.0f), 1.4f);
    chosen->stage = Stage::attack;
    if (stealing) { chosen->stolenL = oldL; chosen->stolenR = oldR; chosen->stealRemaining = stealSamples; }
    refresh(*chosen, true);
}
void Engine::noteOff(int channel, int note) noexcept
{
    if (!validChannel(channel)) return;
    for (auto& voice : voices)
        if (voice.channel == channel - 1 && voice.note == note && voice.keyDown)
        {
            voice.keyDown = false;
            if (!pedal[static_cast<size_t>(channel - 1)]) voice.release();
        }
}
void Engine::pitchWheel(int channel, int value) noexcept
{
    if (!validChannel(channel)) return;
    const int clamped = std::clamp(value, 0, 16383);
    bend[static_cast<size_t>(channel - 1)] = static_cast<float>(clamped - 8192) / (clamped < 8192 ? 8192.0f : 8191.0f);
    refreshCountdown = 0;
}
void Engine::controller(int channel, int number, int value) noexcept
{
    if (!validChannel(channel)) return;
    const auto ch = static_cast<size_t>(channel - 1);
    const float amount = static_cast<float>(std::clamp(value, 0, 127)) / 127.0f;
    if (number == 1) wheel[ch] = amount;
    if (number == 7) volume[ch] = amount;
    if (number == 11) expression[ch] = amount;
    if (number == 64 || number == 121)
    {
        pedal[ch] = number == 64 && value >= 64;
        if (!pedal[ch])
            for (auto& voice : voices) if (voice.channel == channel - 1 && !voice.keyDown) voice.release();
    }
    if (number == 121) { bend[ch] = 0.0f; wheel[ch] = 0.0f; expression[ch] = 1.0f; }
    if (number == 120 || number == 123)
        for (auto& voice : voices)
            if (voice.channel == channel - 1)
            {
                voice.keyDown = false;
                if (number == 120) { voice = Voice {}; voice.core.prepare(sampleRate); }
                else if (!pedal[ch]) voice.release();
            }
    if (number == 120) body.reset();   // All Sound Off 同时切断效果总线尾音
    refreshCountdown = 0;
}
float Engine::limit(float sample) noexcept
{
    if (!std::isfinite(sample)) return 0.0f;
    const float magnitude = std::abs(sample);
    if (magnitude <= 0.80f) return sample;
    return std::copysign(0.80f + 0.199f * std::tanh((magnitude - 0.80f) / 0.199f), sample);
}
void Engine::render(float* left, float* right, int samples) noexcept
{
    if (left == nullptr || samples <= 0) return;
    lastPeak = 0.0f;
    for (int sample = 0; sample < samples; ++sample)
    {
        if (refreshCountdown <= 0)
        {
            for (auto& voice : voices) if (voice.stage != Stage::idle) refresh(voice, false);
            updateBodyActivation();
            refreshCountdown = 64;
        }
        --refreshCountdown;
        float l = 0.0f, r = 0.0f;
        bool anySignal = false;
        for (auto& voice : voices)
        {
            if (voice.stage == Stage::idle) continue;
            anySignal = true;
            const float noise = voice.noise();
            const float envelope = voice.nextEnvelope() * voice.velocity;
            float mono = voice.core.process(noise, voice.coreActivation);
            // 振幅包络先于 body 效果总线：回声/混响缓冲里只写入「包络之后」的信号，
            // slow-attack 的 pad 不会在尾音里残留未加包络的瞬态音头。
            mono *= envelope;
            mono *= transcript.normalisation;
            voice.channelGain += (voice.targetChannelGain - voice.channelGain) * smoothing;
            const float v = mono * voice.channelGain;
            if (voice.stealRemaining > 0)
            {
                const float blend = static_cast<float>(voice.stealRemaining--) / static_cast<float>(stealSamples);
                voice.lastL = v * (1.0f - blend) + voice.stolenL * blend;
                voice.lastR = v * (1.0f - blend) + voice.stolenR * blend;
            }
            else { voice.lastL = voice.lastR = v; }
            l += voice.lastL; r += voice.lastR;
        }
        // 慢速 AGC：能量追踪真 RMS 做整体响度对齐。静音（无活动 voice）时增益回落到中性 1，
        // 否则静音段会把增益顶到上限，使下一次触发爆出大音量。
        if (anySignal)
        {
            const float energy = l * l + r * r;
            if (energy > rmsFollower) rmsFollower += (energy - rmsFollower) * agcAttack;
            else rmsFollower += (energy - rmsFollower) * agcRelease;
            const float gain = std::clamp(targetRms / (std::sqrt(rmsFollower) + 1.0e-5f), 0.05f, 20.0f);
            autoGain += (gain - autoGain) * agcSmooth;
        }
        else
        {
            // 静音时回落到中性能量，增益归 1，而非让能量跌到 0 把增益顶到上限。
            rmsFollower += (2.0f * targetRms * targetRms - rmsFollower) * agcRelease;
            autoGain += (1.0f - autoGain) * agcSmooth;
        }
        l *= autoGain; r *= autoGain;
        // body 效果总线：AGC 只对齐干声能量，尾音不受 AGC 的静音回落影响；三条链各自做等功率湿干混合。
        body.process(l, r);
        // DC 去除与软限幅。
        const std::array<float, 2> raw { l, r };
        for (size_t ch = 0; ch < 2; ++ch)
        {
            const float filtered = raw[ch] - dcInput[ch] + dcPole * dcOutput[ch];
            dcInput[ch] = raw[ch];
            dcOutput[ch] = std::abs(filtered) < 1.0e-20f ? 0.0f : filtered;
        }
        l = limit(dcOutput[0]); r = limit(dcOutput[1]);
        if (right != nullptr) { left[sample] = l; right[sample] = r; }
        else left[sample] = limit((dcOutput[0] + dcOutput[1]) * 0.70710678f);
        lastPeak = std::max(lastPeak, std::max(std::abs(left[sample]), std::abs(r)));
    }
}
int Engine::activeVoices() const noexcept
{
    int count = 0;
    for (const auto& voice : voices) if (voice.stage != Stage::idle) ++count;
    return count;
}
bool Engine::isNoteActive(int channel, int note) const noexcept
{
    for (const auto& voice : voices)
        if (voice.stage != Stage::idle && voice.channel == channel - 1 && voice.note == note) return true;
    return false;
}
}
