#include "FxBlocks.h"

namespace transcription::fx
{
namespace
{
constexpr float invSqrt8 = 0.35355339f;
constexpr std::array<float, 8> baseMs { 29.3f, 33.7f, 38.9f, 44.2f, 49.6f, 55.1f, 61.7f, 68.3f };
constexpr std::array<float, 8> inputSign { 1.0f, 1.0f, -1.0f, 1.0f, -1.0f, -1.0f, 1.0f, -1.0f };
constexpr std::array<float, 8> shimmerSign { 1.0f, -1.0f, 1.0f, 1.0f, -1.0f, 1.0f, -1.0f, -1.0f };
constexpr std::array<float, 4> diffuseMsL { 4.71f, 3.63f, 12.71f, 9.31f };
constexpr std::array<float, 4> diffuseMsR { 4.93f, 3.41f, 12.29f, 9.87f };

float unitRandom(std::uint32_t& s) noexcept
{
    s ^= s << 13; s ^= s >> 17; s ^= s << 5;
    return static_cast<float>(s >> 8) / 8388608.0f - 1.0f;
}
void hadamard8(std::array<float, 8>& x) noexcept
{
    for (int h = 1; h < 8; h <<= 1)
        for (int i = 0; i < 8; i += h << 1)
            for (int j = i; j < i + h; ++j)
            {
                const float a = x[static_cast<size_t>(j)], b = x[static_cast<size_t>(j + h)];
                x[static_cast<size_t>(j)] = a + b;
                x[static_cast<size_t>(j + h)] = a - b;
            }
    for (auto& v : x) v *= invSqrt8;
}
}

void FdnReverb::prepare(double rate, float largest, std::uint32_t seed)
{
    sampleRate = clampRate(rate);
    maxSize = std::max(0.2f, largest);
    maxModSamples = static_cast<float>(sampleRate * 0.0025);
    std::uint32_t s = seed == 0 ? 0x9e3779b9u : seed;
    for (size_t i = 0; i < lines.size(); ++i)
    {
        baseLength[i] = baseMs[i] * (1.0f + 0.04f * unitRandom(s)) * 0.001f * static_cast<float>(sampleRate);
        lines[i].prepare(static_cast<int>(baseLength[i] * maxSize + maxModSamples) + 8);
        lfos[i].seed(s * 2654435761u + static_cast<std::uint32_t>(i) * 40503u + 1u);
        rateSpread[i] = 0.72f + 0.56f * static_cast<float>(i) / 7.0f + 0.05f * unitRandom(s);
    }
    preL.prepare(static_cast<int>(sampleRate * 0.12));
    preR.prepare(static_cast<int>(sampleRate * 0.12));
    for (size_t i = 0; i < 4; ++i)
    {
        diffuseLengthL[i] = diffuseMsL[i] * 0.001f * static_cast<float>(sampleRate);
        diffuseLengthR[i] = diffuseMsR[i] * 0.001f * static_cast<float>(sampleRate);
        diffuseL[i].prepare(static_cast<int>(diffuseLengthL[i]) + 8);
        diffuseR[i].prepare(static_cast<int>(diffuseLengthR[i]) + 8);
    }
    shifter.prepare(sampleRate);
    shimmerHigh.setCutoff(sampleRate, 7500.0);
    shimmerLow.setCutoff(sampleRate, 320.0);
    for (auto& c : subsonic) c.setCutoff(sampleRate, 25.0);
    for (auto* g : { &decay, &damping, &lowCut, &modDepth, &shimmer, &diffusion, &predelay }) g->setTime(sampleRate, 0.25);
    size.setTime(sampleRate, 0.8);   // 线长变化慢速滑移，避免改密码子时出现「咔哒」与明显变调
    appliedModRate = appliedDamping = appliedLowCut = -1.0f;
    reset();
}
void FdnReverb::reset() noexcept
{
    for (auto& line : lines) line.clear();
    for (auto& d : dampers) d.reset();
    for (auto& c : subsonic) c.reset();
    inputCutL.reset(); inputCutR.reset();
    preL.clear(); preR.clear();
    for (auto& a : diffuseL) a.clear();
    for (auto& a : diffuseR) a.clear();
    shifter.reset();
    shimmerHigh.reset(); shimmerLow.reset();
}
void FdnReverb::setTargets(const ReverbSettings& s, bool snap) noexcept
{
    decay.aim(std::clamp(s.decay, 0.2f, 20.0f), snap);
    size.aim(std::clamp(s.size, 0.2f, maxSize), snap);
    predelay.aim(std::clamp(s.predelay, 0.0f, 0.1f), snap);
    damping.aim(std::clamp(s.damping, 500.0f, 18000.0f), snap);
    lowCut.aim(std::clamp(s.lowCut, 20.0f, 800.0f), snap);
    modDepth.aim(std::clamp(s.modDepth, 0.0f, 2.4f), snap);
    shimmer.aim(std::clamp(s.shimmer, 0.0f, 0.3f), snap);
    diffusion.aim(std::clamp(s.diffusion, 0.0f, 0.78f), snap);
    modRate = std::clamp(s.modRate, 0.02f, 5.0f);
    if (snap) control();
}
void FdnReverb::control() noexcept
{
    decay.step(); size.step(); predelay.step(); damping.step(); lowCut.step();
    modDepth.step(); shimmer.step(); diffusion.step();
    const double rt = static_cast<double>(decay.value) * sampleRate;
    // shimmer 是 f → 2f → 4f… 向上逸出阻尼频带的级联，不在同一频率上闭环；只做轻微环增益让渡。
    const float loopScale = 1.0f - 0.1f * shimmer.value;
    float energy = 0.0f;
    for (size_t i = 0; i < lines.size(); ++i)
    {
        length[i] = baseLength[i] * size.value;
        const float g = static_cast<float>(std::exp(-6.907755 * static_cast<double>(length[i]) / rt));
        gains[i] = g * loopScale;
        energy += g * g;
    }
    // 稳态能量 ≈ 输入 / (1 - g²)；按平均环增益归一化，使不同空间大小的湿声响度接近。
    // 系数 1.5 补偿环内阻尼与输入低切吃掉的能量（以谐波性输入实测校准）。
    const float meanG2 = std::min(energy / static_cast<float>(lines.size()), 0.9995f);
    outScale = std::sqrt((1.0f - meanG2) * 0.125f) * 1.5f;
    modSamples = std::min(maxModSamples, modDepth.value * 0.001f * static_cast<float>(sampleRate));
    if (std::abs(damping.value - appliedDamping) > 1.0f)
    {
        appliedDamping = damping.value;
        for (auto& d : dampers) d.setCutoff(sampleRate, appliedDamping);
    }
    if (std::abs(lowCut.value - appliedLowCut) > 0.5f)
    {
        appliedLowCut = lowCut.value;
        inputCutL.setCutoff(sampleRate, appliedLowCut);
        inputCutR.setCutoff(sampleRate, appliedLowCut);
    }
    if (modRate != appliedModRate)
    {
        appliedModRate = modRate;
        for (size_t i = 0; i < lfos.size(); ++i) lfos[i].setRate(sampleRate, appliedModRate * rateSpread[i]);
    }
}
void FdnReverb::process(float inL, float inR, float& outL, float& outR) noexcept
{
    preL.push(inputCutL.highpass(inL)); preR.push(inputCutR.highpass(inR));
    const float pre = std::max(2.0f, predelay.value * static_cast<float>(sampleRate));
    float dl = preL.readCubic(pre), dr = preR.readCubic(pre);
    const float g = diffusion.value;
    for (size_t i = 0; i < 4; ++i)
    {
        dl = diffuseL[i].process(dl, diffuseLengthL[i], g);
        dr = diffuseR[i].process(dr, diffuseLengthR[i], g);
    }

    std::array<float, 8> y {};
    for (size_t i = 0; i < lines.size(); ++i)
    {
        const float d = length[i] + lfos[i].next() * modSamples;
        y[i] = subsonic[i].highpass(dampers[i].lowpass(lines[i].readCubic(d)));
    }
    outL = (y[0] - y[1] + y[2] - y[3] + y[4] - y[5] + y[6] - y[7]) * outScale;
    outR = (y[0] + y[1] - y[2] - y[3] + y[4] + y[5] - y[6] - y[7]) * outScale;

    float shimmerIn = 0.0f;
    if (shimmer.value > 1.0e-4f)
    {
        const float tap = (y[0] - y[1] - y[2] + y[3] + y[4] - y[5] - y[6] + y[7]) * invSqrt8;
        const float shifted = shimmerLow.highpass(shimmerHigh.lowpass(shifter.process(tap)));
        shimmerIn = std::tanh(shifted) * shimmer.value * invSqrt8;
    }

    std::array<float, 8> f {};
    for (size_t i = 0; i < lines.size(); ++i) f[i] = y[i] * gains[i];
    hadamard8(f);
    for (size_t i = 0; i < lines.size(); ++i)
    {
        float v = f[i] + ((i & 1u) == 0 ? dl : dr) * inputSign[i] + shimmerIn * shimmerSign[i];
        if (!std::isfinite(v)) v = 0.0f;
        lines[i].push(flush(v));
    }
}

}
