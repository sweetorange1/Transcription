#pragma once
#include "Model/Genome.h"
#include "Dsp/SubtractiveCore.h"
#include "Dsp/FxChain.h"
#include <array>
#include <cstdint>

namespace transcription
{
constexpr int voiceCount = 8;

// 旧参数结构保留（参数 ID 与宿主状态兼容），字段已全部降级为 no-op，不再驱动 DSP。
struct Controls
{
    float precision = 0.65f;
    float mutation = 0.08f;
    float attackScale = 1.0f;
    float releaseScale = 1.0f;
    float width = 0.45f;
    float outputDb = -3.0f;
    int noise = 0;
};

class Engine
{
public:
    void prepare(double sampleRate) noexcept;
    void reset() noexcept;
    void setTranscript(const Transcript& transcript) noexcept;
    void setScanPhase(float phase) noexcept;   // 全局转录进度 [0,1]：0=纯噪声，1=完整音色
    void noteOn(int channel, int note, float velocity) noexcept;
    void noteOff(int channel, int note) noexcept;
    void pitchWheel(int channel, int value) noexcept;
    void controller(int channel, int number, int value) noexcept;
    void render(float* left, float* right, int samples) noexcept;
    int activeVoices() const noexcept;
    bool isNoteActive(int channel, int note) const noexcept;
    float peak() const noexcept { return lastPeak; }

private:
    enum class Stage { idle, attack, decay, sustain, release };
    struct Voice
    {
        SubtractiveCore core;
        FxChain fx;
        Stage stage = Stage::idle;
        int channel = 0, note = 60;
        bool keyDown = false;
        std::uint64_t age = 0;
        std::uint32_t random = 1;
        float velocity = 0.0f, envelope = 0.0f;
        float attackStep = 0.01f, decayMultiplier = 0.999f, releaseMultiplier = 0.999f;
        float sustain = 0.7f, channelGain = 1.0f, targetChannelGain = 1.0f;
        float f0 = 440.0f;
        std::array<float, 4> coreActivation { 0.0f, 0.0f, 0.0f, 0.0f };
        std::array<float, 4> fxActivation { 0.0f, 0.0f, 0.0f, 0.0f };
        float lastL = 0.0f, lastR = 0.0f, stolenL = 0.0f, stolenR = 0.0f;
        int stealRemaining = 0;
        float nextEnvelope() noexcept;
        float whiteNoise() noexcept;
        void release() noexcept;
    };
    void refresh(Voice& voice, bool snap) noexcept;
    static float limit(float sample) noexcept;
    Transcript transcript;
    std::array<Voice, voiceCount> voices {};
    std::array<bool, 16> pedal {};
    std::array<float, 16> bend {}, wheel {}, volume {}, expression {};
    double sampleRate = 44100.0;
    float smoothing = 0.005f, dcPole = 0.999f;
    float scanPhase = 1.0f;         // 无编辑器时默认 complete（完整音色）
    float rmsFollower = 0.0f, autoGain = 1.0f;
    float agcAttack = 0.01f, agcRelease = 0.005f, agcSmooth = 0.02f, targetRms = 0.12f;
    std::array<float, 2> dcInput {}, dcOutput {};
    std::uint64_t serial = 0;
    int refreshCountdown = 0, stealSamples = 220;
    float lastPeak = 0.0f;
};
}
