#pragma once
#include <JuceHeader.h>
#include "Dsp/TranscriptionEngine.h"

namespace transcription
{
inline constexpr std::array<const char*, 7> parameterIds {
    "precision", "mutation", "attackScale", "releaseScale", "width", "output", "noise"
};
juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
class ParameterRefs
{
public:
    explicit ParameterRefs(juce::AudioProcessorValueTreeState& state);
    Controls read() const noexcept;
private:
    std::array<std::atomic<float>*, 7> values {};
};
}
