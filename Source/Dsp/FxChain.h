#pragma once
#include <array>
#include <vector>

namespace transcription
{

// 效果器参数（由 transcript 的 key2 映射产出，无旋钮）。
struct FxParams
{
    float drive = 0.0f;       // 饱和驱动 [0,1]（fxDrive）
    float depth = 0.0f;       // 失谐/合唱深度 [0,1]（fxDepth）
    float time = 0.0f;        // 回声时间（秒）（fxTime）
    float feedback = 0.0f;    // 回声反馈 [0,0.9]（fxFeedback）
    float size = 0.0f;        // 混响空间 [0,1]（fxSize）
    float damp = 0.0f;        // 混响阻尼 [0,1]（fxDamp）
};

// 效果器组：饱和 → 失谐/合唱 → 延迟 → 混响。每 voice 一份。
class FxChain
{
public:
    void prepare(double sampleRate) noexcept;
    void reset() noexcept;
    void setParameters(const FxParams& params) noexcept;
    // activation[0..3] 对应饱和/失谐/延迟/混响的激活系数。
    float process(float input, const std::array<float, 4>& activation) noexcept;

private:
    float saturate(float x) const noexcept;
    float chorus(float x) noexcept;
    float delay(float x) noexcept;
    float reverb(float x) noexcept;

    // 合唱调制延迟
    std::vector<float> chorusBuffer;
    int chorusWrite = 0;
    float chorusPhase = 0.0f;
    // 回声
    std::vector<float> delayBuffer;
    int delayWrite = 0;
    int delaySamples = 1;
    // 混响（Schroeder：4 comb + 2 allpass）
    struct Comb { std::vector<float> buffer; int write = 0; float feedback = 0.0f; float damp = 0.0f; float lowpass = 0.0f; };
    struct Allpass { std::vector<float> buffer; int write = 0; };
    std::array<Comb, 4> combs;
    std::array<Allpass, 2> allpasses;

    float fxDrive = 0.0f, fxDepth = 0.0f, fxFeedback = 0.0f, fxSize = 0.0f, fxDamp = 0.0f;
    double sampleRate = 44100.0;
};

}
