#include "SubtractiveCore.h"
#include <algorithm>
#include <cmath>

namespace transcription
{
namespace
{
constexpr double pi = 3.14159265358979323846;
float softclip(float x, float drive) noexcept
{
    if (drive <= 0.0f) return x;
    const float amount = 1.0f + drive * 4.0f;
    return std::tanh(x * amount) / std::tanh(amount);
}
}

void SubtractiveCore::Biquad::lowpass(double rate, double freq, double q) noexcept
{
    const double f = std::clamp(freq, 20.0, rate * 0.45);
    const double w = 2.0 * pi * f / rate;
    const double cw = std::cos(w), sw = std::sin(w);
    const double alpha = sw / (2.0 * std::clamp(q, 0.1, 40.0));
    const double a0 = 1.0 + alpha;
    tb0 = ((1.0 - cw) / 2.0) / a0;
    tb1 = (1.0 - cw) / a0;
    tb2 = ((1.0 - cw) / 2.0) / a0;
    ta1 = (-2.0 * cw) / a0;
    ta2 = (1.0 - alpha) / a0;
}
void SubtractiveCore::Biquad::bandpass(double rate, double freq, double q) noexcept
{
    const double f = std::clamp(freq, 20.0, rate * 0.45);
    const double w = 2.0 * pi * f / rate;
    const double cw = std::cos(w), sw = std::sin(w);
    const double alpha = sw / (2.0 * std::clamp(q, 0.1, 200.0));
    const double a0 = 1.0 + alpha;
    tb0 = alpha / a0;
    tb1 = 0.0;
    tb2 = -alpha / a0;
    ta1 = (-2.0 * cw) / a0;
    ta2 = (1.0 - alpha) / a0;
}
void SubtractiveCore::Biquad::peak(double rate, double freq, double q, double gainDb) noexcept
{
    const double f = std::clamp(freq, 20.0, rate * 0.45);
    const double w = 2.0 * pi * f / rate;
    const double cw = std::cos(w), sw = std::sin(w);
    const double alpha = sw / (2.0 * std::clamp(q, 0.1, 200.0));
    const double a = std::pow(10.0, gainDb / 40.0);
    const double a0 = 1.0 + alpha / a;
    tb0 = (1.0 + alpha * a) / a0;
    tb1 = (-2.0 * cw) / a0;
    tb2 = (1.0 - alpha * a) / a0;
    ta1 = (-2.0 * cw) / a0;
    ta2 = (1.0 - alpha / a) / a0;
}
float SubtractiveCore::Biquad::tick(float x, float smoothing) noexcept
{
    b0 += (tb0 - b0) * smoothing;
    b1 += (tb1 - b1) * smoothing;
    b2 += (tb2 - b2) * smoothing;
    a1 += (ta1 - a1) * smoothing;
    a2 += (ta2 - a2) * smoothing;
    const double v = static_cast<double>(x);
    const double y = b0 * v + s1;
    s1 = b1 * v - a1 * y + s2;
    s2 = b2 * v - a2 * y;
    if (!std::isfinite(s1) || !std::isfinite(s2) || std::abs(s1) > 1.0e8 || std::abs(s2) > 1.0e8)
    {
        s1 = s2 = 0.0;
        return 0.0f;
    }
    return static_cast<float>(y);
}
void SubtractiveCore::Biquad::snap() noexcept
{
    b0 = tb0; b1 = tb1; b2 = tb2; a1 = ta1; a2 = ta2;
    s1 = s2 = 0.0;
}

void SubtractiveCore::prepare(double rate) noexcept
{
    sampleRate = std::isfinite(rate) && rate >= 8000.0 && rate <= 384000.0 ? rate : 44100.0;
    smoothing = static_cast<float>(1.0 - std::exp(-1.0 / (sampleRate * 0.004)));
    reset();
}
void SubtractiveCore::reset() noexcept
{
    for (auto& r : resonators) r = Biquad {};
    tilt = Biquad {}; formant = Biquad {};
    gains.fill(0.0f);
}
void SubtractiveCore::setParameters(double rate, float f0, const CoreParams& params, bool snap) noexcept
{
    sampleRate = std::isfinite(rate) && rate >= 8000.0 && rate <= 384000.0 ? rate : 44100.0;
    smoothing = static_cast<float>(1.0 - std::exp(-1.0 / (sampleRate * 0.004)));
    harmonicCount = std::clamp(params.harmonics, 1, maxHarmonics);
    drive = std::clamp(params.drive, 0.0f, 1.0f);

    // 谐振组：谐波 k（1..32）的频率 = f0*k，Q 由 resonance 决定，增益随谐波衰减并叠加奇偶偏置。
    const float q = 25.0f + std::clamp(params.resonance, 0.0f, 1.0f) * 480.0f;
    const float tiltExp = std::clamp(params.slope / 6.0f - 1.0f, 0.0f, 3.0f);
    const float bias = std::clamp(params.oddEven, -1.0f, 1.0f);
    float energy = 0.0f;
    for (int k = 1; k <= maxHarmonics; ++k)
    {
        const bool active = k <= harmonicCount;
        const float base = active ? static_cast<float>(1.0 / std::pow(static_cast<double>(k), tiltExp)) : 0.0f;
        const float parity = (k % 2 == 1) ? (1.0f + bias) : (1.0f - bias);
        const float g = base * parity;
        gains[static_cast<size_t>(k - 1)] = g;
        const float freq = f0 * static_cast<float>(k);
        resonators[static_cast<size_t>(k - 1)].bandpass(sampleRate, freq, q);
        // 能量守恒补偿：按带通等效噪声带宽归一化，使谐振组总输出能量与输入白噪声同量级，
        // 避免高 Q 窄带把乐音削得比噪声安静一截。
        const float bwRatio = static_cast<float>(pi * freq) / (q * static_cast<float>(sampleRate));
        energy += g * g * bwRatio;
    }
    const float norm = energy > 1.0e-8f ? 1.0f / std::sqrt(energy) : 0.0f;
    for (auto& g : gains) g *= norm;

    tilt.lowpass(sampleRate, std::clamp(params.cutoff, 20.0f, 20000.0f), 0.7071);
    const float amt = std::clamp(params.formantAmt, 0.0f, 1.0f);
    formant.peak(sampleRate, std::clamp(params.formant, 80.0f, 8000.0f), 6.0, amt * 18.0);

    // 新音符起音：直接把目标系数写入当前系数。否则 biquad 会从 reset() 的
    // 恒等直通（b0 = 1）用 4 ms 时间常数慢慢收敛到窄带通，而这期间 gains[]
    // 已按带通带宽归一化（约 100 倍的增益），谐振组会把原始白噪声一次性
    // 放大出 +20 dB 左右的宽带起音突发。
    if (snap)
    {
        for (auto& r : resonators) r.snap();
        tilt.snap();
        formant.snap();
    }
}
float SubtractiveCore::process(float input, const std::array<float, 4>& activation) noexcept
{
    // 级1：谐波谐振组（8 路带通求和）。
    float resonance = 0.0f;
    for (int k = 0; k < harmonicCount; ++k)
        resonance += resonators[static_cast<size_t>(k)].tick(input, smoothing) * gains[static_cast<size_t>(k)];

    float out = input;
    out = out + (resonance - out) * std::clamp(activation[0], 0.0f, 1.0f);

    // 级2：频谱倾斜（低通）。
    const float tilted = tilt.tick(out, smoothing);
    out = out + (tilted - out) * std::clamp(activation[1], 0.0f, 1.0f);

    // 级3：共振峰（并行峰值叠加）。
    const float formanted = out + formant.tick(out, smoothing);
    out = out + (formanted - out) * std::clamp(activation[2], 0.0f, 1.0f);

    // 级4：谐波偏置（预热饱和）。
    const float driven = softclip(out, drive);
    out = out + (driven - out) * std::clamp(activation[3], 0.0f, 1.0f);

    return out;
}

}
