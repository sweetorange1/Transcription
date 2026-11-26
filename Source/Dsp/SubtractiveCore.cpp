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
    // 温和偶次谐波电子管预热（轻微非对称软饱和），避免生硬方波削波刺耳感。
    const float amount = 1.0f + drive * 2.6f;
    const float shaped = std::tanh((x + 0.08f * drive * x * x) * amount) / std::tanh(amount);
    return shaped;
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
void SubtractiveCore::Biquad::highpass(double rate, double freq, double q) noexcept
{
    const double f = std::clamp(freq, 20.0, rate * 0.45);
    const double w = 2.0 * pi * f / rate;
    const double cw = std::cos(w), sw = std::sin(w);
    const double alpha = sw / (2.0 * std::clamp(q, 0.1, 40.0));
    const double a0 = 1.0 + alpha;
    tb0 = ((1.0 + cw) / 2.0) / a0;
    tb1 = -(1.0 + cw) / a0;
    tb2 = ((1.0 + cw) / 2.0) / a0;
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
void SubtractiveCore::Biquad::highShelf(double rate, double freq, double gainDb) noexcept
{
    const double f = std::clamp(freq, 200.0, rate * 0.45);
    const double w = 2.0 * pi * f / rate;
    const double cw = std::cos(w), sw = std::sin(w);
    const double a = std::pow(10.0, gainDb / 40.0);
    const double alpha = sw * 0.7071067811865476;
    const double twoSqrtAAlpha = 2.0 * std::sqrt(a) * alpha;
    const double a0 = (a + 1.0) - (a - 1.0) * cw + twoSqrtAAlpha;
    tb0 = (a * ((a + 1.0) + (a - 1.0) * cw + twoSqrtAAlpha)) / a0;
    tb1 = (-2.0 * a * ((a - 1.0) + (a + 1.0) * cw)) / a0;
    tb2 = (a * ((a + 1.0) + (a - 1.0) * cw - twoSqrtAAlpha)) / a0;
    ta1 = (2.0 * ((a - 1.0) - (a + 1.0) * cw)) / a0;
    ta2 = ((a + 1.0) - (a - 1.0) * cw - twoSqrtAAlpha) / a0;
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
    subResonator = Biquad {};
    subGain = 0.0f;
    tilt = Biquad {}; formant1 = Biquad {}; formant2 = Biquad {}; presenceShelf = Biquad {}; airFilter = Biquad {};
    gains.fill(0.0f);
    airMix = 0.0f;
    formantMix = 0.0f;
}
void SubtractiveCore::setParameters(double rate, float f0, const CoreParams& params, bool snap) noexcept
{
    sampleRate = std::isfinite(rate) && rate >= 8000.0 && rate <= 384000.0 ? rate : 44100.0;
    smoothing = static_cast<float>(1.0 - std::exp(-1.0 / (sampleRate * 0.004)));
    harmonicCount = std::clamp(params.harmonics, 1, maxHarmonics);
    drive = std::clamp(params.drive, 0.0f, 1.0f);

    const float brightness = std::clamp(params.brightness, 0.0f, 1.0f);
    const float inharm = std::clamp(params.inharmonicity, 0.0f, 1.0f);
    const float ensemble = std::clamp(params.ensemble, 0.0f, 1.0f);
    const float fifthLift = std::clamp(params.fifthLift, 0.0f, 1.0f);
    const float subWarmth = std::clamp(params.subWarmth, 0.0f, 1.0f);
    const float presence = std::clamp(params.presence, 0.0f, 1.0f);
    const float bias = std::clamp(params.oddEven, -1.0f, 1.0f);

    // 品质因数：低 resonance 呈温润气声木管，高 resonance 呈清透晶体共振。
    const float baseQ = 30.0f + std::clamp(params.resonance, 0.0f, 1.0f) * 320.0f;
    // 谐波滚降指数：高 brightness 释放丰富 2–8 次高次泛音，低 brightness 温暖收拢。
    const float rawExp = std::clamp(params.slope / 6.0f - 0.72f, 0.10f, 2.5f);
    const float tiltExp = std::max(0.16f, rawExp * (1.22f - 0.74f * brightness));
    // 物理弦/钢片琴刚度系数 B：仅在芳香环/刚性环核心起效，基频永远保持绝对音准。
    const double stiffB = 0.00165 * static_cast<double>(inharm) * static_cast<double>(inharm);
    const float nyquistLimit = static_cast<float>(sampleRate) * 0.43f;

    // 精确布朗噪声（p=0.995，输出增益 0.05*1.5）在任意频率 f 下的带通等效标准差：
    // 去除会压死高频的常数白噪偏置，让 1..8 路泛音严格按照 targetWeights 真实可听地绽放！
    const auto brownBandStd = [&](float freq, float qVal) -> double
    {
        constexpr double p = 0.995;
        constexpr double gainSq = (0.05 * 1.5) * (0.05 * 1.5) / 3.0;   // 白噪声均匀分布方差 1/3
        const double omega = 2.0 * pi * static_cast<double>(freq) / sampleRate;
        const double denom = std::max(1.0e-7, 1.0 + p * p - 2.0 * p * std::cos(omega));
        const double brownPsd = gainSq / denom;
        const double bwRatio = (pi * static_cast<double>(freq)) / (static_cast<double>(qVal) * sampleRate);
        return std::sqrt(std::max(1.0e-12, brownPsd * bwRatio));
    };

    float targetEnergy = 0.0f;
    std::array<float, maxHarmonics> targetWeights {};

    for (int k = 1; k <= maxHarmonics; ++k)
    {
        const double kd = static_cast<double>(k);
        const double stretch = std::sqrt(1.0 + stiffB * (kd * kd - 1.0));
        const double celesteCents = k > 1 ? (((k % 2 == 0) ? 1.0 : -1.0) * 3.8 * static_cast<double>(ensemble)) : 0.0;
        const double detuneRatio = std::pow(2.0, celesteCents / 1200.0);
        const float freq = static_cast<float>(static_cast<double>(f0) * kd * stretch * detuneRatio);
        const bool active = k <= harmonicCount && freq < nyquistLimit;

        const float qk = std::clamp(baseQ * (1.0f + 0.05f * static_cast<float>(k - 1)), 22.0f, 440.0f);
        resonators[static_cast<size_t>(k - 1)].bandpass(sampleRate, std::min(freq, nyquistLimit), qk);

        if (!active)
        {
            targetWeights[static_cast<size_t>(k - 1)] = 0.0f;
            gains[static_cast<size_t>(k - 1)] = 0.0f;
            continue;
        }

        float w = static_cast<float>(1.0 / std::pow(kd, static_cast<double>(tiltExp)));
        // 奇偶泛音雕塑：基频 k=1 稳固音高，k>=2 鲜明分化空心单簧管（奇次）vs 温暖弦乐/竖琴（偶次）。
        if (k > 1)
        {
            const float parity = (k % 2 == 1) ? (1.0f + 0.92f * bias) : (1.0f - 0.92f * bias);
            w *= std::max(0.05f, parity);
        }
        // 极性羟基/酰胺五度与双八度晶格（k=3, 4, 6）清透提亮。
        if (k == 3 || k == 4 || k == 6)
            w *= 1.0f + 0.85f * fifthLift;
        // 芳香环/高光泽核心高次泛音（k>=4）晶莹抬升，幅度克制以保证丝绸质感绝不刺耳。
        if (k >= 4)
            w *= 1.0f + 0.46f * inharm + 0.34f * presence;

        if (freq > nyquistLimit * 0.78f)
        {
            const float fade = std::clamp((nyquistLimit - freq) / (nyquistLimit * 0.22f), 0.0f, 1.0f);
            w *= fade * fade;
        }

        // 限制高次泛音最大提升倍率，避免极高频放大底噪；布朗噪声下 2..8 次泛音全部精准对齐目标权重。
        const float whiten = static_cast<float>(std::min(300.0, 1.0 / std::max(1.0e-5, brownBandStd(freq, qk))));
        targetWeights[static_cast<size_t>(k - 1)] = w;
        gains[static_cast<size_t>(k - 1)] = w * whiten;
        targetEnergy += w * w;
    }

    // 低八度温暖胸腔共鸣（f0 * 0.5）：仅在疏水/碱性大分子（如 Insulin/Cello 类）且低八度 >= 55 Hz 时柔和浮现。
    const float subFreq = f0 * 0.5f;
    const float subWeight = (harmonicCount >= 2 && subWarmth > 0.22f && subFreq >= 55.0f)
        ? ((subWarmth - 0.22f) / 0.78f) * 0.46f
        : 0.0f;
    const float subQ = std::clamp(baseQ * 0.8f, 24.0f, 260.0f);
    subResonator.bandpass(sampleRate, std::max(40.0f, subFreq), subQ);
    if (subWeight > 1.0e-4f)
    {
        const float subWhiten = static_cast<float>(std::min(160.0, 1.0 / std::max(1.0e-5, brownBandStd(subFreq, subQ))));
        subGain = subWeight * subWhiten;
        targetEnergy += subWeight * subWeight;
    }
    else
    {
        subGain = 0.0f;
    }

    // 目标能量归一化到 ~0.28 RMS（留足多声部和弦动态余量，彻底告别起音撞限幅）。
    const float norm = targetEnergy > 1.0e-8f ? 0.28f / std::sqrt(targetEnergy) : 0.0f;
    for (auto& g : gains) g *= norm;
    subGain *= norm;

    // 频谱倾斜低通：明暗跨度拉宽（暖大提琴 ~1.4kHz ↔ 荧光水晶 ~18kHz），上限温和滚降高频毛刺。
    tilt.lowpass(sampleRate, std::clamp(params.cutoff, 260.0f, 16500.0f), 0.70 + 0.14 * static_cast<double>(brightness));

    // 双共振峰声学共鸣腔（F1 + F2 温和音乐化 Q 值与增益，饱满清晰且绝无尖啸感）。
    formantMix = std::clamp(params.formantAmt, 0.0f, 1.0f);
    const double f1 = std::clamp(static_cast<double>(params.formant), 200.0, 2400.0);
    const double f2 = std::clamp(static_cast<double>(params.formant2), f1 * 1.45, 4800.0);
    formant1.peak(sampleRate, f1, 3.0, 3.0 + 5.2 * static_cast<double>(formantMix));
    formant2.peak(sampleRate, f2, 3.6, 2.2 + 5.8 * static_cast<double>(formantMix));

    // 丝绸高架光泽（2.7–3.9 kHz 起）：避开 2.2 kHz 人耳敏感疲劳峰，暖基因丝绒收敛（-4.5 dB），亮基因清透通灵（+4.0 dB）。
    const double shelfDb = -4.5 + 8.5 * static_cast<double>(presence);
    presenceShelf.highShelf(sampleRate, 2700.0 + 1200.0 * static_cast<double>(brightness), shelfDb);

    // 丝绒气声/擦弦微空气层（经高通 2.4–4.2 kHz 提取细腻呼吸感，温和悦耳不刺耳）。
    const float air = std::clamp(params.air, 0.0f, 1.0f);
    airMix = air * (0.042f + 0.068f * brightness);
    airFilter.highpass(sampleRate, 2300.0 + 1800.0 * static_cast<double>(brightness), 0.75);

    if (snap)
    {
        for (auto& r : resonators) r.snap();
        subResonator.snap();
        tilt.snap();
        formant1.snap();
        formant2.snap();
        presenceShelf.snap();
        airFilter.snap();
    }
}
float SubtractiveCore::process(float input, const std::array<float, 4>& activation) noexcept
{
    // 级1：谐波谐振组（1..8 路带通 + 可选低八度暖腔 + 丝绒微气声层）。
    float resonance = 0.0f;
    for (int k = 0; k < harmonicCount; ++k)
        resonance += resonators[static_cast<size_t>(k)].tick(input, smoothing) * gains[static_cast<size_t>(k)];
    if (subGain > 1.0e-5f)
        resonance += subResonator.tick(input, smoothing) * subGain;
    if (airMix > 1.0e-5f)
        resonance += airFilter.tick(input, smoothing) * airMix;

    float out = input;
    out = out + (resonance - out) * std::clamp(activation[0], 0.0f, 1.0f);

    // 级2：频谱倾斜（低通 + 晶莹光泽高架）。
    const float tilted = presenceShelf.tick(tilt.tick(out, smoothing), smoothing);
    out = out + (tilted - out) * std::clamp(activation[1], 0.0f, 1.0f);

    // 级3：双共振峰声学共鸣腔（串联双峰值 + 精确增益补偿，保持和弦通透动态余量）。
    const float f1Out = formant1.tick(out, smoothing);
    const float f2Out = formant2.tick(f1Out, smoothing);
    const float formantComp = 1.0f / (1.0f + 0.55f * formantMix);
    out = out + (f2Out * formantComp - out) * std::clamp(activation[2], 0.0f, 1.0f);

    // 级4：温和预热饱和。
    const float driven = softclip(out, drive);
    out = out + (driven - out) * std::clamp(activation[3], 0.0f, 1.0f);

    return out;
}

}
