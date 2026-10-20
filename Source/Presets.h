#pragma once
#include "Model/Genome.h"
#include <array>

namespace transcription
{
struct Preset
{
    const char* name;
    const char* sequence;
    const char* source;
    const char* description;
};
constexpr int presetCount = 5;
inline constexpr int defaultPresetIndex = 1;   // Insulin / INS：插件启动时的默认基因
const std::array<Preset, presetCount>& factoryPresets() noexcept;
Genome presetGenome(int index) noexcept;
}
