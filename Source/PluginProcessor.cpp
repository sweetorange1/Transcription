#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <cmath>

TranscriptionAudioProcessor::TranscriptionAudioProcessor()
    : AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      parameters(*this, nullptr, "TranscriptionParameters", transcription::createParameterLayout()),
      parameterRefs(parameters), genome(transcription::presetGenome(transcription::presetCount - 1)), audioGenome(genome)
{
    // 默认从最小阅读框（Start + Stop：只有起始与终止密码子）开始，
    // 用户逐个添加密码子时转录场景中的 DNA 链随之变长。
    const auto& controls = transcription::factoryPresets()[transcription::presetCount - 1].controls;
    const std::array<float, 7> values { controls.precision, controls.mutation, controls.attackScale,
                                        controls.releaseScale, controls.width, controls.outputDb,
                                        static_cast<float>(controls.noise) };
    for (size_t i = 0; i < values.size(); ++i)
        if (auto* parameter = parameters.getParameter(transcription::parameterIds[i]))
            parameter->setValueNotifyingHost(parameter->convertTo0to1(values[i]));
    lastPreset.store(transcription::presetCount - 1);
    mailbox.publish(genome);
}
void TranscriptionAudioProcessor::prepareToPlay(double sampleRate, int)
{
    engine.prepare(sampleRate);
    mailbox.read(audioGenome, audioVersion);
    engine.setTranscript(transcription::transcribe(audioGenome));
    keyboardState.reset();
    meter.store(0.0f); voiceMeter.store(0);
}
void TranscriptionAudioProcessor::releaseResources() { reset(); }
void TranscriptionAudioProcessor::reset()
{
    engine.reset();
    keyboardState.reset();
    meter.store(0.0f); voiceMeter.store(0);
}
bool TranscriptionAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    return layouts.getMainInputChannelSet().isDisabled()
        && (layouts.getMainOutputChannelSet() == juce::AudioChannelSet::mono()
            || layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo());
}
void TranscriptionAudioProcessor::handleMidi(const juce::MidiMessage& message) noexcept
{
    const int channel = message.getChannel();
    if (message.isNoteOn()) engine.noteOn(channel, message.getNoteNumber(), message.getFloatVelocity());
    else if (message.isNoteOff()) engine.noteOff(channel, message.getNoteNumber());
    else if (message.isPitchWheel()) engine.pitchWheel(channel, message.getPitchWheelValue());
    else if (message.isController()) engine.controller(channel, message.getControllerNumber(), message.getControllerValue());
}
void TranscriptionAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    buffer.clear();
    const int count = buffer.getNumSamples();
    if (mailbox.read(audioGenome, audioVersion)) engine.setTranscript(transcription::transcribe(audioGenome));
    engine.setScanPhase(scanPhase.load(std::memory_order_relaxed));
    if (buffer.getNumChannels() == 0 || count == 0)
    {
        for (const auto metadata : midi)
            if (metadata.numBytes <= 3)
            {
                const auto message = metadata.getMessage();
                keyboardState.processNextMidiEvent(message);
                handleMidi(message);
            }
        midi.clear();
        meter.store(0.0f, std::memory_order_relaxed);
        voiceMeter.store(engine.activeVoices(), std::memory_order_relaxed);
        return;
    }
    keyboardState.processNextMidiBuffer(midi, 0, count, true);
    auto* left = buffer.getWritePointer(0);
    auto* right = buffer.getNumChannels() > 1 ? buffer.getWritePointer(1) : nullptr;
    float peak = 0.0f;
    int position = 0;
    const auto renderTo = [&](int end)
    {
        if (end > position)
        {
            engine.render(left + position, right == nullptr ? nullptr : right + position, end - position);
            peak = juce::jmax(peak, engine.peak());
            position = end;
        }
    };
    for (const auto metadata : midi)
    {
        renderTo(juce::jlimit(position, count, metadata.samplePosition));
        if (metadata.numBytes <= 3) handleMidi(metadata.getMessage());
    }
    renderTo(count);
    meter.store(peak, std::memory_order_relaxed);
    voiceMeter.store(engine.activeVoices(), std::memory_order_relaxed);
    midi.clear();
}
void TranscriptionAudioProcessor::processBlockBypassed(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    buffer.clear(); midi.clear(); reset();
}
juce::AudioProcessorEditor* TranscriptionAudioProcessor::createEditor()
{
    return new TranscriptionAudioProcessorEditor(*this);
}
transcription::Genome TranscriptionAudioProcessor::currentGenome() const
{
    const juce::ScopedLock lock(modelMutex);
    return genome;
}
bool TranscriptionAudioProcessor::setGenome(const transcription::Genome& next)
{
    if (next.size < 0 || next.size > transcription::maxBases) return false;
    transcription::Genome validated;
    for (int i = 0; i < next.size; ++i)
    {
        if (!validated.append(next.bases[static_cast<size_t>(i)])) return false;
        validated.silent[static_cast<size_t>(i)] = next.silent[static_cast<size_t>(i)];
    }
    transcription::enforceFrame(validated);
    const juce::ScopedLock lock(modelMutex);
    if (genome == validated) return true;
    genome = validated;
    mailbox.publish(genome);
    modelRevision.fetch_add(1);
    updateHostDisplay(juce::AudioProcessorListener::ChangeDetails().withNonParameterStateChanged(true));
    return true;
}
void TranscriptionAudioProcessor::setParameter(const juce::String& id, float value)
{
    if (!std::isfinite(value)) return;
    if (auto* parameter = parameters.getParameter(id))
    {
        parameter->beginChangeGesture();
        parameter->setValueNotifyingHost(parameter->convertTo0to1(value));
        parameter->endChangeGesture();
    }
}
void TranscriptionAudioProcessor::loadPreset(int index)
{
    if (index < 0 || index >= transcription::presetCount) return;
    const auto& c = transcription::factoryPresets()[static_cast<size_t>(index)].controls;
    const std::array<float, 7> values { c.precision, c.mutation, c.attackScale, c.releaseScale, c.width, c.outputDb, static_cast<float>(c.noise) };
    for (size_t i = 0; i < values.size(); ++i) setParameter(transcription::parameterIds[i], values[i]);
    setGenome(transcription::presetGenome(index));
    lastPreset.store(index);
}
int TranscriptionAudioProcessor::matchingPreset() const
{
    const auto current = currentGenome();
    const auto controls = currentControls();
    for (int i = 0; i < transcription::presetCount; ++i)
        if (current == transcription::presetGenome(i)
            && transcription::sameControls(controls, transcription::factoryPresets()[static_cast<size_t>(i)].controls)) return i;
    return -1;
}
void TranscriptionAudioProcessor::setCurrentProgram(int index) { loadPreset(index); }
const juce::String TranscriptionAudioProcessor::getProgramName(int index)
{
    return index >= 0 && index < transcription::presetCount ? transcription::factoryPresets()[static_cast<size_t>(index)].name : juce::String();
}
void TranscriptionAudioProcessor::getStateInformation(juce::MemoryBlock& destination)
{
    const auto snapshot = currentGenome();
    juce::XmlElement root("TranscriptionState");
    root.setAttribute("version", 1);
    root.setAttribute("editorWidth", editorWidth.load());
    root.setAttribute("editorTab", editorTab.load());
    root.setAttribute("program", lastPreset.load());
    auto* dna = root.createNewChildElement("Genome");
    dna->setAttribute("bases", juce::String(snapshot.dna()));
    juce::String mask;
    for (int i = 0; i < snapshot.size; ++i) mask += snapshot.silent[static_cast<size_t>(i)] ? "1" : "0";
    dna->setAttribute("introns", mask);
    root.addChildElement(parameters.copyState().createXml().release());
    copyXmlToBinary(root, destination);
}
void TranscriptionAudioProcessor::setStateInformation(const void* data, int bytes)
{
    if (data == nullptr || bytes <= 0 || bytes > 1024 * 1024) return;
    const auto root = getXmlFromBinary(data, bytes);
    if (root == nullptr || !root->hasTagName("TranscriptionState") || root->getIntAttribute("version") != 1) return;
    const auto* dna = root->getChildByName("Genome");
    const auto* parameterXml = root->getChildByName("TranscriptionParameters");
    if (dna == nullptr || parameterXml == nullptr || !dna->hasAttribute("bases") || !dna->hasAttribute("introns")) return;
    transcription::Genome restored;
    const auto text = dna->getStringAttribute("bases");
    if (text.length() > transcription::maxBases || !transcription::Genome::parse(text.toStdString(), restored)) return;
    const auto mask = dna->getStringAttribute("introns");
    if (mask.length() != restored.size || mask.containsAnyOf("23456789") || !mask.containsOnly("01")) return;
    for (int i = 0; i < restored.size; ++i) restored.silent[static_cast<size_t>(i)] = mask[i] == '1';
    auto tree = juce::ValueTree::fromXml(*parameterXml);
    if (!tree.isValid()) return;
    for (const auto* id : transcription::parameterIds)
    {
        const auto entry = tree.getChildWithProperty("id", id);
        if (!entry.isValid() || !entry.hasProperty("value")) return;
        const float value = static_cast<float>(entry.getProperty("value"));
        const auto range = parameters.getParameter(id)->getNormalisableRange();
        if (!std::isfinite(value) || value < range.start || value > range.end) return;
    }
    parameters.replaceState(tree);
    setGenome(restored);
    lastPreset.store(juce::jlimit(0, transcription::presetCount - 1, root->getIntAttribute("program", 0)));
    editorWidth.store(juce::jlimit(615, 1640, root->getIntAttribute("editorWidth", 820)));
    editorTab.store(juce::jlimit(0, 1, root->getIntAttribute("editorTab", 0)));
    modelRevision.fetch_add(1);
}
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new TranscriptionAudioProcessor(); }
