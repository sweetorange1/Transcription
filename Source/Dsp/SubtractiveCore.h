#pragma once
#include <array>

namespace transcription
{

// 减法核心参数（由 transcript 的 key1 映射产出，无旋钮）。
struct CoreParams
{
    int harmonics = 16;          // 激活谐波数（coreHarmonics）
    float resonance = 0.5f;      // 谐振 Q 控制 [0,1]（coreResonance）
    float cutoff = 8000.0f;      // 频谱倾斜低通截止（coreCutoff，Hz）
    float slope = 12.0f;         // 谐波增益衰减斜率 dB/oct（coreSlope）
    float formant = 1200.0f;     // 共振峰频率（coreFormant，Hz）
    float formantAmt = 0.0f;     // 共振峰量 [0,1]（coreFormantAmt）
    float oddEven = 0.0f;        // 奇/偶谐波偏置 [-1,1]（coreOddEven）
    float drive = 0.0f;          // 预热饱和驱动 [0,1]（coreDrive）
};

// 减法核心：谐波谐振组 → 频谱倾斜 → 共振峰 → 谐波偏置（预热饱和）。
// 每 voice 一份；参数由 setParameters 设定，逐级激活系数由 process 传入。
class SubtractiveCore
{
public:
    void prepare(double sampleRate) noexcept;
    void reset() noexcept;
    void setParameters(double sampleRate, float f0, const CoreParams& params) noexcept;
    // activation[0..3] 对应谐振组/倾斜/共振峰/偏置的激活系数（0=旁路直通，1=完全激活）。
    float process(float input, const std::array<float, 4>& activation) noexcept;

private:
    struct Biquad
    {
        double s1 = 0.0, s2 = 0.0;
        double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0;
        double tb0 = 1.0, tb1 = 0.0, tb2 = 0.0, ta1 = 0.0, ta2 = 0.0;
        void lowpass(double rate, double freq, double q) noexcept;
        void bandpass(double rate, double freq, double q) noexcept;
        void peak(double rate, double freq, double q, double gainDb) noexcept;
        float tick(float x, float smoothing) noexcept;
        void snap() noexcept;
    };
    std::array<Biquad, 32> resonators;
    std::array<float, 32> gains {};
    Biquad tilt, formant;
    float drive = 0.0f;
    double sampleRate = 44100.0;
    float smoothing = 0.005f;
    int harmonicCount = 16;
};

}
