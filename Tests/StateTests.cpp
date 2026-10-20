#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <cmath>
#include <iostream>

namespace
{
int failures = 0;
void check(bool condition, const char* message)
{
    if (!condition) { ++failures; std::cerr << "FAIL: " << message << '\n'; }
}
juce::MouseEvent mouse(juce::Component& component, juce::Point<float> point, int modifiers = juce::ModifierKeys::leftButtonModifier, bool dragged = false)
{
    const auto now = juce::Time::getCurrentTime();
    return { juce::Desktop::getInstance().getMainMouseSource(), point, juce::ModifierKeys(modifiers),
             1.0f, 0, 0, 0, 0, &component, &component, now, point, now, 1, dragged };
}
void interactionTests()
{
    GeneCanvas canvas;
    canvas.setSize(772, 382);
    const auto original = transcription::presetGenome(0);
    auto edited = original;
    int edits = 0, requested = -1, stopRequests = 0;
    bool insertMode = false;
    canvas.onEdit = [&](const transcription::Genome& next) { edited = next; ++edits; };
    canvas.onChooseCodon = [&](int index, bool insert) { requested = index; insertMode = insert; };
    canvas.onChooseStop = [&] { ++stopRequests; };
    canvas.setData(original);

    canvas.mouseDown(mouse(canvas, { 130, 339 }));
    canvas.mouseUp(mouse(canvas, { 130, 339 }));
    check(requested == 1 && !insertMode && edits == 0 && canvas.currentGenome() == original,
          "clicking a middle codon opens a replacement chooser without editing bases");
    canvas.mouseDown(mouse(canvas, { 60, 339 }, juce::ModifierKeys::rightButtonModifier));
    check(edits == 0 && canvas.currentGenome() == original, "right-clicking the fixed start codon does not delete it");
    requested = -1;
    canvas.mouseDown(mouse(canvas, { 60, 339 }));
    canvas.mouseUp(mouse(canvas, { 60, 339 }));
    check(requested == -1 && stopRequests == 0 && edits == 0, "the start codon is fixed and only shows a hint");

    check(canvas.setCodon(1, "UGG") && edited.dna().substr(3, 3) == "TGG" && edited.size == original.size && edits == 1,
          "one codon choice replaces exactly three bases and converts RNA U to DNA T");
    const auto valid = canvas.currentGenome();
    check(!canvas.setCodon(0, "UGG") && !canvas.setCodon(valid.codons() - 1, "UGG") && canvas.currentGenome() == valid,
          "boundary codons cannot be replaced as normal codons");
    check(canvas.setStopCodon("UAA") && canvas.currentGenome().dna().substr(canvas.currentGenome().size - 3) == "TAA",
          "the stop codon switches to TAA");
    check(canvas.setStopCodon("UAG") && canvas.currentGenome().dna().substr(canvas.currentGenome().size - 3) == "TAG",
          "the stop codon switches to TAG");
    check(!canvas.setStopCodon("GCU") && canvas.currentGenome().dna().substr(canvas.currentGenome().size - 3) == "TAG",
          "a non-stop codon is rejected for the stop position");

    const auto beforeDel = canvas.currentGenome();
    check(canvas.removeCodon(2) && canvas.currentGenome().codons() == beforeDel.codons() - 1, "removal acts on a whole codon");
    const auto v2 = canvas.currentGenome();
    check(!canvas.removeCodon(0) && !canvas.removeCodon(v2.codons() - 1) && canvas.currentGenome() == v2,
          "boundary codons cannot be removed");

    const auto beforeAdd = canvas.currentGenome();
    const int addAt = beforeAdd.codons() - 1;
    check(canvas.insertCodon(addAt, "GCU") && canvas.currentGenome().codons() == beforeAdd.codons() + 1
          && canvas.currentGenome().dna().substr(addAt * 3, 3) == "GCT"
          && canvas.currentGenome().dna().substr(canvas.currentGenome().size - 3) == beforeAdd.dna().substr(beforeAdd.size - 3),
          "adding inserts a codon before the stop codon");

    const auto v3 = canvas.currentGenome();
    check(!canvas.setCodon(1, "A") && !canvas.insertCodon(1, "XYZ") && !canvas.insertCodon(0, "GCU")
          && canvas.currentGenome() == v3, "invalid choices never partially modify DNA");

    canvas.setData(transcription::presetGenome(1));
    check(!canvas.setCodon(32, "AUG") && canvas.currentGenome().size == 96, "codon editing respects the existing 96-base limit");

    transcription::Genome partial;
    transcription::Genome::parse("ATGG", partial); canvas.setData(partial);
    check(canvas.currentGenome().dna() == "ATGTGA", "legacy partial DNA is normalised to a start + stop frame");

    canvas.setData(original); canvas.setSize(1544, 764); requested = -1;
    canvas.mouseDown(mouse(canvas, { 260, 678 }));
    canvas.mouseUp(mouse(canvas, { 260, 678 }));
    check(requested == 1, "whole-codon hit testing scales with the native editor");

    canvas.setSize(772, 382); canvas.setData({});
    check(canvas.currentGenome().dna() == "ATGTGA" && canvas.currentGenome().codons() == 2,
          "an empty template becomes the minimal start + stop frame");
    requested = -1; stopRequests = 0;
    canvas.mouseDown(mouse(canvas, { 130, 339 }));
    canvas.mouseUp(mouse(canvas, { 130, 339 }));
    check(stopRequests == 1 && requested == -1, "clicking the stop codon opens the stop chooser");
    requested = -1;
    canvas.mouseDown(mouse(canvas, { 708, 298 }));
    check(requested == 1 && insertMode, "the + Add button requests an insertion before the stop codon");

    // 右键快速删除中间密码子（起始与终止固定，不受影响）。
    canvas.setData(original); edits = 0;
    const int beforeRc = canvas.currentGenome().codons();
    canvas.mouseDown(mouse(canvas, { 130, 339 }, juce::ModifierKeys::rightButtonModifier));
    check(edits == 1 && canvas.currentGenome().codons() == beforeRc - 1, "right-clicking a middle codon deletes it");
    canvas.mouseDown(mouse(canvas, { 60, 339 }, juce::ModifierKeys::rightButtonModifier));
    check(edits == 1 && canvas.currentGenome().codons() == beforeRc - 1, "right-clicking the fixed start codon is ignored");
}
void processTests()
{
    GeneCanvas canvas;
    canvas.setSize(772, 382);
    transcription::Genome genome;
    transcription::Genome::parse("AUG GCU UGA", genome);
    canvas.setData(genome);
    int edits = 0;
    canvas.onEdit = [&](const transcription::Genome&) { ++edits; };
    canvas.toggleRunning();
    check(canvas.phase() == GeneCanvas::Phase::binding, "transcription begins by binding polymerase");
    canvas.advance(0.8);
    check(canvas.phase() == GeneCanvas::Phase::opening, "binding is followed by opening the DNA bubble");
    canvas.advance(0.8); canvas.advance(0.6);
    check(canvas.phase() == GeneCanvas::Phase::elongating && canvas.nascentRna() == "AUGG", "RNA grows nucleotide by nucleotide in coding-strand order");
    canvas.pause(); const float paused = canvas.progress(); canvas.advance(5.0);
    check(canvas.progress() == paused, "pause freezes the transcription position");
    canvas.toggleRunning(); canvas.advance(0.7);
    check(canvas.phase() == GeneCanvas::Phase::releasing && canvas.nascentRna() == "AUGGCUUGA", "RNA completes when the polymerase reaches the end");
    canvas.advance(2.0);
    check(canvas.phase() == GeneCanvas::Phase::complete && !canvas.isRunning(), "RNA release completes without automatically looping");
    check(edits == 0 && canvas.currentGenome() == genome, "transcription never mutates DNA or the audio patch");
    canvas.toggleRunning();
    check(canvas.phase() == GeneCanvas::Phase::binding && canvas.producedBases() == 0, "replay returns to the binding stage");
    canvas.resetProcess();
    const auto enzyme = canvas.polymerasePosition();
    // 与 GeneCanvas 内部一致：转录区世界长度 = 碱基数 × 9px（一个密码子三对碱基，无额外侧翼）。
    const float dnaSpan = static_cast<float>(genome.size) * 9.0f;
    const float halfDrag = dnaSpan * 0.5f;   // 链身较短，用与链长成比例的位移，避免拖出编码区被截断
    canvas.mouseDown(mouse(canvas, enzyme));
    canvas.mouseDrag(mouse(canvas, enzyme + juce::Point<float>(halfDrag, 0), juce::ModifierKeys::leftButtonModifier, true));
    canvas.mouseUp(mouse(canvas, enzyme + juce::Point<float>(halfDrag, 0), 0, true));
    check(std::abs(canvas.progress() - 0.5f) < 0.0001f && !canvas.isRunning(), "dragging polymerase directly scrubs and pauses the process");
    // 链身长度必须就是碱基数 × 碱基对间距：拖过整条编码区正好等于走完全程，
    // 这样画出的碱基对细线数量才等于密码子数 × 3。
    canvas.resetProcess();
    const auto startEnzyme = canvas.polymerasePosition();
    const juce::Point<float> fullDrag { dnaSpan, 0.0f };
    canvas.mouseDown(mouse(canvas, startEnzyme));
    canvas.mouseDrag(mouse(canvas, startEnzyme + fullDrag, juce::ModifierKeys::leftButtonModifier, true));
    canvas.mouseUp(mouse(canvas, startEnzyme + fullDrag, 0, true));
    check(std::abs(canvas.progress() - 1.0f) < 0.0001f,
          "dragging across the coding region covers exactly the whole transcript, so one codon is three base pairs");
    canvas.seek(1.0f); canvas.toggleRunning(); canvas.advance(2);
    check(canvas.nascentRna() == "AUGGCUUGA" && edits == 0, "manual traversal still produces the complete matching RNA");

    // 拖到最左端：触发聚合酶与 DNA 的分离动画，动画结束后回到未绑定状态。
    canvas.seek(0.5f);
    const auto midEnzyme = canvas.polymerasePosition();
    canvas.mouseDown(mouse(canvas, midEnzyme));
    canvas.mouseDrag(mouse(canvas, { 94.0f, midEnzyme.y }, juce::ModifierKeys::leftButtonModifier, true));
    canvas.mouseUp(mouse(canvas, { 94.0f, midEnzyme.y }, 0, true));
    check(canvas.phase() == GeneCanvas::Phase::detaching && canvas.progress() == 0.0f && !canvas.isRunning(),
          "dragging polymerase to the far left starts the detachment animation");
    const auto lifted = canvas.polymerasePosition();
    check(std::abs(lifted.x - midEnzyme.x) < 1.0f,
          "the detachment animation lifts the polymerase where it was released instead of snapping to the DNA start");
    canvas.advance(0.5);
    const auto almostHome = canvas.polymerasePosition();
    canvas.advance(0.1);
    const auto home = canvas.polymerasePosition();
    check(canvas.phase() == GeneCanvas::Phase::ready && canvas.progress() == 0.0f,
          "the detachment animation completes back to the unbound state");
    check(std::abs(almostHome.x - home.x) < 8.0f,
          "the polymerase drifts back to the start site without jumping when the animation ends");
}
void pickerTests()
{
    CodonPicker picker;
    picker.setSize(820, 560);
    const auto genome = transcription::presetGenome(0);
    juce::String selected;
    int picked = -1, removed = -1;
    bool insertPicked = false;
    CodonPicker::Mode pickedMode = CodonPicker::Mode::amino;
    picker.onPick = [&](int index, const juce::String& codon, CodonPicker::Mode mode, bool insert)
        { picked = index; selected = codon; pickedMode = mode; insertPicked = insert; };
    picker.onRemove = [&](int index) { removed = index; };

    picker.open(0, genome, CodonPicker::Mode::amino);
    check(picker.choiceCount() == 20, "the compact set holds 20 amino acids without a stop codon");
    picker.mouseDown(mouse(picker, { 162, 221 }));
    check(picked == 0 && selected == "AUG" && pickedMode == CodonPicker::Mode::amino && !picker.isVisible(),
          "a single click chooses a complete codon and closes the picker");

    picker.open(1, genome, CodonPicker::Mode::amino);
    picker.mouseDown(mouse(picker, { 616, 415 }));
    juce::StringArray choices;
    int stops = 0;
    for (int i = 0; i < picker.choiceCount(); ++i)
    {
        const auto codon = picker.choice(i);
        choices.addIfNotAlreadyThere(codon);
        if (transcription::translate(static_cast<char>(codon[0]), static_cast<char>(codon[1]), static_cast<char>(codon[2])) == '*') ++stops;
    }
    check(choices.size() == 61 && stops == 0, "all 61 coding codons remain available, stop codons excluded");
    picker.mouseDown(mouse(picker, { 155, 162 }));
    check(picked == 1 && selected == "UUU", "the expanded library chooses the clicked codon");

    picker.open(genome.codons() - 1, genome, CodonPicker::Mode::stop);
    check(picker.choiceCount() == 3, "the stop chooser offers exactly three codons");
    picker.mouseDown(mouse(picker, { 284, 297 }));
    check(picked == genome.codons() - 1 && selected == "UAA" && pickedMode == CodonPicker::Mode::stop,
          "a click picks one of the three stop codons");

    picker.open(2, genome, CodonPicker::Mode::amino); picker.mouseDown(mouse(picker, { 172, 414 }));
    check(removed == 2 && !picker.isVisible(), "removal is explicit and scoped to the chosen codon");

    picker.open(0, genome, CodonPicker::Mode::amino);
    picker.keyPressed(juce::KeyPress(juce::KeyPress::rightKey)); picker.keyPressed(juce::KeyPress(juce::KeyPress::returnKey));
    check(selected == "GGU", "the compact chooser supports arrow keys and enter");

    picker.open(0, genome, CodonPicker::Mode::amino); picker.keyPressed(juce::KeyPress(juce::KeyPress::escapeKey));
    check(!picker.isVisible(), "escape cancels without editing DNA");
}
void openEditorStateTests()
{
    TranscriptionAudioProcessor donor, target;
    auto editor = std::unique_ptr<juce::AudioProcessorEditor>(target.createEditor());
    GeneCanvas* scene = nullptr;
    juce::TextButton* transport = nullptr;
    int visibleButtons = 0, textEditors = 0, sliders = 0;
    for (auto* child : editor->getChildren())
    {
        if (auto* candidate = dynamic_cast<GeneCanvas*>(child)) scene = candidate;
        if (auto* button = dynamic_cast<juce::TextButton*>(child))
        {
            if (child->isVisible()) ++visibleButtons;
            if (button->getButtonText() == "Start transcription") transport = button;
        }
        if (dynamic_cast<juce::TextEditor*>(child) != nullptr) ++textEditors;
        if (dynamic_cast<juce::Slider*>(child) != nullptr) ++sliders;
    }
    check(scene != nullptr && visibleButtons == 3 && textEditors == 0 && sliders == 0, "the main screen contains one process scene and only three buttons, without compiler fields or sound knobs");
    check(transport != nullptr && transport->getTooltip().contains("Visual only"),
          "transport labels and hints use English");
    check(scene != nullptr && scene->getTooltip().contains("RNA polymerase"), "scene guidance uses English biochemical terminology");
    const auto tick = []
    {
        for (int i = 0; i < 12; ++i) { juce::Thread::sleep(10); juce::Timer::callPendingTimersSynchronously(); }
    };
    const std::array<std::array<int, 2>, 4> scenarios {{ { 1025, 1 }, { 1230, 1 }, { 615, 0 }, { 1640, 0 } }};
    for (const auto& scenario : scenarios)
    {
        const int width = scenario[0], tab = scenario[1];
        donor.editorWidth.store(width); donor.editorTab.store(tab);
        juce::MemoryBlock saved; donor.getStateInformation(saved);
        target.setStateInformation(saved.getData(), static_cast<int>(saved.getSize())); tick();
        check(editor->getWidth() == width && editor->getHeight() == juce::roundToInt(static_cast<double>(width) * 560.0 / 820.0), "the single scene restores width and aspect ratio while open");
        juce::MemoryBlock roundTrip; target.getStateInformation(roundTrip);
        const auto xml = juce::AudioProcessor::getXmlFromBinary(roundTrip.getData(), static_cast<int>(roundTrip.getSize()));
        check(xml != nullptr && xml->getIntAttribute("editorWidth") == width && xml->getIntAttribute("editorTab") == tab,
              "legacy tab state is preserved without reintroducing the old pages");
    }
    editor->setSize(902, 616); tick();
    check(editor->getWidth() == 902 && target.editorWidth.load() == 902, "manual resizing remains persistent");
    if (scene != nullptr)
    {
        target.loadPreset(0); tick();   // 默认实例只有起始与终止密码子；用较长的工厂序列测试整组替换
        const auto original = target.currentGenome();
        const auto controls = target.currentControls();
        scene->setCodon(1, "GCU");
        check(target.currentGenome() != original && transcription::sameControls(controls, target.currentControls()), "codon selection updates the saved DNA without altering sound controls");
        editor->keyPressed(juce::KeyPress('Z', juce::ModifierKeys(juce::ModifierKeys::commandModifier), 0));
        check(target.currentGenome() == original, "a whole-codon choice is one undo operation");
        scene->toggleRunning(); scene->advance(4.0);
        check(transport != nullptr && transport->getButtonText() == "Pause", "running transcription shows the English pause action");
        scene->toggleRunning();
        check(transport != nullptr && transport->getButtonText() == "Resume", "paused transcription shows the English resume action");
        scene->toggleRunning(); scene->advance(100.0);
        check(transport != nullptr && transport->getButtonText() == "Transcribe again", "completed transcription shows the English replay action");
        check(target.soundingVoices() == 0 && target.currentGenome() == original, "running the visual process neither triggers MIDI nor rewrites DNA");
    }
}
void stateTests()
{
    TranscriptionAudioProcessor fresh;
    check(fresh.currentGenome().dna() == "ATGTGA" && fresh.currentGenome().codons() == 2
              && fresh.matchingPreset() == transcription::presetCount - 1,
          "a new instance starts from the minimal start + stop frame");

    TranscriptionAudioProcessor source;
    source.loadPreset(2);
    auto genome = source.currentGenome(); genome.setIntron(3, 9, true); genome.erase(31);
    source.setGenome(genome); source.setParameter("precision", 0.333f); source.setParameter("output", -12.5f);
    source.editorWidth.store(1025); source.editorTab.store(1);
    const auto stored = source.currentGenome();
    juce::MemoryBlock saved; source.getStateInformation(saved);
    TranscriptionAudioProcessor restored;
    restored.setStateInformation(saved.getData(), static_cast<int>(saved.getSize()));
    check(restored.currentGenome() == stored, "DNA and per-base splice flags survive host state");
    check(transcription::sameControls(restored.currentControls(), source.currentControls()), "all seven parameters survive host state");
    check(restored.editorWidth.load() == 1025 && restored.editorTab.load() == 1, "window width and tab survive host state");
    check(restored.getCurrentProgram() == 2, "factory program context survives host state");
    restored.prepareToPlay(48000, 256);
    juce::AudioBuffer<float> buffer(2, 256); juce::MidiBuffer midi;
    midi.addEvent(juce::MidiMessage::noteOn(1, 60, 0.8f), 128);
    restored.processBlock(buffer, midi);
    check(buffer.getMagnitude(0, 128) == 0.0f, "MIDI note-on is not rendered before its sample offset");
    float peak = buffer.getMagnitude(0, 256);
    for (int block = 0; block < 120; ++block) { restored.processBlock(buffer, midi); peak = juce::jmax(peak, buffer.getMagnitude(0, 256)); }
    check(peak > 0.001f && peak <= 1.0f, "restored state produces audio without creating an editor");
    check(midi.isEmpty(), "instrument does not emit incoming MIDI");
    restored.processBlockBypassed(buffer, midi);
    check(buffer.getMagnitude(0, 256) == 0.0f && restored.soundingVoices() == 0, "host bypass silences the instrument and clears voices");
    midi.addEvent(juce::MidiMessage::noteOn(1, 64, 0.9f), 0);
    restored.processBlock(buffer, midi);
    { auto liveEditor = std::unique_ptr<juce::AudioProcessorEditor>(restored.createEditor()); }
    for (int block = 0; block < 300; ++block) restored.processBlock(buffer, midi);
    check(restored.soundingVoices() == 1, "closing the editor does not release notes received from the host");
    juce::AudioBuffer<float> emptyBuffer(2, 0);
    midi.addEvent(juce::MidiMessage::noteOff(1, 64), 0);
    restored.processBlock(emptyBuffer, midi);
    for (int block = 0; block < 1600; ++block) restored.processBlock(buffer, midi);
    check(restored.soundingVoices() == 0, "zero-sample blocks still deliver MIDI note-off without leaving stuck voices");
    const auto before = restored.currentGenome();
    const char garbage[] = "not a plugin state";
    restored.setStateInformation(garbage, sizeof(garbage));
    restored.setStateInformation(nullptr, 1);
    check(restored.currentGenome() == before, "malformed state is ignored");
    auto xml = juce::AudioProcessor::getXmlFromBinary(saved.getData(), static_cast<int>(saved.getSize()));
    check(xml != nullptr, "state is versioned XML");
    if (xml != nullptr)
    {
        xml->getChildByName("Genome")->setAttribute("bases", "ATGX");
        juce::MemoryBlock invalid;
        juce::AudioProcessor::copyXmlToBinary(*xml, invalid);
        restored.setStateInformation(invalid.getData(), static_cast<int>(invalid.getSize()));
        check(restored.currentGenome() == before, "invalid DNA cannot partially replace state");
    }
    source.setGenome({}); source.getStateInformation(saved);
    restored.setStateInformation(saved.getData(), static_cast<int>(saved.getSize()));
    check(restored.currentGenome().dna() == "ATGTGA", "an empty genome round-trips as the minimal start + stop frame");
    auto editor = std::unique_ptr<juce::AudioProcessorEditor>(source.createEditor());
    check(editor->getWidth() == 1025 && editor->getHeight() == 700, "editor opens with the restored aspect ratio");
    editor->setSize(820, 560);
    auto image = editor->createComponentSnapshot(editor->getLocalBounds());
    check(image.isValid() && image.getWidth() == 820 && image.getHeight() == 560, "both editor construction and software rendering succeed");
    for (int i = 0; i < source.getNumPrograms(); ++i)
    {
        source.setCurrentProgram(i);
        check(source.matchingPreset() == i && source.getProgramName(i).isNotEmpty(), "host programs load complete named presets");
    }
}
bool snapshot(const juce::File& file, int mode)
{
    TranscriptionAudioProcessor processor;
    if (mode == 6) processor.loadPreset(0);   // 长序列（Central Dogma）：验证 DNA 链变长后的滚动与端点标记
    if (mode == 7)   // 塑形中间态：core=3、body=1，停在核心窗口内，噪声与谐波同屏
    {
        transcription::Genome gene;
        transcription::Genome::parse("ATG GGT GCT GTT CTT TAA", gene);
        processor.setGenome(gene);
    }
    if (mode == 8 || mode == 9)   // 实时频谱：真实渲染一段音频，让 FFT 拿到稳定的谐波峰
    {
        processor.loadPreset(mode == 8 ? 0 : transcription::presetCount - 1);   // 9 = Start + Stop，纯噪声
        processor.prepareToPlay(48000.0, 512);
        juce::AudioBuffer<float> buffer(2, 512);
        juce::MidiBuffer midi;
        midi.addEvent(juce::MidiMessage::noteOn(1, 69, 0.9f), 0);
        for (int block = 0; block < 70; ++block)
        {
            buffer.clear();
            processor.processBlock(buffer, midi);
            midi.clear();
        }
    }
    auto editor = std::unique_ptr<juce::AudioProcessorEditor>(processor.createEditor());
    if (mode == 4) editor->setSize(615, 420);
    for (auto* child : editor->getChildren())
    {
        if (auto* scene = dynamic_cast<GeneCanvas*>(child))
        {
            if (mode == 1 || mode == 4) scene->seek(0.62f);
            if (mode == 3) { scene->toggleRunning(); scene->advance(100); }
            if (mode == 6) scene->seek(0.45f);
            if (mode == 7) scene->seek(0.10f);
            if (mode == 8 || mode == 9)
            {
                scene->setOutputLevel(processor.outputPeak());
                for (int i = 0; i < 24; ++i) scene->advance(1.0 / 30.0);
                if (mode == 8) scene->seek(0.45f);
                std::array<float, transcription::spectrumBandCount> bands {};
                for (int i = 0; i < transcription::spectrumBandCount; ++i)
                    bands[static_cast<size_t>(i)] = processor.spectrumBandDb(i);
                scene->setSpectrum(bands);
            }
        }
        if (auto* picker = dynamic_cast<CodonPicker*>(child))
            if (mode == 2 || mode == 5)
            {
                picker->open(1, processor.currentGenome(), CodonPicker::Mode::amino);
                if (mode == 5) picker->mouseDown(mouse(*picker, { 616, 415 }));
            }
    }
    auto image = editor->createComponentSnapshot(editor->getLocalBounds(), true, 1.5f);
    auto stream = file.createOutputStream();
    if (stream == nullptr || !stream->setPosition(0) || stream->truncate().failed()) return false;
    return juce::PNGImageFormat().writeImageToStream(image, *stream);
}
bool audioPreview(const juce::File& file)
{
    constexpr int rate = 48000, blockSize = 256, secondsPerPreset = 5;
    TranscriptionAudioProcessor processor;
    processor.prepareToPlay(rate, blockSize);
    auto fileStream = file.createOutputStream();
    if (fileStream == nullptr || !fileStream->setPosition(0) || fileStream->truncate().failed()) return false;
    std::unique_ptr<juce::OutputStream> stream = std::move(fileStream);
    juce::WavAudioFormat format;
    auto writer = format.createWriterFor(stream, juce::AudioFormatWriterOptions()
        .withSampleRate(rate).withNumChannels(2).withBitsPerSample(24));
    if (writer == nullptr) return false;
    juce::AudioBuffer<float> buffer(2, blockSize);
    for (int preset = 0; preset < transcription::presetCount; ++preset)
    {
        processor.reset(); processor.loadPreset(preset);
        int position = 0;
        while (position < secondsPerPreset * rate)
        {
            const int length = juce::jmin(blockSize, secondsPerPreset * rate - position);
            buffer.setSize(2, length, false, false, true);
            juce::MidiBuffer midi;
            if (position == 0)
            {
                midi.addEvent(juce::MidiMessage::noteOn(1, 60, 0.85f), 0);
                midi.addEvent(juce::MidiMessage::noteOn(1, 67, 0.7f), 0);
                midi.addEvent(juce::MidiMessage::noteOn(1, 72, 0.6f), 0);
            }
            const int release = rate * 3;
            if (position <= release && position + length > release)
                for (int note : { 60, 67, 72 }) midi.addEvent(juce::MidiMessage::noteOff(1, note), release - position);
            processor.processBlock(buffer, midi);
            if (!writer->writeFromAudioSampleBuffer(buffer, 0, length)) return false;
            position += length;
        }
    }
    return true;
}
}
int main(int argc, char* argv[])
{
    juce::ScopedJuceInitialiser_GUI initialise;
    if (argc == 3)
    {
        const juce::String option(argv[1]);
        const auto file = juce::File::getCurrentWorkingDirectory().getChildFile(juce::String::fromUTF8(argv[2]));
        if (!file.getParentDirectory().createDirectory()) return 1;
        if (option == "--snapshot") return snapshot(file, 0) ? 0 : 1;
        if (option == "--translation-snapshot" || option == "--process-snapshot") return snapshot(file, 1) ? 0 : 1;
        if (option == "--codons-snapshot") return snapshot(file, 2) ? 0 : 1;
        if (option == "--complete-snapshot") return snapshot(file, 3) ? 0 : 1;
        if (option == "--compact-snapshot") return snapshot(file, 4) ? 0 : 1;
        if (option == "--all-codons-snapshot") return snapshot(file, 5) ? 0 : 1;
        if (option == "--long-snapshot") return snapshot(file, 6) ? 0 : 1;
        if (option == "--carving-snapshot") return snapshot(file, 7) ? 0 : 1;
        if (option == "--spectrum-snapshot") return snapshot(file, 8) ? 0 : 1;
        if (option == "--noise-spectrum-snapshot") return snapshot(file, 9) ? 0 : 1;
        if (option == "--audio-preview") return audioPreview(file) ? 0 : 1;
        return 2;
    }
    interactionTests(); processTests(); pickerTests(); openEditorStateTests(); stateTests();
    if (failures != 0) return 1;
    std::cout << "PASS: whole-codon editing, transcription stages, polymerase dragging, simplified UI, state and MIDI\n";
    return 0;
}
