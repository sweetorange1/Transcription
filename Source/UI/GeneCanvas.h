#pragma once
#include <JuceHeader.h>
#include "Model/Genome.h"
#include "Spectrum.h"
#include <functional>

class GeneCanvas final : public juce::Component, public juce::SettableTooltipClient
{
public:
    enum class Phase { ready, binding, opening, elongating, releasing, complete, detaching };
    GeneCanvas();
    void setData(const transcription::Genome&);
    bool setCodon(int index, std::string_view rna);
    bool insertCodon(int index, std::string_view rna);
    bool setStopCodon(std::string_view rna);
    bool removeCodon(int index);
    void toggleRunning();
    void pause() noexcept { running = false; }
    void resetProcess();
    void advance(double seconds);
    // 实际输出峰值（0..1），只用于让信号带与频谱监视器的亮度跟随真实发声。
    void setOutputLevel(float level) noexcept { outputLevel = juce::jlimit(0.0f, 1.0f, level); }
    // 实时输出频谱（dB，对数频段），由编辑器 30 Hz 定时器从 Processor 拉取。
    void setSpectrum(const std::array<float, transcription::spectrumBandCount>& decibels) noexcept;
    void seek(float progress);
    Phase phase() const noexcept;
    bool isRunning() const noexcept { return running; }
    bool isDragging() const noexcept { return dragging; }
    float progress() const noexcept;
    float scanPhase() const noexcept;   // 全局转录进度：ready/binding/opening=0，elongating=progress，releasing/complete=1
    int producedBases() const noexcept;
    juce::String nascentRna() const;
    juce::Point<float> polymerasePosition() const noexcept;
    const transcription::Genome& currentGenome() const noexcept { return genome; }
    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    void mouseMove(const juce::MouseEvent&) override;
    void mouseExit(const juce::MouseEvent&) override;
    void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    std::function<void(const transcription::Genome&)> onEdit;
    std::function<void(int index, bool insert)> onChooseCodon;
    std::function<void()> onChooseStop;
    std::function<void()> onProcessChanged;
private:
    float scale() const noexcept;
    float elongationDuration() const noexcept;
    float bubbleOpenness() const noexcept;
    float releaseProgress() const noexcept;
    float detachProgress() const noexcept;
    float dnaSpan() const noexcept;
    float dnaScroll() const noexcept;
    float dnaScreenX(float worldX) const noexcept;
    float maxScroll() const noexcept;
    juce::Rectangle<float> codonBounds(int index) const noexcept;
    int codonAt(juce::Point<float>) const noexcept;
    void publish();
    void processChanged();
    void drawDna(juce::Graphics&);
    void drawPolymerase(juce::Graphics&);
    void drawRna(juce::Graphics&);
    void drawCodons(juce::Graphics&);
    void drawSignalFlow(juce::Graphics&);
    void drawSpectrumMonitor(juce::Graphics&);
    transcription::Genome genome;
    double elapsed = 0.0, ambientTime = 0.0;
    float scroll = 0.0f, dragStartWorldX = 0.0f, detachX = 0.0f;
    float outputLevel = 0.0f, smoothedLevel = 0.0f;
    std::array<float, transcription::spectrumBandCount> spectrumDb {}, spectrumDisplay {}, spectrumPeak {};
    std::array<int, transcription::spectrumBandCount> spectrumHoldFrames {};
    bool hasSpectrum = false;
    juce::Point<float> dragStart;
    juce::Point<float> codonDragStart;
    float codonScrollStart = 0.0f;
    int hoveredCodon = -1, selectedCodon = -1;
    double hintTimer = -1.0, detachTime = -1.0;
    bool started = false, running = false, dragging = false, enzymeHovered = false;
    bool codonDragging = false, codonDragMoved = false;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(GeneCanvas)
};
