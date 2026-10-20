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
    // 插件唯一的宿主参数：转录进度。0 % = 只有布朗噪声，100 % = 完整音色。
    // 默认 100 %：未打开过界面时直接给完整音色；新实例第一次打开界面会从 0 % 开始（见编辑器）。
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID("scan", 1), "Transcription progress",
        juce::NormalisableRange<float>(0, 1, 0.001f), 1.0f, percent));
    return layout;
}
}
