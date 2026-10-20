#include "Dsp/TranscriptionEngine.h"
#include "Presets.h"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <iostream>
#include <limits>
#include <set>
#include <thread>
#include <vector>

using namespace transcription;
namespace
{
int failures = 0;
void check(bool condition, const char* message)
{
    if (!condition) { ++failures; std::cerr << "FAIL: " << message << '\n'; }
}
Genome dna(const char* text)
{
    Genome result;
    check(Genome::parse(text, result), "test DNA must parse");
    return result;
}
double rms(const std::vector<float>& audio, size_t first = 0)
{
    double power = 0.0;
    for (size_t i = first; i < audio.size(); ++i) power += static_cast<double>(audio[i]) * audio[i];
    return std::sqrt(power / static_cast<double>(std::max<size_t>(1, audio.size() - first)));
}
std::vector<float> render(const Genome& genome, float scanPhase, int note, float velocity, int rate, double seconds)
{
    Engine engine;
    engine.prepare(rate); engine.setTranscript(transcribe(genome)); engine.setScanPhase(scanPhase);
    engine.noteOn(1, note, velocity);
    std::vector<float> audio(static_cast<size_t>(seconds * rate));
    engine.render(audio.data(), nullptr, static_cast<int>(audio.size()));
    return audio;
}
double spectralPower(const std::vector<float>& audio, int rate, double frequency)
{
    const size_t first = audio.size() / 3;
    const double coefficient = 2.0 * std::cos(6.283185307179586 * frequency / rate);
    double previous = 0.0, beforePrevious = 0.0;
    for (size_t i = first; i < audio.size(); ++i)
    {
        const double phase = static_cast<double>(i - first) / static_cast<double>(audio.size() - first);
        const double window = 0.5 - 0.5 * std::cos(6.283185307179586 * phase);
        const double current = static_cast<double>(audio[i]) * window + coefficient * previous - beforePrevious;
        beforePrevious = previous; previous = current;
    }
    return previous * previous + beforePrevious * beforePrevious - coefficient * previous * beforePrevious;
}
double peakNear(const std::vector<float>& audio, int rate, double expected)
{
    double best = 0.0, frequency = 0.0;
    for (int i = -32; i <= 32; ++i)
    {
        const double f = expected + static_cast<double>(i) * 0.25;
        const double power = spectralPower(audio, rate, f);
        if (power > best) { best = power; frequency = f; }
    }
    return frequency;
}
void testGenetics()
{
    int stops = 0;
    std::set<char> aminoAcids;
    for (char a : std::string("TCAG"))
        for (char b : std::string("TCAG"))
            for (char c : std::string("TCAG"))
            {
                const auto letter = translate(a, b, c);
                if (letter == '*') ++stops; else aminoAcids.insert(letter);
            }
    check(stops == 3 && aminoAcids.size() == 20, "standard genetic code must contain 61 sense codons and 20 amino acids");
    check(translate('A', 'T', 'G') == 'M' && translate('T', 'G', 'G') == 'W', "start and tryptophan codons");
    check(translate('U', 'A', 'G') == '*' && translate('T', 'G', 'A') == '*', "RNA and DNA stops");

    auto partial = dna("augGC");
    check(partial.size == 5 && partial.codons() == 1 && partial.rna() == "AUGGC", "partial codons and RNA import");
    const auto unchanged = partial;
    check(!Genome::parse("ATG?", partial) && partial == unchanged, "invalid text does not replace the model");
    check(!Genome::parse(std::string(97, 'A'), partial), "overlong input must fail, not truncate");
    check(!partial.erase(-1) && !partial.erase(100), "safe deletion bounds");

    const auto original = presetGenome(0);
    const auto mutated = mutate(original, 0.8f, 42);
    check(mutated != original && mutated == mutate(original, 0.8f, 42), "deterministic point mutation");
    check(mutated.dna().substr(0, 3) == "ATG" && mutated.dna().substr(mutated.size - 3) == "TGA", "mutation preserves original boundary codons");

    constexpr std::array<const char*, 3> expected {
        "MALWMRLLPLLALLALWGPDPAAAFVNQHLCGSHLVEALYLVCG",
        "MVHLTPEEKSAVTALWGKVNVDEVGGEALGRLLVVYPWTQRFFE",
        "MSKGEELFTGVVPILVELDGDVNGHKFSVSGEGEGDATYGKLTL"
    };
    for (int i = 1; i <= 3; ++i)
    {
        const auto real = presetGenome(i);
        std::string protein;
        for (int c = 0; c < real.codons(); ++c) protein += translate(real.bases[c * 3], real.bases[c * 3 + 1], real.bases[c * 3 + 2]);
        check(real.size == 96 && protein.size() == 32 && protein.back() == '*'
              && protein.substr(0, 31) == std::string(expected[static_cast<size_t>(i - 1)]).substr(0, 31),
              "NCBI fragment matches the published translation, closed by a terminal stop");
    }
}
void testMailbox()
{
    GenomeMailbox mailbox;
    const auto first = presetGenome(1);
    auto second = presetGenome(3); second.setIntron(4, 18, true);
    mailbox.publish(first);
    std::atomic<bool> finished { false };
    std::thread writer([&]
    {
        for (int i = 0; i < 6000; ++i) mailbox.publish(i % 2 == 0 ? second : first);
        finished.store(true);
    });
    std::uint32_t version = 0;
    Genome snapshot;
    int reads = 0;
    bool coherent = true;
    while (!finished.load())
        if (mailbox.read(snapshot, version)) { coherent = coherent && (snapshot == first || snapshot == second); ++reads; }
    if (mailbox.read(snapshot, version)) { coherent = coherent && (snapshot == first || snapshot == second); ++reads; }
    writer.join();
    check(coherent && reads > 0, "audio mailbox never returns torn sequence or gate data");
    check(!mailbox.read(snapshot, version), "unchanged mailbox does not trigger recompilation");
}
void testTranscriptMapping()
{
    // 终止密码子 → 音色姿态（UAG=pluck、UAA=pad、UGA=lead）。
    check(transcribe(dna("ATG TAG")).stopKind == StopKind::pluck && transcribe(dna("ATG TAG")).envelope.attack < 0.01f, "TAG stop is a pluck");
    check(transcribe(dna("ATG TAA")).stopKind == StopKind::pad && transcribe(dna("ATG TAA")).envelope.attack > 0.4f, "TAA stop is a pad");
    check(transcribe(dna("ATG TGA")).stopKind == StopKind::lead, "TGA stop is a lead");

    // 分段：core = clamp(N-2,0,3)，body = max(0,N-5)。
    check(transcribe(dna("ATG TGA")).coreCount == 0 && transcribe(dna("ATG TGA")).bodyCount == 0, "ATG+TGA is pure noise");
    check(transcribe(dna("ATG GGT TGA")).coreCount == 1 && transcribe(dna("ATG GGT TGA")).bodyCount == 0, "one middle codon -> core=1");
    check(transcribe(dna("ATG GGT GCT GTT TGA")).coreCount == 3, "three middle codons -> core=3");
    check(transcribe(dna("ATG GGT GCT GTT CTT TGA")).coreCount == 3
          && transcribe(dna("ATG GGT GCT GTT CTT TGA")).bodyCount == 1, "four middle codons -> core=3, body=1");

    // key1 映射确定：固定核心序列 → 固定参数。
    const auto a = transcribe(dna("ATG GGT GCT GTT TGA"));
    const auto b = transcribe(dna("ATG GGT GCT GTT TGA"));
    check(a.coreResonance == b.coreResonance && a.coreCutoff == b.coreCutoff && a.coreHarmonics == b.coreHarmonics
          && a.coreSlope == b.coreSlope && a.coreOddEven == b.coreOddEven && a.coreDrive == b.coreDrive,
          "fixed core sequence maps to fixed key1 parameters");
    check(a.coreHarmonics >= 1 && a.coreHarmonics <= 8 && a.coreResonance >= 0.0f && a.coreResonance <= 1.0f,
          "core harmonics and resonance stay within range");
    check(transcribe(dna("ATG GGC GGC GGC TGA")).coreHarmonics == 1,
          "three glycines collapse the bank to a single band");
    check(transcribe(dna("ATG TAT GAT TAT TGA")).coreHarmonics == 8,
          "three low-slope residues open all eight bands");
    check(transcribe(dna("ATG GCT GCT GCT TGA")).coreHarmonics <= 4,
          "a mid-slope core defaults to few bands");

    // body 性状（key2）：L 驱动生长度，GC / 残基家族驱动其余；body 为空时不表达效果链。
    const auto rich = transcribe(dna("ATG GGT GCT GTT CTT ATT CCG TTT TGA"));
    const auto lean = transcribe(dna("ATG GGT GCT GTT CTT TGA"));
    check(rich.bodyCount == 4 && rich.fxGrowth > lean.fxGrowth && lean.fxGrowth > 0.0f,
          "longer body grows the effect chain");
    check(transcribe(dna("ATG GGT GCT GTT TGA")).fxGrowth == 0.0f, "an empty body leaves the effect chain unexpressed");
    for (const auto& t : { rich, lean })
        check(t.fxGrowth <= 1.0f && t.fxSpace >= 0.0f && t.fxSpace <= 1.0f && t.fxMotion >= 0.0f && t.fxMotion <= 1.0f
              && t.fxBreath >= 0.0f && t.fxBreath <= 1.0f && t.fxTone >= -1.0f && t.fxTone <= 1.0f
              && t.fxShimmer >= 0.0f && t.fxShimmer <= 1.0f,
              "body traits stay within their unit ranges");
    check(transcribe(dna("ATG GGT GCT GTT GGC GCC GGG TGA")).fxSpace > transcribe(dna("ATG GGT GCT GTT ATT AAT TTA TGA")).fxSpace,
          "GC-rich bodies open a larger space");
    check(transcribe(dna("ATG GGT GCT GTT GAA GAT GAG TGA")).fxTone > 0.5f && transcribe(dna("ATG GGT GCT GTT AAA CGT AAG TGA")).fxTone < -0.5f,
          "acidic bodies brighten and basic bodies warm the chain");
    check(transcribe(dna("ATG GGT GCT GTT TTC TGG TAC TGA")).fxShimmer > 0.9f, "aromatic residues raise the shimmer");
    check(rich.fxSeed != lean.fxSeed && rich.fxSeed == transcribe(dna("ATG GGT GCT GTT CTT ATT CCG TTT TAA")).fxSeed,
          "body sequence (not the stop codon) seeds the organic drift");
}
void testScanPhaseAndActivation()
{
    const auto music = dna("ATG GGT GCT GTT TGA");   // core=3, body=0
    const auto noise = dna("ATG TGA");               // core=0
    const int rate = 48000;

    // 转录前（scanPhase=0）纯白噪声：无谐波峰。
    const auto before = render(music, 0.0f, 69, 1.0f, rate, 1.0);
    check(spectralPower(before, rate, 440.0) < 4.0 * spectralPower(before, rate, 610.0),
          "pre-transcription output is a noise floor (brown) without a harmonic peak");

    // 转录完成（scanPhase=1）完整音色：基频峰显著。
    const auto after = render(music, 1.0f, 69, 1.0f, rate, 1.0);
    check(spectralPower(after, rate, 440.0) > 20.0 * spectralPower(after, rate, 610.0),
          "completed transcription produces a pitched harmonic peak");

    // core=0：无论 scanPhase 都保持噪声（无谐波峰）。
    const auto pureNoise = render(noise, 1.0f, 69, 1.0f, rate, 1.0);
    check(spectralPower(pureNoise, rate, 440.0) < 4.0 * spectralPower(pureNoise, rate, 610.0),
          "a core-less transcript stays noise even when complete");

    // 中间 scanPhase：谐波峰介于噪声与完整之间（激活系数单调）。
    const auto mid = render(music, 0.5f, 69, 1.0f, rate, 1.0);
    check(spectralPower(mid, rate, 440.0) > spectralPower(before, rate, 440.0),
          "harmonic energy grows with scan phase");
}
void testPitchAndDynamics()
{
    const auto simple = dna("ATG GGT GCT GTT TGA");
    const int rate = 48000;
    const auto a4 = render(simple, 1.0f, 69, 1.0f, rate, 1.0);
    const auto a5 = render(simple, 1.0f, 81, 1.0f, rate, 1.0);
    const auto first = peakNear(a4, rate, 440.0);
    const auto second = peakNear(a5, rate, 880.0);
    // 布朗噪声的地板向低频倾斜，argmax 会被基频峰下方的裙摆拉偏约 1%，容差取 6 Hz。
    check(std::abs(first - 440.0) < 6.0 && std::abs(second - 880.0) < 6.0, "resonant core follows MIDI pitch");
    check(rms(a4, a4.size() / 2) > 0.003 && rms(a4) < 0.7, "single-voice output has usable level");
}
void testAutoLoudness()
{
    const int rate = 48000;
    const std::array<Genome, 4> genomes {
        dna("ATG TGA"), dna("ATG GGT TGA"), dna("ATG GGT GCT GTT TGA"), presetGenome(0)
    };
    double reference = -1.0;
    for (const auto& genome : genomes)
    {
        const auto audio = render(genome, 1.0f, 60, 1.0f, rate, 1.5);
        const double level = rms(audio, audio.size() / 2);
        if (reference < 0.0) reference = level;
        const double ratioDb = 20.0 * std::log10(level / reference);
        if (std::abs(ratioDb) >= 3.0) std::cerr << "  auto loudness deviation = " << ratioDb << " dB\n";
        check(std::abs(ratioDb) < 3.0, "auto loudness keeps different genes within 3 dB");
    }
}
struct Stereo { std::vector<float> left, right; };
Stereo renderHeld(const Genome& genome, float scanPhase, int rate, double hold, double total)
{
    Engine engine;
    engine.prepare(rate); engine.setTranscript(transcribe(genome)); engine.setScanPhase(scanPhase);
    engine.noteOn(1, 60, 0.9f);
    Stereo out;
    out.left.resize(static_cast<size_t>(total * rate)); out.right.resize(out.left.size());
    const int held = static_cast<int>(hold * rate), length = static_cast<int>(out.left.size());
    engine.render(out.left.data(), out.right.data(), held);
    engine.noteOff(1, 60);
    engine.render(out.left.data() + held, out.right.data() + held, length - held);
    return out;
}
double windowRms(const Stereo& audio, int rate, double from, double to)
{
    double power = 0.0; int n = 0;
    for (int i = static_cast<int>(from * rate); i < std::min(static_cast<int>(to * rate), static_cast<int>(audio.left.size())); ++i)
    {
        power += 0.5 * (static_cast<double>(audio.left[static_cast<size_t>(i)]) * audio.left[static_cast<size_t>(i)]
                        + static_cast<double>(audio.right[static_cast<size_t>(i)]) * audio.right[static_cast<size_t>(i)]);
        ++n;
    }
    return std::sqrt(power / static_cast<double>(std::max(1, n)));
}
void testBodyChains()
{
    // 同一 core + body，只换终止密码子：三条拓扑不同的 body 链。core=3、body=9 → M=12，
    // scanPhase=0.25 恰好让核心完整而 body 链全部旁路，作为干声参照。
    const int rate = 48000;
    const std::string middle = "GGT GCT GTT CTT TGG GAA AGC TTC CAG GAT CCG AAA ";
    const std::array<const char*, 3> stops { "TAG", "TAA", "TGA" };
    const std::array<StopKind, 3> kinds { StopKind::pluck, StopKind::pad, StopKind::lead };
    std::array<double, 3> tailRatio {};
    for (size_t i = 0; i < stops.size(); ++i)
    {
        const auto genome = dna(("ATG " + middle + stops[i]).c_str());
        const auto t = transcribe(genome);
        check(t.stopKind == kinds[i] && t.coreCount == 3 && t.bodyCount == 9, "body chain test genome segments as expected");
        const auto wet = renderHeld(genome, 1.0f, rate, 2.0, 6.0);
        // 干声参照：相同 core、相同终止密码子、没有 body（效果器整条旁路）。
        const auto dry = renderHeld(dna((std::string("ATG GGT GCT GTT ") + stops[i]).c_str()), 1.0f, rate, 2.0, 6.0);

        bool finite = true, dryMono = true;
        double cross = 0.0, powerL = 0.0, powerR = 0.0;
        for (size_t s = 0; s < wet.left.size(); ++s)
        {
            finite = finite && std::isfinite(wet.left[s]) && std::isfinite(wet.right[s]) && std::abs(wet.left[s]) <= 1.0f && std::abs(wet.right[s]) <= 1.0f;
            dryMono = dryMono && dry.left[s] == dry.right[s];
            if (s >= static_cast<size_t>(rate) && s < static_cast<size_t>(2 * rate))
            {
                cross += static_cast<double>(wet.left[s]) * wet.right[s];
                powerL += static_cast<double>(wet.left[s]) * wet.left[s];
                powerR += static_cast<double>(wet.right[s]) * wet.right[s];
            }
        }
        check(finite, "body chains stay finite and peak-limited");
        check(dryMono, "a bypassed body chain leaves the dry voice untouched (mono)");
        check(cross / std::sqrt(powerL * powerR + 1.0e-20) < 0.995, "an expressed body chain opens a stereo field");

        const double sustainWet = windowRms(wet, rate, 1.0, 2.0), sustainDry = windowRms(dry, rate, 1.0, 2.0);
        const double ratioDb = 20.0 * std::log10(sustainWet / sustainDry);
        if (std::abs(ratioDb) >= 3.0) std::cerr << "  stop " << stops[i] << " sustain wet/dry = " << ratioDb << " dB\n";
        check(std::abs(ratioDb) < 3.0, "body chain keeps sustained loudness within 3 dB of the dry transcript");

        const double tailWet = windowRms(wet, rate, 2.8, 3.3), tailDry = windowRms(dry, rate, 2.8, 3.3);
        check(tailWet > 3.0 * tailDry + 1.0e-6, "body chain leaves a living tail after the note is released");
        tailRatio[i] = windowRms(wet, rate, 4.0, 5.0) / sustainWet;

        if (kinds[i] == StopKind::pad)
            check(windowRms(wet, rate, 0.0, 0.02) < 0.5 * windowRms(wet, rate, 0.5, 0.52),
                  "the pad chain never adds an onset head before the slow swell");
    }
    check(tailRatio[1] > tailRatio[0], "the Nebula (pad) chain rings longer than the Spore (pluck) chain");

    // 中途换终止密码子：两条链 80 ms 交叉淡化，不产生突跳。
    Engine engine;
    engine.prepare(rate);
    engine.setTranscript(transcribe(dna(("ATG " + middle + "TGA").c_str())));
    engine.setScanPhase(1.0f);
    engine.noteOn(1, 60, 0.9f);
    std::vector<float> left(static_cast<size_t>(rate)), right(left.size());
    engine.render(left.data(), right.data(), rate);
    const auto maxStep = [&](size_t from, size_t to)
    {
        float step = 0.0f;
        for (size_t s = std::max<size_t>(1, from); s < to; ++s) step = std::max(step, std::abs(left[s] - left[s - 1]));
        return step;
    };
    const float before = maxStep(static_cast<size_t>(rate) / 2, left.size());
    engine.setTranscript(transcribe(dna(("ATG " + middle + "TAA").c_str())));
    engine.render(left.data(), right.data(), rate / 5);
    const float during = maxStep(0, static_cast<size_t>(rate) / 5);
    check(during < 3.0f * before + 1.0e-4f, "switching the stop codon crossfades between chains without a click");
}
void testBodyWetRamp()
{
    // 同一个 14 密码子序列：第 4 个密码子附近湿音应接近 0，随 body 推进单调变浓，
    // 到终止密码子才达到当前完整效果（不能再出现“读完第 4 个就到位”）。
    const int rate = 48000;
    const auto genome = dna("ATG GGT GCT GTT CTT TGG GAA AGC TTC CAG GAT CCG AAA TGA");
    constexpr float codons = 14.0f;
    const auto early = renderHeld(genome, 4.0f / codons, rate, 2.0, 6.0);
    const auto middle = renderHeld(genome, 8.0f / codons, rate, 2.0, 6.0);
    const auto full = renderHeld(genome, 1.0f, rate, 2.0, 6.0);
    // 干声参照：相同 core、相同终止密码子、没有 body（效果器整条旁路）。
    // 用差值能量度量实际加入的效果量：0 = 完全干声，1 = 完整效果。
    const auto dry = renderHeld(dna("ATG GGT GCT GTT TGA"), 1.0f, rate, 2.0, 6.0);
    const auto wetAmount = [&](const Stereo& audio)
    {
        double difference = 0.0, reference = 0.0;
        int n = 0;
        for (int i = rate; i < 2 * rate && i < static_cast<int>(audio.left.size()); ++i)
        {
            const double dl = static_cast<double>(audio.left[static_cast<size_t>(i)]) - dry.left[static_cast<size_t>(i)];
            const double dr = static_cast<double>(audio.right[static_cast<size_t>(i)]) - dry.right[static_cast<size_t>(i)];
            difference += 0.5 * (dl * dl + dr * dr);
            reference += 0.5 * (static_cast<double>(dry.left[static_cast<size_t>(i)]) * dry.left[static_cast<size_t>(i)]
                              + static_cast<double>(dry.right[static_cast<size_t>(i)]) * dry.right[static_cast<size_t>(i)]);
            ++n;
        }
        return std::sqrt(difference / std::max(1, n)) / (std::sqrt(reference / std::max(1, n)) + 1.0e-12);
    };
    const double earlyWet = wetAmount(early);
    const double middleWet = wetAmount(middle);
    const double fullWet = wetAmount(full);
    check(fullWet > 4.0 * earlyWet + 1.0e-6, "effect is still near-dry at the fourth codon");
    check(middleWet > 1.5 * earlyWet + 1.0e-7 && middleWet < 0.85 * fullWet,
          "effect grows across the body instead of reaching full early");
}
void testCoreOnsetSnaps()
{
    // 谐振核心在新音符起点必须直接把带通系数写入当前系数。否则 biquad 会从
    // reset() 的恒等直通（b0 = 1）平滑收敛，而这期间 gains[] 已按带通带宽归一化，
    // 32 路求和会把白噪声放大出 +20 dB 左右的宽带起音突发（UAA pad 下表现为音头）。
    const int rate = 48000;
    SubtractiveCore core;
    core.prepare(rate);
    CoreParams params;
    params.harmonics = 16; params.resonance = 0.8f; params.cutoff = 8000.0f;
    params.slope = 17.0f; params.formant = 1200.0f; params.formantAmt = 0.6f;
    params.oddEven = 0.0f; params.drive = 0.5f;
    core.setParameters(rate, 440.0f, params, true);
    const std::array<float, 4> activation { 1.0f, 1.0f, 1.0f, 1.0f };
    std::uint32_t random = 0x9e3779b9u;
    std::vector<float> audio(static_cast<size_t>(rate) / 5);   // 200 ms
    for (auto& sample : audio)
    {
        random ^= random << 13; random ^= random >> 17; random ^= random << 5;
        sample = core.process(static_cast<float>(random >> 8) / 8388608.0f - 1.0f, activation);
    }
    const auto windowRms = [&](int begin, int end)
    {
        double power = 0.0;
        for (int i = begin; i < end; ++i) power += static_cast<double>(audio[static_cast<size_t>(i)]) * audio[static_cast<size_t>(i)];
        return std::sqrt(power / static_cast<double>(end - begin));
    };
    const double onset = windowRms(0, rate / 200);                        // 0–5 ms
    const double steady = windowRms(rate / 10, rate / 10 + rate / 200);   // 100–105 ms
    check(steady > 0.0 && onset < 3.0 * steady,
          "resonator bank starts on its bandpass coefficients instead of a pass-through burst");
}
void testMidiAndStability()
{
    Engine engine;
    engine.prepare(48000);
    engine.setTranscript(transcribe(presetGenome(0)));
    engine.setScanPhase(1.0f);
    std::vector<float> left(48000), right(48000);
    engine.noteOn(1, 60, 1); engine.controller(1, 64, 127); engine.noteOff(1, 60);
    engine.render(left.data(), right.data(), 48000);
    check(engine.activeVoices() == 1 && rms(left, 24000) > 0.001, "sustain pedal holds released MIDI keys");
    engine.controller(2, 64, 0); engine.render(left.data(), right.data(), 48000);
    check(engine.activeVoices() == 1, "pedals are MIDI-channel independent");
    engine.controller(1, 64, 0); engine.render(left.data(), right.data(), 48000);
    check(engine.activeVoices() == 0, "pedal release completes the envelope");
    for (int second = 0; second < 14; ++second) engine.render(left.data(), right.data(), 48000);
    check(rms(left, 40000) < 0.0001, "the body effect tail decays to silence after release");
    for (int i = 0; i < 32; ++i) engine.noteOn(1, 36 + i, 1.0f);
    check(engine.activeVoices() == voiceCount, "polyphony is bounded with voice stealing");
    engine.controller(1, 120, 0);
    check(engine.activeVoices() == 0, "all sound off clears voices immediately");
    engine.reset();
    engine.setScanPhase(0.0f);
    for (int note = 48; note < 56; ++note) engine.noteOn(1, note, 0.7f);
    engine.render(left.data(), right.data(), 48000);
    engine.noteOn(1, 72, 0.8f); engine.noteOn(1, 73, 0.8f);
    check(engine.isNoteActive(1, 72) && engine.isNoteActive(1, 73)
          && !engine.isNoteActive(1, 48) && !engine.isNoteActive(1, 49), "voice stealing chooses older notes across envelope stages");
    engine.reset();
    engine.setScanPhase(1.0f);
    engine.noteOn(1, 69, 1); engine.pitchWheel(1, 16383);
    engine.render(left.data(), nullptr, 48000);
    check(std::abs(peakNear(left, 48000, 493.8833) - 493.8833) < 4.0, "pitch wheel range is two semitones");

    bool finite = true;
    for (int rate : { 8000, 44100, 48000, 96000, 192000 })
    {
        engine.prepare(rate);
        engine.setTranscript(transcribe(presetGenome(3)));
        engine.setScanPhase(1.0f);
        for (int i = 0; i < 8; ++i) engine.noteOn(i + 1, i % 2 == 0 ? i * 2 : 120 - i, 1.0f);
        for (int block = 0; block < 160; ++block)
        {
            engine.setScanPhase(block % 2 == 0 ? 1.0f : 0.0f);
            engine.pitchWheel(1, block % 2 == 0 ? 0 : 16383);
            if (block % 7 == 0) engine.setTranscript(transcribe(presetGenome(block % presetCount)));
            const int length = 1 + (block * 37) % 513;
            engine.render(left.data(), right.data(), length);
            for (int s = 0; s < length; ++s) finite = finite && std::isfinite(left[s]) && std::isfinite(right[s]) && std::abs(left[s]) <= 1.0f && std::abs(right[s]) <= 1.0f;
        }
    }
    engine.setScanPhase(std::numeric_limits<float>::quiet_NaN());
    engine.render(left.data(), nullptr, 10);
    for (int i = 0; i < 10; ++i) finite = finite && std::isfinite(left[i]);
    check(finite, "extreme notes, scan phases, sample rates and invalid input remain finite and peak-limited");
}
void testBlockIndependence()
{
    Engine whole, split;
    for (auto* engine : { &whole, &split })
    {
        engine->prepare(48000);
        engine->setTranscript(transcribe(presetGenome(0)));
        engine->setScanPhase(0.75f);
        engine->noteOn(1, 60, 0.8f);
    }
    std::vector<float> reference(8192), fragmented(8192);
    whole.render(reference.data(), nullptr, static_cast<int>(reference.size()));
    for (size_t position = 0; position < fragmented.size();)
    {
        const int count = static_cast<int>(std::min<size_t>(37, fragmented.size() - position));
        split.render(fragmented.data() + position, nullptr, count);
        position += static_cast<size_t>(count);
    }
    float difference = 0;
    for (size_t i = 0; i < reference.size(); ++i) difference = std::max(difference, std::abs(reference[i] - fragmented[i]));
    check(difference < 0.0000001f, "unchanged parameters keep audio independent of host block segmentation");
}
}
int main()
{
    testGenetics(); testMailbox(); testTranscriptMapping(); testScanPhaseAndActivation();
    testPitchAndDynamics(); testAutoLoudness(); testBodyChains(); testBodyWetRamp(); testCoreOnsetSnaps();
    testMidiAndStability(); testBlockIndependence();
    if (failures != 0) { std::cerr << failures << " test(s) failed\n"; return 1; }
    std::cout << "PASS: genetics, mapping, scan-phase activation, pitch, velocity, loudness, body chains, MIDI and DSP stability\n";
    return 0;
}
