#pragma once
#include <JuceHeader.h>
#include "Parameters.h"
#include "Presets.h"

class TranscriptionAudioProcessor final : public juce::AudioProcessor
{
public:
    TranscriptionAudioProcessor();
    ~TranscriptionAudioProcessor() override = default;
    void prepareToPlay(double sampleRate, int maximumBlockSize) override;
    void releaseResources() override;
    void reset() override;
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void processBlockBypassed(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 8.0; }
    int getNumPrograms() override { return transcription::presetCount; }
    int getCurrentProgram() override { return lastPreset.load(); }
    void setCurrentProgram(int index) override;
    const juce::String getProgramName(int index) override;
    void changeProgramName(int, const juce::String&) override {}
    void getStateInformation(juce::MemoryBlock& destination) override;
    void setStateInformation(const void* data, int bytes) override;

    transcription::Genome currentGenome() const;
    bool setGenome(const transcription::Genome& genome);
    transcription::Controls currentControls() const noexcept { return parameterRefs.read(); }
    int matchingPreset() const;
    void loadPreset(int index);
    void setParameter(const juce::String& id, float value);
    std::uint32_t revision() const noexcept { return modelRevision.load(); }
    float outputPeak() const noexcept { return meter.load(); }
    int soundingVoices() const noexcept { return voiceMeter.load(); }
    juce::AudioProcessorValueTreeState parameters;
    juce::MidiKeyboardState keyboardState;
    std::atomic<int> editorWidth { 820 }, editorTab { 0 };
    std::atomic<float> scanPhase { 1.0f };   // 全局转录进度 [0,1]，由编辑器动画写入

private:
    void handleMidi(const juce::MidiMessage& message) noexcept;
    transcription::ParameterRefs parameterRefs;
    mutable juce::CriticalSection modelMutex;
    transcription::Genome genome, audioGenome;
    transcription::GenomeMailbox mailbox;
    transcription::Engine engine;
    std::uint32_t audioVersion = 0;
    std::atomic<std::uint32_t> modelRevision { 0 };
    std::atomic<int> lastPreset { 0 }, voiceMeter { 0 };
    std::atomic<float> meter { 0.0f };
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TranscriptionAudioProcessor)
};
