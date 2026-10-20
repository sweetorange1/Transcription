#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

// body 区效果链的基础模块：全部为实时安全（prepare 之外不分配内存）、确定性（与宿主分块无关）。
namespace transcription::fx
{
constexpr float pi = 3.14159265358979f;
constexpr float twoPi = 6.28318530717959f;
constexpr int controlInterval = 16;   // 控制率：每 16 采样更新一次滤波系数与慢速调制

inline float flush(float x) noexcept { return std::abs(x) < 1.0e-15f ? 0.0f : x; }
inline float clampRate(double rate) noexcept
{
    return static_cast<float>(std::isfinite(rate) && rate >= 8000.0 && rate <= 384000.0 ? rate : 44100.0);
}

// 控制率一阶平滑：每次 step() 前进一个控制周期。
struct Glide
{
    float value = 0.0f, target = 0.0f, coeff = 1.0f;
    void setTime(double sampleRate, double seconds) noexcept
    {
        coeff = static_cast<float>(1.0 - std::exp(-static_cast<double>(controlInterval) / (std::max(1.0e-4, seconds) * sampleRate)));
    }
    void aim(float next, bool snap) noexcept { target = next; if (snap) value = next; }
    float step() noexcept { value += (target - value) * coeff; return value; }
};

struct OnePole
{
    float a = 0.0f, z = 0.0f;
    void setCutoff(double sampleRate, double hz) noexcept
    {
        a = static_cast<float>(std::exp(-2.0 * 3.14159265358979 * std::clamp(hz, 1.0, sampleRate * 0.45) / sampleRate));
    }
    float lowpass(float x) noexcept { z = flush(x + (z - x) * a); return z; }
    float highpass(float x) noexcept { return x - lowpass(x); }
    void reset() noexcept { z = 0.0f; }
};

// Simper TPT 状态变量滤波器：调制下稳定，band 输出为单位峰值增益。
struct Svf
{
    float k = 1.4f, a1 = 0.0f, a2 = 0.0f, a3 = 0.0f, ic1 = 0.0f, ic2 = 0.0f;
    void set(double sampleRate, double hz, double q) noexcept
    {
        const double g = std::tan(3.14159265358979 * std::clamp(hz, 10.0, sampleRate * 0.45) / sampleRate);
        const double damping = 1.0 / std::max(0.3, q);
        const double b1 = 1.0 / (1.0 + g * (g + damping));
        k = static_cast<float>(damping);
        a1 = static_cast<float>(b1);
        a2 = static_cast<float>(g * b1);
        a3 = static_cast<float>(g * g * b1);
    }
    void tick(float v0, float& low, float& band) noexcept
    {
        const float v3 = v0 - ic2;
        const float v1 = a1 * ic1 + a2 * v3;
        const float v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = flush(2.0f * v1 - ic1);
        ic2 = flush(2.0f * v2 - ic2);
        low = v2;
        band = k * v1;
    }
    void reset() noexcept { ic1 = ic2 = 0.0f; }
};

// 2 的幂环形延迟线；read(1) 为最近写入的采样，readCubic 为 Hermite 分数延迟。
class DelayLine
{
public:
    void prepare(int maxDelaySamples)
    {
        int size = 16;
        while (size < maxDelaySamples + 8) size <<= 1;
        buffer.assign(static_cast<size_t>(size), 0.0f);
        mask = size - 1;
        write = 0;
    }
    void clear() noexcept { std::fill(buffer.begin(), buffer.end(), 0.0f); write = 0; }
    void push(float x) noexcept { buffer[static_cast<size_t>(write)] = x; write = (write + 1) & mask; }
    float read(int delay) const noexcept { return buffer[static_cast<size_t>((write - delay) & mask)]; }
    float readCubic(float delay) const noexcept
    {
        const float d = std::clamp(delay, 2.0f, static_cast<float>(mask - 4));
        const int i = static_cast<int>(d);
        const float f = d - static_cast<float>(i);
        const float ym1 = read(i - 1), y0 = read(i), y1 = read(i + 1), y2 = read(i + 2);
        const float c1 = 0.5f * (y1 - ym1);
        const float c2 = ym1 - 2.5f * y0 + 2.0f * y1 - 0.5f * y2;
        const float c3 = 0.5f * (y2 - ym1) + 1.5f * (y0 - y1);
        return ((c3 * f + c2) * f + c1) * f + y0;
    }
    int capacity() const noexcept { return mask - 4; }
    bool ready() const noexcept { return !buffer.empty(); }

private:
    std::vector<float> buffer;
    int mask = 0, write = 0;
};

// Schroeder 全通扩散器（分数延迟，可缓慢调制）。
class Allpass
{
public:
    void prepare(int maxDelaySamples) { line.prepare(maxDelaySamples); }
    void clear() noexcept { line.clear(); }
    float process(float x, float delay, float g) noexcept
    {
        const float delayed = line.readCubic(delay);
        const float in = flush(x + g * delayed);
        line.push(in);
        return delayed - g * in;
    }

private:
    DelayLine line;
};

// 「有机」低频调制：Catmull-Rom 平滑随机游走 + 黄金比例慢正弦，不周期、不机械。
class OrganicLfo
{
public:
    void seed(std::uint32_t s) noexcept
    {
        state = s == 0 ? 0x2545f491u : s;
        for (auto& k : knots) k = random();
        phase = 0.5f * (random() + 1.0f);
        sinePhase = 0.5f * (random() + 1.0f);
        value = 0.0f;
    }
    void setRate(double tickRate, double hz) noexcept
    {
        increment = static_cast<float>(std::clamp(hz, 0.001, 40.0) / tickRate);
    }
    float next() noexcept
    {
        phase += increment;
        if (phase >= 1.0f)
        {
            phase -= 1.0f;
            knots[0] = knots[1]; knots[1] = knots[2]; knots[2] = knots[3]; knots[3] = random();
        }
        sinePhase += increment * 0.618034f;
        if (sinePhase >= 1.0f) sinePhase -= 1.0f;
        const float t = phase, p0 = knots[0], p1 = knots[1], p2 = knots[2], p3 = knots[3];
        const float wander = 0.5f * (2.0f * p1 + (p2 - p0) * t + (2.0f * p0 - 5.0f * p1 + 4.0f * p2 - p3) * t * t
                                     + (3.0f * p1 - p0 - 3.0f * p2 + p3) * t * t * t);
        value = std::clamp(0.65f * wander + 0.35f * std::sin(twoPi * sinePhase), -1.0f, 1.0f);
        return value;
    }
    float current() const noexcept { return value; }

private:
    float random() noexcept
    {
        state ^= state << 13; state ^= state >> 17; state ^= state << 5;
        return static_cast<float>(state >> 8) / 8388608.0f - 1.0f;
    }
    std::array<float, 4> knots {};
    std::uint32_t state = 0x2545f491u;
    float phase = 0.0f, sinePhase = 0.0f, increment = 0.0f, value = 0.0f;
};

// 双颗粒八度上移（shimmer 回授用）：sin² / cos² 互补窗，跳点落在零增益处。
class OctaveShifter
{
public:
    void prepare(double sampleRate)
    {
        window = static_cast<float>(sampleRate * 0.085);
        line.prepare(static_cast<int>(window) + 8);
        step = 1.0f / window;
        reset();
    }
    void reset() noexcept { line.clear(); phase = 0.0f; }
    float process(float x) noexcept
    {
        line.push(x);
        phase += step;
        if (phase >= 1.0f) phase -= 1.0f;
        float other = phase + 0.5f;
        if (other >= 1.0f) other -= 1.0f;
        const float s = std::sin(pi * phase);
        const float w = s * s;
        return line.readCubic(2.0f + (1.0f - phase) * window) * w
             + line.readCubic(2.0f + (1.0f - other) * window) * (1.0f - w);
    }

private:
    DelayLine line;
    float window = 4000.0f, step = 0.00025f, phase = 0.0f;
};

struct ReverbSettings
{
    float decay = 3.0f;       // RT60（秒）
    float size = 1.0f;        // 线长缩放
    float predelay = 0.02f;   // 秒
    float damping = 6000.0f;  // 环内低通（Hz）
    float lowCut = 150.0f;    // 输入低切（Hz）
    float modDepth = 0.5f;    // 线长调制深度（毫秒）
    float modRate = 0.3f;     // 线长调制速率（Hz）
    float shimmer = 0.0f;     // 八度上移回授量 [0, 0.3]
    float diffusion = 0.7f;   // 输入扩散全通系数
};

// 8 线反馈延迟网络混响：输入低切 + 扩散 → 8 条调制延迟线（阻尼 + 次声保护）→ Hadamard 正交混合；
// 可选八度上移回授（shimmer）。shimmer 注入经 tanh 限幅（有界输入），线性环增益恒 < 1，系统 BIBO 稳定；
// 低切放在输入端而非环内，避免每圈都削弱基频导致尾音过早消失。
class FdnReverb
{
public:
    void prepare(double sampleRate, float maxSize, std::uint32_t seed);
    void reset() noexcept;
    void setTargets(const ReverbSettings& settings, bool snap) noexcept;
    void control() noexcept;
    void process(float inL, float inR, float& outL, float& outR) noexcept;

private:
    static constexpr int lineCount = 8;
    double sampleRate = 44100.0;
    float maxSize = 1.0f, maxModSamples = 1.0f;
    std::array<DelayLine, lineCount> lines;
    std::array<OnePole, lineCount> dampers, subsonic;
    std::array<OrganicLfo, lineCount> lfos;
    std::array<float, lineCount> baseLength {}, length {}, gains {}, rateSpread {};
    DelayLine preL, preR;
    OnePole inputCutL, inputCutR;
    std::array<Allpass, 4> diffuseL, diffuseR;
    std::array<float, 4> diffuseLengthL {}, diffuseLengthR {};
    OctaveShifter shifter;
    OnePole shimmerHigh, shimmerLow;
    Glide decay, size, predelay, damping, lowCut, modDepth, shimmer, diffusion;
    float modRate = 0.3f, appliedModRate = -1.0f;
    float appliedDamping = -1.0f, appliedLowCut = -1.0f;
    float modSamples = 0.0f, outScale = 0.1f;
};

}
