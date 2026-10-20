#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <cmath>

TranscriptionAudioProcessor::TranscriptionAudioProcessor()
    : AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      parameters(*this, nullptr, "TranscriptionParameters", transcription::createParameterLayout()),
      genome(transcription::presetGenome(transcription::defaultPresetIndex)), audioGenome(genome)
{
    // 默认载入 Insulin / INS：一条真实 CDS 片段，核心区/中间区都完整，
    // 新用户第一次播放转录就能听到完整的三段式音色变化（而不是只有噪声的 Start + Stop）。
    lastPreset.store(transcription::defaultPresetIndex);
    scanParameter = parameters.getParameter(scanParameterId);
    mailbox.publish(genome);
}
void TranscriptionAudioProcessor::prepareToPlay(double sampleRate, int)
{
    engine.prepare(sampleRate);
    mailbox.read(audioGenome, audioVersion);
    engine.setTranscript(transcription::transcribe(audioGenome));
    keyboardState.reset();
    meter.store(0.0f); voiceMeter.store(0);

    // 频谱分析缓冲（只在 prepare 中初始化，音频线程不再分配）。
    for (int i = 0; i < spectrumFftSize; ++i)
        spectrumWindow[static_cast<size_t>(i)] = 0.5f - 0.5f * std::cos(juce::MathConstants<float>::twoPi
            * static_cast<float>(i) / static_cast<float>(spectrumFftSize - 1));
    spectrumRing.fill(0.0f);
    spectrumBuffer.fill(0.0f);
    spectrumSmoothed.fill(transcription::spectrumMinDb);
    for (auto& band : spectrumBands) band.store(transcription::spectrumMinDb, std::memory_order_relaxed);
    spectrumWrite = 0; spectrumHop = 0;
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
    // 转录进度完全以 scan 参数为准：宿主自动化、工程恢复、界面动画都写到同一个参数。
    const float progress = scanParameter != nullptr ? scanParameter->getValue() : 1.0f;
    scanPhase.store(progress, std::memory_order_relaxed);
    engine.setScanPhase(progress);
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
    analyseSpectrum(left, right, count);
    meter.store(peak, std::memory_order_relaxed);
    voiceMeter.store(engine.activeVoices(), std::memory_order_relaxed);
    midi.clear();
}
void TranscriptionAudioProcessor::analyseSpectrum(const float* left, const float* right, int samples) noexcept
{
    if (left == nullptr || samples <= 0) return;
    const double rate = getSampleRate() > 0.0 ? getSampleRate() : 44100.0;
    const float binHz = static_cast<float>(rate) / static_cast<float>(spectrumFftSize);
    // Hann 窗的相干增益为 0.5，且实信号 DFT 峰值约为 N/4，这里乘 4/N 让满幅正弦回到 0 dB。
    const float scale = 4.0f / static_cast<float>(spectrumFftSize);

    for (int i = 0; i < samples; ++i)
    {
        spectrumRing[static_cast<size_t>(spectrumWrite)] = right != nullptr
            ? 0.5f * (left[i] + right[i]) : left[i];
        spectrumWrite = (spectrumWrite + 1) % spectrumFftSize;
        if (++spectrumHop < spectrumHopSamples) continue;
        spectrumHop = 0;

        for (int j = 0; j < spectrumFftSize; ++j)
            spectrumBuffer[static_cast<size_t>(j)] =
                spectrumRing[static_cast<size_t>((spectrumWrite + j) % spectrumFftSize)]
                * spectrumWindow[static_cast<size_t>(j)];
        std::fill(spectrumBuffer.begin() + spectrumFftSize, spectrumBuffer.end(), 0.0f);
        spectrumFft.performFrequencyOnlyForwardTransform(spectrumBuffer.data());

        // 对数频段 40 Hz – 16 kHz，按带宽求平均功率（PSD），使白噪声读作平坦地板。
        constexpr float minimumHz = 40.0f, maximumHz = 16000.0f;
        for (int b = 0; b < transcription::spectrumBandCount; ++b)
        {
            const float t0 = static_cast<float>(b) / static_cast<float>(transcription::spectrumBandCount);
            const float t1 = static_cast<float>(b + 1) / static_cast<float>(transcription::spectrumBandCount);
            const float f0 = minimumHz * std::pow(maximumHz / minimumHz, t0);
            const float f1 = minimumHz * std::pow(maximumHz / minimumHz, t1);
            const int start = juce::jmax(1, static_cast<int>(std::floor(f0 / binHz)));
            const int end = juce::jmin(spectrumFftSize / 2,
                juce::jmax(start + 1, static_cast<int>(std::ceil(f1 / binHz))));
            double power = 0.0;
            for (int k = start; k < end; ++k)
            {
                const double magnitude = static_cast<double>(spectrumBuffer[static_cast<size_t>(k)]) * scale;
                power += magnitude * magnitude;
            }
            power /= static_cast<double>(end - start);
            const float db = static_cast<float>(10.0 * std::log10(power + 1.0e-10));
            float& smooth = spectrumSmoothed[static_cast<size_t>(b)];
            smooth += (db - smooth) * (db > smooth ? 0.55f : 0.12f);
            spectrumBands[static_cast<size_t>(b)].store(
                juce::jlimit(transcription::spectrumMinDb, transcription::spectrumMaxDb, smooth),
                std::memory_order_relaxed);
        }
    }
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
    setGenome(transcription::presetGenome(index));
    lastPreset.store(index);
}
int TranscriptionAudioProcessor::matchingPreset() const
{
    const auto current = currentGenome();
    for (int i = 0; i < transcription::presetCount; ++i)
        if (current == transcription::presetGenome(i)) return i;
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
    const auto scanEntry = tree.getChildWithProperty("id", scanParameterId);
    const bool hadScan = scanEntry.isValid();
    if (hadScan)
    {
        const float value = static_cast<float>(scanEntry.getProperty("value"));
        if (!std::isfinite(value) || value < 0.0f || value > 1.0f) return;
    }
    parameters.replaceState(tree);   // 注意：replaceState 会把缺失的参数补进 tree，所以先记下 hadScan
    // 旧工程没有 scan 参数：回到待机，而不是沿用上一个工程的位置。
    if (!hadScan && scanParameter != nullptr)
        scanParameter->setValueNotifyingHost(0.0f);
    stateRestored.store(true);
    setGenome(restored);
    lastPreset.store(juce::jlimit(0, transcription::presetCount - 1, root->getIntAttribute("program", 0)));
    editorWidth.store(juce::jlimit(615, 1640, root->getIntAttribute("editorWidth", 820)));
    editorTab.store(juce::jlimit(0, 1, root->getIntAttribute("editorTab", 0)));
    modelRevision.fetch_add(1);
}
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new TranscriptionAudioProcessor(); }
