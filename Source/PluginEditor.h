#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "UI/ScientificLookAndFeel.h"
#include "UI/GeneCanvas.h"
#include <vector>

class CodonPicker final : public juce::Component
{
public:
    enum class Mode { amino, stop };
    CodonPicker();
    void open(int index, const transcription::Genome&, Mode mode, bool insert = false);
    void dismiss();
    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseMove(const juce::MouseEvent&) override;
    void mouseExit(const juce::MouseEvent&) override;
    bool keyPressed(const juce::KeyPress&) override;
    int choiceCount() const noexcept;
    juce::String choice(int index) const;
    std::function<void(int index, const juce::String& codon, Mode mode, bool insert)> onPick;
    std::function<void(int)> onRemove;
private:
    float scale() const noexcept;
    juce::Rectangle<float> panel() const noexcept;
    juce::Rectangle<float> cellBounds(int index) const noexcept;
    int hitCell(juce::Point<float>) const noexcept;
    void select(int index);
    Mode mode = Mode::amino;
    int target = 0, hovered = -1, keyboardChoice = 0;
    bool allCodons = false, replacing = false, inserting = false;
    juce::String current;
};

// 首次打开时的引导层：只画在画布上方，不拦截鼠标，点击直接落到右下角的转录按钮。
class FirstRunHint final : public juce::Component
{
public:
    FirstRunHint();
    void paint(juce::Graphics&) override;
    void setPhase(double value) noexcept { phase = value; repaint(); }
private:
    double phase = 0.0;
};

class TranscriptionAudioProcessorEditor final : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit TranscriptionAudioProcessorEditor(TranscriptionAudioProcessor&);
    ~TranscriptionAudioProcessorEditor() override;
    void paint(juce::Graphics&) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseMove(const juce::MouseEvent&) override;
    void mouseExit(const juce::MouseEvent&) override;
    bool keyPressed(const juce::KeyPress&) override;
private:
    void timerCallback() override;
    juce::Rectangle<int> bounds(float x, float y, float w, float h) const;
    void configureButton(juce::TextButton&, const char*, bool flat = true);
    void showPresets();
    void showMore();
    void showIntro();
    void choosePreset(int index);
    void applyEdit(const transcription::Genome&);
    void undoEdit(bool redo);
    void updateAction();
    void drawStages(juce::Graphics&);
    TranscriptionAudioProcessor& processor;
    transcription::ui::ScientificLookAndFeel lookAndFeel;
    GeneCanvas canvas;
    CodonPicker picker;
    juce::TooltipWindow tooltips;
    FirstRunHint hint;
    juce::PropertiesFile settings;
    bool onboarding = false, demoNoteHeld = false;
    double demoNoteStarted = 0.0;
    juce::RangedAudioParameter* scanParameter = nullptr;
    float lastScanWritten = 0.0f;
    double lastScanWriteMs = 0.0;
    bool wasDriving = false;
    juce::TextButton preset, transport, more;
    transcription::Genome genome;
    std::vector<transcription::Genome> undoStack, redoStack;
    std::uint32_t lastRevision = 0;
    double lastTick = 0.0;
    float uiScale = 1.0f;
    bool websiteHovered = false;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TranscriptionAudioProcessorEditor)
};
