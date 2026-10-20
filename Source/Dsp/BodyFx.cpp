#include "BodyFx.h"
#include <algorithm>
#include <cmath>

namespace transcription
{
namespace fx
{
namespace
{
float equalPowerDry(float mix) noexcept { return std::cos(std::clamp(mix, 0.0f, 1.0f) * pi * 0.5f); }
float equalPowerWet(float mix) noexcept { return std::sin(std::clamp(mix, 0.0f, 1.0f) * pi * 0.5f); }
float brightness(float tone, float octaves) noexcept { return std::exp2(std::clamp(tone, -1.0f, 1.0f) * octaves); }
void blend(float& x, float wet, float amount) noexcept { x += (wet - x) * amount; }
}

Genes Genes::from(const Transcript& t) noexcept
{
    Genes g;
    const auto unit = [](float v, float fallback) { return std::isfinite(v) ? std::clamp(v, 0.0f, 1.0f) : fallback; };
    g.growth = unit(t.fxGrowth, 0.0f);
    g.space = unit(t.fxSpace, 0.5f);
    g.motion = unit(t.fxMotion, 0.5f);
    g.breath = unit(t.fxBreath, 0.5f);
    g.tone = std::isfinite(t.fxTone) ? std::clamp(t.fxTone, -1.0f, 1.0f) : 0.0f;
    g.shimmer = unit(t.fxShimmer, 0.0f);
    g.seed = t.fxSeed == 0 ? 1u : t.fxSeed;
    return g;
}
float Genes::jitter(std::uint32_t salt) const noexcept
{
    std::uint32_t x = seed ^ (salt * 0x9e3779b9u);
    x ^= x >> 16; x *= 0x7feb352du; x ^= x >> 15; x *= 0x846ca68bu; x ^= x >> 16;
    return static_cast<float>(x >> 8) / 8388608.0f - 1.0f;
}

// ---------------------------------------------------------------- Halo
void Halo::prepare(double rate, std::uint32_t seed) noexcept
{
    sampleRate = clampRate(rate);
    air.setCutoff(sampleRate, 6000.0);
    lfo.seed(seed);
    lfo.setRate(sampleRate / controlInterval, 0.07);
    for (auto* g : { &width, &airAmount, &drift }) g->setTime(sampleRate, 0.3);
    reset();
}
void Halo::setTargets(float w, float a, float d, float rate, bool snap) noexcept
{
    width.aim(std::clamp(w, 0.5f, 1.8f), snap);
    airAmount.aim(std::clamp(a, 0.0f, 0.5f), snap);
    drift.aim(std::clamp(d, 0.0f, 0.15f), snap);
    lfo.setRate(sampleRate / controlInterval, rate);
    if (snap) control();
}
void Halo::control() noexcept
{
    width.step(); airAmount.step(); drift.step();
    wobble = lfo.next() * drift.value;
}
void Halo::process(float& left, float& right) noexcept
{
    // 只放大 side：单声道内容响度不变，宽度只作用于混响/合唱已有的立体声成分。
    float mid = 0.5f * (left + right);
    const float side = 0.5f * (left - right) * width.value;
    mid += air.highpass(mid) * airAmount.value;
    left = (mid + side) * (1.0f + wobble);
    right = (mid - side) * (1.0f - wobble);
}

// ---------------------------------------------------------------- Nebula（pad）
void NebulaChain::prepare(double rate)
{
    sampleRate = clampRate(rate);
    msToSamples = static_cast<float>(sampleRate * 0.001);
    breathLfo.seed(0x51ed27u); swellLfo.seed(0x7a3c9u);
    ensemble.prepare(static_cast<int>(sampleRate * 0.032));
    for (size_t k = 0; k < ensembleLfo.size(); ++k) ensembleLfo[k].seed(0x1000193u * static_cast<std::uint32_t>(k + 3));
    hall.prepare(sampleRate, 1.6f, 0xa11ce5u);
    halo.prepare(sampleRate, 0x0c0ffeeu);
    for (auto* g : { &breathCut, &breathDepth, &swellDepth, &ensembleDepth }) g->setTime(sampleRate, 0.4);
    for (auto& g : ensembleBase) g.setTime(sampleRate, 0.6);
    hallMix.setTime(sampleRate, 0.3);
    reset();
}
void NebulaChain::reset() noexcept
{
    for (auto& f : breathFilter) f.reset();
    ensemble.clear();
    hall.reset();
    halo.reset();
}
void NebulaChain::configure(const Genes& g, bool snap) noexcept
{
    const double controlRate = sampleRate / controlInterval;
    breathCut.aim(2400.0f * brightness(g.tone, 0.75f), snap);
    breathDepth.aim(0.55f + 0.65f * g.motion, snap);
    swellDepth.aim(0.035f + 0.06f * g.breath, snap);
    const double breathHz = (0.07 + 0.2 * g.breath) * (1.0 + 0.15 * g.jitter(1));
    breathLfo.setRate(controlRate, breathHz);
    swellLfo.setRate(controlRate, breathHz * 0.77);

    constexpr std::array<float, 3> baseMs { 9.5f, 14.2f, 19.6f };
    for (size_t k = 0; k < ensembleBase.size(); ++k)
    {
        ensembleBase[k].aim(baseMs[k] * (1.0f + 0.08f * g.jitter(10u + static_cast<std::uint32_t>(k))), snap);
        ensembleLfo[k].setRate(sampleRate, (0.12 + 0.16 * g.motion) * (1.0 + 0.35 * g.jitter(20u + static_cast<std::uint32_t>(k))));
    }
    ensembleDepth.aim(1.0f + 2.6f * g.motion, snap);

    ReverbSettings rs;
    rs.decay = (3.0f + 5.0f * g.space) * (0.85f + 0.3f * g.growth);
    rs.size = 1.05f + 0.55f * g.space;
    rs.predelay = 0.015f + 0.03f * g.space;
    rs.damping = 5200.0f * brightness(g.tone, 0.8f);
    rs.lowCut = 130.0f;
    rs.modDepth = 0.35f + 0.9f * g.motion;
    rs.modRate = 0.15f + 0.35f * g.breath;
    rs.shimmer = (0.05f + 0.2f * g.shimmer) * (0.6f + 0.4f * g.growth);
    rs.diffusion = 0.7f;
    hall.setTargets(rs, snap);
    hallMix.aim(0.36f + 0.22f * g.growth, snap);
    halo.setTargets(1.2f + 0.3f * g.motion, 0.12f + 0.12f * std::max(0.0f, g.tone) + 0.06f * g.shimmer,
                    0.05f + 0.05f * g.breath, 0.05f + 0.08f * g.breath, snap);
    if (snap) control();
}
void NebulaChain::control() noexcept
{
    const float b = breathLfo.next();
    const float cut = breathCut.step() * std::exp2(b * breathDepth.step());
    for (auto& f : breathFilter) f.set(sampleRate, cut, 0.85);
    swell = 1.0f + swellDepth.step() * swellLfo.next();
    for (auto& g : ensembleBase) g.step();
    ensembleDepth.step();
    hall.control();
    const float m = hallMix.step();
    hallDry = equalPowerDry(m); hallWet = equalPowerWet(m);
    halo.control();
}
void NebulaChain::process(float& l, float& r, const Activation& a) noexcept
{
    // 1 细胞膜呼吸：有机 LFO 缓慢开合的低通 + 极轻的振幅起伏。
    float low = 0.0f, band = 0.0f;
    breathFilter[0].tick(l, low, band);
    const float bl = (0.78f * low + 0.22f * l) * swell;
    breathFilter[1].tick(r, low, band);
    const float br = (0.78f * low + 0.22f * r) * swell;
    blend(l, bl, a[0]); blend(r, br, a[0]);

    // 2 纤毛合唱：三条独立漂移的调制延迟，左右分布 → 宽而不晃。
    ensemble.push(0.5f * (l + r));
    const float depth = ensembleDepth.value * 0.5f;
    std::array<float, 3> taps {};
    for (size_t k = 0; k < taps.size(); ++k)
        taps[k] = ensemble.readCubic((ensembleBase[k].value + depth * ensembleLfo[k].next()) * msToSamples);
    const float el = 0.8f * l + 0.52f * (taps[0] + 0.6f * taps[1]);
    const float er = 0.8f * r + 0.52f * (taps[2] + 0.6f * taps[1]);
    blend(l, el, a[1]); blend(r, er, a[1]);

    // 3 八度微光大厅：长尾 FDN + shimmer 回授，泛音向上缓慢升腾。
    float hl = 0.0f, hr = 0.0f;
    hall.process(l, r, hl, hr);
    blend(l, l * hallDry + hl * hallWet, a[2]);
    blend(r, r * hallDry + hr * hallWet, a[2]);

    // 4 光晕。
    float ol = l, orr = r;
    halo.process(ol, orr);
    blend(l, ol, a[3]); blend(r, orr, a[3]);
}

// ---------------------------------------------------------------- Spore（pluck）
namespace
{
constexpr std::array<float, 4> bloomMs { 1.9f, 3.1f, 4.7f, 6.7f };
constexpr std::array<float, 4> bloomSkew { 1.11f, 1.07f, 0.93f, 1.05f };
}
void SporeChain::prepare(double rate)
{
    sampleRate = clampRate(rate);
    msToSamples = static_cast<float>(sampleRate * 0.001);
    for (auto& a : bloomL) a.prepare(static_cast<int>(sampleRate * 0.014));
    for (auto& a : bloomR) a.prepare(static_cast<int>(sampleRate * 0.014));
    bloomLfo.seed(0x3c6ef372u);
    echoL.prepare(static_cast<int>(sampleRate * 0.5));
    echoR.prepare(static_cast<int>(sampleRate * 0.6));
    wowL.seed(0xa54ff53au); wowR.seed(0x510e527fu);
    lowL.setCutoff(sampleRate, 220.0); lowR.setCutoff(sampleRate, 220.0);
    scatterL.prepare(static_cast<int>(sampleRate * 0.015));
    scatterR.prepare(static_cast<int>(sampleRate * 0.015));
    plate.prepare(sampleRate, 0.95f, 0x9b05688cu);
    halo.prepare(sampleRate, 0x1f83d9abu);
    for (auto& g : bloomLength) g.setTime(sampleRate, 0.5);
    for (auto* g : { &bloomCoef, &echoFeedback, &echoWet, &wowDepth, &scatterCoef, &echoTone, &plateMix }) g->setTime(sampleRate, 0.3);
    echoTime.setTime(sampleRate, 0.6); echoRatio.setTime(sampleRate, 0.6);
    appliedTone = -1.0f;
    reset();
}
void SporeChain::reset() noexcept
{
    for (auto& a : bloomL) a.clear();
    for (auto& a : bloomR) a.clear();
    echoL.clear(); echoR.clear();
    toneL.reset(); toneR.reset(); lowL.reset(); lowR.reset();
    scatterL.clear(); scatterR.clear();
    plate.reset();
    halo.reset();
}
void SporeChain::configure(const Genes& g, bool snap) noexcept
{
    const float spread = 0.8f + 0.5f * g.space;
    for (size_t k = 0; k < bloomLength.size(); ++k)
        bloomLength[k].aim(bloomMs[k] * spread * (1.0f + 0.06f * g.jitter(30u + static_cast<std::uint32_t>(k))), snap);
    bloomCoef.aim(0.5f + 0.18f * g.motion, snap);
    bloomLfo.setRate(sampleRate / controlInterval, 0.15 + 0.3 * g.breath);

    echoTime.aim((0.15f + 0.2f * g.space) * (1.0f + 0.06f * g.jitter(40)), snap);
    echoRatio.aim(0.75f + 0.08f * g.jitter(41), snap);
    echoFeedback.aim(0.32f + 0.3f * g.growth, snap);
    echoWet.aim(0.24f + 0.12f * g.growth, snap);
    wowDepth.aim(0.3f + 1.2f * g.motion, snap);
    const double wowHz = 0.35 + 0.6 * g.breath;
    wowL.setRate(sampleRate, wowHz * (1.0 + 0.2 * g.jitter(42)));
    wowR.setRate(sampleRate, wowHz * (1.0 + 0.2 * g.jitter(43)));
    scatterCoef.aim(0.45f + 0.2f * g.motion, snap);
    echoTone.aim(4200.0f * brightness(g.tone, 0.7f), snap);

    ReverbSettings rs;
    rs.decay = (1.1f + 2.2f * g.space) * (0.85f + 0.3f * g.growth);
    rs.size = 0.55f + 0.35f * g.space;
    rs.predelay = 0.006f + 0.012f * g.space;
    rs.damping = 8500.0f * brightness(g.tone, 0.8f);
    rs.lowCut = 170.0f;
    rs.modDepth = 0.15f + 0.45f * g.motion;
    rs.modRate = 0.45f + 0.6f * g.breath;
    rs.shimmer = (0.03f + 0.14f * g.shimmer) * (0.5f + 0.5f * g.growth);
    rs.diffusion = 0.72f;
    plate.setTargets(rs, snap);
    plateMix.aim(0.22f + 0.12f * g.growth, snap);
    halo.setTargets(1.15f + 0.25f * g.motion, 0.18f + 0.15f * std::max(0.0f, g.tone) + 0.08f * g.shimmer,
                    0.04f + 0.04f * g.breath, 0.08f + 0.1f * g.breath, snap);
    if (snap) control();
}
void SporeChain::control() noexcept
{
    for (auto& g : bloomLength) g.step();
    bloomCoef.step();
    bloomMod = bloomLfo.next() * 0.035f;
    echoTime.step(); echoRatio.step(); echoFeedback.step(); echoWet.step();
    wowDepth.step(); scatterCoef.step(); echoTone.step();
    if (std::abs(echoTone.value - appliedTone) > 2.0f)
    {
        appliedTone = echoTone.value;
        toneL.setCutoff(sampleRate, appliedTone); toneR.setCutoff(sampleRate, appliedTone);
    }
    plate.control();
    const float m = plateMix.step();
    plateDry = equalPowerDry(m); plateWet = equalPowerWet(m);
    halo.control();
}
void SporeChain::process(float& l, float& r, const Activation& a) noexcept
{
    // 1 胞质色散：四级缓慢呼吸的全通，把硬拨弦的瞬态晕开成玻璃质的「绽放」。
    float bl = l, br = r;
    const float c = bloomCoef.value, mod = 1.0f + bloomMod;
    for (size_t k = 0; k < bloomL.size(); ++k)
    {
        const float len = bloomLength[k].value * msToSamples * mod;
        bl = bloomL[k].process(bl, len, c);
        br = bloomR[k].process(br, len * bloomSkew[k], c);
    }
    // 全通链 |H| = 1，主要输出色散后的信号，只留少量干声，避免干湿相加的梳状抵消。
    blend(l, 0.15f * l + 0.97f * bl, a[0]);
    blend(r, 0.15f * r + 0.97f * br, a[0]);

    // 2 孢子回声：左右交叉回授、非网格时值；每次回授都更暗、更散、轻微漂移。
    const float wow = wowDepth.value * msToSamples;
    const float time = echoTime.value * static_cast<float>(sampleRate);
    const float el = echoL.readCubic(time + wowL.next() * wow);
    const float er = echoR.readCubic(time * echoRatio.value + wowR.next() * wow);
    const float fb = echoFeedback.value, sc = scatterCoef.value;
    const float intoL = scatterL.process(toneL.lowpass(lowL.highpass(er)), 8.3f * msToSamples, sc);
    const float intoR = scatterR.process(toneR.lowpass(lowR.highpass(el)), 11.7f * msToSamples, sc);
    const float in = 0.5f * (l + r);
    echoL.push(flush(in + intoL * fb));
    echoR.push(flush(0.6f * in + intoR * fb));
    const float wet = echoWet.value;
    blend(l, l + el * wet, a[1]);
    blend(r, r + er * wet, a[1]);

    // 3 玻璃生物荧光板：中短 FDN，明亮、少量八度微光。
    float pl = 0.0f, pr = 0.0f;
    plate.process(l, r, pl, pr);
    blend(l, l * plateDry + pl * plateWet, a[2]);
    blend(r, r * plateDry + pr * plateWet, a[2]);

    // 4 光晕。
    float ol = l, orr = r;
    halo.process(ol, orr);
    blend(l, ol, a[3]); blend(r, orr, a[3]);
}

// ---------------------------------------------------------------- Voice（lead）
namespace
{
// 元音路径 u → o → a → e → i（后元音到前元音，连续滑移不跳变）。
constexpr std::array<float, 5> vowelF1 { 300.0f, 570.0f, 730.0f, 530.0f, 270.0f };
constexpr std::array<float, 5> vowelF2 { 870.0f, 840.0f, 1090.0f, 1840.0f, 2290.0f };
float vowelAt(const std::array<float, 5>& table, float position) noexcept
{
    const float p = std::clamp(position, 0.0f, 4.0f);
    const int i = std::min(3, static_cast<int>(p));
    const float t = p - static_cast<float>(i);
    const float a = std::log2(table[static_cast<size_t>(i)]), b = std::log2(table[static_cast<size_t>(i + 1)]);
    return std::exp2(a + (b - a) * t);
}
}
void VoiceChain::prepare(double rate)
{
    sampleRate = clampRate(rate);
    msToSamples = static_cast<float>(sampleRate * 0.001);
    vowelLfo.seed(0x6a09e667u);
    doubler.prepare(static_cast<int>(sampleRate * 0.02));
    doubleL.seed(0xbb67ae85u); doubleR.seed(0x3c6ef373u);
    echoL.prepare(static_cast<int>(sampleRate * 0.8));
    echoR.prepare(static_cast<int>(sampleRate * 0.8));
    lowL.setCutoff(sampleRate, 260.0); lowR.setCutoff(sampleRate, 260.0);
    envAttack = static_cast<float>(1.0 - std::exp(-1.0 / (sampleRate * 0.008)));
    envRelease = static_cast<float>(1.0 - std::exp(-1.0 / (sampleRate * 0.32)));
    hall.prepare(sampleRate, 1.4f, 0x5be0cd19u);
    for (auto* g : { &vowelCenter, &vowelSpan, &doubleBaseL, &doubleBaseR, &echoTime, &echoRatio }) g->setTime(sampleRate, 0.5);
    for (auto* g : { &glowAmount, &doubleDepth, &echoFeedback, &echoWet, &echoTone, &hallMix }) g->setTime(sampleRate, 0.3);
    formantShift.setTime(sampleRate, 0.4);
    appliedTone = -1.0f;
    reset();
}
void VoiceChain::reset() noexcept
{
    for (auto& f : formants) f.reset();
    doubler.clear();
    echoL.clear(); echoR.clear();
    toneL.reset(); toneR.reset(); lowL.reset(); lowR.reset();
    envelope = 0.0f;
    hall.reset();
}
void VoiceChain::configure(const Genes& g, bool snap) noexcept
{
    vowelCenter.aim(std::clamp(2.0f + 1.3f * g.jitter(50), 0.6f, 3.4f), snap);
    vowelSpan.aim(0.5f + 1.2f * g.motion, snap);
    glowAmount.aim(0.9f + 0.7f * g.growth, snap);
    formantShift.aim(brightness(g.tone, 0.3f), snap);
    vowelLfo.setRate(sampleRate / controlInterval, (0.04 + 0.16 * g.breath) * (1.0 + 0.2 * g.jitter(51)));

    doubleBaseL.aim(7.3f * (1.0f + 0.06f * g.jitter(52)), snap);
    doubleBaseR.aim(11.9f * (1.0f + 0.06f * g.jitter(53)), snap);
    doubleDepth.aim(0.15f + 0.45f * g.motion, snap);
    doubleL.setRate(sampleRate, 0.21 * (1.0 + 0.3 * g.jitter(54)));
    doubleR.setRate(sampleRate, 0.29 * (1.0 + 0.3 * g.jitter(55)));

    echoTime.aim((0.29f + 0.17f * g.space) * (1.0f + 0.05f * g.jitter(56)), snap);
    echoRatio.aim(1.33f + 0.06f * g.jitter(57), snap);
    echoFeedback.aim(0.26f + 0.26f * g.growth, snap);
    echoWet.aim(0.36f + 0.16f * g.growth, snap);
    echoTone.aim(3000.0f * brightness(g.tone, 0.7f), snap);

    ReverbSettings rs;
    rs.decay = (2.0f + 3.2f * g.space) * (0.85f + 0.3f * g.growth);
    rs.size = 0.85f + 0.5f * g.space;
    rs.predelay = 0.03f + 0.04f * g.space;
    rs.damping = 6200.0f * brightness(g.tone, 0.8f);
    rs.lowCut = 160.0f;
    rs.modDepth = 0.3f + 0.7f * g.motion;
    rs.modRate = 0.22f + 0.4f * g.breath;
    rs.shimmer = 0.05f * g.shimmer * g.growth;
    rs.diffusion = 0.68f;
    hall.setTargets(rs, snap);
    hallMix.aim(0.24f + 0.16f * g.growth, snap);
    if (snap) control();
}
void VoiceChain::control() noexcept
{
    const float position = vowelCenter.step() + vowelSpan.step() * vowelLfo.next();
    const float shift = formantShift.step();
    formants[0].set(sampleRate, vowelAt(vowelF1, position) * shift, 4.0);
    formants[1].set(sampleRate, vowelAt(vowelF2, position) * shift, 5.5);
    glowComp = 1.0f / (1.0f + 0.1f * glowAmount.step());
    doubleBaseL.step(); doubleBaseR.step(); doubleDepth.step();
    echoTime.step(); echoRatio.step(); echoFeedback.step(); echoWet.step(); echoTone.step();
    if (std::abs(echoTone.value - appliedTone) > 2.0f)
    {
        appliedTone = echoTone.value;
        toneL.setCutoff(sampleRate, appliedTone); toneR.setCutoff(sampleRate, appliedTone);
    }
    hall.control();
    const float m = hallMix.step();
    hallDry = equalPowerDry(m); hallWet = equalPowerWet(m);
}
void VoiceChain::process(float& l, float& r, const Activation& a) noexcept
{
    // 1 元音共振光泽：两个共振峰沿 u-o-a-e-i 缓慢滑移，叠加在干声之上 → 像在「吟唱」。
    float low = 0.0f, f1 = 0.0f, f2 = 0.0f;
    const float mid = 0.5f * (l + r);
    formants[0].tick(mid, low, f1);
    formants[1].tick(mid, low, f2);
    const float glow = (f1 + 0.7f * f2) * glowAmount.value;
    blend(l, (l + glow) * glowComp, a[0]);
    blend(r, (r + glow) * glowComp, a[0]);

    // 2 微移双声：左右各一条极短的漂移延迟，几音分的微失谐 → 宽、贵、不发虚。
    doubler.push(0.5f * (l + r));
    const float depth = doubleDepth.value * 0.5f;
    const float tapL = doubler.readCubic((doubleBaseL.value + depth * doubleL.next()) * msToSamples);
    const float tapR = doubler.readCubic((doubleBaseR.value + depth * doubleR.next()) * msToSamples);
    blend(l, 0.8f * l + 0.42f * tapL, a[1]);
    blend(r, 0.8f * r + 0.42f * tapR, a[1]);

    // 3 呼吸避让回声：演奏时回声退后，停顿时回声浮现；回授逐次变暗。
    const float centre = 0.5f * (l + r);
    const float magnitude = std::abs(centre);
    envelope += (magnitude - envelope) * (magnitude > envelope ? envAttack : envRelease);
    const float duck = 1.0f / (1.0f + 8.0f * envelope);
    const float time = echoTime.value * static_cast<float>(sampleRate);
    const float el = echoL.readCubic(time);
    const float er = echoR.readCubic(time * echoRatio.value);
    const float fb = echoFeedback.value;
    echoL.push(flush(centre + toneL.lowpass(lowL.highpass(er)) * fb));
    echoR.push(flush(0.35f * centre + toneR.lowpass(lowR.highpass(el)) * fb));
    const float wet = echoWet.value * duck;
    blend(l, l + el * wet, a[2]);
    blend(r, r + er * wet, a[2]);

    // 4 大厅：较长预延迟，让主旋律保持清晰。
    float hl = 0.0f, hr = 0.0f;
    hall.process(l, r, hl, hr);
    blend(l, l * hallDry + hl * hallWet, a[3]);
    blend(r, r * hallDry + hr * hallWet, a[3]);
}
}

// ---------------------------------------------------------------- BodyFx
void BodyFx::prepare(double rate)
{
    sampleRate = fx::clampRate(rate);
    spore.prepare(sampleRate);
    nebula.prepare(sampleRate);
    voice.prepare(sampleRate);
    activationCoeff = static_cast<float>(1.0 - std::exp(-1.0 / (sampleRate * 0.04)));
    fadeStep = static_cast<float>(1.0 / (sampleRate * 0.08));
    sleepAfter = static_cast<int>(sampleRate * 0.3);
    prepared = true;
    reset();
}
void BodyFx::reset() noexcept
{
    const int index = std::clamp(static_cast<int>(active), 0, 2);
    gain.fill(0.0f); target.fill(0.0f);
    gain[static_cast<size_t>(index)] = target[static_cast<size_t>(index)] = 1.0f;
    if (prepared)
        for (int i = 0; i < 3; ++i) { resetChain(i); configureChain(i, true); }
    activation = activationTarget;
    wet = wetTarget = 1.0f;
    controlCount = 0; quietSamples = 0;
    bypassed = true; fresh = true;
}
void BodyFx::configure(const Transcript& transcript) noexcept
{
    genes = fx::Genes::from(transcript);
    const StopKind next = transcript.stopKind;
    const auto index = static_cast<size_t>(std::clamp(static_cast<int>(next), 0, 2));
    if (!prepared || fresh)
    {
        active = next;
        gain.fill(0.0f); target.fill(0.0f);
        gain[index] = target[index] = 1.0f;
        if (prepared) for (int i = 0; i < 3; ++i) configureChain(i, true);
        return;
    }
    if (next != active)
    {
        if (gain[index] <= 0.0f) resetChain(static_cast<int>(index));
        target.fill(0.0f);
        target[index] = 1.0f;
        active = next;
    }
    for (int i = 0; i < 3; ++i) configureChain(i, gain[static_cast<size_t>(i)] <= 0.0f);
}
void BodyFx::setActivation(const fx::Activation& next) noexcept
{
    for (size_t k = 0; k < next.size(); ++k)
        activationTarget[k] = std::isfinite(next[k]) ? std::clamp(next[k], 0.0f, 1.0f) : 0.0f;
    if (fresh) activation = activationTarget;
}
void BodyFx::setWet(float value) noexcept
{
    wetTarget = std::isfinite(value) ? std::clamp(value, 0.0f, 1.0f) : 0.0f;
    if (fresh) wet = wetTarget;
}
void BodyFx::process(float& left, float& right) noexcept
{
    if (!prepared) return;
    fresh = false;
    const float dryL = left, dryR = right;
    float strongest = 0.0f;
    for (size_t k = 0; k < activation.size(); ++k)
    {
        activation[k] += (activationTarget[k] - activation[k]) * activationCoeff;
        strongest = std::max(strongest, std::max(activation[k], activationTarget[k]));
    }

    // 全旁路（body 为空或转录尚未读到 body）：输出干声；离开旁路前各链缓存已清空，不会回放陈旧尾音。
    if (strongest < 1.0e-5f)
    {
        if (!bypassed)
        {
            for (int i = 0; i < 3; ++i) resetChain(i);
            bypassed = true;
        }
        gain = target;
        return;
    }
    bypassed = false;

    // 静默休眠：无输入且尾音已低于 -120 dB 时跳过计算。
    const bool silentInput = std::abs(left) + std::abs(right) < 1.0e-9f;
    if (silentInput && quietSamples >= sleepAfter) return;

    if (--controlCount <= 0)
    {
        controlCount = fx::controlInterval;
        for (int i = 0; i < 3; ++i)
            if (gain[static_cast<size_t>(i)] > 0.0f || target[static_cast<size_t>(i)] > 0.0f) controlChain(i);
    }

    float outL = 0.0f, outR = 0.0f;
    for (size_t i = 0; i < gain.size(); ++i)
    {
        if (gain[i] < target[i]) gain[i] = std::min(target[i], gain[i] + fadeStep);
        else if (gain[i] > target[i]) gain[i] = std::max(target[i], gain[i] - fadeStep);
        if (gain[i] <= 0.0f && target[i] <= 0.0f) continue;
        float l = left, r = right;
        processChain(static_cast<int>(i), l, r);
        outL += l * gain[i];
        outR += r * gain[i];
    }
    if (!std::isfinite(outL) || !std::isfinite(outR))
    {
        for (int i = 0; i < 3; ++i) resetChain(i);
        outL = left; outR = right;
    }
    // 全局湿音深度：整条链已含干声，这里再做一次 dry → chain 的交叉淡化，
    // 让“效果浓度”严格跟随转录进度，而不是被某一级激活提前带满。
    wet += (wetTarget - wet) * activationCoeff;
    outL = dryL + (outL - dryL) * wet;
    outR = dryR + (outR - dryR) * wet;
    quietSamples = silentInput && std::abs(outL) + std::abs(outR) < 1.0e-6f ? quietSamples + 1 : 0;
    left = outL; right = outR;
}
void BodyFx::configureChain(int index, bool snap) noexcept
{
    switch (index)
    {
        case 0: spore.configure(genes, snap); break;
        case 1: nebula.configure(genes, snap); break;
        default: voice.configure(genes, snap); break;
    }
}
void BodyFx::resetChain(int index) noexcept
{
    switch (index)
    {
        case 0: spore.reset(); break;
        case 1: nebula.reset(); break;
        default: voice.reset(); break;
    }
}
void BodyFx::controlChain(int index) noexcept
{
    switch (index)
    {
        case 0: spore.control(); break;
        case 1: nebula.control(); break;
        default: voice.control(); break;
    }
}
void BodyFx::processChain(int index, float& left, float& right) noexcept
{
    const auto& a = activation;
    switch (index)
    {
        case 0: spore.process(left, right, a); break;
        case 1: nebula.process(left, right, a); break;
        default: voice.process(left, right, a); break;
    }
}
}
