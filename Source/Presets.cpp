#include "Presets.h"
#include <algorithm>
#include <cmath>

namespace transcription
{
const std::array<Preset, presetCount>& factoryPresets() noexcept
{
    static const std::array<Preset, presetCount> presets {{
        { "Central Dogma", "ATG GGC GCC GTT CTT ATT CCG TTT TGG TCT ACC TGC AAC CAG GAC GAA AAG CGT CAT TAC TGA",
          "Designed sequence / all 20 amino acids", "A living, balanced starting point.",
          { 0.65f, 0.08f, 1.0f, 1.0f, 0.45f, -3.0f, 0 } },
        { "Insulin / INS", "ATG GCC CTG TGG ATG CGC CTC CTG CCC CTG CTG GCG CTG CTG GCC CTC TGG GGA CCT GAC CCA GCC GCA GCC TTT GTG AAC CAA CAC CTG TGC TGA",
          "H. sapiens / NM_000207.3 / nt 60-155", "CDS fragment 1-32 / soft, even resonance.",
          { 0.75f, 0.035f, 2.0f, 1.8f, 0.6f, -6.0f, 1 } },
        { "Hemoglobin / HBB", "ATG GTG CAT CTG ACT CCT GAG GAG AAG TCT GCC GTT ACT GCC CTG TGG GGC AAG GTG AAC GTG GAT GAA GTT GGT GGT GAG GCC CTG GGC AGG TGA",
          "H. sapiens / NM_000518.5 / nt 51-146", "CDS fragment 1-32 / focused, string-like air.",
          { 0.85f, 0.045f, 0.7f, 0.8f, 0.35f, -3.0f, 0 } },
        { "Fluorescence / GFP", "ATG AGT AAA GGA GAA GAA CTT TTC ACT GGA GTT GTC CCA ATT CTT GTT GAA TTA GAT GGT GAT GTT AAT GGG CAC AAA TTT TCT GTC AGT GGA TGA",
          "A. victoria / M62653.1 / nt 26-121", "CDS fragment 1-32 / bright, moving partials.",
          { 0.48f, 0.22f, 1.4f, 2.0f, 0.85f, -3.0f, 0 } },
        { "Start + Stop", "ATG TGA", "Designed sequence / minimal reading frame",
          "A single noise-excited resonance. MIDI gated.",
          { 0.88f, 0.0f, 0.25f, 0.3f, 0.0f, -3.0f, 0 } }
    }};
    return presets;
}
Genome presetGenome(int index) noexcept
{
    Genome genome;
    Genome::parse(factoryPresets()[static_cast<size_t>(std::clamp(index, 0, presetCount - 1))].sequence, genome);
    return genome;
}
bool sameControls(const Controls& a, const Controls& b) noexcept
{
    constexpr float epsilon = 0.0001f;
    return std::abs(a.precision - b.precision) < epsilon && std::abs(a.mutation - b.mutation) < epsilon
        && std::abs(a.attackScale - b.attackScale) < epsilon && std::abs(a.releaseScale - b.releaseScale) < epsilon
        && std::abs(a.width - b.width) < epsilon && std::abs(a.outputDb - b.outputDb) < epsilon && a.noise == b.noise;
}
}
