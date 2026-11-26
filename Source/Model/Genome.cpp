#include "Genome.h"
#include <algorithm>
#include <cmath>

namespace transcription
{
namespace
{
constexpr std::array<Residue, 21> residues {{
    { 'G', "Glycine",       "Gly", Family::hydrophobic, 1.00f, 310,  0.0f, 1.85f, 1.00f, 0.70f, "Flexible / pure fundamental" },
    { 'A', "Alanine",       "Ala", Family::hydrophobic, 0.82f, 230,  0.0f, 1.30f, 0.85f, 1.12f, "Small / soft upper partials" },
    { 'V', "Valine",        "Val", Family::hydrophobic, 0.95f, 170,  2.5f, 0.95f, 1.00f, 1.00f, "Branched / slight detuning" },
    { 'L', "Leucine",       "Leu", Family::hydrophobic, 1.00f, 200, -1.5f, 0.95f, 0.65f, 1.35f, "Branched / even harmonics" },
    { 'I', "Isoleucine",    "Ile", Family::hydrophobic, 0.96f, 215,  1.5f, 0.95f, 0.70f, 1.30f, "Isomer / paired partials" },
    { 'P', "Proline",       "Pro", Family::hydrophobic, 1.04f, 250,  0.0f, 0.85f, 1.45f, 0.42f, "Rigid ring / odd harmonics" },
    { 'F', "Phenylalanine", "Phe", Family::hydrophobic, 1.20f, 320,  0.0f, 0.63f, 1.00f, 1.00f, "Aromatic / narrow resonance" },
    { 'W', "Tryptophan",    "Trp", Family::hydrophobic, 1.12f,  65, -1.0f, 0.60f, 1.00f, 1.00f, "Indole / broad resonance" },
    { 'M', "Methionine",    "Met", Family::hydrophobic, 1.25f, 220,  0.0f, 1.05f, 1.00f, 1.00f, "Initiation / tonal anchor" },
    { 'S', "Serine",        "Ser", Family::polar,       1.08f, 125,  0.0f, 0.68f, 1.00f, 1.00f, "Hydroxyl / bright overtones" },
    { 'T', "Threonine",     "Thr", Family::polar,       1.05f, 145,  3.0f, 0.68f, 1.00f, 1.00f, "Hydroxyl / bright and moving" },
    { 'C', "Cysteine",      "Cys", Family::polar,       1.06f, 240,  0.0f, 1.00f, 0.70f, 1.40f, "Disulfide / warm even partials" },
    { 'N', "Asparagine",    "Asn", Family::polar,       1.03f, 155, -0.7f, 0.82f, 1.00f, 1.00f, "Amide / middle resonance" },
    { 'Q', "Glutamine",     "Gln", Family::polar,       1.08f, 165,  0.7f, 0.86f, 1.00f, 1.00f, "Amide / rounded resonance" },
    { 'Y', "Tyrosine",      "Tyr", Family::polar,       1.24f, 280,  0.0f, 0.52f, 1.00f, 1.00f, "Phenol / upper resonance" },
    { 'D', "Aspartate",     "Asp", Family::acidic,      1.20f, 350, -0.5f, 0.52f, 1.10f, 0.95f, "Acidic / sharp and bright" },
    { 'E', "Glutamate",     "Glu", Family::acidic,      1.17f, 300,  0.5f, 0.58f, 1.10f, 0.95f, "Acidic / bright resonance" },
    { 'K', "Lysine",        "Lys", Family::basic,       1.25f, 300,  0.0f, 1.25f, 0.90f, 1.12f, "Basic / warm lower spectrum" },
    { 'R', "Arginine",      "Arg", Family::basic,       1.30f, 390,  0.0f, 1.12f, 0.90f, 1.12f, "Basic / strong resonance" },
    { 'H', "Histidine",     "His", Family::basic,       1.08f, 190,  0.0f, 0.83f, 1.00f, 1.00f, "pH-sensitive / wheel motion" },
    { '*', "Termination",   "Stop", Family::stop,      0.00f, 100,  0.0f, 1.00f, 1.00f, 1.00f, "Boundary / no expression" }
}};
char normalise(char c) noexcept
{
    if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
    if (c == 'U') c = 'T';
    return c;
}
int baseIndex(char c) noexcept
{
    switch (normalise(c)) { case 'T': return 0; case 'C': return 1; case 'A': return 2; case 'G': return 3; default: return -1; }
}
std::uint32_t randomStep(std::uint32_t& state) noexcept
{
    state ^= state << 13; state ^= state >> 17; state ^= state << 5;
    return state;
}
float familyCutoff(Family family) noexcept
{
    switch (family)
    {
        case Family::hydrophobic: return 2800.0f;
        case Family::basic: return 5400.0f;
        case Family::polar: return 10500.0f;
        case Family::acidic: return 16500.0f;
        default: return 8000.0f;
    }
}
float familyFormant(Family family) noexcept
{
    switch (family)
    {
        case Family::basic: return 390.0f;
        case Family::hydrophobic: return 540.0f;
        case Family::polar: return 740.0f;
        case Family::acidic: return 1040.0f;
        default: return 680.0f;
    }
}
float familyFormant2(Family family) noexcept
{
    switch (family)
    {
        case Family::hydrophobic: return 1180.0f;
        case Family::basic: return 1580.0f;
        case Family::polar: return 2380.0f;
        case Family::acidic: return 3350.0f;
        default: return 1850.0f;
    }
}
float residueBrightness(char aa, Family family) noexcept
{
    switch (aa)
    {
        case 'G': return 0.10f;   // 极简纯音
        case 'A': return 0.24f;
        case 'L': return 0.34f;
        case 'I': return 0.38f;
        case 'V': return 0.42f;
        case 'M': return 0.46f;
        case 'K': return 0.36f;
        case 'R': return 0.48f;
        case 'C': return 0.40f;
        case 'P': return 0.58f;
        case 'Q': return 0.56f;
        case 'N': return 0.62f;
        case 'H': return 0.66f;
        case 'W': return 0.64f;
        case 'F': return 0.78f;
        case 'S': return 0.76f;
        case 'T': return 0.80f;
        case 'Y': return 0.90f;
        case 'E': return 0.88f;
        case 'D': return 0.96f;
        default:  return family == Family::acidic ? 0.9f : 0.45f;
    }
}
float residueInharmonicity(char aa) noexcept
{
    switch (aa)
    {
        case 'W': return 0.95f;   // 吲哚双环：钢片琴/玻璃晶鸣
        case 'Y': return 0.88f;   // 苯酚环：清脆水晶泛音
        case 'F': return 0.78f;   // 苯环：清透铃音
        case 'H': return 0.65f;   // 咪唑环：温润钟鸣
        case 'P': return 0.52f;   // 刚性吡咯环：木质/管乐微刚度
        case 'C': return 0.22f;   // 硫键微光泽
        case 'M': return 0.16f;
        default:  return 0.0f;
    }
}
float residueAir(char aa) noexcept
{
    switch (aa)
    {
        case 'W': return 0.85f;   // 宽 Q 吲哚共鸣 → 丝绒弓弦气声
        case 'S': return 0.72f;   // 羟基清风
        case 'T': return 0.68f;
        case 'N': return 0.52f;
        case 'Q': return 0.48f;
        case 'H': return 0.44f;
        case 'V': return 0.34f;
        case 'A': return 0.28f;
        case 'G': return 0.18f;
        default:  return 0.22f;
    }
}
float residueMorph(char aa, Family family) noexcept
{
    // 正值 = 起音清亮敲击后温柔收拢（竖琴/玻璃感）；负值 = 起音雾感随后泛音绽放（弓弦/人声感）
    switch (aa)
    {
        case 'D': case 'E': case 'Y': case 'F': return 0.85f;
        case 'P': case 'S': case 'T': return 0.52f;
        case 'H': case 'W': return 0.28f;
        case 'G': case 'A': return -0.18f;
        case 'L': case 'I': case 'C': return -0.62f;
        case 'K': case 'R': return -0.78f;
        case 'V': case 'M': return -0.35f;
        case 'N': case 'Q': return -0.45f;
        default: return family == Family::acidic ? 0.6f : -0.2f;
    }
}
float residueSubWarmth(char aa, Family family) noexcept
{
    // 醇厚疏水支链与碱性带电残基赋予低八度温暖胸腔感（大提琴/暖管风琴），极性/酸性保持轻盈清透。
    switch (aa)
    {
        case 'L': return 0.92f;
        case 'W': return 0.85f;
        case 'R': return 0.88f;
        case 'K': return 0.80f;
        case 'C': return 0.78f;
        case 'I': return 0.72f;
        case 'M': return 0.68f;
        case 'A': return 0.50f;
        case 'V': return 0.35f;
        case 'Q': return 0.28f;
        case 'H': return 0.20f;
        default:  return family == Family::basic ? 0.6f : 0.0f;
    }
}
float residuePresence(char aa, Family family) noexcept
{
    // 羟基/酚基/酸性/芳环驱动清透高频结晶光泽（荧光竖琴/水晶铃音/空气合唱）。
    switch (aa)
    {
        case 'D': return 1.00f;
        case 'Y': return 0.96f;
        case 'E': return 0.90f;
        case 'S': return 0.86f;
        case 'T': return 0.84f;
        case 'F': return 0.80f;
        case 'H': return 0.64f;
        case 'P': return 0.60f;
        case 'N': return 0.56f;
        case 'Q': return 0.44f;
        case 'V': return 0.36f;
        case 'W': return 0.38f;
        default:  return family == Family::acidic ? 0.9f : 0.10f;
    }
}
float residueOddEvenBias(const Residue& r) noexcept
{
    // 在原有 odd/even 表之上进一步拉开典型结构家族的奇偶泛音性格：
    // 环状/酸性偏向空心奇次（木管/水晶管风琴），支链/硫键/碱性偏向饱满偶次（温暖弦乐/竖琴）。
    const float base = (r.odd - r.even) / std::max(1.0e-6f, r.odd + r.even);
    switch (r.letter)
    {
        case 'P': return 0.82f;    // 强奇次空心单簧管
        case 'D': return 0.55f;
        case 'E': return 0.44f;
        case 'F': return 0.36f;
        case 'Y': return 0.30f;
        case 'S': return 0.24f;
        case 'L': return -0.64f;   // 饱满偶次温暖大提琴
        case 'C': return -0.72f;   // 偶次八度丝绒共鸣
        case 'I': return -0.52f;
        case 'K': return -0.48f;
        case 'R': return -0.42f;
        case 'A': return -0.36f;
        case 'W': return -0.26f;
        case 'Q': return -0.22f;
        case 'H': return 0.18f;
        default:  return std::clamp(base * 1.6f, -0.85f, 0.85f);
    }
}
StopKind stopKindOf(char b1, char b2) noexcept
{
    if (b1 == 'A') return b2 == 'G' ? StopKind::pluck : StopKind::pad;   // TAG → pluck，TAA → pad
    return StopKind::lead;                                                // TGA → lead
}
EnvelopeShape stopEnvelope(StopKind kind) noexcept
{
    switch (kind)
    {
        case StopKind::pluck: return { 0.004f, 0.18f, 0.25f, 0.12f };
        case StopKind::pad:   return { 0.60f, 0.50f, 0.80f, 1.60f };
        default:              return { 0.02f, 0.15f, 0.70f, 0.35f };
    }
}
}

bool Genome::parse(std::string_view text, Genome& result) noexcept
{
    Genome parsed;
    for (char c : text)
    {
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') continue;
        if (!parsed.append(c)) return false;
    }
    result = parsed;
    return true;
}
std::string Genome::dna() const { return { bases.data(), static_cast<size_t>(size) }; }
std::string Genome::rna() const
{
    auto text = dna();
    std::replace(text.begin(), text.end(), 'T', 'U');
    return text;
}
bool Genome::append(char base) noexcept
{
    base = normalise(base);
    if (size >= maxBases || baseIndex(base) < 0) return false;
    bases[static_cast<size_t>(size)] = base;
    silent[static_cast<size_t>(size++)] = false;
    return true;
}
bool Genome::cycle(int index) noexcept
{
    if (index < 0 || index >= size) return false;
    auto& c = bases[static_cast<size_t>(index)];
    switch (c) { case 'A': c = 'T'; break; case 'T': c = 'C'; break; case 'C': c = 'G'; break; default: c = 'A'; break; }
    return true;
}
bool Genome::erase(int index) noexcept
{
    if (index < 0 || index >= size) return false;
    for (int i = index; i + 1 < size; ++i)
    {
        bases[static_cast<size_t>(i)] = bases[static_cast<size_t>(i + 1)];
        silent[static_cast<size_t>(i)] = silent[static_cast<size_t>(i + 1)];
    }
    bases[static_cast<size_t>(--size)] = 0;
    silent[static_cast<size_t>(size)] = false;
    return true;
}
bool Genome::insertCodon(int index, char first, char second, char third) noexcept
{
    first = normalise(first); second = normalise(second); third = normalise(third);
    if (index < 0 || index > codons() || size + 3 > maxBases) return false;
    if (baseIndex(first) < 0 || baseIndex(second) < 0 || baseIndex(third) < 0) return false;
    for (int i = size - 1; i >= index * 3; --i)
    {
        bases[static_cast<size_t>(i + 3)] = bases[static_cast<size_t>(i)];
        silent[static_cast<size_t>(i + 3)] = silent[static_cast<size_t>(i)];
    }
    bases[static_cast<size_t>(index * 3)] = first;
    bases[static_cast<size_t>(index * 3 + 1)] = second;
    bases[static_cast<size_t>(index * 3 + 2)] = third;
    silent[static_cast<size_t>(index * 3)] = false;
    silent[static_cast<size_t>(index * 3 + 1)] = false;
    silent[static_cast<size_t>(index * 3 + 2)] = false;
    size += 3;
    return true;
}
Genome Genome::minimal() noexcept
{
    Genome genome;
    genome.append('A'); genome.append('T'); genome.append('G');
    genome.append('T'); genome.append('G'); genome.append('A');
    return genome;
}
void Genome::setIntron(int firstCodon, int endCodon, bool muted) noexcept
{
    for (int i = std::clamp(firstCodon * 3, 0, size); i < std::clamp(endCodon * 3, 0, size); ++i)
        silent[static_cast<size_t>(i)] = muted;
}
bool Genome::isIntron(int codon) const noexcept
{
    if (codon < 0 || codon >= codons()) return false;
    for (int j = 0; j < 3; ++j)
        if (silent[static_cast<size_t>(codon * 3 + j)]) return true;
    return false;
}
bool Genome::operator==(const Genome& other) const noexcept
{
    if (size != other.size) return false;
    for (int i = 0; i < size; ++i)
        if (bases[static_cast<size_t>(i)] != other.bases[static_cast<size_t>(i)]
            || silent[static_cast<size_t>(i)] != other.silent[static_cast<size_t>(i)]) return false;
    return true;
}
char complement(char base) noexcept
{
    switch (base) { case 'A': return 'T'; case 'T': return 'A'; case 'C': return 'G'; case 'G': return 'C'; default: return ' '; }
}
char translate(char first, char second, char third) noexcept
{
    constexpr char code[] = "FFLLSSSSYY**CC*WLLLLPPPPHHQQRRRRIIIMTTTTNNKKSSRRVVVVAAAADDEEGGGG";
    const int a = baseIndex(first), b = baseIndex(second), c = baseIndex(third);
    return a < 0 || b < 0 || c < 0 ? '*' : code[a * 16 + b * 4 + c];
}
const Residue& residueInfo(char letter) noexcept
{
    for (const auto& r : residues) if (r.letter == letter) return r;
    return residues.back();
}
const char* familyName(Family family) noexcept
{
    switch (family)
    {
        case Family::hydrophobic: return "HYDROPHOBIC";
        case Family::polar: return "POLAR / UNCHARGED";
        case Family::acidic: return "ACIDIC / NEGATIVE";
        case Family::basic: return "BASIC / POSITIVE";
        default: return "STOP CODON";
    }
}

Transcript transcribe(const Genome& genome) noexcept
{
    Transcript result;
    const int n = genome.codons();

    // 分段：C[0]=ATG 启动子，C[1..3]=核心 core，C[4..N-2]=中间 body，C[N-1]=终止。
    result.coreCount = std::clamp(n - 2, 0, 3);
    result.bodyCount = std::max(0, n - 5);

    // 终止密码子（DNA 形式）→ 音色姿态 + ADSR。
    if (n >= 1)
    {
        const int last = (n - 1) * 3;
        result.stopKind = stopKindOf(genome.bases[static_cast<size_t>(last + 1)], genome.bases[static_cast<size_t>(last + 2)]);
    }
    result.envelope = stopEnvelope(result.stopKind);

    // ---- key1：核心区 C[1 .. 1+coreCount-1]，位置分工打破“三残基均值坍缩” ----
    // C[1]（核心首残基）主导谐波骨架与暖/亮基因性格；
    // C[2]（核心中残基）主导双共振峰声学共鸣腔与滤波带宽；
    // C[3]（核心末残基）主导气声质感、起音频谱演化与动态活力。
    if (result.coreCount > 0)
    {
        std::array<char, 3> coreAA {};
        std::array<const Residue*, 3> coreRes {};
        float qSum = 0.0f, slopeSum = 0.0f, gainSum = 0.0f;
        float centsAbsSum = 0.0f;
        int polarCount = 0, chargedCount = 0, uniqueFamilies = 0;
        std::array<bool, 4> seenFamily {};

        for (int idx = 0; idx < result.coreCount; ++idx)
        {
            const int off = (idx + 1) * 3;
            const char aa = translate(genome.bases[static_cast<size_t>(off)],
                                      genome.bases[static_cast<size_t>(off + 1)],
                                      genome.bases[static_cast<size_t>(off + 2)]);
            coreAA[static_cast<size_t>(idx)] = aa;
            const auto& r = residueInfo(aa);
            coreRes[static_cast<size_t>(idx)] = &r;
            qSum += r.q; slopeSum += r.slope; gainSum += r.gain;
            centsAbsSum += std::abs(r.cents);
            if (r.family == Family::polar) ++polarCount;
            if (r.family == Family::acidic || r.family == Family::basic) ++chargedCount;
            const int famIdx = static_cast<int>(r.family);
            if (famIdx >= 0 && famIdx < 4 && !seenFamily[static_cast<size_t>(famIdx)])
            {
                seenFamily[static_cast<size_t>(famIdx)] = true;
                ++uniqueFamilies;
            }
        }
        const float c = static_cast<float>(result.coreCount);
        const float meanQ = qSum / c, meanSlope = slopeSum / c, meanGain = gainSum / c;

        // 位置加权平均工具：指定主导位占 68%，其余核心残基共占 32%，既防均值坍缩又保持整体协作。
        const auto posWeighted = [&](int primaryIdx, auto fn) -> float
        {
            if (result.coreCount == 1) return fn(0);
            const int p = std::min(primaryIdx, result.coreCount - 1);
            float wSum = 0.0f, vSum = 0.0f;
            for (int k = 0; k < result.coreCount; ++k)
            {
                const float w = (k == p) ? 0.68f : (0.32f / static_cast<float>(result.coreCount - 1));
                wSum += w;
                vSum += w * fn(k);
            }
            return vSum / wSum;
        };

        // 主导位 0（C[1]）：谐波骨架、奇偶偏置、晶体微伸展、低八度暖腔
        const float domSlope = posWeighted(0, [&](int k) { return coreRes[static_cast<size_t>(k)]->slope; });
        const float blendSlope = 0.55f * domSlope + 0.45f * meanSlope;
        const float normalized = std::clamp((1.82f - blendSlope) / 1.30f, 0.0f, 1.0f);
        result.coreHarmonics = static_cast<int>(std::lround(std::clamp(1.0f + 7.0f * std::pow(normalized, 1.45f), 1.0f, 8.0f)));
        result.coreOddEven = std::clamp(posWeighted(0, [&](int k) { return residueOddEvenBias(*coreRes[static_cast<size_t>(k)]); }), -0.92f, 0.92f);
        // 芳香环只要出现在核心任意位都能点亮部分晶体伸展，首残基为芳香环时最强。
        float maxInharm = 0.0f;
        for (int k = 0; k < result.coreCount; ++k)
            maxInharm = std::max(maxInharm, residueInharmonicity(coreAA[static_cast<size_t>(k)]));
        const float wInharm = posWeighted(0, [&](int k) { return residueInharmonicity(coreAA[static_cast<size_t>(k)]); });
        result.coreInharmonicity = std::clamp(0.55f * wInharm + 0.45f * maxInharm, 0.0f, 1.0f);
        result.coreSubWarmth = std::clamp(posWeighted(0, [&](int k)
        {
            return residueSubWarmth(coreAA[static_cast<size_t>(k)], coreRes[static_cast<size_t>(k)]->family);
        }), 0.0f, 1.0f);

        // 主导位 1（C[2]）：双共振峰共鸣腔、低通明暗、谐振品质因数
        const float domQ = posWeighted(1, [&](int k) { return coreRes[static_cast<size_t>(k)]->q; });
        result.coreResonance = std::clamp((0.65f * domQ + 0.35f * meanQ) / 390.0f, 0.0f, 1.0f);
        const float domCutoff = posWeighted(1, [&](int k) { return familyCutoff(coreRes[static_cast<size_t>(k)]->family); });
        result.coreCutoff = std::clamp(domCutoff * (0.72f + 0.48f * meanGain), 1200.0f, 18500.0f);
        result.coreFormant = std::clamp(posWeighted(1, [&](int k)
        {
            const auto& r = *coreRes[static_cast<size_t>(k)];
            const float qNorm = std::clamp((r.q - 65.0f) / 325.0f, 0.0f, 1.0f);
            return familyFormant(r.family) * (0.80f + 0.42f * qNorm);
        }), 240.0f, 2400.0f);
        result.coreFormant2 = std::clamp(posWeighted(1, [&](int k)
        {
            const auto& r = *coreRes[static_cast<size_t>(k)];
            const float qNorm = std::clamp((r.q - 65.0f) / 325.0f, 0.0f, 1.0f);
            return familyFormant2(r.family) * (0.78f + 0.46f * (1.0f - qNorm * 0.55f));
        }), 850.0f, 4800.0f);

        // 主导位 2（C[3]）：丝绒气声、频谱演化方向、明亮光泽与五度晶格
        result.coreAir = std::clamp(posWeighted(2, [&](int k) { return residueAir(coreAA[static_cast<size_t>(k)]); }), 0.0f, 1.0f);
        result.coreMorph = std::clamp(posWeighted(2, [&](int k)
        {
            return residueMorph(coreAA[static_cast<size_t>(k)], coreRes[static_cast<size_t>(k)]->family);
        }), -1.0f, 1.0f);
        result.coreBrightness = std::clamp(0.55f * posWeighted(0, [&](int k)
        {
            return residueBrightness(coreAA[static_cast<size_t>(k)], coreRes[static_cast<size_t>(k)]->family);
        }) + 0.45f * posWeighted(2, [&](int k)
        {
            return residueBrightness(coreAA[static_cast<size_t>(k)], coreRes[static_cast<size_t>(k)]->family);
        }), 0.0f, 1.0f);
        result.corePresence = std::clamp(posWeighted(0, [&](int k)
        {
            return residuePresence(coreAA[static_cast<size_t>(k)], coreRes[static_cast<size_t>(k)]->family);
        }), 0.0f, 1.0f);
        result.coreFifthLift = std::clamp(posWeighted(0, [&](int k)
        {
            const char aa = coreAA[static_cast<size_t>(k)];
            if (aa == 'S' || aa == 'T' || aa == 'Y') return 1.0f;
            if (aa == 'N' || aa == 'Q' || aa == 'F' || aa == 'H') return 0.65f;
            return 0.0f;
        }), 0.0f, 1.0f);

        result.coreSlope = std::clamp(2.2f + blendSlope * 9.5f, 4.0f, 20.0f);
        const float polarRatio = static_cast<float>(polarCount) / c;
        const float chargedRatio = static_cast<float>(chargedCount) / c;
        result.coreFormantAmt = std::clamp(0.36f + 0.44f * polarRatio + 0.20f * chargedRatio, 0.0f, 1.0f);
        result.coreDrive = chargedRatio;
        const float diversity = static_cast<float>(std::max(0, uniqueFamilies - 1)) / 2.0f;
        result.coreEnsemble = std::clamp((centsAbsSum / c) / 2.2f * 0.65f + diversity * 0.45f, 0.0f, 1.0f);
    }

    // ---- key2：中间区 C[4 .. N-2] → body 效果链性状（拓扑由终止密码子决定） ----
    const int bodyStart = 4, bodyEnd = n - 1;
    int bodyGc = 0, bodyBases = 0, bodyPolar = 0, bodyHydro = 0, bodyAcidic = 0, bodyBasic = 0, bodyRing = 0, bodyGly = 0;
    int bodyBrightPolar = 0, bodyHeavyHydro = 0;
    std::uint32_t hash = 2166136261u;   // FNV-1a
    for (int i = bodyStart; i < bodyEnd; ++i)
    {
        const int off = i * 3;
        for (int j = 0; j < 3; ++j)
        {
            const char base = genome.bases[static_cast<size_t>(off + j)];
            if (base == 'C' || base == 'G') ++bodyGc;
            ++bodyBases;
            hash = (hash ^ static_cast<std::uint32_t>(static_cast<unsigned char>(base))) * 16777619u;
        }
        const char aa = translate(genome.bases[static_cast<size_t>(off)], genome.bases[static_cast<size_t>(off + 1)], genome.bases[static_cast<size_t>(off + 2)]);
        const auto& r = residueInfo(aa);
        if (r.family == Family::polar) ++bodyPolar;
        if (r.family == Family::hydrophobic) ++bodyHydro;
        if (r.family == Family::acidic) ++bodyAcidic;
        if (r.family == Family::basic) ++bodyBasic;
        if (aa == 'F' || aa == 'W' || aa == 'Y' || aa == 'H' || aa == 'P') ++bodyRing;
        if (aa == 'G') ++bodyGly;
        if (aa == 'S' || aa == 'T' || aa == 'Y' || aa == 'F') ++bodyBrightPolar;
        if (aa == 'L' || aa == 'I' || aa == 'M' || aa == 'A') ++bodyHeavyHydro;
    }
    const int L = result.bodyCount;
    if (L > 0)
    {
        const float len = static_cast<float>(L);
        // 生长度：L=1 → 0.15，L=6 → 0.63，L=27（满序列）→ 0.99；饱和曲线，短 body 也立即可听。
        result.fxGrowth = 1.0f - std::exp(-len / 6.0f);
        // GC 含量 25%..75% 映射到 0..1（GC 越高双链越稳定 → 空间越大、尾音越长）。
        const float gc = static_cast<float>(bodyGc) / static_cast<float>(std::max(1, bodyBases));
        result.fxSpace = std::clamp((gc - 0.25f) / 0.5f, 0.0f, 1.0f);
        result.fxMotion = std::clamp((static_cast<float>(bodyPolar) + 0.5f * static_cast<float>(bodyGly)) / len / 0.6f, 0.0f, 1.0f);
        result.fxBreath = std::clamp(static_cast<float>(bodyHydro) / len / 0.7f, 0.0f, 1.0f);
        // 色泽：酸碱差为主（3x），叠加极性亮残基 vs 疏水醇厚残基的温和化学色彩，打破真实蛋白均值归零。
        const float chargeTone = 3.0f * static_cast<float>(bodyAcidic - bodyBasic) / len;
        const float hydroPolarTone = 0.85f * static_cast<float>(bodyBrightPolar - bodyHeavyHydro) / len;
        result.fxTone = std::clamp(chargeTone + hydroPolarTone, -1.0f, 1.0f);
        result.fxShimmer = std::clamp(static_cast<float>(bodyRing) / len / 0.35f, 0.0f, 1.0f);
        result.fxSeed = hash == 0 ? 1u : hash;
    }

    // 静态归一化：谐振组已在能量守恒下输出，这里保持中性，残余差异交给慢速 AGC 微调。
    result.normalisation = 1.0f;
    return result;
}

Genome mutate(const Genome& genome, float probability, std::uint32_t seed) noexcept
{
    Genome result = genome;
    std::array<int, maxBases> candidates {};
    int count = 0, changed = 0;
    const float chance = std::isfinite(probability) ? std::clamp(probability, 0.0f, 1.0f) : 0.0f;
    if (seed == 0) seed = 0x6d2b79f5u;
    const int lastCodon = genome.codons() - 1;
    for (int i = 0; i < genome.size; ++i)
    {
        if (i / 3 == 0 || i / 3 == lastCodon) continue;   // 跳过起始与终止密码子
        candidates[static_cast<size_t>(count++)] = i;
        if (static_cast<float>(randomStep(seed) >> 8) / 16777216.0f < chance)
        {
            const int cycles = 1 + static_cast<int>(randomStep(seed) % 3);
            for (int j = 0; j < cycles; ++j) result.cycle(i);
            ++changed;
        }
    }
    if (changed == 0 && count > 0)
        result.cycle(candidates[static_cast<size_t>(randomStep(seed) % static_cast<std::uint32_t>(count))]);
    return result;
}

void enforceFrame(Genome& genome) noexcept
{
    const auto isStart = [&](int i)
    {
        return i + 3 <= genome.size && genome.bases[static_cast<size_t>(i)] == 'A'
            && genome.bases[static_cast<size_t>(i + 1)] == 'T' && genome.bases[static_cast<size_t>(i + 2)] == 'G';
    };
    const auto isStop = [&](int i)
    {
        if (i + 3 > genome.size || genome.bases[static_cast<size_t>(i)] != 'T') return false;
        const char b1 = genome.bases[static_cast<size_t>(i + 1)];
        const char b2 = genome.bases[static_cast<size_t>(i + 2)];
        return (b1 == 'A' && (b2 == 'A' || b2 == 'G')) || (b1 == 'G' && b2 == 'A');
    };

    // 丢弃残缺尾碱基，保证整密码子边界。
    genome.size -= genome.size % 3;

    // 空序列补为最小框架 ATG + TGA。
    if (genome.size == 0)
    {
        genome = Genome::minimal();
        return;
    }
    // 首密码子非 ATG 时补起始密码子（满序列则直接覆盖首 3 位）。
    if (!isStart(0))
    {
        if (genome.size + 3 <= maxBases) genome.insertCodon(0, 'A', 'T', 'G');
        else { genome.bases[0] = 'A'; genome.bases[1] = 'T'; genome.bases[2] = 'G'; }
    }
    // 尾密码子非 Stop 时补终止密码子（满序列则覆盖末尾 3 位）。
    const int last = genome.size - 3;
    if (last >= 0 && !isStop(last))
    {
        if (genome.size + 3 <= maxBases) genome.insertCodon(genome.codons(), 'T', 'G', 'A');
        else
        {
            genome.bases[static_cast<size_t>(last)] = 'T';
            genome.bases[static_cast<size_t>(last + 1)] = 'G';
            genome.bases[static_cast<size_t>(last + 2)] = 'A';
        }
    }
}

void GenomeMailbox::publish(const Genome& genome) noexcept
{
    std::array<std::uint32_t, 10> packed {};
    packed[9] = static_cast<std::uint32_t>(genome.size);
    for (int i = 0; i < genome.size; ++i)
    {
        packed[static_cast<size_t>(i / 16)] |= static_cast<std::uint32_t>(baseIndex(genome.bases[static_cast<size_t>(i)])) << ((i % 16) * 2);
        if (genome.silent[static_cast<size_t>(i)]) packed[static_cast<size_t>(6 + i / 32)] |= 1u << (i % 32);
    }
    version.fetch_add(1);
    for (size_t i = 0; i < words.size(); ++i) words[i].store(packed[i]);
    version.fetch_add(1);
}
bool GenomeMailbox::read(Genome& genome, std::uint32_t& lastVersion) const noexcept
{
    const auto before = version.load();
    if ((before & 1u) != 0 || before == lastVersion) return false;
    std::array<std::uint32_t, 10> packed {};
    for (size_t i = 0; i < words.size(); ++i) packed[i] = words[i].load();
    if (before != version.load()) return false;
    Genome snapshot;
    snapshot.size = static_cast<int>(std::min(packed[9], static_cast<std::uint32_t>(maxBases)));
    constexpr char bases[] = "TCAG";
    for (int i = 0; i < snapshot.size; ++i)
    {
        snapshot.bases[static_cast<size_t>(i)] = bases[(packed[static_cast<size_t>(i / 16)] >> ((i % 16) * 2)) & 3u];
        snapshot.silent[static_cast<size_t>(i)] = (packed[static_cast<size_t>(6 + i / 32)] & (1u << (i % 32))) != 0;
    }
    genome = snapshot;
    lastVersion = before;
    return true;
}
}
