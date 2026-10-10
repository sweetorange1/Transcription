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
    check(a.coreHarmonics >= 4 && a.coreHarmonics <= 32 && a.coreResonance >= 0.0f && a.coreResonance <= 1.0f,
          "core harmonics and resonance stay within range");

    // body 统计：L 驱动 fxTime/fxFeedback，占比驱动其余。
    const auto rich = transcribe(dna("ATG GGT GCT GTT CTT ATT CCG TTT TGA"));
    check(rich.bodyCount == 4 && rich.fxTime > transcribe(dna("ATG GGT GCT GTT CTT TGA")).fxTime,
          "longer body lengthens echo time");
    check(rich.fxSize >= 0.0f && rich.fxSize <= 1.0f && rich.fxDamp <= 1.0f && rich.fxDepth <= 1.0f && rich.fxDrive <= 1.0f,
          "body statistics map FX to the unit range");
}
void testScanPhaseAndActivation()
{
    const auto music = dna("ATG GGT GCT GTT TGA");   // core=3, body=0
    const auto noise = dna("ATG TGA");               // core=0
    const int rate = 48000;

    // 转录前（scanPhase=0）纯白噪声：无谐波峰。
    const auto before = render(music, 0.0f, 69, 1.0f, rate, 1.0);
    check(spectralPower(before, rate, 440.0) < 4.0 * spectralPower(before, rate, 610.0),
          "pre-transcription output is flat white noise without a harmonic peak");

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
    check(std::abs(first - 440.0) < 4.0 && std::abs(second - 880.0) < 4.0, "resonant core follows MIDI pitch");
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
        check(std::abs(ratioDb) < 3.0, "auto loudness keeps different genes within 3 dB");
    }
}
void testEnvelopePrecedesEffects()
{
    // pad 音色（slow attack）带有效 delay：振幅包络必须先于效果器组，否则 delay 回声里
    // 会残留未加包络的瞬态音头（按下音符后干音音头被 attack 衰减，但回声突然变响）。
    const auto pad = dna("ATG GGT GCT GTT CTT ATT CCG TTT TAA");   // 7 个中间密码子：core=3, body=2
    const auto t = transcribe(pad);
    check(t.stopKind == StopKind::pad && t.envelope.attack > 0.4f && t.fxTime > 0.02f,
          "pad transcript carries a slow attack and an active echo");
    const int rate = 48000;
    const auto audio = render(pad, 1.0f, 60, 1.0f, rate, 1.0);
    const int echoAt = static_cast<int>(t.fxTime * rate);
    check(echoAt > 0 && echoAt < static_cast<int>(t.envelope.attack * rate),
          "the first echo lands inside the pad's attack window");
    const int win = rate / 100;   // 10 ms
    const auto rms = [&](int begin, int end)
    {
        double power = 0.0; int n = 0;
        for (int s = std::max(0, begin); s < std::min(static_cast<int>(audio.size()), end); ++s)
        { power += static_cast<double>(audio[s]) * audio[s]; ++n; }
        return std::sqrt(power / static_cast<double>(std::max(1, n)));
    };
    const double justBefore = rms(echoAt - win, echoAt);
    const double justAfter = rms(echoAt + win, echoAt + 2 * win);
    check(justAfter < justBefore * 2.5 + 1.0e-4,
          "the delay echo follows the pad's smooth attack instead of carrying a transient head");
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
    check(engine.activeVoices() == 0 && rms(left, 40000) < 0.0001, "pedal release completes the envelope");
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
    testPitchAndDynamics(); testAutoLoudness(); testEnvelopePrecedesEffects(); testMidiAndStability(); testBlockIndependence();
    if (failures != 0) { std::cerr << failures << " test(s) failed\n"; return 1; }
    std::cout << "PASS: genetics, mapping, scan-phase activation, pitch, velocity, loudness, MIDI and DSP stability\n";
    return 0;
}
