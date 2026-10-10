#include "Parameters.h"
#include <cmath>

namespace transcription
{
juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    const auto percent = juce::AudioParameterFloatAttributes()
        .withStringFromValueFunction([](float value, int) { return juce::String(juce::roundToInt(value * 100.0f)) + " %"; })
        .withValueFromStringFunction([](const juce::String& text) { return text.getFloatValue() * 0.01f; });
    const auto factor = juce::AudioParameterFloatAttributes()
        .withStringFromValueFunction([](float value, int) { return juce::String(value, 2) + " x"; })
        .withValueFromStringFunction([](const juce::String& text) { return text.getFloatValue(); });
    const auto decibels = juce::AudioParameterFloatAttributes()
        .withStringFromValueFunction([](float value, int) { return juce::String(value, 1) + " dB"; })
        .withValueFromStringFunction([](const juce::String& text) { return text.getFloatValue(); });
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID("precision", 1), "Transcription precision", juce::NormalisableRange<float>(0, 1, 0.001f), 0.65f, percent));
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID("mutation", 1), "Mutation", juce::NormalisableRange<float>(0, 1, 0.001f), 0.08f, percent));
    auto scaleRange = juce::NormalisableRange<float>(0.25f, 4.0f, 0.01f);
    scaleRange.setSkewForCentre(1.0f);
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID("attackScale", 1), "Folding attack scale", scaleRange, 1.0f, factor));
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID("releaseScale", 1), "Folding release scale", scaleRange, 1.0f, factor));
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID("width", 1), "Stereo width", juce::NormalisableRange<float>(0, 1, 0.001f), 0.45f, percent));
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID("output", 1), "Output", juce::NormalisableRange<float>(-48, 0, 0.1f), -3.0f, decibels));
    layout.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID("noise", 1), "Chromatin noise colour", juce::StringArray { "White", "Pink", "Brown" }, 0));
    return layout;
}
ParameterRefs::ParameterRefs(juce::AudioProcessorValueTreeState& state)
{
    for (size_t i = 0; i < values.size(); ++i) values[i] = state.getRawParameterValue(parameterIds[i]);
}
Controls ParameterRefs::read() const noexcept
{
    Controls c;
    c.precision = values[0]->load(std::memory_order_relaxed);
    c.mutation = values[1]->load(std::memory_order_relaxed);
    c.attackScale = values[2]->load(std::memory_order_relaxed);
    c.releaseScale = values[3]->load(std::memory_order_relaxed);
    c.width = values[4]->load(std::memory_order_relaxed);
    c.outputDb = values[5]->load(std::memory_order_relaxed);
    const float colour = values[6]->load(std::memory_order_relaxed);
    c.noise = std::isfinite(colour) ? juce::jlimit(0, 2, juce::roundToInt(colour)) : 0;
    return c;
}
}
