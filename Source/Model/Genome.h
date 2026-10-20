#pragma once
#include <array>
#include <atomic>
#include <cstdint>
#include <string>
#include <string_view>

namespace transcription
{
constexpr int maxCodons = 32;
constexpr int maxBases = maxCodons * 3;

enum class Family { hydrophobic, polar, acidic, basic, stop };

struct Residue
{
    char letter;
    const char* name;
    const char* abbreviation;
    Family family;
    float gain;
    float q;
    float cents;
    float slope;
    float odd;
    float even;
    const char* character;
};

struct Genome
{
    std::array<char, maxBases> bases {};
    std::array<bool, maxBases> silent {};
    int size = 0;

    static bool parse(std::string_view text, Genome& result) noexcept;
    std::string dna() const;
    std::string rna() const;
    bool append(char base) noexcept;
    bool cycle(int index) noexcept;
    bool erase(int index) noexcept;
    bool insertCodon(int index, char first, char second, char third) noexcept;
    void setIntron(int firstCodon, int endCodon, bool muted) noexcept;
    bool isIntron(int codon) const noexcept;
    int codons() const noexcept { return size / 3; }
    static Genome minimal() noexcept;
    bool operator==(const Genome& other) const noexcept;
    bool operator!=(const Genome& other) const noexcept { return !(*this == other); }
};

struct EnvelopeShape
{
    float attack = 0.015f;
    float decay = 0.3f;
    float sustain = 0.65f;
    float release = 0.45f;
};

// 终止密码子决定音色姿态与 body 效果链拓扑：
// UAG Amber=pluck（Spore 孢子链）、UAA Ochre=pad（Nebula 星云链）、UGA Opal=lead（Voice 声腔链）。
enum class StopKind : int { pluck = 0, pad = 1, lead = 2 };

// 转录结果：分段（core/body/stop）+ key1/key2 映射后的 DSP 参数 + 终止 ADSR。
struct Transcript
{
    int coreCount = 0;                 // 核心区密码子数 0..3
    int bodyCount = 0;                 // 中间区密码子数
    StopKind stopKind = StopKind::pluck;
    EnvelopeShape envelope;            // 由终止密码子决定的 ADSR

    // key1 → 减法核心
    float coreResonance = 0.5f;
    int coreHarmonics = 6;
    float coreCutoff = 8000.0f;
    float coreSlope = 12.0f;
    float coreFormant = 1200.0f;
    float coreFormantAmt = 0.0f;
    float coreOddEven = 0.0f;
    float coreDrive = 0.0f;

    // key2 → body 效果链性状（链的拓扑由 stopKind 选择，见 Dsp/BodyFx.*）
    float fxGrowth = 0.0f;             // 生长度 [0,1]：body 长度 → 湿度 / 回授 / 尾音
    float fxSpace = 0.5f;              // 空间 [0,1]：GC 含量（热稳定性）→ 空间尺寸与衰减
    float fxMotion = 0.5f;             // 律动 [0,1]：极性残基 + 甘氨酸 → 调制深度
    float fxBreath = 0.5f;             // 呼吸 [0,1]：疏水残基 → 呼吸 / 漂移速率
    float fxTone = 0.0f;               // 色泽 [-1,1]：酸性(+) 明亮 / 碱性(−) 温暖
    float fxShimmer = 0.0f;            // 微光 [0,1]：芳香 / 环状残基（F W Y H P）→ 八度光晕
    std::uint32_t fxSeed = 1;          // body 序列哈希 → 每条基因独特的漂移性格

    float normalisation = 1.0f;        // 静态响度归一化系数
};

char complement(char base) noexcept;
char translate(char first, char second, char third) noexcept;
const Residue& residueInfo(char letter) noexcept;
const char* familyName(Family family) noexcept;
Transcript transcribe(const Genome& genome) noexcept;
Genome mutate(const Genome& genome, float probability, std::uint32_t seed) noexcept;
void enforceFrame(Genome& genome) noexcept;

class GenomeMailbox
{
public:
    void publish(const Genome& genome) noexcept;
    bool read(Genome& genome, std::uint32_t& lastVersion) const noexcept;
private:
    // Writers are serialised by the processor; the audio reader never waits.
    std::array<std::atomic<std::uint32_t>, 10> words {};
    std::atomic<std::uint32_t> version { 0 };
};
}
