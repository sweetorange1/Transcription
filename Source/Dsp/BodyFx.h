#pragma once
#include "Dsp/FxBlocks.h"
#include "Model/Genome.h"
#include <array>
#include <cstdint>

namespace transcription
{
namespace fx
{
// body 区（C[4..N-2]）性状的效果器视图；由 transcribe() 的 key2 产出。
struct Genes
{
    float growth = 0.0f;    // 生长度：body 长度 → 湿度 / 回授 / 衰减
    float space = 0.5f;     // 空间：GC 含量 → 空间尺寸与尾音长度
    float motion = 0.5f;    // 律动：极性 + 甘氨酸 → 调制深度
    float breath = 0.5f;    // 呼吸：疏水残基 → 呼吸 / 漂移速率
    float tone = 0.0f;      // 色泽：酸性(+)明亮 / 碱性(−)温暖
    float shimmer = 0.0f;   // 微光：芳香 / 环状残基 → 八度泛音光晕
    std::uint32_t seed = 1; // 序列哈希 → 每条基因独特的漂移性格
    static Genes from(const Transcript& transcript) noexcept;
    float jitter(std::uint32_t salt) const noexcept;   // 确定性 [-1, 1]
};

using Activation = std::array<float, 4>;

// 光晕：M/S 展宽 + 空气感高频 + 极慢的有机声像漂移。
class Halo
{
public:
    void prepare(double sampleRate, std::uint32_t seed) noexcept;
    void reset() noexcept { air.reset(); }
    void setTargets(float width, float airAmount, float drift, float driftRate, bool snap) noexcept;
    void control() noexcept;
    void process(float& left, float& right) noexcept;

private:
    double sampleRate = 44100.0;
    OnePole air;
    OrganicLfo lfo;
    Glide width, airAmount, drift;
    float wobble = 0.0f;
};

// UAA Ochre / Pad —— 「Nebula 星云」：细胞膜呼吸 → 纤毛合唱 → 八度微光大厅 → 光晕。
class NebulaChain
{
public:
    void prepare(double sampleRate);
    void reset() noexcept;
    void configure(const Genes& genes, bool snap) noexcept;
    void control() noexcept;
    void process(float& left, float& right, const Activation& activation) noexcept;

private:
    double sampleRate = 44100.0;
    float msToSamples = 44.1f;
    std::array<Svf, 2> breathFilter;
    OrganicLfo breathLfo, swellLfo;
    Glide breathCut, breathDepth, swellDepth;
    float swell = 1.0f;
    DelayLine ensemble;
    std::array<OrganicLfo, 3> ensembleLfo;
    std::array<Glide, 3> ensembleBase;
    Glide ensembleDepth;
    FdnReverb hall;
    Glide hallMix;
    float hallDry = 1.0f, hallWet = 0.0f;
    Halo halo;
};

// UAG Amber / Pluck —— 「Spore 孢子」：胞质色散 → 扩散孢子回声 → 玻璃生物荧光板 → 光晕。
class SporeChain
{
public:
    void prepare(double sampleRate);
    void reset() noexcept;
    void configure(const Genes& genes, bool snap) noexcept;
    void control() noexcept;
    void process(float& left, float& right, const Activation& activation) noexcept;

private:
    double sampleRate = 44100.0;
    float msToSamples = 44.1f;
    std::array<Allpass, 4> bloomL, bloomR;
    std::array<Glide, 4> bloomLength;
    Glide bloomCoef;
    OrganicLfo bloomLfo;
    float bloomMod = 0.0f;
    DelayLine echoL, echoR;
    OrganicLfo wowL, wowR;
    OnePole toneL, toneR, lowL, lowR;
    Allpass scatterL, scatterR;
    Glide echoTime, echoRatio, echoFeedback, echoWet, wowDepth, scatterCoef, echoTone;
    float appliedTone = -1.0f;
    FdnReverb plate;
    Glide plateMix;
    float plateDry = 1.0f, plateWet = 0.0f;
    Halo halo;
};

// UGA Opal / Lead —— 「Voice 声腔」：元音共振光泽 → 微移双声 → 呼吸避让回声 → 大厅。
class VoiceChain
{
public:
    void prepare(double sampleRate);
    void reset() noexcept;
    void configure(const Genes& genes, bool snap) noexcept;
    void control() noexcept;
    void process(float& left, float& right, const Activation& activation) noexcept;

private:
    double sampleRate = 44100.0;
    float msToSamples = 44.1f;
    std::array<Svf, 2> formants;
    OrganicLfo vowelLfo;
    Glide vowelCenter, vowelSpan, glowAmount, formantShift;
    float glowComp = 1.0f;
    DelayLine doubler;
    OrganicLfo doubleL, doubleR;
    Glide doubleBaseL, doubleBaseR, doubleDepth;
    DelayLine echoL, echoR;
    OnePole toneL, toneR, lowL, lowR;
    Glide echoTime, echoRatio, echoFeedback, echoWet, echoTone;
    float appliedTone = -1.0f;
    float envelope = 0.0f, envAttack = 0.01f, envRelease = 0.001f;
    FdnReverb hall;
    Glide hallMix;
    float hallDry = 1.0f, hallWet = 0.0f;
};
}

// body 区效果总线：位于声部求和与自动响度之后、DC/限幅之前（全局立体声，一份）。
// 终止密码子选择拓扑（三条完全不同的链），body 性状驱动链内参数；切换终止密码子时
// 新旧两条链做 80 ms 线性交叉淡化（每条链输出都含干声，线性加权和保持干声不变）。
class BodyFx
{
public:
    void prepare(double sampleRate);
    void reset() noexcept;
    void configure(const Transcript& transcript) noexcept;
    void setActivation(const fx::Activation& activation) noexcept;
    // 整体湿音深度（0=干声，1=当前完整效果）。由转录进度驱动：第 4 个密码子处为 0，
    // 到终止密码子才升到 1，因此效果不会在读完前几个密码子时就“到位”。
    void setWet(float wet) noexcept;
    void process(float& left, float& right) noexcept;
    StopKind kind() const noexcept { return active; }

private:
    void configureChain(int index, bool snap) noexcept;
    void resetChain(int index) noexcept;
    void controlChain(int index) noexcept;
    void processChain(int index, float& left, float& right) noexcept;

    fx::SporeChain spore;     // StopKind::pluck
    fx::NebulaChain nebula;   // StopKind::pad
    fx::VoiceChain voice;     // StopKind::lead
    fx::Genes genes;
    StopKind active = StopKind::pluck;
    std::array<float, 3> gain { 1.0f, 0.0f, 0.0f }, target { 1.0f, 0.0f, 0.0f };
    fx::Activation activation {}, activationTarget {};
    float wet = 1.0f, wetTarget = 1.0f;
    double sampleRate = 44100.0;
    float activationCoeff = 0.001f, fadeStep = 0.0005f;
    int controlCount = 0, quietSamples = 0, sleepAfter = 22050;
    bool prepared = false, fresh = true, bypassed = true;
};
}
