#include "Presets.h"
#include <algorithm>
#include <cmath>

namespace transcription
{
const std::array<Preset, presetCount>& factoryPresets() noexcept
{
    static const std::array<Preset, presetCount> presets {{
        { "Central Dogma", "ATG GGC GCC GTT CTT ATT CCG TTT TGG TCT ACC TGC AAC CAG GAC GAA AAG CGT CAT TAC TGA",
          "Designed sequence / all 20 amino acids", "A living, balanced starting point." },
        { "Insulin / INS", "ATG GCC CTG TGG ATG CGC CTC CTG CCC CTG CTG GCG CTG CTG GCC CTC TGG GGA CCT GAC CCA GCC GCA GCC TTT GTG AAC CAA CAC CTG TGC TGA",
          "H. sapiens / NM_000207.3 / nt 60-155", "CDS fragment 1-32 / soft, even resonance." },
        { "Hemoglobin / HBB", "ATG GTG CAT CTG ACT CCT GAG GAG AAG TCT GCC GTT ACT GCC CTG TGG GGC AAG GTG AAC GTG GAT GAA GTT GGT GGT GAG GCC CTG GGC AGG TGA",
          "H. sapiens / NM_000518.5 / nt 51-146", "CDS fragment 1-32 / focused, string-like air." },
        { "Fluorescence / GFP", "ATG AGT AAA GGA GAA GAA CTT TTC ACT GGA GTT GTC CCA ATT CTT GTT GAA TTA GAT GGT GAT GTT AAT GGG CAC AAA TTT TCT GTC AGT GGA TGA",
          "A. victoria / M62653.1 / nt 26-121", "CDS fragment 1-32 / bright, moving partials." },
        { "Start + Stop", "ATG TGA", "Designed sequence / minimal reading frame",
          "A single noise-excited resonance. MIDI gated." }
    }};
    return presets;
}
Genome presetGenome(int index) noexcept
{
    Genome genome;
    Genome::parse(factoryPresets()[static_cast<size_t>(std::clamp(index, 0, presetCount - 1))].sequence, genome);
    return genome;
}
}
