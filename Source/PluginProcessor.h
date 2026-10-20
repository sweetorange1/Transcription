#pragma once
#include <JuceHeader.h>
#include <juce_dsp/juce_dsp.h>
#include "Parameters.h"
#include "Presets.h"
#include "Spectrum.h"
#include "Dsp/TranscriptionEngine.h"

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
    int matchingPreset() const;
    void loadPreset(int index);
    void setParameter(const juce::String& id, float value);
    std::uint32_t revision() const noexcept { return modelRevision.load(); }
    float outputPeak() const noexcept { return meter.load(); }
    int soundingVoices() const noexcept { return voiceMeter.load(); }
    // 实时输出频谱：对数频段、dB（spectrumMinDb..spectrumMaxDb），由音频线程写入。
    float spectrumBandDb(int index) const noexcept
    {
        return index >= 0 && index < transcription::spectrumBandCount
            ? spectrumBands[static_cast<size_t>(index)].load(std::memory_order_relaxed)
            : transcription::spectrumMinDb;
    }
    juce::AudioProcessorValueTreeState parameters;
    juce::MidiKeyboardState keyboardState;
    std::atomic<int> editorWidth { 820 }, editorTab { 0 };
    std::atomic<float> scanPhase { 1.0f };   // 当前生效的转录进度 [0,1]（每块从 scan 参数刷新）
    std::atomic<bool> suppressOnboarding { false };   // 离线截图 / 自动化测试时关闭首次引导
    std::atomic<bool> forceOnboarding { false };      // 忽略已引导标记，强制渲染引导（截图用）
    std::atomic<bool> stateRestored { false };        // 宿主是否恢复过工程状态（编辑器据此决定初始位置）
    std::atomic<bool> transportUsed { false };        // 本次实例里用户是否真正驱动过转录（拖动/播放）

private:
    static constexpr const char* scanParameterId = "scan";
    juce::RangedAudioParameter* scanParameter = nullptr;
    void handleMidi(const juce::MidiMessage& message) noexcept;
    void analyseSpectrum(const float* left, const float* right, int samples) noexcept;
    mutable juce::CriticalSection modelMutex;
    transcription::Genome genome, audioGenome;
    transcription::GenomeMailbox mailbox;
    transcription::Engine engine;
    std::uint32_t audioVersion = 0;
    std::atomic<std::uint32_t> modelRevision { 0 };
    std::atomic<int> lastPreset { transcription::defaultPresetIndex }, voiceMeter { 0 };
    std::atomic<float> meter { 0.0f };

    // 频谱分析：4096 点 Hann 窗 FFT（Δf≈11.7 Hz @48k），每 512 采样计算一次，压成 96 个对数频段。
    static constexpr int spectrumFftOrder = 12;
    static constexpr int spectrumFftSize = 1 << spectrumFftOrder;
    static constexpr int spectrumHopSamples = 512;
    juce::dsp::FFT spectrumFft { spectrumFftOrder };
    std::array<float, spectrumFftSize> spectrumWindow {};
    std::array<float, spectrumFftSize * 2> spectrumBuffer {};
    std::array<float, spectrumFftSize> spectrumRing {};
    std::array<std::atomic<float>, transcription::spectrumBandCount> spectrumBands {};
    std::array<float, transcription::spectrumBandCount> spectrumSmoothed {};
    int spectrumWrite = 0, spectrumHop = 0;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TranscriptionAudioProcessor)
};
