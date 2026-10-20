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
        case Family::hydrophobic: return 3000.0f;
        case Family::polar: return 8000.0f;
        case Family::basic: return 12000.0f;
        case Family::acidic: return 16000.0f;
        default: return 8000.0f;
    }
}
float familyFormant(Family family) noexcept
{
    switch (family)
    {
        case Family::polar: return 1800.0f;
        case Family::hydrophobic: return 900.0f;
        case Family::acidic:
        case Family::basic: return 1200.0f;
        default: return 1200.0f;
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

    // ---- key1：核心区 C[1 .. 1+coreCount-1]，缺失位不参与统计 ----
    if (result.coreCount > 0)
    {
        float qSum = 0.0f, slopeSum = 0.0f, gainSum = 0.0f, oddSum = 0.0f, evenSum = 0.0f;
        float cutoffSum = 0.0f, formantSum = 0.0f;
        int polarCount = 0, chargedCount = 0;
        for (int i = 1; i < 1 + result.coreCount; ++i)
        {
            const int off = i * 3;
            const char aa = translate(genome.bases[static_cast<size_t>(off)], genome.bases[static_cast<size_t>(off + 1)], genome.bases[static_cast<size_t>(off + 2)]);
            const auto& r = residueInfo(aa);
            qSum += r.q; slopeSum += r.slope; gainSum += r.gain; oddSum += r.odd; evenSum += r.even;
            cutoffSum += familyCutoff(r.family);
            formantSum += familyFormant(r.family);
            if (r.family == Family::polar) ++polarCount;
            if (r.family == Family::acidic || r.family == Family::basic) ++chargedCount;
        }
        const float c = static_cast<float>(result.coreCount);
        const float meanQ = qSum / c, meanSlope = slopeSum / c, meanGain = gainSum / c;
        const float meanOdd = oddSum / c, meanEven = evenSum / c;
        result.coreResonance = std::clamp(meanQ / 390.0f, 0.0f, 1.0f);
        // 8 路带通并联，映射刻意向“少谐波”偏置：以 (2−slope)/1.5 为 0..1 的归一化量，
        // 取 4.5 次幂后放大到 1..8。平均 slope 0.9（随机组合的中心）约得 2 路；
        // 只有 average slope 很低（多为 Tyr/Asp/Glu）时才升到 6–8 路，Gly 这类高 slope 稳定落到 1 路。
        const float normalized = std::clamp((2.0f - meanSlope) / 1.5f, 0.0f, 1.0f);
        result.coreHarmonics = static_cast<int>(std::lround(std::clamp(8.0f * std::pow(normalized, 4.5f), 1.0f, 8.0f)));
        result.coreCutoff = cutoffSum / c * (0.8f + 0.4f * meanGain);
        result.coreSlope = std::clamp(6.0f + meanSlope * 6.0f, 6.0f, 24.0f);
        result.coreFormant = formantSum / c;
        result.coreFormantAmt = static_cast<float>(polarCount) / c;
        const float oddEvenSum = meanOdd + meanEven;
        result.coreOddEven = oddEvenSum > 1.0e-6f ? std::clamp((meanOdd - meanEven) / oddEvenSum, -1.0f, 1.0f) : 0.0f;
        result.coreDrive = static_cast<float>(chargedCount) / c;
    }

    // ---- key2：中间区 C[4 .. N-2] → body 效果链性状（拓扑由终止密码子决定） ----
    const int bodyStart = 4, bodyEnd = n - 1;
    int bodyGc = 0, bodyBases = 0, bodyPolar = 0, bodyHydro = 0, bodyAcidic = 0, bodyBasic = 0, bodyRing = 0, bodyGly = 0;
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
        result.fxTone = std::clamp(3.0f * static_cast<float>(bodyAcidic - bodyBasic) / len, -1.0f, 1.0f);
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
