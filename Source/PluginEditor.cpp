#include "PluginEditor.h"
#include <algorithm>

using namespace transcription;
namespace ui = transcription::ui;
namespace
{
constexpr std::array<const char*, 20> commonCodons {
    "AUG", "GGU", "GCU", "GUU", "CUU", "AUU", "CCU",
    "UUU", "UGG", "UCU", "ACU", "UGU", "AAU", "CAA",
    "UAU", "GAU", "GAA", "AAA", "CGU", "CAU"
};
constexpr std::array<const char*, 3> stopCodons { "UAA", "UAG", "UGA" };
// 三种终止密码子对应的音头类型与简介（顺序与 stopCodons 一致：UAA=铺底、UAG=拨弦、UGA=主音）。
constexpr std::array<const char*, 3> stopReleases { "Pad", "Pluck", "Lead" };
constexpr std::array<const char*, 3> stopReleaseHints { "Slow swell", "Quick attack", "Bright lead" };
// 全部 61 个编码密码子（UCAG 顺序，剔除 3 个终止密码子）。
const std::array<juce::String, 61> allCodingCodons = []
{
    std::array<juce::String, 61> out {};
    constexpr char bases[] = "UCAG";
    int n = 0;
    for (int i = 0; i < 64; ++i)
    {
        const auto codon = juce::String::charToString(bases[i / 16]) + juce::String::charToString(bases[(i / 4) % 4]) + juce::String::charToString(bases[i % 4]);
        if (transcription::translate(static_cast<char>(codon[0]), static_cast<char>(codon[1]), static_cast<char>(codon[2])) == '*') continue;
        out[static_cast<size_t>(n++)] = codon;
    }
    return out;
}();
const char* phaseName(GeneCanvas::Phase phase)
{
    switch (phase)
    {
        case GeneCanvas::Phase::binding: return "Polymerase binds the promoter";
        case GeneCanvas::Phase::opening: return "DNA opens into a transcription bubble";
        case GeneCanvas::Phase::elongating: return "RNA grows along the template";
        case GeneCanvas::Phase::releasing: return "RNA released; DNA closes";
        case GeneCanvas::Phase::complete: return "Transcription complete";
        case GeneCanvas::Phase::detaching: return "Polymerase detaches from DNA";
        default: return "From DNA to RNA";
    }
}
}
CodonPicker::CodonPicker()
{
    setName("Codon library");
    setWantsKeyboardFocus(true);
    setVisible(false);
}
float CodonPicker::scale() const noexcept { return juce::jmax(0.01f, static_cast<float>(getWidth()) / 820.0f); }
int CodonPicker::choiceCount() const noexcept
{
    if (mode == Mode::stop) return 3;
    return allCodons ? 61 : 20;
}
juce::Rectangle<float> CodonPicker::panel() const noexcept
{
    if (mode == Mode::stop) return { 200, 200, 420, 190 };
    if (allCodons) return { 102, 56, 616, 444 };
    return { 102, 110, 616, 336 };
}
juce::Rectangle<float> CodonPicker::cellBounds(int index) const noexcept
{
    if (mode == Mode::stop) return { 226.0f + static_cast<float>(index) * 130.0f, 272.0f, 116, 74 };
    if (allCodons) return { 126.0f + static_cast<float>(index % 8) * 70.0f, 148.0f + static_cast<float>(index / 8) * 37.0f, 62, 32 };
    return { 126.0f + static_cast<float>(index % 7) * 82.0f, 196.0f + static_cast<float>(index / 7) * 64.0f, 72, 54 };
}
juce::String CodonPicker::choice(int index) const
{
    if (index < 0 || index >= choiceCount()) return {};
    if (mode == Mode::stop) return stopCodons[static_cast<size_t>(index)];
    if (!allCodons) return commonCodons[static_cast<size_t>(index)];
    return allCodingCodons[static_cast<size_t>(index)];
}
int CodonPicker::hitCell(juce::Point<float> point) const noexcept
{
    for (int i = 0; i < choiceCount(); ++i) if (cellBounds(i).contains(point)) return i;
    return -1;
}
void CodonPicker::open(int index, const Genome& genome, Mode m, bool insert)
{
    target = index; mode = m; inserting = insert;
    replacing = !insert && index * 3 < genome.size;
    current = juce::String(genome.rna()).substring(index * 3, index * 3 + 3);
    allCodons = false; hovered = -1; keyboardChoice = 0;
    for (int i = 0; i < choiceCount(); ++i) if (choice(i) == current) keyboardChoice = i;
    setVisible(true); toFront(false);
    if (isShowing()) grabKeyboardFocus();
    repaint();
}
void CodonPicker::dismiss() { setVisible(false); }
void CodonPicker::select(int index)
{
    const auto codon = choice(index);
    if (codon.isEmpty()) return;
    dismiss();
    if (onPick) onPick(target, codon, mode, inserting);
}
void CodonPicker::paint(juce::Graphics& g)
{
    g.addTransform(juce::AffineTransform::scale(scale()));
    g.fillAll(juce::Colours::white.withAlpha(0.90f));
    const auto card = panel();
    juce::DropShadow(juce::Colours::black.withAlpha(0.07f), 16, { 0, 4 }).drawForRectangle(g, card.toNearestInt());
    g.setColour(juce::Colours::white); g.fillRoundedRectangle(card, 6);
    g.setColour(ui::line); g.drawRoundedRectangle(card, 6, 1);

    const auto closeBounds = mode == Mode::stop
        ? juce::Rectangle<float>(578, card.getY() + 18, 28, 28)
        : juce::Rectangle<float>(672, card.getY() + 18, 28, 28);
    ui::text(g, "×", closeBounds, 22, ui::muted, juce::Justification::centred);

    if (mode == Mode::stop)
    {
        ui::text(g, "Choose a stop codon", { card.getX() + 26, card.getY() + 18, 340, 28 }, 21);
        ui::text(g, "Each stop codon gives the release its own attack", { card.getX() + 26, card.getY() + 48, 340, 20 }, 11, ui::muted);
        for (int i = 0; i < choiceCount(); ++i)
        {
            const auto codon = choice(i);
            const auto cell = cellBounds(i);
            const bool selected = current == codon;
            const bool highlight = hovered == i || (hasKeyboardFocus(true) && keyboardChoice == i);
            g.setColour(selected ? ui::green.withAlpha(0.10f) : (highlight ? ui::paper : juce::Colours::white));
            g.fillRoundedRectangle(cell, 4);
            g.setColour(selected || highlight ? ui::green.withAlpha(0.5f) : ui::line);
            g.drawRoundedRectangle(cell, 4, 0.8f);
            ui::text(g, codon, { cell.getX(), cell.getY() + 3, cell.getWidth(), 24 }, 18, ui::ink, juce::Justification::centred);
            ui::text(g, stopReleases[static_cast<size_t>(i)], { cell.getX(), cell.getY() + 27, cell.getWidth(), 16 }, 11, ui::green, juce::Justification::centred);
            ui::text(g, stopReleaseHints[static_cast<size_t>(i)], { cell.getX(), cell.getY() + 48, cell.getWidth(), 18 }, 8.5f, ui::muted, juce::Justification::centred);
        }
        return;
    }

    ui::text(g, inserting ? "Add a codon" : (replacing ? "Replace codon" : "Choose a codon"), { 126, card.getY() + 20, 488, 28 }, 21);
    ui::text(g, replacing ? "Choose all three bases at once." : "Add to DNA; base pairing is automatic.",
             { 126, card.getY() + 54, 476, 20 }, 11, ui::muted);
    for (int i = 0; i < choiceCount(); ++i)
    {
        const auto codon = choice(i);
        const auto& residue = residueInfo(translate(static_cast<char>(codon[0]), static_cast<char>(codon[1]), static_cast<char>(codon[2])));
        const auto colour = ui::familyColour(residue.family);
        const auto cell = cellBounds(i);
        const bool selected = current == codon;
        const bool highlight = hovered == i || (hasKeyboardFocus(true) && keyboardChoice == i);
        g.setColour(selected ? ui::green.withAlpha(0.10f) : (highlight ? ui::paper : juce::Colours::white));
        g.fillRoundedRectangle(cell, 4);
        g.setColour(selected || highlight ? colour.withAlpha(0.5f) : ui::line);
        g.drawRoundedRectangle(cell, 4, 0.8f);
        if (allCodons)
        {
            ui::text(g, codon, { cell.getX() + 4, cell.getY() + 2, cell.getWidth() - 8, 16 }, 11.5f, ui::ink, juce::Justification::centred);
            ui::text(g, residue.abbreviation, { cell.getX(), cell.getY() + 18, cell.getWidth(), 12 }, 8, colour, juce::Justification::centred);
        }
        else
        {
            ui::text(g, codon, { cell.getX(), cell.getY() + 6, cell.getWidth(), 24 }, 17, ui::ink, juce::Justification::centred);
            ui::text(g, residue.abbreviation, { cell.getX(), cell.getY() + 32, cell.getWidth(), 15 }, 10, colour, juce::Justification::centred);
        }
    }
    const float bottom = card.getBottom() - 43;
    ui::text(g, replacing ? "Remove codon" : "RNA codons / U converts to T in DNA", { 126, bottom, 360, 26 }, 10.5f,
             replacing ? ui::baseColour('T') : ui::muted);
    ui::text(g, allCodons ? "Show compact set" : "View all 61 codons", { 536, bottom, 160, 26 }, 10.5f, ui::green, juce::Justification::centredRight);
}
void CodonPicker::mouseDown(const juce::MouseEvent& event)
{
    const auto p = event.position / scale();
    const auto card = panel();
    const auto closeBounds = mode == Mode::stop
        ? juce::Rectangle<float>(578, card.getY() + 18, 28, 28)
        : juce::Rectangle<float>(672, card.getY() + 18, 28, 28);
    if (!card.contains(p) || closeBounds.contains(p)) { dismiss(); return; }
    if (!event.mods.isLeftButtonDown()) return;
    if (mode == Mode::stop)
    {
        const int hit = hitCell(p);
        if (hit >= 0) select(hit);
        return;
    }
    if (juce::Rectangle<float>(536, card.getBottom() - 43, 160, 26).contains(p))
    {
        allCodons = !allCodons; hovered = -1; keyboardChoice = 0; repaint(); return;
    }
    if (replacing && juce::Rectangle<float>(126, card.getBottom() - 43, 110, 26).contains(p))
    {
        dismiss(); if (onRemove) onRemove(target); return;
    }
    const int hit = hitCell(p);
    if (hit >= 0) select(hit);
}
void CodonPicker::mouseMove(const juce::MouseEvent& event)
{
    hovered = hitCell(event.position / scale());
    setMouseCursor(juce::MouseCursor::PointingHandCursor);
    repaint();
}
void CodonPicker::mouseExit(const juce::MouseEvent&) { hovered = -1; repaint(); }
bool CodonPicker::keyPressed(const juce::KeyPress& key)
{
    if (key == juce::KeyPress::escapeKey) { dismiss(); return true; }
    if (key == juce::KeyPress::returnKey) { select(keyboardChoice); return true; }
    int delta = 0;
    if (mode == Mode::stop)
    {
        if (key == juce::KeyPress::leftKey) delta = -1;
        if (key == juce::KeyPress::rightKey) delta = 1;
    }
    else
    {
        if (key == juce::KeyPress::leftKey) delta = -1;
        if (key == juce::KeyPress::rightKey) delta = 1;
        if (key == juce::KeyPress::upKey) delta = allCodons ? -8 : -7;
        if (key == juce::KeyPress::downKey) delta = allCodons ? 8 : 7;
    }
    if (delta == 0) return false;
    keyboardChoice = juce::jlimit(0, choiceCount() - 1, keyboardChoice + delta); repaint(); return true;
}

TranscriptionAudioProcessorEditor::TranscriptionAudioProcessorEditor(TranscriptionAudioProcessor& p)
    : AudioProcessorEditor(p), processor(p), tooltips(this, 700)
{
    setLookAndFeel(&lookAndFeel);
    setWantsKeyboardFocus(true);
    configureButton(preset, "Central Dogma");
    preset.getProperties().set("preset", true);
    configureButton(more, "···");
    more.setTooltip(juce::String::fromUTF8("Undo, redo and reset"));
    configureButton(transport, "Start transcription", false);
    transport.setToggleState(true, juce::dontSendNotification);
    transport.setTooltip(juce::String::fromUTF8("Play or pause the transcription animation. Visual only: does not trigger notes or change the sound engine."));
    preset.onClick = [this] { showPresets(); };
    more.onClick = [this] { showMore(); };
    transport.onClick = [this] { canvas.toggleRunning(); };
    addAndMakeVisible(canvas);
    canvas.onEdit = [this](const Genome& next) { applyEdit(next); };
    canvas.onChooseCodon = [this](int index, bool insert) { picker.open(index, processor.currentGenome(), CodonPicker::Mode::amino, insert); };
    canvas.onChooseStop = [this] { picker.open(processor.currentGenome().codons() - 1, processor.currentGenome(), CodonPicker::Mode::stop); };
    canvas.onProcessChanged = [this] { updateAction(); };
    addChildComponent(picker);
    picker.onPick = [this](int index, const juce::String& codon, CodonPicker::Mode mode, bool insert)
    {
        if (mode == CodonPicker::Mode::stop) canvas.setStopCodon(codon.toStdString());
        else if (insert) canvas.insertCodon(index, codon.toStdString());
        else canvas.setCodon(index, codon.toStdString());
    };
    picker.onRemove = [this](int index) { canvas.removeCodon(index); };
    genome = processor.currentGenome(); canvas.setData(genome);
    lastRevision = processor.revision();
    const int width = juce::jlimit(615, 1640, processor.editorWidth.load());
    setResizable(true, false); setResizeLimits(615, 420, 1640, 1120);
    if (auto* constraint = getConstrainer()) constraint->setFixedAspectRatio(820.0 / 560.0);
    setSize(width, juce::roundToInt(static_cast<double>(width) * 560.0 / 820.0));
    lastTick = juce::Time::getMillisecondCounterHiRes();
    timerCallback(); startTimerHz(30);
}
TranscriptionAudioProcessorEditor::~TranscriptionAudioProcessorEditor()
{
    stopTimer();
    juce::PopupMenu::dismissAllActiveMenus();
    setLookAndFeel(nullptr);
}
void TranscriptionAudioProcessorEditor::configureButton(juce::TextButton& button, const char* label, bool flat)
{
    button.setButtonText(juce::String::fromUTF8(label));
    button.getProperties().set("flat", flat);
    button.setColour(juce::TextButton::textColourOnId, ui::green);
    button.setMouseCursor(juce::MouseCursor::PointingHandCursor);
    addAndMakeVisible(button);
}
juce::Rectangle<int> TranscriptionAudioProcessorEditor::bounds(float x, float y, float w, float h) const
{
    return { juce::roundToInt(x * uiScale), juce::roundToInt(y * uiScale), juce::roundToInt(w * uiScale), juce::roundToInt(h * uiScale) };
}
void TranscriptionAudioProcessorEditor::resized()
{
    uiScale = static_cast<float>(getWidth()) / 820.0f;
    lookAndFeel.scale = uiScale;
    processor.editorWidth.store(getWidth());
    preset.setBounds(bounds(279, 9, 262, 29));
    more.setBounds(bounds(756, 9, 40, 29));
    canvas.setBounds(bounds(24, 126, 772, 382));
    transport.setBounds(bounds(651, 518, 145, 31));
    picker.setBounds(getLocalBounds());
}
void TranscriptionAudioProcessorEditor::updateAction()
{
    const auto phase = canvas.phase();
    transport.setEnabled(canvas.currentGenome().size > 0);
    transport.setButtonText(juce::String::fromUTF8(canvas.isRunning() ? "Pause" : (phase == GeneCanvas::Phase::ready ? "Start transcription"
        : (phase == GeneCanvas::Phase::complete ? "Transcribe again" : "Resume"))));
    repaint();
}
void TranscriptionAudioProcessorEditor::showPresets()
{
    juce::PopupMenu menu;
    menu.setLookAndFeel(&lookAndFeel);
    menu.addSectionHeader(juce::String::fromUTF8("Choose a gene fragment"));
    const int current = processor.matchingPreset();
    for (int i = 0; i < presetCount; ++i) menu.addItem(i + 1, factoryPresets()[static_cast<size_t>(i)].name, true, current == i);
    const juce::Component::SafePointer<TranscriptionAudioProcessorEditor> safe(this);
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&preset).withMinimumWidth(juce::roundToInt(262 * uiScale)),
                      [safe](int result) { if (safe != nullptr && result > 0) safe->choosePreset(result - 1); });
}
void TranscriptionAudioProcessorEditor::showMore()
{
    juce::PopupMenu menu;
    menu.setLookAndFeel(&lookAndFeel);
    menu.addItem(1, juce::String::fromUTF8("Undo codon edit    Ctrl+Z"), !undoStack.empty());
    menu.addItem(2, juce::String::fromUTF8("Redo    Ctrl+Shift+Z"), !redoStack.empty());
    menu.addSeparator();
    menu.addItem(3, juce::String::fromUTF8("Reset transcription"));
    menu.addItem(4, juce::String::fromUTF8("Clear middle codons"), genome.codons() > 2);
    menu.addSeparator();
    menu.addSectionHeader(juce::String::fromUTF8("Visual model: DNA → RNA, not protein translation."));
    menu.addSectionHeader(juce::String::fromUTF8("Start (ATG) and stop codons are fixed boundaries."));
    const juce::Component::SafePointer<TranscriptionAudioProcessorEditor> safe(this);
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&more), [safe](int result)
    {
        if (safe == nullptr) return;
        if (result == 1 || result == 2) safe->undoEdit(result == 2);
        if (result == 3) safe->canvas.resetProcess();
        if (result == 4) safe->applyEdit(Genome::minimal());
    });
}
void TranscriptionAudioProcessorEditor::choosePreset(int index)
{
    processor.loadPreset(index); undoStack.clear(); redoStack.clear();
    picker.dismiss();
    genome = processor.currentGenome(); canvas.setData(genome); canvas.resetProcess();
    lastRevision = processor.revision(); updateAction(); timerCallback();
}
void TranscriptionAudioProcessorEditor::applyEdit(const Genome& next)
{
    const auto previous = processor.currentGenome();
    if (next != previous && processor.setGenome(next))
    {
        if (undoStack.size() >= 64) undoStack.erase(undoStack.begin());
        undoStack.push_back(previous); redoStack.clear();
    }
    genome = processor.currentGenome(); canvas.setData(genome);
    lastRevision = processor.revision(); updateAction(); timerCallback();
}
void TranscriptionAudioProcessorEditor::undoEdit(bool redo)
{
    auto& from = redo ? redoStack : undoStack;
    auto& to = redo ? undoStack : redoStack;
    if (from.empty()) return;
    to.push_back(processor.currentGenome()); processor.setGenome(from.back()); from.pop_back();
    genome = processor.currentGenome(); canvas.setData(genome);
    picker.dismiss(); lastRevision = processor.revision(); updateAction(); timerCallback();
}
void TranscriptionAudioProcessorEditor::timerCallback()
{
    const double now = juce::Time::getMillisecondCounterHiRes();
    const double delta = juce::jlimit(0.0, 0.1, (now - lastTick) * 0.001);
    lastTick = now;
    if (processor.revision() != lastRevision)
    {
        const auto next = processor.currentGenome();
        if (next != genome) { undoStack.clear(); redoStack.clear(); picker.dismiss(); }
        genome = next; canvas.setData(genome); lastRevision = processor.revision(); updateAction();
    }
    const int width = juce::jlimit(615, 1640, processor.editorWidth.load());
    if (getWidth() != width) setSize(width, juce::roundToInt(static_cast<double>(width) * 560.0 / 820.0));
    const int current = processor.matchingPreset();
    preset.setButtonText(juce::String::fromUTF8(current >= 0 ? factoryPresets()[static_cast<size_t>(current)].name : "Custom fragment"));
    preset.setTooltip(juce::String::fromUTF8(current >= 0 ? factoryPresets()[static_cast<size_t>(current)].source : "Choose a gene fragment"));
    canvas.advance(delta);
    canvas.setOutputLevel(processor.outputPeak());
    {
        std::array<float, transcription::spectrumBandCount> bands {};
        for (int i = 0; i < transcription::spectrumBandCount; ++i)
            bands[static_cast<size_t>(i)] = processor.spectrumBandDb(i);
        canvas.setSpectrum(bands);
    }
    processor.scanPhase.store(canvas.scanPhase(), std::memory_order_relaxed);
    repaint();
}
void TranscriptionAudioProcessorEditor::drawStages(juce::Graphics& g)
{
    constexpr std::array<const char*, 4> names { "Bind", "Unwind", "Extend", "Release" };
    const auto phase = canvas.phase();
    int active = -1;
    if (phase != GeneCanvas::Phase::ready)
    {
        if (canvas.isDragging())
        {
            // 拖动聚合酶时，阶段随延伸进度逐渐点亮（Bind → Unwind → Extend → Release），
            // 而不是一拖动就直接跳到 Extend。
            const float p = canvas.progress();
            active = p >= 1.0f ? 3 : (p < 0.2f ? 0 : (p < 0.4f ? 1 : 2));
        }
        else
        {
            active = juce::jmin(3, static_cast<int>(phase) - 1);
        }
    }
    for (int i = 0; i < 4; ++i)
    {
        const float x = 32.0f + static_cast<float>(i) * 100.0f;
        g.setColour(i <= active ? ui::green : ui::line);
        g.fillEllipse(x, 531, 4, 4);
        ui::text(g, names[static_cast<size_t>(i)], { x + 11, 523, 45, 20 }, 10, i <= active ? ui::green : ui::muted);
        if (i < 3) { g.setColour(ui::line); g.drawLine(x + 60, 533, x + 86, 533, 0.8f); }
    }
}
void TranscriptionAudioProcessorEditor::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colours::white);
    g.addTransform(juce::AffineTransform::scale(uiScale));
    g.setColour(ui::line); g.drawHorizontalLine(46, 0, 820);
    ui::text(g, "iisaacbeats.cn", { 22, 13, 110, 22 }, 15, websiteHovered ? ui::blue : ui::ink);
    ui::text(g, "v" + juce::String(JucePlugin_VersionString), { 137, 16, 86, 17 }, 11.5f, ui::muted.withAlpha(0.7f));
    if (websiteHovered) { g.setColour(ui::blue); g.drawLine(22, 35, 121, 35, 0.8f); }
    ui::text(g, "03 / BIOCHEMISTRY", { 25, 64, 220, 17 }, 9.5f, ui::muted);
    ui::text(g, "Transcription", { 23, 82, 205, 33 }, 27, ui::ink);
    ui::text(g, "Biochemistry / Transcription", { 215, 91, 160, 19 }, 11, ui::muted);
    ui::text(g, phaseName(canvas.phase()), { 486, 70, 306, 23 }, 13, ui::green, juce::Justification::centredRight);
    const auto detail = canvas.phase() == GeneCanvas::Phase::ready ? "Choose codons and watch transcription unfold"
        : (canvas.phase() == GeneCanvas::Phase::complete ? "RNA leaves the template; DNA rejoins" : "Template 3' → 5'  /  RNA synthesis 5' → 3'");
    ui::text(g, detail, { 414, 98, 378, 17 }, 9.5f, ui::muted, juce::Justification::centredRight);
    g.setColour(ui::line); g.drawHorizontalLine(510, 24, 796);
    drawStages(g);
    ui::text(g, "Transcription model", { 478, 525, 146, 17 }, 9, ui::muted, juce::Justification::centredRight);
}
void TranscriptionAudioProcessorEditor::mouseMove(const juce::MouseEvent& event)
{
    websiteHovered = juce::Rectangle<float>(22, 13, 110, 24).contains(event.position / uiScale);
    setMouseCursor(websiteHovered ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
    repaint();
}
void TranscriptionAudioProcessorEditor::mouseExit(const juce::MouseEvent&) { websiteHovered = false; repaint(); }
void TranscriptionAudioProcessorEditor::mouseDown(const juce::MouseEvent& event)
{
    if (juce::Rectangle<float>(22, 13, 110, 24).contains(event.position / uiScale)) juce::URL("https://iisaacbeats.cn").launchInDefaultBrowser();
}
bool TranscriptionAudioProcessorEditor::keyPressed(const juce::KeyPress& key)
{
    if (picker.isVisible()) return picker.keyPressed(key);
    if (key.getModifiers().isCommandDown() && (key.getKeyCode() == 'Z' || key.getKeyCode() == 'z')) { undoEdit(key.getModifiers().isShiftDown()); return true; }
    if (key.getModifiers().isCommandDown() && (key.getKeyCode() == 'Y' || key.getKeyCode() == 'y')) { undoEdit(true); return true; }
    if (key == juce::KeyPress::spaceKey) { canvas.toggleRunning(); return true; }
    return false;
}
