#include "FxChain.h"
#include <algorithm>
#include <cmath>

namespace transcription
{
namespace
{
constexpr double pi = 3.14159265358979323846;
}

void FxChain::prepare(double rate) noexcept
{
    sampleRate = std::isfinite(rate) && rate >= 8000.0 && rate <= 384000.0 ? rate : 44100.0;
    const int sr = static_cast<int>(sampleRate);
    chorusBuffer.assign(static_cast<size_t>(sr / 20) + 2, 0.0f);   // 50ms
    delayBuffer.assign(static_cast<size_t>(sr) + 2, 0.0f);         // 1s
    const std::array<int, 4> combLengths { static_cast<int>(sr * 0.040), static_cast<int>(sr * 0.036), static_cast<int>(sr * 0.033), static_cast<int>(sr * 0.029) };
    for (size_t i = 0; i < combs.size(); ++i) combs[i].buffer.assign(static_cast<size_t>(combLengths[i]) + 2, 0.0f);
    const std::array<int, 2> apLengths { static_cast<int>(sr * 0.0051), static_cast<int>(sr * 0.0017) };
    for (size_t i = 0; i < allpasses.size(); ++i) allpasses[i].buffer.assign(static_cast<size_t>(apLengths[i]) + 2, 0.0f);
    reset();
}
void FxChain::reset() noexcept
{
    std::fill(chorusBuffer.begin(), chorusBuffer.end(), 0.0f);
    std::fill(delayBuffer.begin(), delayBuffer.end(), 0.0f);
    for (auto& c : combs) { std::fill(c.buffer.begin(), c.buffer.end(), 0.0f); c.write = 0; c.lowpass = 0.0f; }
    for (auto& a : allpasses) { std::fill(a.buffer.begin(), a.buffer.end(), 0.0f); a.write = 0; }
    chorusWrite = 0; delayWrite = 0; chorusPhase = 0.0f;
}
void FxChain::setParameters(const FxParams& params) noexcept
{
    fxDrive = std::clamp(params.drive, 0.0f, 1.0f);
    fxDepth = std::clamp(params.depth, 0.0f, 1.0f);
    delaySamples = std::clamp(static_cast<int>(std::clamp(params.time, 0.0f, 1.0f) * sampleRate), 1, static_cast<int>(delayBuffer.size()) - 2);
    fxFeedback = std::clamp(params.feedback, 0.0f, 0.9f);
    fxSize = std::clamp(params.size, 0.0f, 1.0f);
    fxDamp = std::clamp(params.damp, 0.0f, 1.0f);
    for (size_t i = 0; i < combs.size(); ++i)
    {
        combs[i].feedback = 0.70f + 0.13f * fxSize;
        combs[i].damp = 0.25f + 0.55f * fxDamp;
    }
}
float FxChain::saturate(float x) const noexcept
{
    const float amount = 1.0f + fxDrive * 4.0f;
    return std::tanh(x * amount) / std::tanh(amount);
}
float FxChain::chorus(float x) noexcept
{
    chorusPhase += 0.7f / static_cast<float>(sampleRate);
    if (chorusPhase > 1.0f) chorusPhase -= 1.0f;
    const float lfo = std::sin(static_cast<float>(2.0 * pi) * chorusPhase);
    const float delay = (0.008f + 0.008f * lfo) * fxDepth;
    float readPos = static_cast<float>(chorusWrite) - delay * static_cast<float>(sampleRate);
    const int size = static_cast<int>(chorusBuffer.size());
    while (readPos < 0.0f) readPos += static_cast<float>(size);
    const int i0 = static_cast<int>(readPos) % size;
    const int i1 = (i0 + 1) % size;
    const float frac = readPos - std::floor(readPos);
    chorusBuffer[static_cast<size_t>(chorusWrite)] = x;
    chorusWrite = (chorusWrite + 1) % size;
    const float wet = chorusBuffer[static_cast<size_t>(i0)] * (1.0f - frac) + chorusBuffer[static_cast<size_t>(i1)] * frac;
    return x + (wet - x) * fxDepth;
}
float FxChain::delay(float x) noexcept
{
    const int size = static_cast<int>(delayBuffer.size());
    const int read = (delayWrite - delaySamples + size) % size;
    const float wet = delayBuffer[static_cast<size_t>(read)];
    delayBuffer[static_cast<size_t>(delayWrite)] = x + wet * fxFeedback;
    delayWrite = (delayWrite + 1) % size;
    return x + wet;
}
float FxChain::reverb(float x) noexcept
{
    float wet = 0.0f;
    for (auto& c : combs)
    {
        const int size = static_cast<int>(c.buffer.size());
        const int read = (c.write + 1) % size;
        c.lowpass = c.buffer[static_cast<size_t>(read)] * (1.0f - c.damp) + c.lowpass * c.damp;
        c.buffer[static_cast<size_t>(c.write)] = x + c.lowpass * c.feedback;
        c.write = (c.write + 1) % size;
        wet += c.lowpass;
    }
    wet *= 0.25f;
    for (auto& a : allpasses)
    {
        const int size = static_cast<int>(a.buffer.size());
        const int read = (a.write + 1) % size;
        const float buf = a.buffer[static_cast<size_t>(read)];
        a.buffer[static_cast<size_t>(a.write)] = wet + buf * 0.5f;
        a.write = (a.write + 1) % size;
        wet = buf - wet * 0.5f;
    }
    return x + wet * (0.2f + 0.8f * fxSize);
}
float FxChain::process(float input, const std::array<float, 4>& activation) noexcept
{
    float out = input;
    out += (saturate(out) - out) * std::clamp(activation[0], 0.0f, 1.0f);
    out += (chorus(out) - out) * std::clamp(activation[1], 0.0f, 1.0f);
    out += (delay(out) - out) * std::clamp(activation[2], 0.0f, 1.0f);
    out += (reverb(out) - out) * std::clamp(activation[3], 0.0f, 1.0f);
    return out;
}

}
