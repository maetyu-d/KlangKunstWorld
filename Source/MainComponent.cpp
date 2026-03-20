#include "MainComponent.h"

#include <algorithm>
#include <array>
#include <limits>
#include <optional>

namespace
{
constexpr float baseTileWidth = 12.0f;
constexpr float baseTileHeight = 6.0f;
constexpr float baseVerticalStep = baseTileHeight;
constexpr int boardInset = 26;
constexpr int islandGapSize = 20;
constexpr int floorBandHeight = 12;
constexpr int floorBandGap = 12;
const std::array<juce::Point<int>, 4> snakeDirections {{
    { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 }
}};
const std::array<juce::Colour, 8> snakeColours {{
    juce::Colour::fromRGB(255, 92, 92),
    juce::Colour::fromRGB(255, 178, 66),
    juce::Colour::fromRGB(246, 227, 81),
    juce::Colour::fromRGB(94, 210, 116),
    juce::Colour::fromRGB(78, 221, 220),
    juce::Colour::fromRGB(115, 139, 245),
    juce::Colour::fromRGB(181, 98, 237),
    juce::Colour::fromRGB(242, 96, 198)
}};

juce::String scaleToString(MainComponent::ScaleType scale)
{
    switch (scale)
    {
        case MainComponent::ScaleType::chromatic: return "Chromatic";
        case MainComponent::ScaleType::major: return "Major";
        case MainComponent::ScaleType::minor: return "Minor";
        case MainComponent::ScaleType::dorian: return "Dorian";
        case MainComponent::ScaleType::pentatonic: return "Pentatonic";
    }

    return "Minor";
}

juce::String synthToString(MainComponent::SynthEngine synth)
{
    switch (synth)
    {
        case MainComponent::SynthEngine::digitalV4: return "Nova Drift";
        case MainComponent::SynthEngine::fmGlass: return "Prism FM";
        case MainComponent::SynthEngine::velvetNoise: return "Mallet Bloom";
        case MainComponent::SynthEngine::chipPulse: return "Arcade Pulse";
        case MainComponent::SynthEngine::guitarPluck: return "Guitar Pluck";
    }

    return "Nova Drift";
}

juce::String drumModeToString(MainComponent::DrumMode mode)
{
    switch (mode)
    {
        case MainComponent::DrumMode::reactiveBreakbeat: return "Reactive 909";
        case MainComponent::DrumMode::rezStraight: return "909 Rez Straight";
        case MainComponent::DrumMode::tightPulse: return "909 Tight Pulse";
        case MainComponent::DrumMode::forwardStep: return "909 Forward Step";
        case MainComponent::DrumMode::railLine: return "909 Rail Line";
    }

    return "Reactive 909";
}
}

bool MainComponent::WaveVoice::canPlaySound(juce::SynthesiserSound* s)
{
    return dynamic_cast<WaveSound*>(s) != nullptr;
}

void MainComponent::WaveVoice::startNote(int midiNoteNumber, float velocity, juce::SynthesiserSound*, int)
{
    const auto sampleRate = getSampleRate();
    level = velocity;
    noteAgeSeconds = 0.0f;
    noiseSeed = static_cast<uint32_t>(0x9E3779B9u ^ (static_cast<uint32_t>(midiNoteNumber) * 2654435761u));
    sampleHoldValue = 0.0f;
    sampleHoldCounter = 0;
    sampleHoldPeriod = 1;
    lpState = 0.0f;
    hpState = 0.0f;
    percussionMode = midiNoteNumber >= 120;
    percussionType = juce::jlimit(0, 3, midiNoteNumber - 120);
    noiseLP = 0.0f;
    noiseHP = 0.0f;
    lastNoise = 0.0f;
    chipSfxType = percussionMode ? 0 : (midiNoteNumber % 4);

    const auto cyclesPerSecond = juce::MidiMessage::getMidiNoteInHertz(midiNoteNumber);
    const auto cyclesPerSample = cyclesPerSecond / juce::jmax(1.0, sampleRate);
    angleDelta = cyclesPerSample * juce::MathConstants<double>::twoPi;
    currentAngle = 0.0;
    modAngle = 0.0;
    subAngle = 0.0;

    if (engine == SynthEngine::fmGlass)
        modDelta = cyclesPerSample * 2.0 * juce::MathConstants<double>::twoPi;
    else if (engine == SynthEngine::digitalV4)
        modDelta = cyclesPerSample * 0.613 * juce::MathConstants<double>::twoPi;
    else if (engine == SynthEngine::chipPulse)
        modDelta = cyclesPerSample * 4.0 * juce::MathConstants<double>::twoPi;
    else if (engine == SynthEngine::guitarPluck)
        modDelta = cyclesPerSample * 2.01 * juce::MathConstants<double>::twoPi;
    else
        modDelta = cyclesPerSample * 1.997 * juce::MathConstants<double>::twoPi;

    subDelta = cyclesPerSample * 0.5 * juce::MathConstants<double>::twoPi;

    if (percussionMode)
    {
        double drumHz = 120.0;
        if (percussionType == 0) drumHz = 52.0;
        else if (percussionType == 1) drumHz = 182.0;
        else if (percussionType == 2) drumHz = 420.0;
        else drumHz = 260.0;

        const double drumCyclesPerSample = drumHz / juce::jmax(1.0, sampleRate);
        angleDelta = drumCyclesPerSample * juce::MathConstants<double>::twoPi;
        modDelta = drumCyclesPerSample * 2.1 * juce::MathConstants<double>::twoPi;
        subDelta = drumCyclesPerSample * 0.5 * juce::MathConstants<double>::twoPi;
    }

    ksDelay.clear();
    ksIndex = 0;
    ksLast = 0.0f;
    if (engine == SynthEngine::guitarPluck)
    {
        const double hz = juce::jmax(30.0, cyclesPerSecond);
        const int ksLen = juce::jlimit(16, 4096, static_cast<int>(std::round(sampleRate / hz)));
        ksDelay.resize(static_cast<size_t>(ksLen), 0.0f);
        for (int i = 0; i < ksLen; ++i)
        {
            noiseSeed = noiseSeed * 1664525u + 1013904223u;
            const float n = static_cast<float>((noiseSeed >> 9) & 0x7FFFFFu) / 4194303.5f * 2.0f - 1.0f;
            ksDelay[static_cast<size_t>(i)] = n * (0.70f * velocity);
        }
    }

    if (percussionMode)
    {
        adsrParams.attack = 0.0005f;
        adsrParams.decay = percussionType == 2 ? 0.035f : percussionType == 0 ? 0.14f : 0.09f;
        adsrParams.sustain = 0.0f;
        adsrParams.release = percussionType == 2 ? 0.01f : 0.03f;
    }
    else if (engine == SynthEngine::digitalV4)
    {
        adsrParams.attack = 0.006f;
        adsrParams.decay = 0.24f;
        adsrParams.sustain = 0.38f;
        adsrParams.release = 0.30f;
    }
    else if (engine == SynthEngine::velvetNoise)
    {
        adsrParams.attack = 0.0004f;
        adsrParams.decay = 0.13f;
        adsrParams.sustain = 0.0f;
        adsrParams.release = 0.025f;
    }
    else if (engine == SynthEngine::chipPulse)
    {
        adsrParams.attack = 0.0001f;
        adsrParams.decay = 0.075f;
        adsrParams.sustain = 0.10f;
        adsrParams.release = 0.045f;
    }
    else if (engine == SynthEngine::guitarPluck)
    {
        adsrParams.attack = 0.0004f;
        adsrParams.decay = 0.17f;
        adsrParams.sustain = 0.12f;
        adsrParams.release = 0.14f;
    }
    else
    {
        adsrParams.attack = 0.003f;
        adsrParams.decay = 0.18f;
        adsrParams.sustain = 0.20f;
        adsrParams.release = 0.10f;
    }

    adsr.setParameters(adsrParams);
    adsr.noteOn();
}

void MainComponent::WaveVoice::stopNote(float, bool allowTailOff)
{
    if (allowTailOff)
        adsr.noteOff();
    else
        clearCurrentNote();
}

void MainComponent::WaveVoice::renderNextBlock(juce::AudioBuffer<float>& outputBuffer, int startSample, int numSamples)
{
    if (! isVoiceActive())
        return;

    const auto sr = static_cast<float>(juce::jmax(1.0, getSampleRate()));
    for (int s = 0; s < numSamples; ++s)
    {
        const auto env = adsr.getNextSample();
        const auto transient = std::exp(-noteAgeSeconds * 24.0f);

        noiseSeed = noiseSeed * 1664525u + 1013904223u;
        const auto white = static_cast<float>((noiseSeed >> 9) & 0x7FFFFFu) / 4194303.5f * 2.0f - 1.0f;

        if (percussionMode)
        {
            float voicedPerc = 0.0f;
            switch (percussionType)
            {
                case 0:
                {
                    const float pitchDrop = 1.0f + 4.4f * std::exp(-noteAgeSeconds * 48.0f);
                    const float body = static_cast<float>(std::sin(currentAngle * pitchDrop));
                    const float bodyEnv = std::exp(-noteAgeSeconds * 11.5f);
                    const float clickEnv = std::exp(-noteAgeSeconds * 165.0f);
                    const float clickTone = static_cast<float>(std::sin(currentAngle * 10.0));
                    voicedPerc = 0.95f * body * bodyEnv + 0.19f * clickTone * clickEnv + 0.08f * white * clickEnv;
                    break;
                }
                case 1:
                {
                    noiseLP += 0.15f * (white - noiseLP);
                    noiseHP = white - noiseLP;
                    const float snapEnv = std::exp(-noteAgeSeconds * 47.0f);
                    const float toneEnv = std::exp(-noteAgeSeconds * 20.0f);
                    const float tone1 = static_cast<float>(std::sin(currentAngle * 1.86));
                    const float tone2 = static_cast<float>(std::sin(currentAngle * 2.71));
                    const float preSnap = white - lastNoise;
                    voicedPerc = 0.42f * tone1 * toneEnv
                               + 0.18f * tone2 * toneEnv
                               + (0.62f * noiseHP + 0.18f * preSnap) * snapEnv;
                    break;
                }
                case 2:
                {
                    noiseLP += 0.24f * (white - noiseLP);
                    noiseHP = white - noiseLP;
                    const float hat = std::exp(-noteAgeSeconds * 92.0f);
                    const auto metallic = std::copysign(1.0f, white * 0.78f + noiseHP * 0.22f);
                    voicedPerc = (0.56f * metallic + 0.30f * noiseHP) * hat;
                    break;
                }
                case 3:
                default:
                {
                    const auto diff = white - lastNoise;
                    lastNoise = white;
                    const auto clickEnv = static_cast<float>(std::exp(-noteAgeSeconds * 96.0f));
                    const auto gate = (sampleHoldCounter <= 0 ? 1.0f : 0.0f);
                    if (sampleHoldCounter <= 0)
                        sampleHoldCounter = 2 + static_cast<int>(noiseSeed & 3u);
                    --sampleHoldCounter;
                    voicedPerc = (0.74f * diff + 0.16f * white) * clickEnv * gate * 1.6f;
                    break;
                }
            }

            voicedPerc = std::tanh(voicedPerc * 1.75f) * 0.69f;
            const float sample = voicedPerc * level * env * 0.80f;
            for (int c = 0; c < outputBuffer.getNumChannels(); ++c)
                outputBuffer.addSample(c, startSample + s, sample);

            currentAngle += angleDelta;
            modAngle += modDelta;
            subAngle += subDelta;
            if (currentAngle >= juce::MathConstants<double>::twoPi) currentAngle -= juce::MathConstants<double>::twoPi;
            if (modAngle >= juce::MathConstants<double>::twoPi) modAngle -= juce::MathConstants<double>::twoPi;
            if (subAngle >= juce::MathConstants<double>::twoPi) subAngle -= juce::MathConstants<double>::twoPi;
            noteAgeSeconds += 1.0f / sr;
            continue;
        }
        const auto sub = static_cast<float>(std::sin(subAngle));
        const auto click = white * transient * 0.18f;

        float voiced = 0.0f;
        if (engine == SynthEngine::digitalV4)
        {
            const auto vibrato = 0.009 * std::sin(modAngle * 0.14);
            const auto oscA = std::sin(currentAngle + vibrato);
            const auto oscB = std::sin(currentAngle * 2.0 + 0.12 * std::sin(modAngle * 0.10));
            const auto oscC = std::sin(currentAngle * 3.0 + 0.06 * std::sin(modAngle * 0.06));
            const auto subWarm = std::sin(subAngle + 0.05 * std::sin(modAngle * 0.05));
            const auto raw = static_cast<float>(0.67 * oscA + 0.14 * oscB + 0.06 * oscC + 0.16 * subWarm + click * 0.002f);

            const auto cutoff = 180.0f + 1020.0f * env + 130.0f * static_cast<float>(0.5 + 0.5 * std::sin(modAngle * 0.03));
            const auto alpha = std::exp(-juce::MathConstants<float>::twoPi * cutoff / sr);
            lpState = alpha * lpState + (1.0f - alpha) * raw;
            const auto hp = raw - lpState;
            hpState = 0.996f * hpState + 0.004f * hp;

            voiced = static_cast<float>(std::tanh((0.97f * lpState + 0.005f * hpState + 0.06f * subWarm) * 0.90f) * 0.66f);
        }
        else if (engine == SynthEngine::fmGlass)
        {
            const float idxMain = 0.72f * std::exp(-noteAgeSeconds * 3.6f) + 0.13f;
            const float idxAir = 0.21f * std::exp(-noteAgeSeconds * 6.8f);
            const auto slowDrift = 0.025f * std::sin(modAngle * 0.11);
            const auto mod1 = std::sin(modAngle + slowDrift);
            const auto mod2 = std::sin(modAngle * 0.75 + 0.6 * std::sin(modAngle * 0.09));

            const auto carrier = std::sin(currentAngle + idxMain * mod1 + idxAir * mod2);
            const auto harmonic2 = std::sin(currentAngle * 2.0 + idxMain * 0.22f * mod1);
            const auto harmonic3 = std::sin(currentAngle * 3.0 + idxMain * 0.10f * mod2);
            const auto glass = std::sin(currentAngle * 4.0 + modAngle * 0.07);
            const auto raw = static_cast<float>(0.81 * carrier
                                              + 0.11 * harmonic2
                                              + 0.05 * harmonic3
                                              + 0.03 * glass
                                              + click * 0.012f);

            const auto cutoff = 520.0f + 2350.0f * env + 240.0f * static_cast<float>(0.5 + 0.5 * std::sin(modAngle * 0.02));
            const auto alpha = std::exp(-juce::MathConstants<float>::twoPi * cutoff / sr);
            lpState = alpha * lpState + (1.0f - alpha) * raw;
            const auto hp = raw - lpState;
            hpState = 0.993f * hpState + 0.007f * hp;
            voiced = static_cast<float>(std::tanh((0.95f * lpState + 0.04f * hpState) * 0.96f) * 0.67f);
        }
        else if (engine == SynthEngine::chipPulse)
        {
            const float phaseA = static_cast<float>(currentAngle / juce::MathConstants<double>::twoPi);
            const float phaseB = static_cast<float>((currentAngle * 2.0) / juce::MathConstants<double>::twoPi);
            const float pA = phaseA - std::floor(phaseA);
            const float pB = phaseB - std::floor(phaseB);
            static constexpr float dutySet[4] = { 0.125f, 0.25f, 0.5f, 0.75f };

            float raw = 0.0f;
            switch (chipSfxType)
            {
                case 0:
                {
                    const float chirpUp = 1.0f + 0.55f * (1.0f - std::exp(-noteAgeSeconds * 120.0f));
                    const float coinPhase = static_cast<float>((currentAngle * chirpUp) / juce::MathConstants<double>::twoPi);
                    const float pc = coinPhase - std::floor(coinPhase);
                    const float pulse = (pc < 0.125f) ? 1.0f : -1.0f;
                    raw = 0.88f * pulse + 0.12f * click;
                    break;
                }
                case 1:
                {
                    const float chirpDown = 1.0f + 0.42f * std::exp(-noteAgeSeconds * 20.0f);
                    const float jumpPhase = static_cast<float>((currentAngle * chirpDown) / juce::MathConstants<double>::twoPi);
                    const float pj = jumpPhase - std::floor(jumpPhase);
                    const float pulse = (pj < 0.25f) ? 1.0f : -1.0f;
                    const float body = 2.0f * std::abs(2.0f * pj - 1.0f) - 1.0f;
                    raw = 0.72f * pulse + 0.20f * body + 0.08f * click;
                    break;
                }
                case 2:
                {
                    const float sweep = 1.0f + 1.05f * std::exp(-noteAgeSeconds * 16.0f);
                    const float laserPhase = static_cast<float>((currentAngle * sweep) / juce::MathConstants<double>::twoPi);
                    const float pl = laserPhase - std::floor(laserPhase);
                    const float pulse = (pl < dutySet[static_cast<size_t>((noiseSeed >> 7) & 3u)]) ? 1.0f : -1.0f;
                    const float ring = static_cast<float>(std::sin(modAngle * 0.42));
                    raw = 0.76f * pulse + 0.14f * ring + 0.14f * white * std::exp(-noteAgeSeconds * 45.0f);
                    break;
                }
                case 3:
                default:
                {
                    const float pulseA = (pA < 0.5f) ? 1.0f : -1.0f;
                    const float pulseB = (pB < 0.125f) ? 1.0f : -1.0f;
                    raw = 0.70f * pulseA + 0.22f * pulseB + 0.10f * click + 0.07f * white * std::exp(-noteAgeSeconds * 95.0f);
                    break;
                }
            }

            const float stepped = std::round(raw * 7.0f) * (1.0f / 7.0f);
            hpState = 0.975f * hpState + 0.025f * (stepped - lpState);
            lpState = stepped;
            voiced = static_cast<float>(std::tanh((0.84f * stepped + 0.18f * hpState) * 0.96f) * 0.72f);
        }
        else if (engine == SynthEngine::guitarPluck)
        {
            if (ksDelay.empty())
            {
                voiced = 0.0f;
            }
            else
            {
                const int n = static_cast<int>(ksDelay.size());
                const int nextIdx = (ksIndex + 1) % n;
                const float y0 = ksDelay[static_cast<size_t>(ksIndex)];
                const float y1 = ksDelay[static_cast<size_t>(nextIdx)];
                const float avg = 0.5f * (y0 + y1);
                const float damping = 0.9925f - 0.012f * static_cast<float>(0.5 + 0.5 * std::sin(modAngle * 0.04));
                const float pickBurst = (white - noiseLP) * std::exp(-noteAgeSeconds * 62.0f) * 0.040f;
                noiseLP += 0.25f * (white - noiseLP);
                const float write = avg * damping + pickBurst;

                ksDelay[static_cast<size_t>(ksIndex)] = write;
                ksIndex = nextIdx;

                const float body = 0.82f * y0 + 0.12f * sub + 0.08f * click;
                lpState += 0.14f * (body - lpState);
                hpState += 0.010f * ((body - lpState) - hpState);
                voiced = static_cast<float>(std::tanh((0.92f * lpState + 0.05f * hpState + 0.08f * ksLast) * 1.12f) * 0.72f);
                ksLast = y0;
            }
        }
        else
        {
            const float toneEnv = std::exp(-noteAgeSeconds * 9.5f);
            const float metalEnv = std::exp(-noteAgeSeconds * 17.0f);
            const float strikeEnv = std::exp(-noteAgeSeconds * 95.0f);

            noiseLP += 0.36f * (white - noiseLP);
            const float strike = (0.72f * white + 0.28f * (white - noiseLP)) * strikeEnv * 0.20f;

            const auto fundamental = std::sin(currentAngle);
            const auto r2 = std::sin(currentAngle * 3.99 + 0.12 * std::sin(modAngle * 0.3));
            const auto r3 = std::sin(currentAngle * 6.83 + 0.08 * std::sin(modAngle * 0.43));
            const auto r4 = std::sin(currentAngle * 9.77 + 0.05 * std::sin(modAngle * 0.57));

            const auto body = static_cast<float>(0.74 * fundamental * toneEnv
                                               + 0.17 * r2 * metalEnv
                                               + 0.07 * r3 * metalEnv
                                               + 0.04 * r4 * metalEnv
                                               + 0.06 * sub * toneEnv);

            lpState += 0.18f * ((body + strike) - lpState);
            voiced = std::tanh((0.82f * lpState + 0.18f * body + strike) * 1.34f) * 0.72f;
        }

        const float sample = voiced * level * env;
        for (int c = 0; c < outputBuffer.getNumChannels(); ++c)
            outputBuffer.addSample(c, startSample + s, sample);

        currentAngle += angleDelta;
        modAngle += modDelta;
        subAngle += subDelta;
        if (currentAngle >= juce::MathConstants<double>::twoPi) currentAngle -= juce::MathConstants<double>::twoPi;
        if (modAngle >= juce::MathConstants<double>::twoPi) modAngle -= juce::MathConstants<double>::twoPi;
        if (subAngle >= juce::MathConstants<double>::twoPi) subAngle -= juce::MathConstants<double>::twoPi;
        noteAgeSeconds += 1.0f / sr;
    }

    if (! adsr.isActive())
        clearCurrentNote();
}

MainComponent::MainComponent()
{
    voxels.resize(static_cast<size_t>(gridWidth * gridDepth * gridHeight), 0u);
    camera.zoom = 2.2f;
    targetZoom = camera.zoom;
    camera.heightScale = 1.0f;
    camera.panX = 0.0f;
    camera.panY = 0.0f;
    for (int i = 0; i < 10; ++i)
        synth.addVoice(new WaveVoice(synthEngine));
    synth.addSound(new WaveSound());
    for (int i = 0; i < 6; ++i)
        beatSynth.addVoice(new WaveVoice(synthEngine));
    beatSynth.addSound(new WaveSound());
    randomiseVoxels();
    setOpaque(true);
    setSize(1500, 980);
    setWantsKeyboardFocus(true);
    addKeyListener(this);
    setAudioChannels(0, 2);
    startTimerHz(60);
}

MainComponent::~MainComponent()
{
    shutdownAudio();
}

void MainComponent::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();
    auto hudArea = bounds.removeFromTop(78.0f);
    auto gridArea = bounds.reduced(12.0f);

    drawBackdrop(g, bounds);
    drawWireframeGrid(g, gridArea);
    drawHud(g, hudArea);
}

void MainComponent::resized()
{
}

void MainComponent::mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails& wheel)
{
    if (isolatedSlab.isValid() && performanceMode)
        return;

    const float delta = wheel.deltaY != 0.0f ? wheel.deltaY : wheel.deltaX;
    if (delta == 0.0f)
        return;

    const float sensitivity = wheel.isSmooth ? 0.30f : 0.48f;
    const float factor = std::exp(delta * sensitivity);
    targetZoom = juce::jlimit(0.2f, 10.0f, targetZoom * factor);
    repaint();
}

void MainComponent::mouseMove(const juce::MouseEvent& event)
{
    if (isolatedSlab.isValid())
    {
        if (performanceMode)
        {
            auto bounds = getLocalBounds().toFloat();
            bounds.removeFromTop(78.0f);
            const auto nextCell = performanceCellAtPosition(event.position, bounds.reduced(12.0f));
            if (nextCell != performanceHoverCell)
            {
                performanceHoverCell = nextCell;
                repaint();
            }
            return;
        }

        auto bounds = getLocalBounds().toFloat();
        bounds.removeFromTop(78.0f);
        if (updateCursorFromPosition(event.position, bounds.reduced(12.0f)))
            repaint();
        return;
    }

    if (layoutMode != LayoutMode::FourIslandsFourFloors)
    {
        if (hoveredSlab.isValid())
        {
            hoveredSlab = {};
            repaint();
        }
        return;
    }

    auto bounds = getLocalBounds().toFloat();
    bounds.removeFromTop(78.0f);
    const auto slab = slabAtPosition(event.position, bounds.reduced(12.0f));
    if (slab.quadrant != hoveredSlab.quadrant || slab.floor != hoveredSlab.floor)
    {
        hoveredSlab = slab;
        repaint();
    }
}

void MainComponent::mouseExit(const juce::MouseEvent&)
{
    if (performanceHoverCell.has_value())
    {
        performanceHoverCell.reset();
        repaint();
    }

    if (hoveredSlab.isValid())
    {
        hoveredSlab = {};
        repaint();
    }
}

void MainComponent::mouseUp(const juce::MouseEvent& event)
{
    if (layoutMode != LayoutMode::FourIslandsFourFloors)
        return;

    auto bounds = getLocalBounds().toFloat();
    bounds.removeFromTop(78.0f);
    const auto gridArea = bounds.reduced(12.0f);
    const auto slab = slabAtPosition(event.position, gridArea);

    if (isolatedSlab.isValid())
    {
        if (performanceMode)
        {
            performanceHoverCell = performanceCellAtPosition(event.position, gridArea);
            repaint();
            return;
        }

        if (editCursor.active && cellInSelectedSlab(editCursor.x, editCursor.y, isolatedSlab))
        {
            const bool remove = event.mods.isRightButtonDown() || event.mods.isCtrlDown();
            setVoxel(editCursor.x, editCursor.y, editCursor.z, ! remove);
            repaint();
            return;
        }

        if (updateCursorFromPosition(event.position, gridArea))
        {
            repaint();
            return;
        }

        isolatedSlab = {};
        hoveredSlab = {};
        editCursor = {};
        repaint();
        return;
    }

    if (slab.isValid())
    {
        isolatedSlab = slab;
        hoveredSlab = slab;
        resetEditCursor();
        performanceMode = false;
        performanceRegionMode = 2;
        performanceSnakes.clear();
        performanceDiscs.clear();
        performanceHoverCell.reset();
        performanceSelectedDirection = { 1, 0 };
        performanceTick = 0;
        performanceBeatEnergy = 0.0f;
        repaint();
    }
}

void MainComponent::prepareToPlay(int, double sampleRate)
{
    const juce::ScopedLock sl(synthLock);
    currentSampleRate = sampleRate;
    synth.setCurrentPlaybackSampleRate(sampleRate);
    beatSynth.setCurrentPlaybackSampleRate(sampleRate);
}

void MainComponent::getNextAudioBlock(const juce::AudioSourceChannelInfo& bufferToFill)
{
    bufferToFill.clearActiveBufferRegion();

    const juce::ScopedLock sl(synthLock);
    juce::MidiBuffer midi;
    juce::MidiBuffer beatMidi;
    const float blockSeconds = static_cast<float>(bufferToFill.numSamples / juce::jmax(1.0, currentSampleRate));
    const double stepSeconds = 60.0 / bpm / 4.0;

    double localTime = 0.0;
    while (localTime < blockSeconds)
    {
        const double remainingToStep = stepSeconds - beatStepAccumulator;
        if (localTime + remainingToStep > blockSeconds)
        {
            beatStepAccumulator += blockSeconds - localTime;
            break;
        }

        localTime += remainingToStep;
        beatStepAccumulator = 0.0;
        const int step = beatStepIndex % 16;
        const int sampleOffset = juce::jlimit(0, juce::jmax(0, bufferToFill.numSamples - 1),
                                              static_cast<int>(std::round(localTime * currentSampleRate)));

        if (performanceMode)
        {
            const int phrase = beatBarIndex % 4;
            const float density = juce::jlimit(0.0f, 1.0f,
                                               performanceBeatEnergy
                                                 + 0.04f * static_cast<float>(performanceSnakes.size())
                                                 + 0.02f * static_cast<float>(performanceDiscs.size()));
            if (drumMode == DrumMode::reactiveBreakbeat)
            {
                const bool dense = density > 0.45f;
                const bool frantic = density > 0.72f;
                const bool kick = step == 0
                               || step == 10
                               || (step == 7 && phrase >= 1)
                               || (step == 13 && phrase == 3)
                               || (step == 3 && phrase == 2)
                               || (dense && (step == 6 || step == 14))
                               || (frantic && (step == 2 || step == 11));
                const bool snare = step == 4 || step == 12;
                const bool ghostSnare = (step == 3 && phrase != 0)
                                     || (step == 11 && phrase >= 2)
                                     || (step == 15 && phrase == 3)
                                     || (dense && step == 6)
                                     || (frantic && (step == 9 || step == 14));
                const bool hat = step != 4 && step != 12;
                const bool openHat = step == 6 || step == 14 || (dense && step == 11);
                const bool glitch = (step == 9 && phrase >= 2) || (step == 15 && phrase == 1) || (frantic && (step == 5 || step == 13));

                if (kick) addBeatEvent(beatMidi, 120, step == 0 ? 0.96f : 0.72f, sampleOffset, bufferToFill.numSamples);
                if (snare) addBeatEvent(beatMidi, 121, 0.78f + 0.08f * static_cast<float>(phrase == 3), sampleOffset, bufferToFill.numSamples);
                if (ghostSnare) addBeatEvent(beatMidi, 121, 0.28f + 0.08f * static_cast<float>(phrase), sampleOffset + bufferToFill.numSamples / 32, bufferToFill.numSamples);
                if (hat) addBeatEvent(beatMidi, 122, (step % 2 == 0) ? 0.42f : 0.24f, sampleOffset, bufferToFill.numSamples);
                if (openHat) addBeatEvent(beatMidi, 122, 0.30f, sampleOffset + bufferToFill.numSamples / 24, bufferToFill.numSamples);
                if ((step == 5 || step == 13) && (phrase >= 1 || dense))
                {
                    addBeatEvent(beatMidi, 122, 0.22f, sampleOffset + bufferToFill.numSamples / 48, bufferToFill.numSamples);
                    addBeatEvent(beatMidi, 122, 0.19f, sampleOffset + bufferToFill.numSamples / 24, bufferToFill.numSamples);
                }
                if (dense && (step == 7 || step == 15))
                {
                    addBeatEvent(beatMidi, 122, 0.16f, sampleOffset + bufferToFill.numSamples / 64, bufferToFill.numSamples);
                    addBeatEvent(beatMidi, 122, 0.15f, sampleOffset + bufferToFill.numSamples / 40, bufferToFill.numSamples);
                    if (frantic)
                        addBeatEvent(beatMidi, 122, 0.14f, sampleOffset + bufferToFill.numSamples / 24, bufferToFill.numSamples);
                }
                if (glitch) addBeatEvent(beatMidi, 123, 0.34f + 0.1f * static_cast<float>(phrase), sampleOffset, bufferToFill.numSamples);
            }
            else
            {
                auto hit = [this, &beatMidi, sampleOffset, &bufferToFill] (int midiNote, float velocity)
                {
                    addBeatEvent(beatMidi, midiNote, juce::jlimit(0.0f, 1.0f, velocity * 0.90f), sampleOffset, bufferToFill.numSamples);
                };

                switch (drumMode)
                {
                    case DrumMode::rezStraight:
                        if ((step % 4) == 0) hit(120, 0.82f);
                        if (step == 4 || step == 12) hit(121, 0.60f);
                        if ((step % 2) == 1) hit(122, 0.18f + ((step % 4) == 3 ? 0.05f : 0.0f));
                        break;

                    case DrumMode::tightPulse:
                        if (step == 0 || step == 8 || step == 12) hit(120, 0.78f);
                        if (step == 4 || step == 12) hit(121, 0.58f);
                        if ((step % 2) == 1) hit(122, 0.17f);
                        if (step == 15) hit(123, 0.18f);
                        break;

                    case DrumMode::forwardStep:
                        if (step == 0 || step == 8 || step == 14) hit(120, 0.76f);
                        if (step == 4 || step == 12) hit(121, 0.56f);
                        if ((step % 2) == 1) hit(122, 0.16f + ((step == 11 || step == 15) ? 0.05f : 0.0f));
                        break;

                    case DrumMode::railLine:
                        if ((step % 4) == 0) hit(120, 0.72f);
                        if (step == 4 || step == 12) hit(121, 0.52f);
                        if ((step % 2) == 1) hit(122, 0.15f);
                        if (step == 8 || step == 15) hit(123, 0.16f);
                        break;

                    case DrumMode::reactiveBreakbeat:
                    default:
                        break;
                }
            }
        }

        ++beatStepIndex;
        if (beatStepIndex % 16 == 0)
            ++beatBarIndex;
    }

    for (auto it = pendingNoteOffs.begin(); it != pendingNoteOffs.end();)
    {
        if (it->secondsRemaining <= blockSeconds)
        {
            midi.addEvent(juce::MidiMessage::noteOff(1, it->note), juce::jmax(0, bufferToFill.numSamples - 1));
            it = pendingNoteOffs.erase(it);
        }
        else
        {
            it->secondsRemaining -= blockSeconds;
            ++it;
        }
    }

    for (auto it = pendingBeatNoteOffs.begin(); it != pendingBeatNoteOffs.end();)
    {
        if (it->secondsRemaining <= blockSeconds)
        {
            beatMidi.addEvent(juce::MidiMessage::noteOff(1, it->note), juce::jmax(0, bufferToFill.numSamples - 1));
            it = pendingBeatNoteOffs.erase(it);
        }
        else
        {
            it->secondsRemaining -= blockSeconds;
            ++it;
        }
    }

    synth.renderNextBlock(*bufferToFill.buffer, midi, bufferToFill.startSample, bufferToFill.numSamples);
    beatSynth.renderNextBlock(*bufferToFill.buffer, beatMidi, bufferToFill.startSample, bufferToFill.numSamples);
}

void MainComponent::releaseResources()
{
}

bool MainComponent::keyPressed(const juce::KeyPress& key, juce::Component*)
{
    return keyPressed(key);
}

bool MainComponent::keyPressed(const juce::KeyPress& key)
{
    if (isolatedSlab.isValid())
    {
        if (key == juce::KeyPress::returnKey)
        {
            performanceMode = ! performanceMode;
            if (performanceMode && performanceSnakes.empty())
                setPerformanceSnakeCount(1);
            repaint();
            return true;
        }
        if (key == juce::KeyPress::escapeKey) { isolatedSlab = {}; hoveredSlab = {}; editCursor = {}; performanceMode = false; performanceRegionMode = 2; performanceSnakes.clear(); performanceDiscs.clear(); performanceHoverCell.reset(); repaint(); return true; }

        if (performanceMode)
        {
            if (key == juce::KeyPress('t'))
            {
                synthEngine = static_cast<SynthEngine>((static_cast<int>(synthEngine) + 1) % 5);
                repaint();
                return true;
            }
            if (key == juce::KeyPress('b'))
            {
                drumMode = static_cast<DrumMode>((static_cast<int>(drumMode) + 1) % 5);
                repaint();
                return true;
            }
            if (key == juce::KeyPress('k'))
            {
                keyRoot = (keyRoot + 1) % 12;
                repaint();
                return true;
            }
            if (key == juce::KeyPress('l'))
            {
                scale = static_cast<ScaleType>((static_cast<int>(scale) + 1) % 5);
                repaint();
                return true;
            }
            if (key == juce::KeyPress::leftKey) { performanceSelectedDirection = { -1, 0 }; repaint(); return true; }
            if (key == juce::KeyPress::rightKey) { performanceSelectedDirection = { 1, 0 }; repaint(); return true; }
            if (key == juce::KeyPress::upKey) { performanceSelectedDirection = { 0, -1 }; repaint(); return true; }
            if (key == juce::KeyPress::downKey) { performanceSelectedDirection = { 0, 1 }; repaint(); return true; }
            if (key == juce::KeyPress('y'))
            {
                if (performanceHoverCell.has_value())
                {
                    const auto cell = *performanceHoverCell;
                    auto existing = std::find_if(performanceDiscs.begin(), performanceDiscs.end(),
                                                 [cell] (const ReflectorDisc& disc) { return disc.cell == cell; });
                    if (existing != performanceDiscs.end())
                    {
                        auto it = std::find(snakeDirections.begin(), snakeDirections.end(), existing->direction);
                        size_t index = it == snakeDirections.end() ? 0u : static_cast<size_t>(std::distance(snakeDirections.begin(), it));
                        index = (index + 1u) % snakeDirections.size();
                        existing->direction = snakeDirections[index];
                        performanceSelectedDirection = existing->direction;
                    }
                    else
                    {
                        performanceDiscs.push_back({ cell, performanceSelectedDirection });
                    }
                    repaint();
                }
                return true;
            }
            if (key == juce::KeyPress('z'))
            {
                performanceRegionMode = (performanceRegionMode + 1) % 3;
                setPerformanceSnakeCount(static_cast<int>(performanceSnakes.size()));
                repaint();
                return true;
            }
            if (key == juce::KeyPress('0')) { setPerformanceSnakeCount(0); repaint(); return true; }
            if (key == juce::KeyPress('1')) { setPerformanceSnakeCount(1); repaint(); return true; }
            if (key == juce::KeyPress('2')) { setPerformanceSnakeCount(2); repaint(); return true; }
            if (key == juce::KeyPress('3')) { setPerformanceSnakeCount(3); repaint(); return true; }
            if (key == juce::KeyPress('4')) { setPerformanceSnakeCount(4); repaint(); return true; }
            if (key == juce::KeyPress('5')) { setPerformanceSnakeCount(5); repaint(); return true; }
            if (key == juce::KeyPress('6')) { setPerformanceSnakeCount(6); repaint(); return true; }
            if (key == juce::KeyPress('7')) { setPerformanceSnakeCount(7); repaint(); return true; }
            if (key == juce::KeyPress('8')) { setPerformanceSnakeCount(8); repaint(); return true; }
            return true;
        }

        if (key == juce::KeyPress::leftKey) { moveEditCursor(-1, 0, 0); repaint(); return true; }
        if (key == juce::KeyPress::rightKey) { moveEditCursor(1, 0, 0); repaint(); return true; }
        if (key == juce::KeyPress::upKey) { moveEditCursor(0, -1, 0); repaint(); return true; }
        if (key == juce::KeyPress::downKey) { moveEditCursor(0, 1, 0); repaint(); return true; }
        if (key == juce::KeyPress::pageUpKey || key == juce::KeyPress(']')) { moveEditCursor(0, 0, 1); repaint(); return true; }
        if (key == juce::KeyPress::pageDownKey || key == juce::KeyPress('[')) { moveEditCursor(0, 0, -1); repaint(); return true; }
        if (key == juce::KeyPress('p'))
        {
            if (editCursor.active)
                setVoxel(editCursor.x, editCursor.y, editCursor.z, true);
            repaint();
            return true;
        }
        if (key == juce::KeyPress::deleteKey || key == juce::KeyPress::backspaceKey || key == juce::KeyPress('x'))
        {
            if (editCursor.active)
                setVoxel(editCursor.x, editCursor.y, editCursor.z, false);
            repaint();
            return true;
        }
    }

    if (key == juce::KeyPress('t'))
    {
        synthEngine = static_cast<SynthEngine>((static_cast<int>(synthEngine) + 1) % 5);
        repaint();
        return true;
    }
    if (key == juce::KeyPress('b'))
    {
        drumMode = static_cast<DrumMode>((static_cast<int>(drumMode) + 1) % 5);
        repaint();
        return true;
    }
    if (key == juce::KeyPress('k'))
    {
        keyRoot = (keyRoot + 1) % 12;
        repaint();
        return true;
    }
    if (key == juce::KeyPress('l'))
    {
        scale = static_cast<ScaleType>((static_cast<int>(scale) + 1) % 5);
        repaint();
        return true;
    }
    if (key == juce::KeyPress('u'))
    {
        if (quantizeWorldToCurrentScale())
            repaint();
        return true;
    }
    if (key == juce::KeyPress('g')) { splitIntoFourIslands(); repaint(); return true; }
    if (key == juce::KeyPress('q')) { rotateCamera(-1); return true; }
    if (key == juce::KeyPress('e')) { rotateCamera(1); return true; }
    if (key == juce::KeyPress('w')) { panCamera(0.0f, 42.0f); return true; }
    if (key == juce::KeyPress('s')) { panCamera(0.0f, -42.0f); return true; }
    if (key == juce::KeyPress('a')) { panCamera(42.0f, 0.0f); return true; }
    if (key == juce::KeyPress('d')) { panCamera(-42.0f, 0.0f); return true; }
    if (key == juce::KeyPress('-')) { changeHeightScale(-0.08f); return true; }
    if (key == juce::KeyPress('=')) { changeHeightScale(0.08f); return true; }
    if (key == juce::KeyPress('r')) { randomiseVoxels(); repaint(); return true; }
    return false;
}

void MainComponent::timerCallback()
{
    const float delta = targetZoom - camera.zoom;
    bool needsRepaint = false;

    if (std::abs(delta) >= 0.001f)
    {
        camera.zoom += delta * 0.28f;
        needsRepaint = true;
    }
    else
    {
        camera.zoom = targetZoom;
    }

    if (performanceMode && ! performanceSnakes.empty())
    {
        ++performanceTick;
        performanceBeatEnergy *= 0.986f;
        if (performanceTick % 8 == 0)
        {
            stepPerformanceSnakes();
            needsRepaint = true;
        }
    }
    else
    {
        performanceBeatEnergy *= 0.97f;
    }

    if (needsRepaint)
        repaint();
}

void MainComponent::randomiseVoxels()
{
    juce::Random rng;
    layoutMode = LayoutMode::OneBoard;
    hoveredSlab = {};
    isolatedSlab = {};
    editCursor = {};
    performanceMode = false;
    performanceRegionMode = 2;
    performanceSnakes.clear();
    performanceDiscs.clear();
    performanceHoverCell.reset();
    performanceTick = 0;
    beatStepAccumulator = 0.0;
    beatStepIndex = 0;
    beatBarIndex = 0;
    performanceBeatEnergy = 0.0f;
    voxelCount = 0;
    filledVoxels.clear();
    filledVoxels.reserve(static_cast<size_t>(gridWidth * gridDepth * gridHeight * voxelFillRatio * 1.3f));
    std::fill(voxels.begin(), voxels.end(), 0u);

    for (int z = 0; z < gridHeight; ++z)
    {
        for (int y = 0; y < gridDepth; ++y)
        {
            for (int x = 0; x < gridWidth; ++x)
            {
                auto filled = rng.nextFloat() < voxelFillRatio;

                if (! filled && z < 12 && rng.nextFloat() < 0.00875f)
                    filled = true;

                voxels[voxelIndex(x, y, z)] = filled ? 1u : 0u;
                if (filled)
                {
                    ++voxelCount;
                    filledVoxels.push_back(FilledVoxel { static_cast<uint16_t>(x), static_cast<uint16_t>(y), static_cast<uint8_t>(z) });
                }
            }
        }
    }
}

void MainComponent::splitIntoFourIslands()
{
    layoutMode = layoutMode == LayoutMode::OneBoard ? LayoutMode::FourIslands
              : layoutMode == LayoutMode::FourIslands ? LayoutMode::FourIslandsFourFloors
              : LayoutMode::OneBoard;
    hoveredSlab = {};
    isolatedSlab = {};
    editCursor = {};
    performanceMode = false;
    performanceRegionMode = 2;
    performanceSnakes.clear();
    performanceDiscs.clear();
    performanceHoverCell.reset();
    performanceTick = 0;
    beatStepAccumulator = 0.0;
    beatStepIndex = 0;
    beatBarIndex = 0;
    performanceBeatEnergy = 0.0f;
}

bool MainComponent::hasVoxel(int x, int y, int z) const
{
    if (x < 0 || x >= gridWidth || y < 0 || y >= gridDepth || z < 0 || z >= gridHeight)
        return false;

    return voxels[voxelIndex(x, y, z)] != 0;
}

void MainComponent::setVoxel(int x, int y, int z, bool filled)
{
    if (x < 0 || x >= gridWidth || y < 0 || y >= gridDepth || z < 0 || z >= gridHeight)
        return;

    auto& cell = voxels[voxelIndex(x, y, z)];
    const bool wasFilled = cell != 0;
    if (wasFilled == filled)
        return;

    cell = filled ? 1u : 0u;

    if (filled)
    {
        ++voxelCount;
        filledVoxels.push_back(FilledVoxel { static_cast<uint16_t>(x), static_cast<uint16_t>(y), static_cast<uint8_t>(z) });
        return;
    }

    --voxelCount;
    filledVoxels.erase(std::remove_if(filledVoxels.begin(), filledVoxels.end(),
                                      [x, y, z] (const FilledVoxel& voxel)
                                      {
                                          return voxel.x == static_cast<uint16_t>(x)
                                              && voxel.y == static_cast<uint16_t>(y)
                                              && voxel.z == static_cast<uint8_t>(z);
                                      }),
                      filledVoxels.end());
}

size_t MainComponent::voxelIndex(int x, int y, int z) const
{
    return static_cast<size_t>(x + gridWidth * (y + gridDepth * z));
}

juce::Point<int> MainComponent::islandOffsetForCell(int x, int y) const
{
    if (layoutMode == LayoutMode::OneBoard)
        return { 0, 0 };

    constexpr int halfGap = islandGapSize / 2;
    const int minXCoord = boardInset;
    const int minYCoord = boardInset;
    const int maxXCoord = gridWidth - boardInset;
    const int maxYCoord = gridDepth - boardInset;
    const int centreX = minXCoord + (maxXCoord - minXCoord) / 2;
    const int centreY = minYCoord + (maxYCoord - minYCoord) / 2;

    if (x >= minXCoord && x <= maxXCoord && y >= minYCoord && y <= maxYCoord)
        return { x < centreX ? -halfGap : halfGap, y < centreY ? -halfGap : halfGap };

    return { 0, 0 };
}

int MainComponent::renderBaseZForLayer(int z) const
{
    if (layoutMode != LayoutMode::FourIslandsFourFloors)
        return z;

    const int floorIndex = z / floorBandHeight;
    const int localZ = z % floorBandHeight;
    return floorIndex * (floorBandHeight + floorBandGap) + localZ;
}

int MainComponent::renderedWorldHeight() const
{
    if (layoutMode != LayoutMode::FourIslandsFourFloors)
        return gridHeight;

    return 4 * floorBandHeight + 3 * floorBandGap;
}

int MainComponent::quadrantForCell(int x, int y) const
{
    const int minXCoord = boardInset;
    const int minYCoord = boardInset;
    const int maxXCoord = gridWidth - boardInset;
    const int maxYCoord = gridDepth - boardInset;
    const int centreX = minXCoord + (maxXCoord - minXCoord) / 2;
    const int centreY = minYCoord + (maxYCoord - minYCoord) / 2;
    const int qx = x < centreX ? 0 : 1;
    const int qy = y < centreY ? 0 : 1;
    return qx + qy * 2;
}

void MainComponent::quadrantBounds(int quadrant, int& x0, int& y0, int& x1, int& y1) const
{
    const int minXCoord = boardInset;
    const int minYCoord = boardInset;
    const int maxXCoord = gridWidth - boardInset;
    const int maxYCoord = gridDepth - boardInset;
    const int centreX = minXCoord + (maxXCoord - minXCoord) / 2;
    const int centreY = minYCoord + (maxYCoord - minYCoord) / 2;

    const bool right = (quadrant % 2) == 1;
    const bool bottom = quadrant >= 2;
    x0 = right ? centreX : minXCoord;
    x1 = right ? maxXCoord : centreX;
    y0 = bottom ? centreY : minYCoord;
    y1 = bottom ? maxYCoord : centreY;
}

juce::Path MainComponent::slabPath(const SlabSelection& slab, juce::Rectangle<float> area) const
{
    juce::Path path;
    if (! slab.isValid())
        return path;

    int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    quadrantBounds(slab.quadrant, x0, y0, x1, y1);
    const auto offset = islandOffsetForCell(x0, y0);
    const int floorZ = slab.floor * (floorBandHeight + floorBandGap);

    const auto p00 = projectPoint(x0 + offset.x, y0 + offset.y, floorZ, area);
    const auto p10 = projectPoint(x1 + offset.x, y0 + offset.y, floorZ, area);
    const auto p11 = projectPoint(x1 + offset.x, y1 + offset.y, floorZ, area);
    const auto p01 = projectPoint(x0 + offset.x, y1 + offset.y, floorZ, area);
    path.startNewSubPath(p00);
    path.lineTo(p10);
    path.lineTo(p11);
    path.lineTo(p01);
    path.closeSubPath();
    return path;
}

MainComponent::SlabSelection MainComponent::slabAtPosition(juce::Point<float> position, juce::Rectangle<float> area) const
{
    if (layoutMode != LayoutMode::FourIslandsFourFloors)
        return {};

    for (int floor = 3; floor >= 0; --floor)
    {
        for (int quadrant = 0; quadrant < 4; ++quadrant)
        {
            SlabSelection slab { quadrant, floor };
            if (slabPath(slab, area).contains(position))
                return slab;
        }
    }

    return {};
}

bool MainComponent::voxelInSelectedSlab(int x, int y, int z, const SlabSelection& slab) const
{
    if (! slab.isValid())
        return false;

    const int floor = z / floorBandHeight;
    return floor == slab.floor && quadrantForCell(x, y) == slab.quadrant;
}

bool MainComponent::cellInSelectedSlab(int x, int y, const SlabSelection& slab) const
{
    return slab.isValid() && quadrantForCell(x, y) == slab.quadrant;
}

juce::Path MainComponent::cursorPath(const EditCursor& cursor, juce::Rectangle<float> area) const
{
    juce::Path path;
    if (! cursor.active || ! isolatedSlab.isValid() || ! cellInSelectedSlab(cursor.x, cursor.y, isolatedSlab))
        return path;

    const int baseZ = renderBaseZForLayer(cursor.z);
    const auto aTop = projectCellCorner(cursor.x,     cursor.y,     baseZ + 1, cursor.x, cursor.y, area);
    const auto bTop = projectCellCorner(cursor.x + 1, cursor.y,     baseZ + 1, cursor.x, cursor.y, area);
    const auto cTop = projectCellCorner(cursor.x + 1, cursor.y + 1, baseZ + 1, cursor.x, cursor.y, area);
    const auto dTop = projectCellCorner(cursor.x,     cursor.y + 1, baseZ + 1, cursor.x, cursor.y, area);
    const auto aBottom = projectCellCorner(cursor.x,     cursor.y,     baseZ, cursor.x, cursor.y, area);
    const auto bBottom = projectCellCorner(cursor.x + 1, cursor.y,     baseZ, cursor.x, cursor.y, area);
    const auto cBottom = projectCellCorner(cursor.x + 1, cursor.y + 1, baseZ, cursor.x, cursor.y, area);
    const auto dBottom = projectCellCorner(cursor.x,     cursor.y + 1, baseZ, cursor.x, cursor.y, area);

    path.startNewSubPath(aTop);
    path.lineTo(bTop);
    path.lineTo(cTop);
    path.lineTo(dTop);
    path.closeSubPath();

    path.startNewSubPath(aTop);
    path.lineTo(aBottom);
    path.startNewSubPath(bTop);
    path.lineTo(bBottom);
    path.startNewSubPath(cTop);
    path.lineTo(cBottom);
    path.startNewSubPath(dTop);
    path.lineTo(dBottom);

    path.startNewSubPath(aBottom);
    path.lineTo(bBottom);
    path.lineTo(cBottom);
    path.lineTo(dBottom);
    path.closeSubPath();

    return path;
}

bool MainComponent::updateCursorFromPosition(juce::Point<float> position, juce::Rectangle<float> area)
{
    if (! isolatedSlab.isValid())
        return false;

    int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    quadrantBounds(isolatedSlab.quadrant, x0, y0, x1, y1);

    float bestDistance = std::numeric_limits<float>::max();
    juce::Point<int> bestCell;
    bool found = false;

    for (int y = y0; y < y1; ++y)
    {
        for (int x = x0; x < x1; ++x)
        {
            juce::Path topFace;
            const int baseZ = renderBaseZForLayer(editCursor.z);
            const auto aTop = projectCellCorner(x,     y,     baseZ + 1, x, y, area);
            const auto bTop = projectCellCorner(x + 1, y,     baseZ + 1, x, y, area);
            const auto cTop = projectCellCorner(x + 1, y + 1, baseZ + 1, x, y, area);
            const auto dTop = projectCellCorner(x,     y + 1, baseZ + 1, x, y, area);
            topFace.startNewSubPath(aTop);
            topFace.lineTo(bTop);
            topFace.lineTo(cTop);
            topFace.lineTo(dTop);
            topFace.closeSubPath();

            if (topFace.contains(position))
            {
                bestCell = { x, y };
                found = true;
                bestDistance = 0.0f;
                break;
            }

            const auto centre = juce::Point<float>((aTop.x + cTop.x) * 0.5f, (aTop.y + cTop.y) * 0.5f);
            const auto distance = centre.getDistanceSquaredFrom(position);
            if (distance < bestDistance)
            {
                bestDistance = distance;
                bestCell = { x, y };
                found = true;
            }
        }

        if (bestDistance == 0.0f)
            break;
    }

    if (! found)
        return false;

    if (! editCursor.active || editCursor.x != bestCell.x || editCursor.y != bestCell.y)
    {
        editCursor.x = bestCell.x;
        editCursor.y = bestCell.y;
        editCursor.active = true;
        return true;
    }

    return false;
}

void MainComponent::resetEditCursor()
{
    editCursor = {};
    if (! isolatedSlab.isValid())
        return;

    int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    quadrantBounds(isolatedSlab.quadrant, x0, y0, x1, y1);
    editCursor.x = x0 + (x1 - x0) / 2;
    editCursor.y = y0 + (y1 - y0) / 2;
    editCursor.z = juce::jlimit(0, gridHeight - 1, isolatedSlab.floor * floorBandHeight);
    editCursor.active = true;
}

void MainComponent::moveEditCursor(int dx, int dy, int dz)
{
    if (! isolatedSlab.isValid())
        return;

    if (! editCursor.active)
        resetEditCursor();

    int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    quadrantBounds(isolatedSlab.quadrant, x0, y0, x1, y1);
    editCursor.x = juce::jlimit(x0, x1 - 1, editCursor.x + dx);
    editCursor.y = juce::jlimit(y0, y1 - 1, editCursor.y + dy);

    const int zMin = isolatedSlab.floor * floorBandHeight;
    const int zMax = zMin + floorBandHeight - 1;
    editCursor.z = juce::jlimit(zMin, zMax, editCursor.z + dz);
    editCursor.active = true;
}

int MainComponent::midiNoteForHeight(int z) const
{
    const int midi = juce::jlimit(24, 108, 59 + z);
    return juce::jlimit(24, 108, quantizeMidiToScale(midi));
}

std::vector<int> MainComponent::currentScaleSteps() const
{
    switch (scale)
    {
        case ScaleType::chromatic: return { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11 };
        case ScaleType::major: return { 0, 2, 4, 5, 7, 9, 11 };
        case ScaleType::minor: return { 0, 2, 3, 5, 7, 8, 10 };
        case ScaleType::dorian: return { 0, 2, 3, 5, 7, 9, 10 };
        case ScaleType::pentatonic: return { 0, 2, 4, 7, 9 };
    }

    return { 0, 2, 3, 5, 7, 8, 10 };
}

int MainComponent::quantizeMidiToScale(int midi) const
{
    if (! quantizeToScale)
        return midi;

    const auto steps = currentScaleSteps();
    int bestNote = midi;
    int bestDistance = 128;

    for (int n = midi - 12; n <= midi + 12; ++n)
    {
        const int pc = (n % 12 + 12) % 12;
        const int rel = (pc - keyRoot + 12) % 12;
        if (std::find(steps.begin(), steps.end(), rel) != steps.end())
        {
            const int d = std::abs(n - midi);
            if (d < bestDistance)
            {
                bestDistance = d;
                bestNote = n;
            }
        }
    }

    return bestNote;
}

int MainComponent::quantizeMidiToCurrentScaleStrict(int midi) const
{
    const auto steps = currentScaleSteps();
    int bestNote = midi;
    int bestDistance = 128;

    for (int n = midi - 12; n <= midi + 12; ++n)
    {
        const int pc = (n % 12 + 12) % 12;
        const int rel = (pc - keyRoot + 12) % 12;
        if (std::find(steps.begin(), steps.end(), rel) != steps.end())
        {
            const int d = std::abs(n - midi);
            if (d < bestDistance)
            {
                bestDistance = d;
                bestNote = n;
            }
        }
    }

    return bestNote;
}

bool MainComponent::quantizeWorldToCurrentScale()
{
    bool changed = false;
    std::vector<uint8_t> newVoxels(voxels.size(), 0u);

    for (const auto& voxel : filledVoxels)
    {
        const int quantizedZ = voxel.z == 0
                                 ? 0
                                 : juce::jlimit(1, gridHeight - 1,
                                                (quantizeMidiToCurrentScaleStrict(juce::jlimit(24, 108, 59 + static_cast<int>(voxel.z))) - 60) + 1);
        const auto newIndex = voxelIndex(voxel.x, voxel.y, quantizedZ);
        newVoxels[newIndex] = 1u;
        changed = changed || (quantizedZ != voxel.z);
    }

    if (! changed)
        return false;

    voxels = std::move(newVoxels);
    filledVoxels.clear();
    voxelCount = 0;
    for (int z = 0; z < gridHeight; ++z)
        for (int y = 0; y < gridDepth; ++y)
            for (int x = 0; x < gridWidth; ++x)
                if (voxels[voxelIndex(x, y, z)] != 0u)
                {
                    filledVoxels.push_back(FilledVoxel { static_cast<uint16_t>(x), static_cast<uint16_t>(y), static_cast<uint8_t>(z) });
                    ++voxelCount;
                }

    return true;
}

void MainComponent::triggerPerformanceNotesAtCell(juce::Point<int> cell)
{
    if (! isolatedSlab.isValid())
        return;

    const int zStart = isolatedSlab.floor * floorBandHeight;
    const int zEnd = zStart + floorBandHeight;
    const juce::ScopedLock sl(synthLock);

    int triggered = 0;
    for (int z = zStart; z < zEnd; ++z)
    {
        if (! hasVoxel(cell.x, cell.y, z))
            continue;

        const int midiNote = midiNoteForHeight(z);
        const float velocity = juce::jlimit(0.15f, 0.92f, 0.34f + 0.04f * static_cast<float>(z - zStart));
        synth.noteOn(1, midiNote, velocity);
        pendingNoteOffs.push_back({ midiNote, 0.16f });
        ++triggered;
    }

    if (triggered == 0)
    {
        const int fallback = midiNoteForHeight(zStart + ((cell.x + cell.y) % floorBandHeight));
        synth.noteOn(1, fallback, 0.18f);
        pendingNoteOffs.push_back({ fallback, 0.08f });
        performanceBeatEnergy = juce::jmin(1.0f, performanceBeatEnergy + 0.03f);
        return;
    }

    performanceBeatEnergy = juce::jmin(1.0f, performanceBeatEnergy + 0.08f + 0.03f * static_cast<float>(triggered - 1));
}

void MainComponent::addBeatEvent(juce::MidiBuffer& buffer, int midiNote, float velocity, int sampleOffset, int blockSamples)
{
    const int onOffset = juce::jlimit(0, juce::jmax(0, blockSamples - 1), sampleOffset);
    buffer.addEvent(juce::MidiMessage::noteOn(1, midiNote, juce::jlimit(0.0f, 1.0f, velocity)), onOffset);

    const float noteLengthSeconds = midiNote == 120 ? 0.12f : midiNote == 121 ? 0.09f : midiNote == 122 ? 0.03f : 0.05f;
    pendingBeatNoteOffs.push_back({ midiNote, noteLengthSeconds });
}

juce::Colour MainComponent::displayColourForVoxel(int x, int y, int z, juce::Colour base) const
{
    if (! isolatedSlab.isValid() && ! hoveredSlab.isValid())
        return base;

    const auto active = isolatedSlab.isValid() ? isolatedSlab : hoveredSlab;
    if (voxelInSelectedSlab(x, y, z, active))
        return base;

    const auto grey = juce::Colour::greyLevel(base.getPerceivedBrightness());
    return base.interpolatedWith(grey, 0.88f)
               .darker(0.42f)
               .withAlpha(base.getFloatAlpha() * 0.42f);
}

float MainComponent::hoverLiftForSlab(const SlabSelection& slab) const
{
    if (! slab.isValid() || isolatedSlab.isValid() || layoutMode != LayoutMode::FourIslandsFourFloors)
        return 0.0f;

    return slab.quadrant == hoveredSlab.quadrant && slab.floor == hoveredSlab.floor ? 10.0f * camera.zoom : 0.0f;
}

juce::String MainComponent::labelForSlab(const SlabSelection& slab) const
{
    static constexpr std::array<const char*, 4> quadrantNames { "NW", "NE", "SW", "SE" };

    if (! slab.isValid())
        return {};

    return juce::String(quadrantNames[static_cast<size_t>(juce::jlimit(0, 3, slab.quadrant))])
         + " Floor " + juce::String(slab.floor + 1);
}

juce::Rectangle<int> MainComponent::performanceRegionBounds() const
{
    int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    quadrantBounds(isolatedSlab.quadrant, x0, y0, x1, y1);

    const int width = x1 - x0;
    const int height = y1 - y0;

    if (performanceRegionMode == 0)
    {
        const int insetX = width / 4;
        const int insetY = height / 4;
        return { x0 + insetX, y0 + insetY, juce::jmax(1, width / 2), juce::jmax(1, height / 2) };
    }

    if (performanceRegionMode == 1)
    {
        const int insetX = width / 8;
        const int insetY = height / 8;
        return { x0 + insetX, y0 + insetY, juce::jmax(1, width - insetX * 2), juce::jmax(1, height - insetY * 2) };
    }

    return { x0, y0, width, height };
}

juce::Rectangle<float> MainComponent::performanceBoardBounds(juce::Rectangle<float> area) const
{
    int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    quadrantBounds(isolatedSlab.quadrant, x0, y0, x1, y1);

    const int slabWidth = x1 - x0;
    const int slabHeight = y1 - y0;
    const auto available = area.reduced(36.0f);
    const float tileSize = juce::jmax(6.0f,
                                      juce::jmin(available.getWidth() / static_cast<float>(slabWidth),
                                                 available.getHeight() / static_cast<float>(slabHeight)));
    return juce::Rectangle<float>(tileSize * static_cast<float>(slabWidth),
                                  tileSize * static_cast<float>(slabHeight))
        .withCentre(area.getCentre());
}

std::optional<juce::Point<int>> MainComponent::performanceCellAtPosition(juce::Point<float> position, juce::Rectangle<float> area) const
{
    if (! isolatedSlab.isValid())
        return std::nullopt;

    int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    quadrantBounds(isolatedSlab.quadrant, x0, y0, x1, y1);

    const auto board = performanceBoardBounds(area);
    if (! board.contains(position))
        return std::nullopt;

    const int slabWidth = x1 - x0;
    const int slabHeight = y1 - y0;
    const float tileSize = board.getWidth() / static_cast<float>(slabWidth);
    const int localX = juce::jlimit(0, slabWidth - 1, static_cast<int>((position.x - board.getX()) / tileSize));
    const int localY = juce::jlimit(0, slabHeight - 1, static_cast<int>((position.y - board.getY()) / tileSize));
    return juce::Point<int>(x0 + localX, y0 + localY);
}

void MainComponent::setPerformanceSnakeCount(int count)
{
    performanceSnakes.clear();
    performanceTick = 0;

    if (! isolatedSlab.isValid())
        return;

    const auto bounds = performanceRegionBounds();
    if (bounds.isEmpty())
        return;

    juce::Random rng;
    count = juce::jlimit(0, 8, count);
    for (int i = 0; i < count; ++i)
    {
        Snake snake;
        snake.colour = snakeColours[static_cast<size_t>(i % static_cast<int>(snakeColours.size()))];
        snake.direction = snakeDirections[static_cast<size_t>(rng.nextInt(static_cast<int>(snakeDirections.size())))];

        const auto start = juce::Point<int>(bounds.getX() + rng.nextInt(bounds.getWidth()),
                                            bounds.getY() + rng.nextInt(bounds.getHeight()));
        snake.body.push_back(start);

        auto tail = start;
        for (int segment = 1; segment < 5; ++segment)
        {
            tail.x = juce::jlimit(bounds.getX(), bounds.getRight() - 1, tail.x - snake.direction.x);
            tail.y = juce::jlimit(bounds.getY(), bounds.getBottom() - 1, tail.y - snake.direction.y);
            snake.body.push_back(tail);
        }

        performanceSnakes.push_back(std::move(snake));
    }
}

void MainComponent::stepPerformanceSnakes()
{
    if (! isolatedSlab.isValid())
        return;

    const auto bounds = performanceRegionBounds();

    auto inside = [&] (juce::Point<int> p)
    {
        return bounds.contains(p);
    };

    auto occupiedByAnySnake = [&] (juce::Point<int> cell, int movingSnakeIndex)
    {
        for (int i = 0; i < static_cast<int>(performanceSnakes.size()); ++i)
        {
            const auto& other = performanceSnakes[static_cast<size_t>(i)];
            for (size_t segment = 0; segment < other.body.size(); ++segment)
            {
                // Let a snake move into its own trailing tail cell since that segment advances away this tick.
                if (i == movingSnakeIndex && segment == other.body.size() - 1)
                    continue;

                if (other.body[segment] == cell)
                    return true;
            }
        }

        return false;
    };

    for (int snakeIndex = 0; snakeIndex < static_cast<int>(performanceSnakes.size()); ++snakeIndex)
    {
        auto& snake = performanceSnakes[static_cast<size_t>(snakeIndex)];
        if (snake.body.empty())
            continue;

        auto chooseDirection = [&] ()
        {
            std::vector<juce::Point<int>> options;
            for (const auto& dir : snakeDirections)
            {
                if (dir.x == -snake.direction.x && dir.y == -snake.direction.y)
                    continue;

                const auto candidate = snake.body.front() + dir;
                if (! inside(candidate) || occupiedByAnySnake(candidate, snakeIndex))
                    continue;

                options.push_back(dir);
            }

            if (options.empty())
            {
                for (const auto& dir : snakeDirections)
                {
                    const auto candidate = snake.body.front() + dir;
                    if (inside(candidate) && ! occupiedByAnySnake(candidate, snakeIndex))
                        options.push_back(dir);
                }
            }

            if (! options.empty())
            {
                // Deterministic fallback order: keep movement stable instead of wandering randomly.
                snake.direction = options.front();
            }
        };

        auto forward = snake.body.front() + snake.direction;
        if (! inside(forward) || occupiedByAnySnake(forward, snakeIndex))
            chooseDirection();

        auto nextHead = snake.body.front() + snake.direction;
        if (! inside(nextHead) || occupiedByAnySnake(nextHead, snakeIndex))
            continue;

        if (const auto disc = std::find_if(performanceDiscs.begin(), performanceDiscs.end(),
                                           [nextHead] (const ReflectorDisc& reflector) { return reflector.cell == nextHead; });
            disc != performanceDiscs.end())
        {
            snake.direction = disc->direction;
            performanceBeatEnergy = juce::jmin(1.0f, performanceBeatEnergy + 0.1f);
        }

        snake.body.insert(snake.body.begin(), nextHead);
        if (snake.body.size() > 7)
            snake.body.pop_back();
        triggerPerformanceNotesAtCell(nextHead);
    }
}

juce::Point<float> MainComponent::projectCellCorner(int x, int y, int z, int cellX, int cellY, juce::Rectangle<float> area) const
{
    const auto offset = islandOffsetForCell(cellX, cellY);
    return projectPoint(x + offset.x, y + offset.y, z, area);
}

juce::Point<float> MainComponent::projectionOffset(juce::Rectangle<float> area) const
{
    const int minXCoord = boardInset;
    const int minYCoord = boardInset;
    const int maxXCoord = gridWidth - boardInset;
    const int maxYCoord = gridDepth - boardInset;
    const auto tileWidth = baseTileWidth * camera.zoom;
    const auto tileHeight = baseTileHeight * camera.zoom;
    const auto verticalStep = baseVerticalStep * camera.zoom * camera.heightScale;

    auto projectRaw = [&] (int x, int y, int z)
    {
        const auto rotated = rotateXY(x, y);
        return juce::Point<float>((rotated.x - rotated.y) * tileWidth * 0.5f,
                                  (rotated.x + rotated.y) * tileHeight * 0.5f - z * verticalStep);
    };

    constexpr int halfGap = islandGapSize / 2;
    int leftX = minXCoord;
    int rightX = maxXCoord;
    int topY = minYCoord;
    int bottomY = maxYCoord;

    if (isolatedSlab.isValid() && layoutMode == LayoutMode::FourIslandsFourFloors)
    {
        quadrantBounds(isolatedSlab.quadrant, leftX, topY, rightX, bottomY);
        const auto offset = islandOffsetForCell(leftX, topY);
        leftX += offset.x;
        rightX += offset.x;
        topY += offset.y;
        bottomY += offset.y;
    }
    else if (layoutMode != LayoutMode::OneBoard)
    {
        leftX -= halfGap;
        rightX += halfGap;
        topY -= halfGap;
        bottomY += halfGap;
    }

    int zMin = 0;
    int zMax = renderedWorldHeight();
    if (isolatedSlab.isValid() && layoutMode == LayoutMode::FourIslandsFourFloors)
    {
        zMin = isolatedSlab.floor * (floorBandHeight + floorBandGap);
        zMax = zMin + floorBandHeight;
    }

    const std::array<juce::Point<float>, 8> corners {{
        projectRaw(leftX, topY, zMin),
        projectRaw(rightX, topY, zMin),
        projectRaw(leftX, bottomY, zMin),
        projectRaw(rightX, bottomY, zMin),
        projectRaw(leftX, topY, zMax),
        projectRaw(rightX, topY, zMax),
        projectRaw(leftX, bottomY, zMax),
        projectRaw(rightX, bottomY, zMax)
    }};

    float minX = corners.front().x;
    float maxX = corners.front().x;
    float minY = corners.front().y;
    float maxY = corners.front().y;

    for (const auto& point : corners)
    {
        minX = juce::jmin(minX, point.x);
        maxX = juce::jmax(maxX, point.x);
        minY = juce::jmin(minY, point.y);
        maxY = juce::jmax(maxY, point.y);
    }

    const auto projectedCentre = juce::Point<float>((minX + maxX) * 0.5f, (minY + maxY) * 0.5f);
    const auto targetCentre = area.getCentre();
    return { targetCentre.x - projectedCentre.x + camera.panX,
             targetCentre.y - projectedCentre.y + camera.panY };
}

juce::Point<float> MainComponent::projectPoint(int x, int y, int z, juce::Rectangle<float> area) const
{
    const auto tileWidth = baseTileWidth * camera.zoom;
    const auto tileHeight = baseTileHeight * camera.zoom;
    const auto verticalStep = baseVerticalStep * camera.zoom * camera.heightScale;
    const auto offset = projectionOffset(area);
    const auto rotated = rotateXY(x, y);

    return { offset.x + (rotated.x - rotated.y) * tileWidth * 0.5f,
             offset.y + (rotated.x + rotated.y) * tileHeight * 0.5f - z * verticalStep };
}

void MainComponent::drawPerformanceView(juce::Graphics& g, juce::Rectangle<float> area)
{
    if (! isolatedSlab.isValid())
        return;

    int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    quadrantBounds(isolatedSlab.quadrant, x0, y0, x1, y1);
    const auto activeRegion = performanceRegionBounds();

    const int slabWidth = x1 - x0;
    const int slabHeight = y1 - y0;
    const auto board = performanceBoardBounds(area);
    const float tileSize = board.getWidth() / static_cast<float>(slabWidth);

    g.setColour(juce::Colour::fromRGBA(7, 12, 30, 235));
    g.fillRoundedRectangle(board.expanded(22.0f), 20.0f);
    g.setColour(juce::Colour::fromRGBA(32, 54, 130, 80));
    g.fillEllipse(board.expanded(54.0f, 48.0f));

    const int zStart = isolatedSlab.floor * floorBandHeight;
    const int zEnd = zStart + floorBandHeight;

    for (int localY = 0; localY < slabHeight; ++localY)
    {
        for (int localX = 0; localX < slabWidth; ++localX)
        {
            const int worldX = x0 + localX;
            const int worldY = y0 + localY;
            auto cell = juce::Rectangle<float>(board.getX() + static_cast<float>(localX) * tileSize,
                                               board.getY() + static_cast<float>(localY) * tileSize,
                                               tileSize,
                                               tileSize);
            const bool activeCell = activeRegion.contains(worldX, worldY);

            g.setColour(activeCell ? juce::Colour::fromRGBA(10, 18, 42, 255)
                                   : juce::Colour::fromRGBA(18, 22, 34, 220));
            g.fillRect(cell);

            std::vector<int> notes;
            for (int z = zStart; z < zEnd; ++z)
                if (hasVoxel(worldX, worldY, z))
                    notes.push_back(z);

            if (! notes.empty())
            {
                const float sliceHeight = cell.getHeight() / static_cast<float>(notes.size());
                for (size_t i = 0; i < notes.size(); ++i)
                {
                    auto slice = juce::Rectangle<float>(cell.getX() + 1.0f,
                                                        cell.getBottom() - sliceHeight * static_cast<float>(i + 1) + 1.0f,
                                                        cell.getWidth() - 2.0f,
                                                        sliceHeight - 2.0f);
                    auto colour = colourForHeight(notes[i]).interpolatedWith(juce::Colours::white, 0.08f);
                    if (! activeCell)
                        colour = colour.interpolatedWith(juce::Colour::greyLevel(colour.getPerceivedBrightness()), 0.78f)
                                       .withMultipliedAlpha(0.4f);
                    g.setColour(colour);
                    g.fillRect(slice);
                }
            }

            g.setColour(activeCell ? juce::Colour::fromRGBA(74, 102, 190, 52)
                                   : juce::Colour::fromRGBA(90, 90, 104, 32));
            g.drawRect(cell, 1.0f);
        }
    }

    const auto regionLocal = juce::Rectangle<float>(board.getX() + static_cast<float>(activeRegion.getX() - x0) * tileSize,
                                                    board.getY() + static_cast<float>(activeRegion.getY() - y0) * tileSize,
                                                    static_cast<float>(activeRegion.getWidth()) * tileSize,
                                                    static_cast<float>(activeRegion.getHeight()) * tileSize);
    g.setColour(juce::Colour::fromRGBA(114, 226, 255, 160));
    g.drawRoundedRectangle(regionLocal.expanded(2.0f), 8.0f, 2.0f);

    for (const auto& snake : performanceSnakes)
    {
        if (snake.body.empty())
            continue;

        juce::Path spine;
        std::vector<juce::Point<float>> centres;
        centres.reserve(snake.body.size());

        for (const auto& segment : snake.body)
        {
            auto cell = juce::Rectangle<float>(board.getX() + static_cast<float>(segment.x - x0) * tileSize,
                                               board.getY() + static_cast<float>(segment.y - y0) * tileSize,
                                               tileSize,
                                               tileSize);
            centres.push_back(cell.getCentre());
        }

        if (! centres.empty())
        {
            spine.startNewSubPath(centres.front());
            for (size_t i = 1; i < centres.size(); ++i)
                spine.lineTo(centres[i]);

            g.setColour(snake.colour.withAlpha(0.18f));
            g.strokePath(spine, juce::PathStrokeType(tileSize * 0.34f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            g.setColour(snake.colour.withAlpha(0.78f));
            g.strokePath(spine, juce::PathStrokeType(tileSize * 0.16f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }

        for (size_t i = 0; i < snake.body.size(); ++i)
        {
            const auto segment = snake.body[i];
            auto cell = juce::Rectangle<float>(board.getX() + static_cast<float>(segment.x - x0) * tileSize,
                                               board.getY() + static_cast<float>(segment.y - y0) * tileSize,
                                               tileSize,
                                               tileSize);
            auto orb = cell.reduced(i == 0 ? tileSize * 0.18f : tileSize * 0.24f);
            auto segmentColour = snake.colour;
            if (i > 0)
                segmentColour = segmentColour.darker(static_cast<float>(i) * 0.05f);

            g.setColour(segmentColour.withAlpha(i == 0 ? 0.22f : 0.12f));
            g.fillEllipse(orb.expanded(tileSize * 0.12f));
            g.setColour(segmentColour.withAlpha(i == 0 ? 0.98f : 0.84f));
            g.fillEllipse(orb);
            g.setColour(juce::Colours::white.withAlpha(i == 0 ? 0.78f : 0.26f));
            g.drawEllipse(orb, i == 0 ? 1.8f : 1.0f);

            if (i == 0)
            {
                const auto centre = orb.getCentre();
                const juce::Point<float> dir(static_cast<float>(snake.direction.x), static_cast<float>(snake.direction.y));
                const juce::Point<float> perp(-dir.y, dir.x);
                const auto eyeOffset = perp * (orb.getWidth() * 0.14f);
                const auto eyeForward = dir * (orb.getWidth() * 0.12f);
                const float eyeSize = juce::jmax(2.0f, orb.getWidth() * 0.10f);

                g.setColour(juce::Colour::fromRGBA(6, 10, 24, 220));
                g.fillEllipse(juce::Rectangle<float>(eyeSize, eyeSize).withCentre(centre + eyeForward + eyeOffset));
                g.fillEllipse(juce::Rectangle<float>(eyeSize, eyeSize).withCentre(centre + eyeForward - eyeOffset));
            }
        }
    }

    for (const auto& disc : performanceDiscs)
    {
        auto cell = juce::Rectangle<float>(board.getX() + static_cast<float>(disc.cell.x - x0) * tileSize,
                                           board.getY() + static_cast<float>(disc.cell.y - y0) * tileSize,
                                           tileSize,
                                           tileSize).reduced(tileSize * 0.16f);
        const auto centre = cell.getCentre();
        const float radius = cell.getWidth() * 0.36f;

        g.setColour(juce::Colour::fromRGBA(16, 26, 58, 230));
        g.fillEllipse(cell);
        g.setColour(juce::Colour::fromRGBA(140, 232, 255, 220));
        g.drawEllipse(cell, 1.6f);

        juce::Path arrow;
        const juce::Point<float> dir(static_cast<float>(disc.direction.x), static_cast<float>(disc.direction.y));
        const auto tip = centre + dir * radius;
        const auto base = centre - dir * (radius * 0.45f);
        const juce::Point<float> perp(-dir.y, dir.x);
        arrow.startNewSubPath(base + perp * (radius * 0.24f));
        arrow.lineTo(tip);
        arrow.lineTo(base - perp * (radius * 0.24f));
        arrow.closeSubPath();
        g.fillPath(arrow);
    }

    if (performanceHoverCell.has_value())
    {
        auto hover = juce::Rectangle<float>(board.getX() + static_cast<float>(performanceHoverCell->x - x0) * tileSize,
                                            board.getY() + static_cast<float>(performanceHoverCell->y - y0) * tileSize,
                                            tileSize,
                                            tileSize).reduced(tileSize * 0.08f);
        g.setColour(juce::Colour::fromRGBA(110, 240, 255, 70));
        g.fillRoundedRectangle(hover, 6.0f);
        g.setColour(juce::Colour::fromRGBA(160, 248, 255, 220));
        g.drawRoundedRectangle(hover, 6.0f, 2.0f);
    }

    g.setColour(juce::Colour::fromRGBA(126, 224, 255, 180));
    g.drawRoundedRectangle(board.expanded(4.0f), 10.0f, 2.0f);
}

void MainComponent::drawWireframeGrid(juce::Graphics& g, juce::Rectangle<float> area)
{
    if (isolatedSlab.isValid() && performanceMode)
    {
        drawPerformanceView(g, area);
        return;
    }

    const int minXCoord = boardInset;
    const int minYCoord = boardInset;
    const int maxXCoord = gridWidth - boardInset;
    const int maxYCoord = gridDepth - boardInset;
    const auto tileWidth = baseTileWidth * camera.zoom;
    const auto tileHeight = baseTileHeight * camera.zoom;
    const auto lineStep = gridLineStep();

    juce::Path floorPlane;
    const int centreX = minXCoord + (maxXCoord - minXCoord) / 2;
    const int centreY = minYCoord + (maxYCoord - minYCoord) / 2;
    const int floorCount = layoutMode == LayoutMode::FourIslandsFourFloors ? 4 : 1;
    const auto activeSlab = isolatedSlab.isValid() ? isolatedSlab : hoveredSlab;

    auto addFloorQuad = [&] (int x0, int y0, int x1, int y1)
    {
        const auto anchorX = x0;
        const auto anchorY = y0;
        const auto offset = islandOffsetForCell(anchorX, anchorY);
        for (int floorIndex = 0; floorIndex < floorCount; ++floorIndex)
        {
            const int quadrant = quadrantForCell(x0, y0);
            const SlabSelection slab { quadrant, floorIndex };
            if (isolatedSlab.isValid() && (slab.quadrant != isolatedSlab.quadrant || slab.floor != isolatedSlab.floor))
                continue;

            const int floorZ = layoutMode == LayoutMode::FourIslandsFourFloors
                                 ? floorIndex * (floorBandHeight + floorBandGap)
                                 : 0;
            const auto slabLift = hoverLiftForSlab(slab);
            const auto p00 = projectPoint(x0 + offset.x, y0 + offset.y, floorZ, area);
            const auto p10 = projectPoint(x1 + offset.x, y0 + offset.y, floorZ, area);
            const auto p11 = projectPoint(x1 + offset.x, y1 + offset.y, floorZ, area);
            const auto p01 = projectPoint(x0 + offset.x, y1 + offset.y, floorZ, area);
            floorPlane.startNewSubPath(p00);
            floorPlane.lineTo(p10);
            floorPlane.lineTo(p11);
            floorPlane.lineTo(p01);
            floorPlane.closeSubPath();
            auto liftedFloor = floorPlane;
            liftedFloor.applyTransform(juce::AffineTransform::translation(0.0f, -slabLift));

            if (layoutMode == LayoutMode::FourIslandsFourFloors && activeSlab.isValid()
                && (slab.quadrant != activeSlab.quadrant || slab.floor != activeSlab.floor))
            {
                g.setColour(juce::Colour::fromRGBA(54, 56, 62, 222));
            }
            else
            {
                g.setColour(juce::Colour::fromRGBA(9, 14, 36, 242));
            }
            g.fillPath(liftedFloor);
            g.setColour(juce::Colour::fromRGBA(59, 90, 188, 52));
            g.strokePath(liftedFloor, juce::PathStrokeType(2.2f));
            floorPlane.clear();
        }
    };

    if (layoutMode != LayoutMode::OneBoard)
    {
        addFloorQuad(minXCoord, minYCoord, centreX, centreY);
        addFloorQuad(centreX, minYCoord, maxXCoord, centreY);
        addFloorQuad(minXCoord, centreY, centreX, maxYCoord);
        addFloorQuad(centreX, centreY, maxXCoord, maxYCoord);
    }
    else
    {
        addFloorQuad(minXCoord, minYCoord, maxXCoord, maxYCoord);
    }

    juce::ColourGradient floorGlow(juce::Colour::fromRGBA(30, 52, 128, 90), area.getCentreX(), area.getCentreY() + 160.0f,
                                   juce::Colour::fromRGBA(13, 18, 49, 0), area.getCentreX(), area.getBottom(), true);
    g.setGradientFill(floorGlow);
    g.fillEllipse(area.getCentreX() - area.getWidth() * 0.43f,
                  area.getCentreY() - 20.0f,
                  area.getWidth() * 0.86f,
                  area.getHeight() * 0.78f);

    juce::Path shadowPath;
    for (const auto& voxel : filledVoxels)
    {
        const int x = static_cast<int>(voxel.x);
        const int y = static_cast<int>(voxel.y);
        if (x < minXCoord || x >= maxXCoord || y < minYCoord || y >= maxYCoord)
            continue;
        if (isolatedSlab.isValid() && ! voxelInSelectedSlab(x, y, static_cast<int>(voxel.z), isolatedSlab))
            continue;

        const int baseZ = renderBaseZForLayer(0);
        const auto a0 = projectCellCorner(x,     y,     baseZ, x, y, area);
        const auto b0 = projectCellCorner(x + 1, y,     baseZ, x, y, area);
        const auto c0 = projectCellCorner(x + 1, y + 1, baseZ, x, y, area);
        const auto d0 = projectCellCorner(x,     y + 1, baseZ, x, y, area);

        juce::Path cellShadow;
        cellShadow.startNewSubPath(a0);
        cellShadow.lineTo(b0);
        cellShadow.lineTo(c0);
        cellShadow.lineTo(d0);
        cellShadow.closeSubPath();
        shadowPath.addPath(cellShadow);
    }
    g.setColour(juce::Colour::fromRGBA(0, 0, 0, 20));
    g.fillPath(shadowPath);

    struct Direction2D
    {
        int dx = 0;
        int dy = 0;
    };

    const std::array<Direction2D, 4> cardinalDirections {{
        { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 }
    }};

    Direction2D leftFaceDirection;
    Direction2D rightFaceDirection;
    bool hasLeftFaceDirection = false;
    bool hasRightFaceDirection = false;
    const auto rotatedOrigin = rotateXY(0, 0);

    for (const auto& dir : cardinalDirections)
    {
        const auto rotatedNeighbour = rotateXY(dir.dx, dir.dy);
        const int rx = rotatedNeighbour.x - rotatedOrigin.x;
        const int ry = rotatedNeighbour.y - rotatedOrigin.y;
        const float screenDx = (rx - ry) * tileWidth * 0.5f;
        const float screenDy = (rx + ry) * tileHeight * 0.5f;

        if (screenDy <= 0.0f)
            continue;

        if (screenDx < 0.0f)
        {
            leftFaceDirection = dir;
            hasLeftFaceDirection = true;
        }
        else if (screenDx > 0.0f)
        {
            rightFaceDirection = dir;
            hasRightFaceDirection = true;
        }
    }

    std::vector<FilledVoxel> renderVoxels;
    renderVoxels.reserve(filledVoxels.size());
    for (const auto& voxel : filledVoxels)
    {
        const int x = static_cast<int>(voxel.x);
        const int y = static_cast<int>(voxel.y);
        if (x < minXCoord || x >= maxXCoord || y < minYCoord || y >= maxYCoord)
            continue;
        if (isolatedSlab.isValid() && ! voxelInSelectedSlab(x, y, static_cast<int>(voxel.z), isolatedSlab))
            continue;
        renderVoxels.push_back(voxel);
    }

    std::sort(renderVoxels.begin(), renderVoxels.end(),
              [this] (const FilledVoxel& a, const FilledVoxel& b)
              {
                  const auto ra = rotateXY(static_cast<int>(a.x), static_cast<int>(a.y));
                  const auto rb = rotateXY(static_cast<int>(b.x), static_cast<int>(b.y));
                  const int depthA = ra.x + ra.y + static_cast<int>(a.z) * 2;
                  const int depthB = rb.x + rb.y + static_cast<int>(b.z) * 2;
                  if (depthA != depthB)
                      return depthA < depthB;
                  if (a.z != b.z)
                      return a.z < b.z;
                  if (ra.y != rb.y)
                      return ra.y < rb.y;
                  return ra.x < rb.x;
              });

    for (const auto& voxel : renderVoxels)
    {
        const int x = static_cast<int>(voxel.x);
        const int y = static_cast<int>(voxel.y);
        const int z = static_cast<int>(voxel.z);
        const int baseZ = renderBaseZForLayer(z);
        const auto colour = colourForHeight(z);
        const SlabSelection voxelSlab { quadrantForCell(x, y), z / floorBandHeight };
        const auto slabLift = hoverLiftForSlab(voxelSlab);
        const bool showTop = (z == gridHeight - 1)
                          || (layoutMode == LayoutMode::FourIslandsFourFloors && (z % floorBandHeight) == floorBandHeight - 1)
                          || ! hasVoxel(x, y, z + 1);
        const bool showLeft = hasLeftFaceDirection ? ! hasVoxel(x + leftFaceDirection.dx, y + leftFaceDirection.dy, z) : true;
        const bool showRight = hasRightFaceDirection ? ! hasVoxel(x + rightFaceDirection.dx, y + rightFaceDirection.dy, z) : true;

        const auto aTop = projectCellCorner(x,     y,     baseZ + 1, x, y, area);
        const auto bTop = projectCellCorner(x + 1, y,     baseZ + 1, x, y, area);
        const auto cTop = projectCellCorner(x + 1, y + 1, baseZ + 1, x, y, area);
        const auto dTop = projectCellCorner(x,     y + 1, baseZ + 1, x, y, area);
        const auto aBottom = projectCellCorner(x,     y,     baseZ, x, y, area);
        const auto bBottom = projectCellCorner(x + 1, y,     baseZ, x, y, area);
        const auto cBottom = projectCellCorner(x + 1, y + 1, baseZ, x, y, area);
        const auto dBottom = projectCellCorner(x,     y + 1, baseZ, x, y, area);

        juce::Path topFace;
        if (showTop)
        {
            topFace.startNewSubPath(aTop);
            topFace.lineTo(bTop);
            topFace.lineTo(cTop);
            topFace.lineTo(dTop);
            topFace.closeSubPath();
        }

        juce::Path leftFace;
        if (showLeft)
        {
            if (leftFaceDirection.dx == -1 && leftFaceDirection.dy == 0)
            {
                leftFace.startNewSubPath(aTop);
                leftFace.lineTo(dTop);
                leftFace.lineTo(dBottom);
                leftFace.lineTo(aBottom);
            }
            else if (leftFaceDirection.dx == 1 && leftFaceDirection.dy == 0)
            {
                leftFace.startNewSubPath(bTop);
                leftFace.lineTo(cTop);
                leftFace.lineTo(cBottom);
                leftFace.lineTo(bBottom);
            }
            else if (leftFaceDirection.dx == 0 && leftFaceDirection.dy == -1)
            {
                leftFace.startNewSubPath(aTop);
                leftFace.lineTo(bTop);
                leftFace.lineTo(bBottom);
                leftFace.lineTo(aBottom);
            }
            else
            {
                leftFace.startNewSubPath(dTop);
                leftFace.lineTo(cTop);
                leftFace.lineTo(cBottom);
                leftFace.lineTo(dBottom);
            }
            leftFace.closeSubPath();
        }

        juce::Path rightFace;
        if (showRight)
        {
            if (rightFaceDirection.dx == -1 && rightFaceDirection.dy == 0)
            {
                rightFace.startNewSubPath(aTop);
                rightFace.lineTo(dTop);
                rightFace.lineTo(dBottom);
                rightFace.lineTo(aBottom);
            }
            else if (rightFaceDirection.dx == 1 && rightFaceDirection.dy == 0)
            {
                rightFace.startNewSubPath(bTop);
                rightFace.lineTo(cTop);
                rightFace.lineTo(cBottom);
                rightFace.lineTo(bBottom);
            }
            else if (rightFaceDirection.dx == 0 && rightFaceDirection.dy == -1)
            {
                rightFace.startNewSubPath(aTop);
                rightFace.lineTo(bTop);
                rightFace.lineTo(bBottom);
                rightFace.lineTo(aBottom);
            }
            else
            {
                rightFace.startNewSubPath(dTop);
                rightFace.lineTo(cTop);
                rightFace.lineTo(cBottom);
                rightFace.lineTo(dBottom);
            }
            rightFace.closeSubPath();
        }

        juce::Path edgePath;
        if (showTop)
        {
            edgePath.startNewSubPath(aTop);
            edgePath.lineTo(bTop);
            edgePath.lineTo(cTop);
            edgePath.lineTo(dTop);
            edgePath.closeSubPath();
        }
        if (showLeft)
        {
            if (leftFaceDirection.dx == -1 && leftFaceDirection.dy == 0)
            {
                edgePath.startNewSubPath(aTop);
                edgePath.lineTo(aBottom);
                edgePath.lineTo(dBottom);
                edgePath.lineTo(dTop);
            }
            else if (leftFaceDirection.dx == 1 && leftFaceDirection.dy == 0)
            {
                edgePath.startNewSubPath(bTop);
                edgePath.lineTo(bBottom);
                edgePath.lineTo(cBottom);
                edgePath.lineTo(cTop);
            }
            else if (leftFaceDirection.dx == 0 && leftFaceDirection.dy == -1)
            {
                edgePath.startNewSubPath(aTop);
                edgePath.lineTo(aBottom);
                edgePath.lineTo(bBottom);
                edgePath.lineTo(bTop);
            }
            else
            {
                edgePath.startNewSubPath(dTop);
                edgePath.lineTo(dBottom);
                edgePath.lineTo(cBottom);
                edgePath.lineTo(cTop);
            }
        }
        if (showRight)
        {
            if (rightFaceDirection.dx == -1 && rightFaceDirection.dy == 0)
            {
                edgePath.startNewSubPath(aTop);
                edgePath.lineTo(aBottom);
                edgePath.lineTo(dBottom);
                edgePath.lineTo(dTop);
            }
            else if (rightFaceDirection.dx == 1 && rightFaceDirection.dy == 0)
            {
                edgePath.startNewSubPath(bTop);
                edgePath.lineTo(bBottom);
                edgePath.lineTo(cBottom);
                edgePath.lineTo(cTop);
            }
            else if (rightFaceDirection.dx == 0 && rightFaceDirection.dy == -1)
            {
                edgePath.startNewSubPath(aTop);
                edgePath.lineTo(aBottom);
                edgePath.lineTo(bBottom);
                edgePath.lineTo(bTop);
            }
            else
            {
                edgePath.startNewSubPath(dTop);
                edgePath.lineTo(dBottom);
                edgePath.lineTo(cBottom);
                edgePath.lineTo(cTop);
            }
        }

        const auto liftTransform = juce::AffineTransform::translation(0.0f, -slabLift);
        if (showTop)
            topFace.applyTransform(liftTransform);
        if (showLeft)
            leftFace.applyTransform(liftTransform);
        if (showRight)
            rightFace.applyTransform(liftTransform);
        edgePath.applyTransform(liftTransform);

        if (showTop)
        {
            g.setColour(displayColourForVoxel(x, y, z, colour.interpolatedWith(juce::Colours::white, 0.15f)));
            g.fillPath(topFace);
        }
        if (showLeft)
        {
            g.setColour(displayColourForVoxel(x, y, z, colour.darker(0.12f)));
            g.fillPath(leftFace);
        }
        if (showRight)
        {
            g.setColour(displayColourForVoxel(x, y, z, colour.darker(0.28f)));
            g.fillPath(rightFace);
        }
        g.setColour(juce::Colour::fromRGBA(8, 10, 20, 168));
        g.strokePath(edgePath, juce::PathStrokeType(1.0f));
    }

    juce::Path floorGrid;

    auto addFloorGrid = [&] (int x0, int y0, int x1, int y1)
    {
        const auto offset = islandOffsetForCell(x0, y0);
        for (int floorIndex = 0; floorIndex < floorCount; ++floorIndex)
        {
            const int quadrant = quadrantForCell(x0, y0);
            const SlabSelection slab { quadrant, floorIndex };
            if (isolatedSlab.isValid() && (slab.quadrant != isolatedSlab.quadrant || slab.floor != isolatedSlab.floor))
                continue;

            const int floorZ = layoutMode == LayoutMode::FourIslandsFourFloors
                                 ? floorIndex * (floorBandHeight + floorBandGap)
                                 : 0;
            const auto slabLift = hoverLiftForSlab(slab);
            for (int x = x0; x <= x1; x += lineStep)
            {
                auto start = projectPoint(x + offset.x, y0 + offset.y, floorZ, area);
                auto end = projectPoint(x + offset.x, y1 + offset.y, floorZ, area);
                floorGrid.startNewSubPath(start);
                floorGrid.lineTo(end);
            }

            for (int y = y0; y <= y1; y += lineStep)
            {
                auto start = projectPoint(x0 + offset.x, y + offset.y, floorZ, area);
                auto end = projectPoint(x1 + offset.x, y + offset.y, floorZ, area);
                floorGrid.startNewSubPath(start);
                floorGrid.lineTo(end);
            }

            floorGrid.applyTransform(juce::AffineTransform::translation(0.0f, -slabLift));

            if (layoutMode == LayoutMode::FourIslandsFourFloors && activeSlab.isValid()
                && (slab.quadrant != activeSlab.quadrant || slab.floor != activeSlab.floor))
            {
                g.setColour(juce::Colour::fromRGBA(96, 96, 104, 24));
            }
            else
            {
                g.setColour(juce::Colour::fromRGBA(95, 122, 214, 34));
            }
            g.strokePath(floorGrid, juce::PathStrokeType(1.1f));
            floorGrid.clear();
        }
    };

    if (layoutMode != LayoutMode::OneBoard)
    {
        addFloorGrid(minXCoord, minYCoord, centreX, centreY);
        addFloorGrid(centreX, minYCoord, maxXCoord, centreY);
        addFloorGrid(minXCoord, centreY, centreX, maxYCoord);
        addFloorGrid(centreX, centreY, maxXCoord, maxYCoord);
    }
    else
    {
        addFloorGrid(minXCoord, minYCoord, maxXCoord, maxYCoord);
    }

    if (layoutMode == LayoutMode::FourIslandsFourFloors && hoveredSlab.isValid() && ! isolatedSlab.isValid())
    {
        auto highlightPath = slabPath(hoveredSlab, area);
        highlightPath.applyTransform(juce::AffineTransform::translation(0.0f, -hoverLiftForSlab(hoveredSlab)));
        const auto slabBounds = highlightPath.getBounds();
        const auto slabCentre = slabBounds.getCentre();
        const auto outlineScale = 1.035f;
        auto expandedHighlightPath = highlightPath;
        expandedHighlightPath.applyTransform(juce::AffineTransform::translation(-slabCentre.x, -slabCentre.y)
                                                 .scaled(outlineScale, outlineScale)
                                                 .translated(slabCentre.x, slabCentre.y));
        const auto pulsePhase = static_cast<float>(std::sin(juce::Time::getMillisecondCounterHiRes() * 0.006));
        const auto pulse = 0.5f + 0.5f * pulsePhase;

        g.setColour(juce::Colour::fromFloatRGBA(0.31f, 0.93f, 1.0f, 0.12f + 0.11f * pulse));
        g.fillPath(expandedHighlightPath);
        g.setColour(juce::Colour::fromFloatRGBA(0.31f, 0.93f, 1.0f, 0.10f + 0.08f * pulse));
        g.strokePath(expandedHighlightPath, juce::PathStrokeType(11.0f + 3.0f * pulse));
        g.setColour(juce::Colour::fromFloatRGBA(0.35f, 0.96f, 1.0f, 0.92f));
        g.strokePath(expandedHighlightPath, juce::PathStrokeType(4.8f));
        g.setColour(juce::Colour::fromRGBA(255, 255, 255, 230));
        g.strokePath(highlightPath, juce::PathStrokeType(1.6f));

        auto labelBounds = juce::Rectangle<float>(132.0f, 24.0f)
                               .withCentre({ slabBounds.getCentreX(), slabBounds.getY() - 16.0f });
        labelBounds = labelBounds.withY(juce::jmax(area.getY() + 8.0f, labelBounds.getY()));

        g.setColour(juce::Colour::fromRGBA(8, 14, 34, 226));
        g.fillRoundedRectangle(labelBounds, 6.0f);
        g.setColour(juce::Colour::fromRGBA(90, 245, 255, 168));
        g.drawRoundedRectangle(labelBounds, 6.0f, 1.4f);
        g.setColour(juce::Colours::white);
        g.setFont(juce::FontOptions(13.0f));
        g.drawFittedText(labelForSlab(hoveredSlab), labelBounds.toNearestInt(), juce::Justification::centred, 1);
    }

    if (isolatedSlab.isValid() && editCursor.active)
    {
        auto indicator = cursorPath(editCursor, area);
        const auto pulsePhase = static_cast<float>(std::sin(juce::Time::getMillisecondCounterHiRes() * 0.008));
        const auto pulse = 0.5f + 0.5f * pulsePhase;
        const auto indicatorBounds = indicator.getBounds().expanded(10.0f + 4.0f * pulse);
        const int cursorBaseZ = renderBaseZForLayer(editCursor.z);
        const int floorBaseZ = renderBaseZForLayer(isolatedSlab.floor * floorBandHeight);

        juce::Path floorFootprint;
        const auto fa = projectCellCorner(editCursor.x,     editCursor.y,     floorBaseZ, editCursor.x, editCursor.y, area);
        const auto fb = projectCellCorner(editCursor.x + 1, editCursor.y,     floorBaseZ, editCursor.x, editCursor.y, area);
        const auto fc = projectCellCorner(editCursor.x + 1, editCursor.y + 1, floorBaseZ, editCursor.x, editCursor.y, area);
        const auto fd = projectCellCorner(editCursor.x,     editCursor.y + 1, floorBaseZ, editCursor.x, editCursor.y, area);
        floorFootprint.startNewSubPath(fa);
        floorFootprint.lineTo(fb);
        floorFootprint.lineTo(fc);
        floorFootprint.lineTo(fd);
        floorFootprint.closeSubPath();

        juce::Path heightBeam;
        const auto ta = projectCellCorner(editCursor.x,     editCursor.y,     cursorBaseZ + 1, editCursor.x, editCursor.y, area);
        const auto tb = projectCellCorner(editCursor.x + 1, editCursor.y,     cursorBaseZ + 1, editCursor.x, editCursor.y, area);
        const auto tc = projectCellCorner(editCursor.x + 1, editCursor.y + 1, cursorBaseZ + 1, editCursor.x, editCursor.y, area);
        const auto td = projectCellCorner(editCursor.x,     editCursor.y + 1, cursorBaseZ + 1, editCursor.x, editCursor.y, area);
        const auto topCentre = juce::Point<float>((ta.x + tb.x + tc.x + td.x) * 0.25f,
                                                  (ta.y + tb.y + tc.y + td.y) * 0.25f);
        const auto floorCentre = juce::Point<float>((fa.x + fb.x + fc.x + fd.x) * 0.25f,
                                                    (fa.y + fb.y + fc.y + fd.y) * 0.25f);
        heightBeam.startNewSubPath(floorCentre);
        heightBeam.lineTo(topCentre);

        g.setColour(juce::Colour::fromFloatRGBA(0.16f, 0.95f, 1.0f, 0.16f));
        g.fillPath(floorFootprint);
        g.setColour(juce::Colour::fromFloatRGBA(0.55f, 0.97f, 1.0f, 0.72f));
        g.strokePath(floorFootprint, juce::PathStrokeType(2.0f));

        g.setColour(juce::Colour::fromFloatRGBA(0.22f, 0.96f, 1.0f, 0.28f + 0.16f * pulse));
        g.strokePath(heightBeam, juce::PathStrokeType(8.0f + 2.0f * pulse, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour(juce::Colour::fromFloatRGBA(1.0f, 1.0f, 1.0f, 0.92f));
        g.strokePath(heightBeam, juce::PathStrokeType(2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        g.setColour(juce::Colour::fromFloatRGBA(0.14f, 0.96f, 1.0f, 0.12f + 0.10f * pulse));
        g.fillEllipse(indicatorBounds);
        g.setColour(juce::Colour::fromFloatRGBA(1.0f, 0.62f, 0.14f, 0.20f));
        g.fillPath(indicator);
        g.setColour(juce::Colour::fromFloatRGBA(0.10f, 0.95f, 1.0f, 0.96f));
        g.strokePath(indicator, juce::PathStrokeType(3.2f + pulse * 1.2f));
        g.setColour(juce::Colour::fromFloatRGBA(1.0f, 1.0f, 1.0f, 0.95f));
        g.strokePath(indicator, juce::PathStrokeType(1.2f));

        auto zLabelBounds = juce::Rectangle<float>(52.0f, 22.0f)
                                .withCentre({ topCentre.x + 46.0f, topCentre.y - 12.0f });
        zLabelBounds = zLabelBounds.withY(juce::jmax(area.getY() + 8.0f, zLabelBounds.getY()));
        g.setColour(juce::Colour::fromRGBA(8, 14, 34, 232));
        g.fillRoundedRectangle(zLabelBounds, 6.0f);
        g.setColour(juce::Colour::fromFloatRGBA(0.35f, 0.96f, 1.0f, 0.88f));
        g.drawRoundedRectangle(zLabelBounds, 6.0f, 1.4f);
        g.setColour(juce::Colours::white);
        g.setFont(juce::FontOptions(12.5f));
        g.drawFittedText("z " + juce::String(editCursor.z), zLabelBounds.toNearestInt(), juce::Justification::centred, 1);
    }
}

void MainComponent::drawHud(juce::Graphics& g, juce::Rectangle<float> area)
{
    g.setColour(juce::Colours::white);
    g.setFont(juce::FontOptions(17.0f));
    const auto modeText = isolatedSlab.isValid()
                            ? (performanceMode
                                ? "PERFORMANCE VIEW | Enter edit | Z arena | Arrows disc dir | Y disc | 0-8 snakes | T synth | B drums | K key | L scale | Esc back"
                                : "ISOLATED EDIT | Enter performance | WASD Pan | Wheel Zoom | Mouse snap | Click place | Right-click remove | Arrows move | [ ] height | T synth | B drums | K key | L scale | U quantize | Esc back")
                            : "BUILD MODE | WASD Pan | Wheel Zoom | Q/E Rotate | -/= Height | G View | R Randomise | T synth | B drums | K key | L scale | U quantize";
    g.drawText(modeText,
               area.removeFromTop(24.0f).reduced(10.0f, 0.0f).toNearestInt(),
               juce::Justification::centredLeft);

    g.setFont(juce::FontOptions(14.0f));
    juce::String detailText = "Random voxel field 128x128x48   z=0 no note   z=1 "
                            + noteNameForHeight(1)
                            + " chromatic upward   Filled " + juce::String(voxelCount)
                            + "   Synth " + synthName()
                            + "   Drums " + drumModeName()
                            + "   Key " + keyName()
                            + " " + scaleName();

    if (isolatedSlab.isValid() && performanceMode)
    {
        const auto regionName = performanceRegionMode == 0 ? "Centre 50%"
                              : performanceRegionMode == 1 ? "Centre 75%"
                              : "Full";
        detailText = "Performance " + labelForSlab(isolatedSlab)
                   + "   Region " + juce::String(regionName)
                   + "   Snakes " + juce::String(static_cast<int>(performanceSnakes.size()))
                    + "   Discs " + juce::String(static_cast<int>(performanceDiscs.size()))
                   + "   Synth " + synthName()
                   + "   Drums " + drumModeName()
                   + "   Key " + keyName()
                   + " " + scaleName();
    }
    else if (isolatedSlab.isValid() && editCursor.active)
    {
        detailText = "Editing " + labelForSlab(isolatedSlab)
                   + "   Cursor x" + juce::String(editCursor.x)
                   + " y" + juce::String(editCursor.y)
                   + " z" + juce::String(editCursor.z)
                   + "   Note " + noteNameForHeight(editCursor.z)
                   + "   Key " + keyName()
                   + " " + scaleName();
    }

    g.drawFittedText(detailText,
                     area.reduced(10.0f, 0.0f).toNearestInt(),
                     juce::Justification::centredLeft, 2);
}

void MainComponent::drawBackdrop(juce::Graphics& g, juce::Rectangle<float>)
{
    g.fillAll(juce::Colour::fromRGB(13, 22, 68));
}

void MainComponent::rotateCamera(int direction)
{
    camera.rotation = ((camera.rotation + direction) % 4 + 4) % 4;
    repaint();
}

void MainComponent::panCamera(float dx, float dy)
{
    camera.panX += dx;
    camera.panY += dy;
    repaint();
}

void MainComponent::changeZoom(float factor)
{
    targetZoom = juce::jlimit(0.2f, 10.0f, targetZoom * factor);
    repaint();
}

void MainComponent::changeHeightScale(float delta)
{
    camera.heightScale = juce::jlimit(0.25f, 2.0f, camera.heightScale + delta);
    repaint();
}

juce::Point<int> MainComponent::rotateXY(int x, int y) const
{
    switch (camera.rotation)
    {
        case 1: return { y, gridWidth - x };
        case 2: return { gridWidth - x, gridDepth - y };
        case 3: return { gridDepth - y, x };
        default: break;
    }

    return { x, y };
}

int MainComponent::gridLineStep() const
{
    if (camera.zoom < 0.35f)
        return 16;
    if (camera.zoom < 0.65f)
        return 8;
    if (camera.zoom < 1.1f)
        return 4;
    return 2;
}

juce::Colour MainComponent::colourForHeight(int z) const
{
    if (z <= 0)
        return juce::Colour::fromRGB(72, 76, 88);

    switch ((z - 1) % 12)
    {
        case 0: return juce::Colour::fromRGB(247, 223, 67);  // C yellow
        case 1: return juce::Colour::fromRGB(240, 147, 49);  // C# orange
        case 2: return juce::Colour::fromRGB(78, 191, 104);  // D green
        case 3: return juce::Colour::fromRGB(61, 210, 201);  // D# cyan
        case 4: return juce::Colour::fromRGB(138, 93, 214);  // E violet
        case 5: return juce::Colour::fromRGB(229, 78, 196);  // F magenta
        case 6: return juce::Colour::fromRGB(232, 60, 56);   // F# red
        case 7: return juce::Colour::fromRGB(238, 100, 46);  // G orange-red
        case 8: return juce::Colour::fromRGB(214, 47, 47);   // G# red
        case 9: return juce::Colour::fromRGB(244, 155, 57);  // A orange
        case 10: return juce::Colour::fromRGB(248, 194, 70); // A# orange-yellow
        case 11: return juce::Colour::fromRGB(247, 173, 84); // B lighter orange
        default: break;
    }

    return juce::Colours::white;
}

juce::String MainComponent::noteNameForHeight(int z) const
{
    if (z <= 0)
        return "No note";

    static constexpr std::array<const char*, 12> names {
        "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"
    };

    const int midi = midiNoteForHeight(z);
    return names[static_cast<size_t>((midi % 12 + 12) % 12)];
}

juce::String MainComponent::keyName() const
{
    static constexpr std::array<const char*, 12> names {
        "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"
    };

    return names[static_cast<size_t>(keyRoot % 12)];
}

juce::String MainComponent::scaleName() const
{
    return scaleToString(scale);
}

juce::String MainComponent::synthName() const
{
    return synthToString(synthEngine);
}

juce::String MainComponent::drumModeName() const
{
    return drumModeToString(drumMode);
}
