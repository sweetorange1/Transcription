#pragma once
#include "Dsp/TranscriptionEngine.h"
#include <array>

namespace transcription
{
struct Preset
{
    const char* name;
    const char* sequence;
    const char* source;
    const char* description;
    Controls controls;
};
constexpr int presetCount = 5;
const std::array<Preset, presetCount>& factoryPresets() noexcept;
Genome presetGenome(int index) noexcept;
bool sameControls(const Controls& first, const Controls& second) noexcept;
}
