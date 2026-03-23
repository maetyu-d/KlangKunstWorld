#include "MainComponent.h"

#include <algorithm>
#include <array>
#include <limits>
#include <optional>
#include <unordered_map>

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

juce::String snakeTriggerModeToString(MainComponent::SnakeTriggerMode mode)
{
    switch (mode)
    {
        case MainComponent::SnakeTriggerMode::headOnly: return "Head Only";
        case MainComponent::SnakeTriggerMode::wholeBody: return "Whole Body";
    }

    return "Head Only";
}

juce::String performanceAgentModeToString(MainComponent::PerformanceAgentMode mode)
{
    switch (mode)
    {
        case MainComponent::PerformanceAgentMode::snakes: return "Snakes";
        case MainComponent::PerformanceAgentMode::trains: return "Trains";
        case MainComponent::PerformanceAgentMode::orbiters: return "Orbiters";
        case MainComponent::PerformanceAgentMode::automata: return "Automata";
    }

    return "Snakes";
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

    drawBackdrop(g, bounds);

    if (isolatedSlab.isValid() && performanceMode)
    {
        auto content = bounds.reduced(18.0f);
        auto sidebar = content.removeFromLeft(288.0f);
        drawPerformanceSidebar(g, sidebar);
        drawWireframeGrid(g, content.reduced(10.0f, 0.0f));
        return;
    }

    auto hudArea = bounds.removeFromTop(112.0f);
    auto gridArea = bounds.reduced(18.0f);
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
            auto content = bounds.reduced(18.0f);
            content.removeFromLeft(288.0f);
            const auto nextCell = performanceCellAtPosition(event.position, content.reduced(10.0f, 0.0f));
            if (nextCell != performanceHoverCell)
            {
                performanceHoverCell = nextCell;
                repaint();
            }
            return;
        }

        auto bounds = getLocalBounds().toFloat();
        bounds.removeFromTop(112.0f);
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
    bounds.removeFromTop(112.0f);
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
    juce::Rectangle<float> gridArea;
    if (isolatedSlab.isValid() && performanceMode)
    {
        auto content = bounds.reduced(18.0f);
        content.removeFromLeft(288.0f);
        gridArea = content.reduced(10.0f, 0.0f);
    }
    else
    {
        bounds.removeFromTop(112.0f);
        gridArea = bounds.reduced(12.0f);
    }
    const auto slab = slabAtPosition(event.position, gridArea);

    if (isolatedSlab.isValid())
    {
        if (performanceMode)
        {
            const auto clickedCell = performanceCellAtPosition(event.position, gridArea);
            performanceHoverCell = clickedCell;

            if (clickedCell.has_value())
            {
                const auto cell = *clickedCell;
                if (performancePlacementMode == PerformancePlacementMode::placeDisc)
                {
                    auto existing = std::find_if(performanceDiscs.begin(), performanceDiscs.end(),
                                                 [cell] (const ReflectorDisc& disc) { return disc.cell == cell; });
                    if (existing != performanceDiscs.end())
                        existing->direction = performanceSelectedDirection;
                    else
                        performanceDiscs.push_back({ cell, performanceSelectedDirection });

                    performanceSelection = { PerformanceSelection::Kind::disc, cell };
                    performanceFlashes.push_back({ cell, juce::Colour::fromRGBA(255, 208, 112, 255), 1.0f, true });
                }
                else if (performancePlacementMode == PerformancePlacementMode::placeTrack)
                {
                    auto existing = std::find_if(performanceTracks.begin(), performanceTracks.end(),
                                                 [cell] (const TrackPiece& track) { return track.cell == cell; });
                    if (existing != performanceTracks.end())
                        existing->horizontal = performanceTrackHorizontal;
                    else
                        performanceTracks.push_back({ cell, performanceTrackHorizontal });

                    performanceSelection = { PerformanceSelection::Kind::track, cell };
                    performanceFlashes.push_back({ cell, juce::Colour::fromRGBA(120, 220, 255, 255), 0.9f, true });
                }
                else
                {
                    auto disc = std::find_if(performanceDiscs.begin(), performanceDiscs.end(),
                                             [cell] (const ReflectorDisc& item) { return item.cell == cell; });
                    if (disc != performanceDiscs.end())
                    {
                        performanceSelection = { PerformanceSelection::Kind::disc, cell };
                        performanceSelectedDirection = disc->direction;
                    }
                    else
                    {
                        auto track = std::find_if(performanceTracks.begin(), performanceTracks.end(),
                                                  [cell] (const TrackPiece& item) { return item.cell == cell; });
                        if (track != performanceTracks.end())
                        {
                            performanceSelection = { PerformanceSelection::Kind::track, cell };
                            performanceTrackHorizontal = track->horizontal;
                        }
                        else
                        {
                            performanceSelection = {};
                        }
                    }
                }
            }
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
        performanceDiscs.clear();
        performanceTracks.clear();
        performanceOrbitCenters.clear();
        performanceAutomataCells.clear();
        performanceFlashes.clear();
        performanceHoverCell.reset();
        performanceSelectedDirection = { 1, 0 };
        performanceTrackHorizontal = true;
        performancePlacementMode = PerformancePlacementMode::selectOnly;
        performanceSelection = {};
        performanceTick = 0;
        performanceBeatEnergy = 0.0f;
        applyPerformancePresetForSlab(slab);
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
        visualStepCounter.store(beatStepIndex, std::memory_order_relaxed);
        if (beatStepIndex % 16 == 0)
        {
            ++beatBarIndex;
            visualBarCounter.store(beatBarIndex, std::memory_order_relaxed);
        }
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
            if (performanceMode && performanceSnakes.empty() && performanceAutomataCells.empty())
                setPerformanceSnakeCount(performanceAgentCount);
            repaint();
            return true;
        }
        if (key == juce::KeyPress::escapeKey) { isolatedSlab = {}; hoveredSlab = {}; editCursor = {}; performanceMode = false; performanceRegionMode = 2; performanceSnakes.clear(); performanceDiscs.clear(); performanceTracks.clear(); performanceOrbitCenters.clear(); performanceAutomataCells.clear(); performanceFlashes.clear(); performanceHoverCell.reset(); performanceSelection = {}; performancePlacementMode = PerformancePlacementMode::selectOnly; repaint(); return true; }

        if (performanceMode)
        {
            if (key == juce::KeyPress('m'))
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
            if (key == juce::KeyPress('h'))
            {
                snakeTriggerMode = snakeTriggerMode == SnakeTriggerMode::headOnly
                                     ? SnakeTriggerMode::wholeBody
                                     : SnakeTriggerMode::headOnly;
                repaint();
                return true;
            }
            if (key == juce::KeyPress('n'))
            {
                performanceAgentMode = static_cast<PerformanceAgentMode>((static_cast<int>(performanceAgentMode) + 1) % 4);
                resetPerformanceAgents();
                repaint();
                return true;
            }
            if (key == juce::KeyPress(',')) { bpm = juce::jlimit(60.0, 220.0, bpm - 2.0); repaint(); return true; }
            if (key == juce::KeyPress('.')) { bpm = juce::jlimit(60.0, 220.0, bpm + 2.0); repaint(); return true; }
            if (key == juce::KeyPress::leftKey) { performanceSelectedDirection = { -1, 0 }; repaint(); return true; }
            if (key == juce::KeyPress::rightKey) { performanceSelectedDirection = { 1, 0 }; repaint(); return true; }
            if (key == juce::KeyPress::upKey) { performanceSelectedDirection = { 0, -1 }; repaint(); return true; }
            if (key == juce::KeyPress::downKey) { performanceSelectedDirection = { 0, 1 }; repaint(); return true; }
            if (key == juce::KeyPress('y'))
            {
                if (performancePlacementMode == PerformancePlacementMode::placeDisc)
                {
                    auto it = std::find(snakeDirections.begin(), snakeDirections.end(), performanceSelectedDirection);
                    size_t index = it == snakeDirections.end() ? 0u : static_cast<size_t>(std::distance(snakeDirections.begin(), it));
                    index = (index + 1u) % snakeDirections.size();
                    performanceSelectedDirection = snakeDirections[index];
                }
                performancePlacementMode = PerformancePlacementMode::placeDisc;
                performanceSelection = {};
                repaint();
                return true;
            }
            if (key == juce::KeyPress('t'))
            {
                if (performancePlacementMode == PerformancePlacementMode::placeTrack)
                    performanceTrackHorizontal = ! performanceTrackHorizontal;
                performancePlacementMode = PerformancePlacementMode::placeTrack;
                performanceSelection = {};
                repaint();
                return true;
            }
            if (key == juce::KeyPress('i'))
            {
                if (performanceHoverCell.has_value())
                {
                    const auto cell = *performanceHoverCell;
                    auto it = std::find(performanceOrbitCenters.begin(), performanceOrbitCenters.end(), cell);
                    if (it != performanceOrbitCenters.end())
                        performanceOrbitCenters.erase(it);
                    else
                        performanceOrbitCenters.push_back(cell);
                    if (performanceAgentMode == PerformanceAgentMode::orbiters)
                        resetPerformanceAgents();
                    repaint();
                }
                return true;
            }
            if (key == juce::KeyPress('u'))
            {
                performancePlacementMode = PerformancePlacementMode::selectOnly;
                repaint();
                return true;
            }
            if (key == juce::KeyPress::deleteKey || key == juce::KeyPress::backspaceKey)
            {
                if (performanceSelection.kind == PerformanceSelection::Kind::disc)
                {
                    performanceDiscs.erase(std::remove_if(performanceDiscs.begin(), performanceDiscs.end(),
                                                          [&] (const ReflectorDisc& disc) { return disc.cell == performanceSelection.cell; }),
                                           performanceDiscs.end());
                    performanceSelection = {};
                    repaint();
                    return true;
                }
                if (performanceSelection.kind == PerformanceSelection::Kind::track)
                {
                    performanceTracks.erase(std::remove_if(performanceTracks.begin(), performanceTracks.end(),
                                                           [&] (const TrackPiece& track) { return track.cell == performanceSelection.cell; }),
                                            performanceTracks.end());
                    performanceSelection = {};
                    repaint();
                    return true;
                }
            }
            if (key == juce::KeyPress('z'))
            {
                performanceRegionMode = (performanceRegionMode + 1) % 3;
                resetPerformanceAgents();
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
        if (key == juce::KeyPress('c'))
        {
            clearIsolatedSlab();
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
    static int lastVisualStepCounter = 0;
    static int lastVisualBarCounter = 0;

    if (std::abs(delta) >= 0.001f)
    {
        camera.zoom += delta * 0.28f;
        needsRepaint = true;
    }
    else
    {
        camera.zoom = targetZoom;
    }

    visualBeatPulse *= 0.88f;
    visualBarPulse *= 0.93f;
    for (auto it = performanceFlashes.begin(); it != performanceFlashes.end();)
    {
        it->intensity *= it->discFlash ? 0.86f : 0.80f;
        if (it->intensity < 0.05f)
            it = performanceFlashes.erase(it);
        else
        {
            ++it;
            needsRepaint = true;
        }
    }
    const auto currentVisualStepCounter = visualStepCounter.load(std::memory_order_relaxed);
    const auto currentVisualBarCounter = visualBarCounter.load(std::memory_order_relaxed);
    if (currentVisualStepCounter != lastVisualStepCounter)
    {
        visualBeatPulse = 1.0f;
        lastVisualStepCounter = currentVisualStepCounter;
        needsRepaint = true;
    }
    if (currentVisualBarCounter != lastVisualBarCounter)
    {
        visualBarPulse = 1.0f;
        lastVisualBarCounter = currentVisualBarCounter;
        needsRepaint = true;
    }

    const float barsPerSecond = static_cast<float>(bpm / 60.0 / 4.0);
    visualBarSweep = std::fmod(visualBarSweep + barsPerSecond / 60.0f, 1.0f);

    const bool hasPerformanceAgents = ! performanceSnakes.empty() || ! performanceAutomataCells.empty();
    if (performanceMode && hasPerformanceAgents)
    {
        ++performanceTick;
        performanceBeatEnergy *= 0.986f;
        if (performanceTick % 8 == 0)
        {
            stepPerformanceAgents();
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
    layoutMode = LayoutMode::OneBoard;
    hoveredSlab = {};
    isolatedSlab = {};
    editCursor = {};
    performanceMode = false;
    performanceRegionMode = 2;
    performanceAgentMode = PerformanceAgentMode::snakes;
    performanceAgentCount = 1;
    performanceSnakes.clear();
    performanceDiscs.clear();
    performanceTracks.clear();
    performanceOrbitCenters.clear();
    performanceAutomataCells.clear();
    performanceFlashes.clear();
    performanceHoverCell.reset();
    performancePlacementMode = PerformancePlacementMode::selectOnly;
    performanceSelection = {};
    performanceTrackHorizontal = true;
    performanceTick = 0;
    beatStepAccumulator = 0.0;
    beatStepIndex = 0;
    beatBarIndex = 0;
    visualStepCounter.store(0, std::memory_order_relaxed);
    visualBarCounter.store(0, std::memory_order_relaxed);
    visualBeatPulse = 0.0f;
    visualBarPulse = 0.0f;
    visualBarSweep = 0.0f;
    performanceBeatEnergy = 0.0f;
    voxelCount = 0;
    filledVoxels.clear();
    std::fill(voxels.begin(), voxels.end(), 0u);
    filledVoxels.reserve(32000);

    static constexpr std::array<int, 7> scaleSteps { 0, 2, 3, 5, 7, 8, 10 };
    static constexpr std::array<std::array<int, 16>, 8> motifMelodies {{
        std::array<int, 16> { 0, 0, 2, 0, 3, 2, 4, 2, 5, 4, 3, 2, 0, 2, 4, 5 },
        std::array<int, 16> { 0, 2, 4, 5, 4, 2, 0, 2, 4, 7, 5, 4, 2, 0, 2, 4 },
        std::array<int, 16> { 0, 3, 5, 3, 7, 5, 3, 2, 0, 2, 3, 5, 7, 5, 3, 2 },
        std::array<int, 16> { 0, 4, 2, 5, 3, 7, 5, 4, 2, 0, 2, 4, 6, 4, 2, 1 },
        std::array<int, 16> { 0, 1, 3, 1, 5, 3, 6, 5, 3, 1, 0, 1, 3, 5, 6, 3 },
        std::array<int, 16> { 0, 2, 0, 4, 2, 5, 4, 7, 5, 4, 2, 0, 2, 4, 5, 7 },
        std::array<int, 16> { 0, 5, 4, 2, 0, 2, 3, 5, 7, 5, 3, 2, 0, 3, 5, 7 },
        std::array<int, 16> { 0, 2, 4, 2, 6, 4, 7, 6, 4, 2, 0, 2, 4, 6, 7, 9 }
    }};
    static constexpr std::array<std::array<int, 16>, 8> motifLanes {{
        std::array<int, 16> { 0, 0, 1, 0, 2, 1, 2, 1, 3, 2, 2, 1, 0, 1, 2, 3 },
        std::array<int, 16> { 0, 1, 2, 3, 2, 1, 0, 1, 2, 3, 3, 2, 1, 0, 1, 2 },
        std::array<int, 16> { 3, 2, 1, 2, 0, 1, 2, 3, 2, 1, 0, 1, 2, 1, 0, 1 },
        std::array<int, 16> { 1, 2, 1, 3, 2, 0, 1, 2, 3, 2, 1, 0, 1, 2, 1, 3 },
        std::array<int, 16> { 0, 2, 0, 2, 1, 3, 1, 3, 2, 0, 2, 0, 3, 1, 3, 1 },
        std::array<int, 16> { 3, 3, 2, 2, 1, 1, 0, 0, 1, 1, 2, 2, 3, 3, 2, 1 },
        std::array<int, 16> { 1, 0, 1, 2, 3, 2, 1, 0, 1, 2, 3, 2, 1, 0, 1, 2 },
        std::array<int, 16> { 2, 1, 0, 1, 2, 3, 2, 1, 0, 1, 2, 3, 2, 1, 0, 1 }
    }};
    static constexpr std::array<std::array<int, 16>, 8> motifDurations {{
        std::array<int, 16> { 2, 1, 1, 2, 1, 1, 2, 1, 2, 1, 1, 1, 2, 1, 1, 2 },
        std::array<int, 16> { 1, 1, 2, 1, 1, 2, 1, 1, 2, 1, 1, 2, 1, 1, 2, 2 },
        std::array<int, 16> { 2, 2, 1, 1, 2, 1, 1, 2, 1, 1, 2, 1, 1, 2, 1, 2 },
        std::array<int, 16> { 1, 2, 1, 2, 1, 1, 2, 1, 1, 2, 1, 1, 2, 1, 2, 1 },
        std::array<int, 16> { 2, 1, 2, 1, 1, 2, 1, 2, 1, 1, 2, 1, 2, 1, 1, 2 },
        std::array<int, 16> { 1, 1, 1, 2, 2, 1, 1, 2, 1, 1, 2, 1, 1, 2, 1, 2 },
        std::array<int, 16> { 2, 1, 1, 2, 1, 2, 1, 1, 2, 1, 2, 1, 1, 2, 1, 1 },
        std::array<int, 16> { 1, 2, 1, 1, 2, 1, 2, 1, 1, 2, 1, 2, 1, 1, 2, 2 }
    }};
    static constexpr std::array<int, 16> slabDensityProfile {
        5, 9, 3, 11,
        7, 4, 10, 6,
        12, 2, 8, 5,
        9, 4, 11, 7
    };
    static constexpr std::array<PerformanceAgentMode, 4> presetModes {
        PerformanceAgentMode::snakes,
        PerformanceAgentMode::trains,
        PerformanceAgentMode::orbiters,
        PerformanceAgentMode::automata
    };

    juce::Random presetRng;
    for (size_t i = 0; i < slabPerformanceModes.size(); ++i)
    {
        slabPerformanceModes[i] = presetModes[static_cast<size_t>(presetRng.nextInt(static_cast<int>(presetModes.size())))];
        slabStartingTempos[i] = 96.0 + static_cast<double>(presetRng.nextInt(75));
    }

    auto stamp = [this] (int x, int y, int z)
    {
        if (x < 0 || x >= gridWidth || y < 0 || y >= gridDepth || z < 0 || z >= gridHeight)
            return;
        voxels[voxelIndex(x, y, z)] = 1u;
    };

    auto stampRect = [&] (int x, int y, int w, int h, int z)
    {
        for (int yy = 0; yy < h; ++yy)
            for (int xx = 0; xx < w; ++xx)
                stamp(x + xx, y + yy, z);
    };

    for (int floor = 0; floor < 4; ++floor)
    {
        for (int quadrant = 0; quadrant < 4; ++quadrant)
        {
            int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
            quadrantBounds(quadrant, x0, y0, x1, y1);

            const int slabIndex = floor * 4 + quadrant;
            const int motifIndex = slabIndex % static_cast<int>(motifMelodies.size());
            const int innerX0 = x0 + 4;
            const int innerY0 = y0 + 4;
            const int innerX1 = x1 - 4;
            const int innerY1 = y1 - 4;
            const int innerWidth = juce::jmax(8, innerX1 - innerX0);
            const int innerHeight = juce::jmax(8, innerY1 - innerY0);
            const int zBase = floor * floorBandHeight;
            const int root = (quadrant * 2 + floor * 3) % 7;
            const int density = slabDensityProfile[static_cast<size_t>(slabIndex % static_cast<int>(slabDensityProfile.size()))];
            const bool denseSlab = density >= 9;
            const bool sparseSlab = density <= 4;
            const bool pulseSlab = (slabIndex % 3) == 0;
            const bool stairSlab = (slabIndex % 3) == 1;
            const bool braidSlab = (slabIndex % 3) == 2;

            std::array<int, 4> laneY {
                innerY0 + innerHeight / 8,
                innerY0 + innerHeight * 3 / 8,
                innerY0 + innerHeight * 5 / 8,
                innerY0 + innerHeight * 7 / 8
            };

            for (int step = 0; step < 16; ++step)
            {
                const int x = innerX0 + (step * innerWidth) / 16;
                const int lane = motifLanes[static_cast<size_t>(motifIndex)][static_cast<size_t>(step)] % 4;
                const int y = laneY[static_cast<size_t>(lane)];
                const int duration = juce::jlimit(1, 5,
                                                  motifDurations[static_cast<size_t>(motifIndex)][static_cast<size_t>(step)]
                                                    + (denseSlab ? 2 : sparseSlab ? 0 : 1));
                const int degree = (root + motifMelodies[static_cast<size_t>(motifIndex)][static_cast<size_t>(step)]) % static_cast<int>(scaleSteps.size());
                const int leadLocalZ = 1 + ((scaleSteps[static_cast<size_t>(degree)] + floor + quadrant) % (floorBandHeight - 1));

                for (int dx = 0; dx < duration; ++dx)
                {
                    stamp(x + dx, y, zBase + leadLocalZ);
                    if (! sparseSlab && ((step + dx + slabIndex) % (denseSlab ? 2 : 3)) == 0)
                        stamp(x + dx, y, zBase + juce::jlimit(0, floorBandHeight - 1, leadLocalZ + 1));
                    if (denseSlab && ((step + dx) % 2) == 0)
                        stamp(x + dx, laneY[static_cast<size_t>((lane + 1) % 4)], zBase + juce::jlimit(0, floorBandHeight - 1, leadLocalZ + 2));
                }

                if (! sparseSlab && (step % 2) == ((quadrant + floor) % 2))
                {
                    const int harmonyA = zBase + ((leadLocalZ + 3) % floorBandHeight);
                    const int harmonyB = zBase + ((leadLocalZ + 7) % floorBandHeight);
                    stamp(x, y, harmonyA);
                    stamp(x + juce::jmin(1, duration - 1), y, harmonyB);
                    if (denseSlab || (step % 4) == 0)
                        stamp(x + juce::jmin(2, duration - 1), y, zBase + ((leadLocalZ + 10) % floorBandHeight));
                }

                if (denseSlab || (step % 4) == 0 || (step % 4) == 2)
                {
                    const int bassLane = laneY[static_cast<size_t>((lane + 3) % 4)];
                    const int bassLocalZ = juce::jlimit(0, floorBandHeight - 1, 1 + ((root + step / 2) % 4));
                    stamp(x, bassLane, zBase + bassLocalZ);
                    stamp(x + 1, bassLane, zBase + bassLocalZ);
                    if (! sparseSlab)
                        stamp(x + 2, bassLane, zBase + bassLocalZ);
                    if (denseSlab)
                        stamp(x + 3, bassLane, zBase + bassLocalZ);
                }

                if ((step + slabIndex) % (sparseSlab ? 5 : 3) == 0)
                {
                    const int accentY = laneY[static_cast<size_t>((lane + 1) % 4)];
                    const int accentLocalZ = juce::jlimit(0, floorBandHeight - 1, leadLocalZ + 1);
                    stamp(x, accentY, zBase + accentLocalZ);
                    if (! sparseSlab)
                        stamp(x + 1, accentY, zBase + juce::jlimit(0, floorBandHeight - 1, accentLocalZ + 2));
                }

                if (! sparseSlab && (step % 2) == 1)
                {
                    const int counterLane = laneY[static_cast<size_t>((lane + 2) % 4)];
                    const int counterLocalZ = juce::jlimit(0, floorBandHeight - 1, 2 + ((leadLocalZ + 5) % 7));
                    stamp(x, counterLane, zBase + counterLocalZ);
                    if (denseSlab || (step % 4) == 3)
                        stamp(x + 1, counterLane, zBase + counterLocalZ);
                }

                if (denseSlab || (step % 4) != 3)
                {
                    const int fillLaneA = laneY[static_cast<size_t>((lane + 1) % 4)];
                    const int fillLaneB = laneY[static_cast<size_t>((lane + 2) % 4)];
                    if (! sparseSlab)
                        stamp(x, fillLaneA, zBase + juce::jlimit(0, floorBandHeight - 1, (leadLocalZ + 2) % floorBandHeight));
                    if (denseSlab || ((step + slabIndex) % 3) == 0)
                        stamp(x, fillLaneB, zBase + juce::jlimit(0, floorBandHeight - 1, (leadLocalZ + 5) % floorBandHeight));
                }

                if (pulseSlab && (step % 2) == 0)
                {
                    for (int laneIndex = 0; laneIndex < 4; ++laneIndex)
                        stamp(x, laneY[static_cast<size_t>(laneIndex)], zBase + ((laneIndex + step / 2) % 4));
                }

                if (stairSlab)
                {
                    const int stairLane = step % 4;
                    stamp(x, laneY[static_cast<size_t>(stairLane)], zBase + juce::jlimit(0, floorBandHeight - 1, 1 + (step % 10)));
                    if (denseSlab)
                        stamp(x + 1, laneY[static_cast<size_t>((stairLane + 1) % 4)], zBase + juce::jlimit(0, floorBandHeight - 1, 2 + (step % 8)));
                }

                if (braidSlab && ! sparseSlab)
                {
                    const int braidLaneA = step % 4;
                    const int braidLaneB = 3 - braidLaneA;
                    stamp(x, laneY[static_cast<size_t>(braidLaneA)], zBase + juce::jlimit(0, floorBandHeight - 1, 3 + ((step + quadrant) % 7)));
                    stamp(x, laneY[static_cast<size_t>(braidLaneB)], zBase + juce::jlimit(0, floorBandHeight - 1, 5 + ((step + floor) % 5)));
                }
            }

            for (int lane = 0; lane < 4; ++lane)
            {
                const int y = laneY[static_cast<size_t>(lane)];
                for (int x = innerX0; x < innerX1; x += (sparseSlab ? 3 : 2))
                {
                    if (((x + lane + slabIndex) % (denseSlab ? 4 : 5)) <= (denseSlab ? 2 : 1))
                    {
                        stamp(x, y, zBase + ((lane + slabIndex) % 3));
                        if (! sparseSlab && ((x + slabIndex) % 4) == 0)
                            stamp(x, y, zBase + 3 + ((lane + floor) % 3));
                    }
                }
            }

            for (int x = innerX0; x < innerX1; ++x)
            {
                const int phase = (x - innerX0 + slabIndex) % (denseSlab ? 5 : 7);
                for (int lane = 0; lane < 4; ++lane)
                {
                    const int y = laneY[static_cast<size_t>(lane)];
                    if (phase == lane || (! sparseSlab && phase == ((lane + 2) % (denseSlab ? 5 : 7))))
                    {
                        const int bedZ = zBase + 1 + ((root + lane * 2 + phase) % 5);
                        stamp(x, y, bedZ);
                    }

                    if (! sparseSlab && ((x + lane + slabIndex) % (denseSlab ? 5 : 7)) <= (denseSlab ? 3 : 2))
                    {
                        const int shimmerZ = zBase + 4 + ((lane + phase + floor) % 5);
                        stamp(x, y, juce::jlimit(0, gridHeight - 1, shimmerZ));
                    }
                }
            }

            const int motifSpanX = juce::jmax(6, innerWidth / 6);
            const int motifSpanY = juce::jmax(3, innerHeight / 6);
            const int centreX = innerX0 + innerWidth / 2;
            const int centreY = innerY0 + innerHeight / 2;

            if (pulseSlab)
            {
                for (int bar = 0; bar < 4; ++bar)
                {
                    const int barX = innerX0 + bar * innerWidth / 4 + ((slabIndex + bar) % 3);
                    const int barZ = zBase + 2 + ((bar * 2 + slabIndex) % 7);
                    stampRect(barX, innerY0 + 1, denseSlab ? 2 : 1, innerHeight - 2, barZ);
                    if (denseSlab)
                        stampRect(barX + 2, innerY0 + innerHeight / 4, 1, innerHeight / 2, zBase + ((barZ - zBase + 3) % floorBandHeight));
                }
            }
            else if (stairSlab)
            {
                for (int stair = 0; stair < 8; ++stair)
                {
                    const int sx = innerX0 + stair * innerWidth / 8;
                    const int sy = innerY0 + stair * innerHeight / 10;
                    const int sz = zBase + 1 + ((stair + slabIndex) % 10);
                    stampRect(sx, sy, denseSlab ? 3 : 2, denseSlab ? 3 : 2, sz);
                    if (! sparseSlab)
                        stampRect(sx, innerY1 - (sy - innerY0) - motifSpanY / 2, 2, 2, zBase + ((sz - zBase + 4) % floorBandHeight));
                }
            }
            else if (braidSlab)
            {
                for (int i = 0; i < innerWidth; i += 2)
                {
                    const int xa = innerX0 + i;
                    const int ya = innerY0 + (i * innerHeight) / juce::jmax(1, innerWidth);
                    const int yb = innerY1 - 1 - (i * innerHeight) / juce::jmax(1, innerWidth);
                    stampRect(xa, ya, denseSlab ? 2 : 1, denseSlab ? 2 : 1, zBase + 3 + ((i / 2 + slabIndex) % 6));
                    stampRect(xa, yb, denseSlab ? 2 : 1, denseSlab ? 2 : 1, zBase + 6 + ((i / 2 + floor) % 4));
                }
            }

            if (denseSlab)
            {
                stampRect(centreX - motifSpanX / 2, centreY - motifSpanY / 2,
                          motifSpanX, motifSpanY, zBase + 2 + (slabIndex % 6));
                stampRect(centreX - motifSpanX / 3, centreY - motifSpanY / 3,
                          juce::jmax(2, motifSpanX / 2), juce::jmax(2, motifSpanY / 2), zBase + 7 + (slabIndex % 3));
            }
            else if (! sparseSlab)
            {
                stampRect(centreX - motifSpanX / 3, centreY - motifSpanY / 3,
                          juce::jmax(2, motifSpanX / 2), juce::jmax(2, motifSpanY / 2), zBase + 4 + (slabIndex % 4));
            }
        }
    }

    for (int z = 0; z < gridHeight; ++z)
        for (int y = 0; y < gridDepth; ++y)
            for (int x = 0; x < gridWidth; ++x)
                if (voxels[voxelIndex(x, y, z)] != 0u)
                {
                    ++voxelCount;
                    filledVoxels.push_back(FilledVoxel { static_cast<uint16_t>(x), static_cast<uint16_t>(y), static_cast<uint8_t>(z) });
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
    performanceAgentMode = PerformanceAgentMode::snakes;
    performanceAgentCount = 1;
    performanceSnakes.clear();
    performanceDiscs.clear();
    performanceTracks.clear();
    performanceOrbitCenters.clear();
    performanceAutomataCells.clear();
    performanceFlashes.clear();
    performanceHoverCell.reset();
    performancePlacementMode = PerformancePlacementMode::selectOnly;
    performanceSelection = {};
    performanceTrackHorizontal = true;
    performanceTick = 0;
    beatStepAccumulator = 0.0;
    beatStepIndex = 0;
    beatBarIndex = 0;
    visualStepCounter.store(0, std::memory_order_relaxed);
    visualBarCounter.store(0, std::memory_order_relaxed);
    visualBeatPulse = 0.0f;
    visualBarPulse = 0.0f;
    visualBarSweep = 0.0f;
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

void MainComponent::clearIsolatedSlab()
{
    if (! isolatedSlab.isValid())
        return;

    int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    quadrantBounds(isolatedSlab.quadrant, x0, y0, x1, y1);
    const int zMin = isolatedSlab.floor * floorBandHeight;
    const int zMax = zMin + floorBandHeight;

    for (int z = zMin; z < zMax; ++z)
        for (int y = y0; y < y1; ++y)
            for (int x = x0; x < x1; ++x)
                voxels[voxelIndex(x, y, z)] = 0u;

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

    resetEditCursor();
}

int MainComponent::midiNoteForHeight(int z) const
{
    const int midi = juce::jlimit(0, 72, 23 + z);
    return juce::jlimit(0, 84, quantizeMidiToScale(midi));
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
        return;
    }

    performanceBeatEnergy = juce::jmin(1.0f, performanceBeatEnergy + 0.08f + 0.03f * static_cast<float>(triggered - 1));
    performanceFlashes.push_back({ cell, juce::Colour::fromRGBA(120, 220, 255, 255), juce::jmin(1.0f, 0.70f + 0.12f * static_cast<float>(triggered - 1)), false });
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

void MainComponent::resetPerformanceAgents()
{
    performanceSnakes.clear();
    performanceAutomataCells.clear();
    performanceFlashes.clear();
    performanceTick = 0;

    if (! isolatedSlab.isValid())
        return;

    const auto bounds = performanceRegionBounds();
    if (bounds.isEmpty())
        return;

    juce::Random rng;
    performanceAgentCount = juce::jlimit(0, 8, performanceAgentCount);

    if (performanceAgentCount == 0)
        return;

    if (performanceAgentMode == PerformanceAgentMode::automata)
    {
        const int seedCount = juce::jmax(6, performanceAgentCount * 10);
        for (int i = 0; i < seedCount; ++i)
        {
            juce::Point<int> cell(bounds.getX() + rng.nextInt(bounds.getWidth()),
                                  bounds.getY() + rng.nextInt(bounds.getHeight()));
            if (std::find(performanceAutomataCells.begin(), performanceAutomataCells.end(), cell) == performanceAutomataCells.end())
                performanceAutomataCells.push_back(cell);
        }
        return;
    }

    if (performanceAgentMode == PerformanceAgentMode::orbiters && performanceOrbitCenters.empty())
        performanceOrbitCenters.push_back(bounds.getCentre());

    for (int i = 0; i < performanceAgentCount; ++i)
    {
        Snake snake;
        snake.colour = snakeColours[static_cast<size_t>(i % static_cast<int>(snakeColours.size()))];
        snake.direction = snakeDirections[static_cast<size_t>(rng.nextInt(static_cast<int>(snakeDirections.size())))];
        snake.clockwise = (i % 2) == 0;

        if (performanceAgentMode == PerformanceAgentMode::orbiters)
        {
            const auto centre = performanceOrbitCenters[static_cast<size_t>(i % static_cast<int>(performanceOrbitCenters.size()))];
            snake.orbitIndex = i % static_cast<int>(performanceOrbitCenters.size());
            const int radius = 3 + (i % 4);
            const auto start = juce::Point<int>(juce::jlimit(bounds.getX(), bounds.getRight() - 1, centre.x + radius),
                                                juce::jlimit(bounds.getY(), bounds.getBottom() - 1, centre.y));
            snake.body.push_back(start);
            snake.direction = snake.clockwise ? juce::Point<int>{ 0, 1 } : juce::Point<int>{ 0, -1 };
        }
        else
        {
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
        }

        performanceSnakes.push_back(std::move(snake));
    }
}

void MainComponent::setPerformanceSnakeCount(int count)
{
    performanceAgentCount = juce::jlimit(0, 8, count);
    resetPerformanceAgents();
}

void MainComponent::stepPerformanceAgents()
{
    switch (performanceAgentMode)
    {
        case PerformanceAgentMode::snakes:
        case PerformanceAgentMode::trains:
            stepPerformanceSnakes();
            break;
        case PerformanceAgentMode::orbiters:
            stepPerformanceOrbiters();
            break;
        case PerformanceAgentMode::automata:
            stepPerformanceAutomata();
            break;
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

        const auto head = snake.body.front();
        if (performanceAgentMode == PerformanceAgentMode::trains)
        {
            std::vector<juce::Point<int>> trackOptions;
            for (const auto& dir : snakeDirections)
            {
                const auto candidate = head + dir;
                if (! inside(candidate) || occupiedByAnySnake(candidate, snakeIndex))
                    continue;
                if (performanceTrackAt(candidate))
                    trackOptions.push_back(dir);
            }

            if (! trackOptions.empty())
            {
                const auto switchDisc = std::find_if(performanceDiscs.begin(), performanceDiscs.end(),
                                                     [head] (const ReflectorDisc& disc) { return disc.cell == head; });
                bool switchedByJunction = false;

                if (switchDisc != performanceDiscs.end())
                {
                    auto switchedDirection = std::find(trackOptions.begin(), trackOptions.end(), switchDisc->direction);
                    if (switchedDirection != trackOptions.end())
                    {
                        snake.direction = *switchedDirection;
                        switchedByJunction = true;
                        performanceFlashes.push_back({ head, juce::Colour::fromRGBA(255, 208, 112, 255), 0.72f, true });
                    }
                }

                if (! switchedByJunction)
                {
                    auto preferred = std::find(trackOptions.begin(), trackOptions.end(), snake.direction);
                    if (preferred != trackOptions.end())
                        snake.direction = *preferred;
                    else
                    {
                        auto nonReverse = std::find_if(trackOptions.begin(), trackOptions.end(),
                                                       [&] (const juce::Point<int>& dir)
                                                       {
                                                           return dir.x != -snake.direction.x || dir.y != -snake.direction.y;
                                                       });
                        snake.direction = nonReverse != trackOptions.end() ? *nonReverse : trackOptions.front();
                    }
                }
            }
        }

        auto forward = snake.body.front() + snake.direction;
        if (! inside(forward) || occupiedByAnySnake(forward, snakeIndex))
        {
            if (snake.body.size() >= 2)
            {
                std::reverse(snake.body.begin(), snake.body.end());
                const auto newHead = snake.body.front();
                const auto nextSegment = snake.body[1];
                snake.direction = { newHead.x - nextSegment.x, newHead.y - nextSegment.y };
            }
            else
            {
                snake.direction = { -snake.direction.x, -snake.direction.y };
            }
        }

        auto nextHead = snake.body.front() + snake.direction;
        if (! inside(nextHead) || occupiedByAnySnake(nextHead, snakeIndex))
            continue;

        if (const auto disc = std::find_if(performanceDiscs.begin(), performanceDiscs.end(),
                                           [nextHead] (const ReflectorDisc& reflector) { return reflector.cell == nextHead; });
            disc != performanceDiscs.end())
        {
            snake.direction = disc->direction;
            performanceBeatEnergy = juce::jmin(1.0f, performanceBeatEnergy + 0.1f);
            performanceFlashes.push_back({ nextHead, juce::Colour::fromRGBA(255, 196, 96, 255), 1.0f, true });
        }

        snake.body.insert(snake.body.begin(), nextHead);
        if (snake.body.size() > 7)
            snake.body.pop_back();

        if (snakeTriggerMode == SnakeTriggerMode::headOnly)
        {
            triggerPerformanceNotesAtCell(nextHead);
        }
        else
        {
            for (const auto& segment : snake.body)
                triggerPerformanceNotesAtCell(segment);
        }
    }
}

void MainComponent::stepPerformanceOrbiters()
{
    if (! isolatedSlab.isValid())
        return;

    const auto bounds = performanceRegionBounds();
    if (performanceOrbitCenters.empty())
        performanceOrbitCenters.push_back(bounds.getCentre());

    auto inside = [&] (juce::Point<int> p) { return bounds.contains(p); };

    for (auto& orbiter : performanceSnakes)
    {
        if (orbiter.body.empty())
            continue;

        const auto centre = performanceOrbitCenters[static_cast<size_t>(orbiter.orbitIndex % static_cast<int>(performanceOrbitCenters.size()))];
        const auto head = orbiter.body.front();
        const auto delta = head - centre;
        const int desiredRadius = 3 + (orbiter.orbitIndex % 4);
        const int distanceSq = delta.x * delta.x + delta.y * delta.y;

        auto axisDir = [] (juce::Point<int> vector)
        {
            if (std::abs(vector.x) >= std::abs(vector.y))
                return juce::Point<int>(vector.x < 0 ? -1 : 1, 0);
            return juce::Point<int>(0, vector.y < 0 ? -1 : 1);
        };

        const juce::Point<int> tangent = orbiter.clockwise
                                           ? juce::Point<int>(delta.y, -delta.x)
                                           : juce::Point<int>(-delta.y, delta.x);
        juce::Point<int> direction = axisDir(tangent.x == 0 && tangent.y == 0 ? juce::Point<int>(1, 0) : tangent);
        if (distanceSq < desiredRadius * desiredRadius)
            direction = axisDir(delta.x == 0 && delta.y == 0 ? juce::Point<int>(1, 0) : delta);
        else if (distanceSq > (desiredRadius + 1) * (desiredRadius + 1))
            direction = axisDir({ -delta.x, -delta.y });

        auto next = head + direction;
        if (! inside(next))
        {
            orbiter.clockwise = ! orbiter.clockwise;
            const auto altTangent = orbiter.clockwise
                                      ? juce::Point<int>(delta.y, -delta.x)
                                      : juce::Point<int>(-delta.y, delta.x);
            direction = axisDir(altTangent.x == 0 && altTangent.y == 0 ? juce::Point<int>(1, 0) : altTangent);
            next = head + direction;
        }

        if (! inside(next))
            continue;

        orbiter.direction = direction;
        orbiter.body[0] = next;
        triggerPerformanceNotesAtCell(next);
    }
}

void MainComponent::stepPerformanceAutomata()
{
    if (! isolatedSlab.isValid())
        return;

    const auto bounds = performanceRegionBounds();
    if (performanceAutomataCells.empty())
    {
        resetPerformanceAgents();
        if (performanceAutomataCells.empty())
            return;
    }

    std::unordered_map<int, int> neighbourCounts;
    auto keyFor = [] (juce::Point<int> cell) { return (cell.y << 16) ^ cell.x; };
    auto cellFor = [] (int key) { return juce::Point<int>(key & 0xffff, key >> 16); };

    for (const auto& cell : performanceAutomataCells)
    {
        for (int dy = -1; dy <= 1; ++dy)
            for (int dx = -1; dx <= 1; ++dx)
            {
                if (dx == 0 && dy == 0)
                    continue;
                const juce::Point<int> n { cell.x + dx, cell.y + dy };
                if (bounds.contains(n))
                    ++neighbourCounts[keyFor(n)];
            }
    }

    std::vector<juce::Point<int>> nextCells;
    for (const auto& [key, count] : neighbourCounts)
    {
        const auto cell = cellFor(key);
        const bool alive = std::find(performanceAutomataCells.begin(), performanceAutomataCells.end(), cell) != performanceAutomataCells.end();
        if ((alive && (count == 2 || count == 3)) || (! alive && count == 3))
            nextCells.push_back(cell);
    }

    if (nextCells.empty())
    {
        resetPerformanceAgents();
        return;
    }

    performanceAutomataCells = nextCells;
    for (const auto& cell : performanceAutomataCells)
        triggerPerformanceNotesAtCell(cell);
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
    const float pulse = 0.5f + 0.5f * static_cast<float>(std::sin(juce::Time::getMillisecondCounterHiRes() * 0.0034));
    const float beatPulse = visualBeatPulse;
    const float barPulse = visualBarPulse;

    juce::ColourGradient boardGlow(juce::Colour::fromRGBA(42, 102, 212, 120),
                                   board.getCentreX(), board.getCentreY() - board.getHeight() * 0.15f,
                                   juce::Colour::fromRGBA(8, 13, 36, 0),
                                   board.getCentreX(), board.getBottom() + 70.0f,
                                   true);
    g.setGradientFill(boardGlow);
    g.fillEllipse(board.expanded(86.0f, 74.0f));
    g.setColour(juce::Colour::fromRGBA(120, 220, 255, static_cast<uint8_t>(18 + 20 * pulse)));
    g.drawEllipse(board.expanded(66.0f, 56.0f), 2.0f + pulse * 1.2f);
    g.setColour(juce::Colour::fromRGBA(120, 220, 255, static_cast<uint8_t>(10 + 60 * barPulse)));
    g.drawEllipse(board.expanded(82.0f + 20.0f * barPulse, 68.0f + 18.0f * barPulse), 1.1f + 1.2f * barPulse);

    g.setColour(juce::Colour::fromRGBA(6, 11, 30, 236));
    g.fillRoundedRectangle(board.expanded(26.0f), 28.0f);
    g.setColour(juce::Colour::fromRGBA(74, 144, 255, 72));
    g.drawRoundedRectangle(board.expanded(26.0f), 28.0f, 1.8f);

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

            juce::Colour tileBase = activeCell ? juce::Colour::fromRGBA(10, 18, 42, 255)
                                               : juce::Colour::fromRGBA(20, 24, 36, 226);
            g.setColour(tileBase);
            g.fillRoundedRectangle(cell.reduced(0.6f), 3.6f);

            g.setColour(activeCell ? juce::Colour::fromRGBA(255, 255, 255, 14)
                                   : juce::Colour::fromRGBA(255, 255, 255, 6));
            g.fillRoundedRectangle(cell.removeFromTop(tileSize * 0.12f).reduced(1.2f, 0.0f), 3.0f);
            if (activeCell && ((localX + localY) % 5) == 0)
            {
                g.setColour(juce::Colour::fromRGBA(120, 220, 255, static_cast<uint8_t>(10 + 18 * pulse)));
                g.fillEllipse(cell.withSizeKeepingCentre(tileSize * 0.22f, tileSize * 0.22f)
                                  .translated(tileSize * 0.18f, -tileSize * 0.16f));
            }

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
                    g.fillRoundedRectangle(slice, 2.4f);
                }
            }

            g.setColour(activeCell ? juce::Colour::fromRGBA(88, 126, 214, 44)
                                   : juce::Colour::fromRGBA(90, 90, 104, 24));
            g.drawRoundedRectangle(cell.reduced(0.6f), 3.6f, 1.0f);
        }
    }

    const auto regionLocal = juce::Rectangle<float>(board.getX() + static_cast<float>(activeRegion.getX() - x0) * tileSize,
                                                    board.getY() + static_cast<float>(activeRegion.getY() - y0) * tileSize,
                                                    static_cast<float>(activeRegion.getWidth()) * tileSize,
                                                    static_cast<float>(activeRegion.getHeight()) * tileSize);
    g.setColour(juce::Colour::fromRGBA(85, 210, 255, 34));
    g.fillRoundedRectangle(regionLocal.expanded(3.0f), 12.0f);
    g.setColour(juce::Colour::fromRGBA(144, 240, 255, 182));
    g.drawRoundedRectangle(regionLocal.expanded(2.5f), 12.0f, 2.2f);

    for (const auto& track : performanceTracks)
    {
        if (! activeRegion.contains(track.cell))
            continue;

        auto cell = juce::Rectangle<float>(board.getX() + static_cast<float>(track.cell.x - x0) * tileSize,
                                           board.getY() + static_cast<float>(track.cell.y - y0) * tileSize,
                                           tileSize,
                                           tileSize).reduced(tileSize * 0.16f);
        g.setColour(juce::Colour::fromRGBA(80, 230, 255, 110));
        if (track.horizontal)
            g.drawLine(cell.getX(), cell.getCentreY(), cell.getRight(), cell.getCentreY(), 3.0f);
        else
            g.drawLine(cell.getCentreX(), cell.getY(), cell.getCentreX(), cell.getBottom(), 3.0f);
        g.setColour(juce::Colour::fromRGBA(255, 255, 255, 56));
        g.drawRoundedRectangle(cell, 4.0f, 1.0f);

        if (performanceSelection.kind == PerformanceSelection::Kind::track && performanceSelection.cell == track.cell)
        {
            g.setColour(juce::Colour::fromRGBA(255, 255, 255, 180));
            g.drawRoundedRectangle(cell.expanded(tileSize * 0.12f), 6.0f, 2.2f);
        }
    }

    for (const auto& orbitCenter : performanceOrbitCenters)
    {
        if (! activeRegion.contains(orbitCenter))
            continue;

        auto cell = juce::Rectangle<float>(board.getX() + static_cast<float>(orbitCenter.x - x0) * tileSize,
                                           board.getY() + static_cast<float>(orbitCenter.y - y0) * tileSize,
                                           tileSize,
                                           tileSize).reduced(tileSize * 0.10f);
        g.setColour(juce::Colour::fromRGBA(255, 170, 120, 42));
        g.fillEllipse(cell.expanded(tileSize * 0.18f));
        g.setColour(juce::Colour::fromRGBA(255, 200, 140, 210));
        g.drawEllipse(cell, 2.2f);
        g.drawEllipse(cell.reduced(tileSize * 0.12f), 1.2f);
        g.drawLine(cell.getCentreX(), cell.getY(), cell.getCentreX(), cell.getBottom(), 1.3f);
        g.drawLine(cell.getX(), cell.getCentreY(), cell.getRight(), cell.getCentreY(), 1.3f);
    }

    if (performanceAgentMode == PerformanceAgentMode::automata)
    {
        for (const auto& cellPoint : performanceAutomataCells)
        {
            if (! activeRegion.contains(cellPoint))
                continue;

            auto cell = juce::Rectangle<float>(board.getX() + static_cast<float>(cellPoint.x - x0) * tileSize,
                                               board.getY() + static_cast<float>(cellPoint.y - y0) * tileSize,
                                               tileSize,
                                               tileSize).reduced(tileSize * 0.18f);
            g.setColour(juce::Colour::fromRGBA(255, 180, 120, 44));
            g.fillRoundedRectangle(cell.expanded(tileSize * 0.16f), 4.0f);
            g.setColour(juce::Colour::fromRGBA(255, 214, 180, 230));
            g.fillRoundedRectangle(cell, 4.0f);
            g.setColour(juce::Colour::fromRGBA(255, 255, 255, 160));
            g.drawRoundedRectangle(cell, 4.0f, 1.2f);
        }
    }

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
            g.setColour(snake.colour.withAlpha(0.10f + 0.08f * pulse));
            g.strokePath(spine, juce::PathStrokeType(tileSize * 0.52f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
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
                                           tileSize).reduced(tileSize * 0.10f);
        const auto centre = cell.getCentre();
        const float radius = cell.getWidth() * 0.38f;
        const bool hovered = performanceHoverCell.has_value() && *performanceHoverCell == disc.cell;
        const auto glowColour = hovered ? juce::Colour::fromRGBA(255, 250, 180, 170)
                                        : juce::Colour::fromRGBA(90, 240, 255, 150);
        const auto ringColour = hovered ? juce::Colour::fromRGBA(255, 248, 210, 250)
                                        : juce::Colour::fromRGBA(170, 244, 255, 245);
        const auto coreColour = hovered ? juce::Colour::fromRGBA(34, 44, 86, 250)
                                        : juce::Colour::fromRGBA(10, 22, 58, 245);

        g.setColour(glowColour.withAlpha(hovered ? 0.30f : 0.22f));
        g.fillEllipse(cell.expanded(tileSize * 0.20f));
        g.setColour(glowColour.withAlpha(0.12f + 0.10f * pulse));
        g.fillEllipse(cell.expanded(tileSize * 0.34f));

        g.setColour(coreColour);
        g.fillEllipse(cell);

        g.setColour(juce::Colour::fromRGBA(255, 255, 255, 64));
        g.fillEllipse(cell.reduced(tileSize * 0.18f).withTrimmedBottom(tileSize * 0.22f));

        g.setColour(ringColour);
        g.drawEllipse(cell, hovered ? 3.2f : 2.4f);
        g.drawEllipse(cell.reduced(tileSize * 0.10f), hovered ? 1.8f : 1.3f);

        const juce::Point<float> dir(static_cast<float>(disc.direction.x), static_cast<float>(disc.direction.y));
        const juce::Point<float> perp(-dir.y, dir.x);
        const auto tip = centre + dir * (radius * 1.12f);
        const auto base = centre - dir * (radius * 0.14f);
        const auto tailStart = centre - dir * (radius * 0.58f);
        const auto tailEnd = centre + dir * (radius * 0.40f);

        juce::Path pointerShadow;
        pointerShadow.startNewSubPath(base + perp * (radius * 0.34f));
        pointerShadow.lineTo(tip + dir * (radius * 0.10f));
        pointerShadow.lineTo(base - perp * (radius * 0.34f));
        pointerShadow.closeSubPath();
        g.setColour(glowColour.withAlpha(hovered ? 0.28f : 0.20f));
        g.fillPath(pointerShadow);

        g.setColour(ringColour.withAlpha(0.70f));
        g.drawLine({ tailStart, tailEnd }, hovered ? 4.2f : 3.4f);
        g.setColour(juce::Colours::white.withAlpha(0.80f));
        g.drawLine({ centre - dir * (radius * 0.44f), centre + dir * (radius * 0.26f) }, hovered ? 1.8f : 1.4f);

        juce::Path arrow;
        arrow.startNewSubPath(base + perp * (radius * 0.28f));
        arrow.lineTo(tip);
        arrow.lineTo(base - perp * (radius * 0.28f));
        arrow.closeSubPath();
        g.setColour(juce::Colour::fromRGBA(255, 255, 255, 238));
        g.fillPath(arrow);

        g.setColour(juce::Colour::fromRGBA(8, 16, 34, 190));
        g.strokePath(arrow, juce::PathStrokeType(1.2f));

        auto nose = juce::Rectangle<float>(radius * 0.34f, radius * 0.34f)
                        .withCentre(centre + dir * (radius * 0.62f));
        g.setColour(hovered ? juce::Colour::fromRGBA(255, 248, 210, 245)
                            : juce::Colour::fromRGBA(120, 240, 255, 235));
        g.fillEllipse(nose);

        juce::Path crosshair;
        crosshair.startNewSubPath(centre.x - radius * 0.34f, centre.y);
        crosshair.lineTo(centre.x + radius * 0.34f, centre.y);
        crosshair.startNewSubPath(centre.x, centre.y - radius * 0.34f);
        crosshair.lineTo(centre.x, centre.y + radius * 0.34f);
        g.setColour(ringColour.withAlpha(0.55f));
        g.strokePath(crosshair, juce::PathStrokeType(1.1f));

        if (performanceSelection.kind == PerformanceSelection::Kind::disc && performanceSelection.cell == disc.cell)
        {
            g.setColour(juce::Colours::white.withAlpha(0.86f));
            g.drawEllipse(cell.expanded(tileSize * 0.16f), 2.4f);
        }
    }

    if (performanceHoverCell.has_value())
    {
        auto previewCell = juce::Rectangle<float>(board.getX() + static_cast<float>(performanceHoverCell->x - x0) * tileSize,
                                                  board.getY() + static_cast<float>(performanceHoverCell->y - y0) * tileSize,
                                                  tileSize,
                                                  tileSize).reduced(tileSize * 0.14f);
        if (performancePlacementMode == PerformancePlacementMode::placeDisc)
        {
            const auto centre = previewCell.getCentre();
            const float radius = previewCell.getWidth() * 0.34f;
            const juce::Point<float> dir(static_cast<float>(performanceSelectedDirection.x), static_cast<float>(performanceSelectedDirection.y));
            const juce::Point<float> perp(-dir.y, dir.x);
            juce::Path arrow;
            const auto tip = centre + dir * (radius * 1.12f);
            const auto base = centre - dir * (radius * 0.14f);
            arrow.startNewSubPath(base + perp * (radius * 0.28f));
            arrow.lineTo(tip);
            arrow.lineTo(base - perp * (radius * 0.28f));
            arrow.closeSubPath();
            g.setColour(juce::Colour::fromRGBA(255, 255, 255, 42));
            g.fillEllipse(previewCell.expanded(tileSize * 0.24f));
            g.setColour(juce::Colour::fromRGBA(255, 236, 200, 210));
            g.fillPath(arrow);
            g.setColour(juce::Colour::fromRGBA(255, 255, 255, 180));
            g.drawEllipse(previewCell, 1.8f);
        }
        else if (performancePlacementMode == PerformancePlacementMode::placeTrack)
        {
            g.setColour(juce::Colour::fromRGBA(120, 220, 255, 84));
            g.fillRoundedRectangle(previewCell, 4.0f);
            g.setColour(juce::Colour::fromRGBA(220, 246, 255, 220));
            if (performanceTrackHorizontal)
                g.drawLine(previewCell.getX(), previewCell.getCentreY(), previewCell.getRight(), previewCell.getCentreY(), 3.2f);
            else
                g.drawLine(previewCell.getCentreX(), previewCell.getY(), previewCell.getCentreX(), previewCell.getBottom(), 3.2f);
        }
    }

    for (const auto& flash : performanceFlashes)
    {
        auto cell = juce::Rectangle<float>(board.getX() + static_cast<float>(flash.cell.x - x0) * tileSize,
                                           board.getY() + static_cast<float>(flash.cell.y - y0) * tileSize,
                                           tileSize,
                                           tileSize);
        const float intensity = juce::jlimit(0.0f, 1.0f, flash.intensity);
        const auto flashColour = flash.colour.withAlpha(0.18f + 0.38f * intensity);
        g.setColour(flashColour);
        g.fillEllipse(cell.expanded(tileSize * (flash.discFlash ? 0.55f : 0.42f) * intensity));

        g.setColour(flash.colour.withAlpha(0.42f * intensity));
        g.drawEllipse(cell.expanded(tileSize * 0.30f * intensity), 1.6f + 2.6f * intensity);

        if (flash.discFlash)
        {
            juce::Path star;
            const auto centre = cell.getCentre();
            const float radius = tileSize * (0.28f + 0.18f * intensity);
            star.startNewSubPath(centre.x - radius, centre.y);
            star.lineTo(centre.x + radius, centre.y);
            star.startNewSubPath(centre.x, centre.y - radius);
            star.lineTo(centre.x, centre.y + radius);
            g.setColour(juce::Colours::white.withAlpha(0.55f * intensity));
            g.strokePath(star, juce::PathStrokeType(1.1f + 1.3f * intensity));
        }
    }

    if (performanceHoverCell.has_value())
    {
        auto hover = juce::Rectangle<float>(board.getX() + static_cast<float>(performanceHoverCell->x - x0) * tileSize,
                                            board.getY() + static_cast<float>(performanceHoverCell->y - y0) * tileSize,
                                            tileSize,
                                            tileSize).reduced(tileSize * 0.08f);
        g.setColour(juce::Colour::fromRGBA(110, 240, 255, 88));
        g.fillRoundedRectangle(hover, 6.0f);
        g.setColour(juce::Colour::fromRGBA(160, 248, 255, 220));
        g.drawRoundedRectangle(hover, 6.0f, 2.0f);
    }

    g.setColour(juce::Colour::fromRGBA(126, 224, 255, 190));
    g.drawRoundedRectangle(board.expanded(5.0f), 12.0f, 2.2f);
    g.setColour(juce::Colour::fromRGBA(190, 244, 255, static_cast<uint8_t>(18 + 74 * beatPulse)));
    g.drawRoundedRectangle(board.expanded(9.0f + 6.0f * beatPulse), 16.0f, 1.2f + 1.6f * beatPulse);
}

void MainComponent::drawPerformanceSidebar(juce::Graphics& g, juce::Rectangle<float> area)
{
    auto panel = area.reduced(10.0f);
    juce::ColourGradient sidebarGradient(juce::Colour::fromRGBA(8, 14, 34, 238),
                                         panel.getX(), panel.getY(),
                                         juce::Colour::fromRGBA(16, 28, 66, 224),
                                         panel.getRight(), panel.getBottom(),
                                         false);
    g.setGradientFill(sidebarGradient);
    g.fillRoundedRectangle(panel, 24.0f);
    g.setColour(juce::Colour::fromRGBA(124, 220, 255, 92));
    g.drawRoundedRectangle(panel, 24.0f, 1.4f);

    auto inner = panel.reduced(18.0f, 18.0f);

    auto badge = inner.removeFromTop(34.0f);
    g.setColour(juce::Colour::fromRGBA(92, 236, 255, 26));
    g.fillRoundedRectangle(badge, 16.0f);
    g.setColour(juce::Colour::fromRGBA(124, 236, 255, 150));
    g.drawRoundedRectangle(badge, 16.0f, 1.2f);
    g.setColour(juce::Colours::white);
    g.setFont(juce::FontOptions(15.0f));
    g.drawText("PERFORMANCE MODE", badge.toNearestInt(), juce::Justification::centred);

    inner.removeFromTop(14.0f);

    auto drawCard = [&] (juce::Rectangle<float> bounds, const juce::String& label, const juce::String& value, const juce::String& sub)
    {
        g.setColour(juce::Colour::fromRGBA(255, 255, 255, 15));
        g.fillRoundedRectangle(bounds, 18.0f);
        g.setColour(juce::Colour::fromRGBA(255, 255, 255, 24));
        g.drawRoundedRectangle(bounds, 18.0f, 1.0f);

        auto content = bounds.reduced(14.0f, 12.0f);
        g.setColour(juce::Colour::fromRGBA(150, 216, 255, 168));
        g.setFont(juce::FontOptions(11.0f));
        g.drawText(label, content.removeFromTop(12.0f).toNearestInt(), juce::Justification::centredLeft);
        g.setColour(juce::Colours::white);
        g.setFont(juce::FontOptions(20.0f));
        g.drawText(value, content.removeFromTop(24.0f).toNearestInt(), juce::Justification::centredLeft);
        g.setColour(juce::Colour::fromRGBA(218, 232, 255, 170));
        g.setFont(juce::FontOptions(12.5f));
        g.drawFittedText(sub, content.toNearestInt(), juce::Justification::centredLeft, 2);
    };

    auto card = inner.removeFromTop(82.0f);
    const auto regionName = performanceRegionMode == 0 ? "Centre 50%"
                          : performanceRegionMode == 1 ? "Centre 75%"
                          : "Full";
    drawCard(card, "REGION", regionName, "Z cycle arena");
    inner.removeFromTop(10.0f);

    card = inner.removeFromTop(82.0f);
    drawCard(card, "TEMPO", juce::String(bpm, 1) + " BPM", ", / . slower faster");
    inner.removeFromTop(10.0f);

    card = inner.removeFromTop(82.0f);
    const auto activeAgentCount = performanceAgentMode == PerformanceAgentMode::automata
                                    ? static_cast<int>(performanceAutomataCells.size())
                                    : static_cast<int>(performanceSnakes.size());
    drawCard(card, "AGENTS", performanceAgentModeName(), juce::String(activeAgentCount) + " active | 0-8 count");
    inner.removeFromTop(10.0f);

    card = inner.removeFromTop(82.0f);
    drawCard(card, "TOOLS", juce::String(static_cast<int>(performanceDiscs.size())) + " discs",
             juce::String(static_cast<int>(performanceTracks.size())) + " tracks | "
             + juce::String(static_cast<int>(performanceOrbitCenters.size())) + " centres");
    inner.removeFromTop(10.0f);

    card = inner.removeFromTop(82.0f);
    const auto placementModeName = performancePlacementMode == PerformancePlacementMode::placeDisc ? "Disc"
                                 : performancePlacementMode == PerformancePlacementMode::placeTrack ? (performanceTrackHorizontal ? "Track H" : "Track V")
                                 : "Select";
    drawCard(card, "TOOL", placementModeName,
             performanceSelection.isValid() ? "Selected cell " + juce::String(performanceSelection.cell.x) + "," + juce::String(performanceSelection.cell.y)
                                            : "No selection");
    inner.removeFromTop(10.0f);

    card = inner.removeFromTop(82.0f);
    drawCard(card, "SOUND", synthName(), drumModeName() + " drums");
    inner.removeFromTop(10.0f);

    card = inner.removeFromTop(82.0f);
    drawCard(card, "NOTE TRIG", snakeTriggerModeName(), "H toggle trigger mode");
    inner.removeFromTop(16.0f);

    auto infoBlock = inner;
    g.setColour(juce::Colour::fromRGBA(255, 255, 255, 12));
    g.fillRoundedRectangle(infoBlock, 18.0f);
    g.setColour(juce::Colour::fromRGBA(255, 255, 255, 24));
    g.drawRoundedRectangle(infoBlock, 18.0f, 1.0f);
    infoBlock.reduce(14.0f, 12.0f);

    g.setColour(juce::Colour::fromRGBA(150, 216, 255, 168));
    g.setFont(juce::FontOptions(11.0f));
    g.drawText("CONTROLS", infoBlock.removeFromTop(12.0f).toNearestInt(), juce::Justification::centredLeft);
    g.setColour(juce::Colours::white);
    g.setFont(juce::FontOptions(13.0f));
    const juce::String help =
        "Enter edit\n"
        "Arrows set disc direction\n"
        "Y select / rotate disc tool\n"
        "T select / rotate track tool\n"
        "U select-only mode\n"
        "Click place or select\n"
        "Backspace delete selected\n"
        "Discs switch train junctions\n"
        "I toggle orbit centre\n"
        "N cycle agent mode\n"
        "M synth   B drums\n"
        "K key   L scale\n"
        "H head / body notes\n"
        ", / . tempo\n"
        "Esc back";
    g.drawFittedText(help, infoBlock.toNearestInt(), juce::Justification::topLeft, 8);
}

void MainComponent::drawWireframeGrid(juce::Graphics& g, juce::Rectangle<float> area)
{
    if (isolatedSlab.isValid() && performanceMode)
    {
        drawPerformanceView(g, area);
        return;
    }

    const float juicePulse = 0.5f + 0.5f * static_cast<float>(std::sin(juce::Time::getMillisecondCounterHiRes() * 0.0018));
    const float beatPulse = visualBeatPulse;
    const float barPulse = visualBarPulse;

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

    juce::ColourGradient floorGlow(juce::Colour::fromRGBA(34, 78, 188, static_cast<uint8_t>(92 + 32 * juicePulse)),
                                   area.getCentreX(), area.getCentreY() + 160.0f,
                                   juce::Colour::fromRGBA(13, 18, 49, 0), area.getCentreX(), area.getBottom(), true);
    g.setGradientFill(floorGlow);
    g.fillEllipse(area.getCentreX() - area.getWidth() * 0.43f,
                  area.getCentreY() - 20.0f,
                  area.getWidth() * 0.86f,
                  area.getHeight() * 0.78f);

    g.setColour(juce::Colour::fromRGBA(92, 186, 255, static_cast<uint8_t>(18 + 18 * juicePulse)));
    g.drawEllipse(area.getCentreX() - area.getWidth() * 0.36f,
                  area.getCentreY() + 10.0f,
                  area.getWidth() * 0.72f,
                  area.getHeight() * 0.54f,
                  2.0f);
    g.setColour(juce::Colour::fromRGBA(120, 220, 255, static_cast<uint8_t>(12 + 60 * barPulse)));
    g.drawEllipse(area.getCentreX() - area.getWidth() * (0.40f + 0.05f * barPulse),
                  area.getCentreY() - 8.0f - 20.0f * barPulse,
                  area.getWidth() * (0.80f + 0.10f * barPulse),
                  area.getHeight() * (0.60f + 0.08f * barPulse),
                  1.2f + 1.0f * barPulse);

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
            g.setColour(displayColourForVoxel(x, y, z, colour.interpolatedWith(juce::Colours::white, 0.52f).withAlpha(0.10f)));
            g.fillPath(topFace);
            g.setColour(displayColourForVoxel(x, y, z, colour.interpolatedWith(juce::Colours::white, 0.15f)));
            g.fillPath(topFace);
            if (((x + y + z) % 9) == 0)
            {
                const auto centre = topFace.getBounds().getCentre();
                g.setColour(juce::Colour::fromRGBA(255, 255, 255, static_cast<uint8_t>(18 + 20 * juicePulse)));
                g.fillEllipse(juce::Rectangle<float>(3.0f + 2.0f * juicePulse, 3.0f + 2.0f * juicePulse).withCentre(centre));
            }
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
        g.setColour(juce::Colour::fromRGBA(8, 10, 20, static_cast<uint8_t>(138 - 36 * beatPulse)));
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

    if (! isolatedSlab.isValid())
    {
        juce::Path radialBurst;
        const auto centre = area.getCentre();
        const float burstRadius = juce::jmin(area.getWidth(), area.getHeight()) * (0.18f + 0.08f * barPulse);
        for (int i = 0; i < 12; ++i)
        {
            const float angle = juce::MathConstants<float>::twoPi * static_cast<float>(i) / 12.0f
                              + visualBarSweep * juce::MathConstants<float>::twoPi;
            const auto inner = juce::Point<float>(centre.x + std::cos(angle) * burstRadius,
                                                  centre.y + std::sin(angle) * burstRadius * 0.58f);
            const auto outer = juce::Point<float>(centre.x + std::cos(angle) * (burstRadius + 90.0f + 70.0f * barPulse),
                                                  centre.y + std::sin(angle) * (burstRadius + 90.0f + 70.0f * barPulse) * 0.58f);
            radialBurst.startNewSubPath(inner);
            radialBurst.lineTo(outer);
        }
        g.setColour(juce::Colour::fromRGBA(120, 220, 255, static_cast<uint8_t>(4 + 22 * barPulse)));
        g.strokePath(radialBurst, juce::PathStrokeType(1.0f));
    }
}

void MainComponent::drawHud(juce::Graphics& g, juce::Rectangle<float> area)
{
    if (isolatedSlab.isValid() && performanceMode)
        return;

    auto panel = area.reduced(12.0f, 10.0f);
    juce::ColourGradient hudGradient(juce::Colour::fromRGBA(9, 15, 38, 230),
                                     panel.getX(), panel.getY(),
                                     juce::Colour::fromRGBA(18, 31, 72, 216),
                                     panel.getRight(), panel.getBottom(),
                                     false);
    g.setGradientFill(hudGradient);
    g.fillRoundedRectangle(panel, 24.0f);
    g.setColour(juce::Colour::fromRGBA(122, 210, 255, 96));
    g.drawRoundedRectangle(panel, 24.0f, 1.6f);

    const auto modeLabel = isolatedSlab.isValid() ? (performanceMode ? "PERFORMANCE" : "EDIT VIEW")
                                                  : "BUILD MODE";
    if (! isolatedSlab.isValid())
    {
        const float juicePulse = 0.5f + 0.5f * static_cast<float>(std::sin(juce::Time::getMillisecondCounterHiRes() * 0.0024));
        auto inner = panel.reduced(18.0f, 14.0f);
        auto topRow = inner.removeFromTop(34.0f);
        auto badge = topRow.removeFromLeft(152.0f);
        topRow.removeFromLeft(12.0f);

        auto controlsCapsule = topRow.withTrimmedLeft(0.0f);
        g.setColour(juce::Colour::fromRGBA(8, 16, 38, 214));
        g.fillRoundedRectangle(controlsCapsule, 17.0f);
        g.setColour(juce::Colour::fromRGBA(110, 212, 255, 64));
        g.drawRoundedRectangle(controlsCapsule, 17.0f, 1.2f);

        g.setColour(juce::Colour::fromRGBA(72, 228, 255, static_cast<uint8_t>(28 + 18 * juicePulse)));
        g.fillRoundedRectangle(badge, 17.0f);
        g.setColour(juce::Colour::fromRGBA(140, 242, 255, static_cast<uint8_t>(154 + 40 * juicePulse)));
        g.drawRoundedRectangle(badge, 17.0f, 1.6f);
        g.setColour(juce::Colour::fromRGBA(255, 255, 255, static_cast<uint8_t>(10 + 12 * juicePulse)));
        g.fillRoundedRectangle(badge.withHeight(11.0f), 17.0f);
        g.setColour(juce::Colours::white);
        g.setFont(juce::FontOptions(15.5f));
        g.drawText(modeLabel, badge.toNearestInt(), juce::Justification::centred);

        g.setColour(juce::Colour::fromRGBA(226, 236, 255, 238));
        g.setFont(juce::FontOptions(15.5f));
        g.drawFittedText("WASD Pan   Wheel Zoom   Q/E Rotate   -/= Height   G View   R Randomise",
                         controlsCapsule.reduced(16.0f, 0.0f).toNearestInt(),
                         juce::Justification::centredLeft,
                         1);

        inner.removeFromTop(12.0f);

        auto infoRow = inner.removeFromTop(32.0f);
        auto drawChip = [&] (juce::Rectangle<float> bounds, const juce::String& label, const juce::String& value)
        {
            g.setColour(juce::Colour::fromRGBA(11, 20, 46, 208));
            g.fillRoundedRectangle(bounds, 13.0f);
            g.setColour(juce::Colour::fromRGBA(96, 184, 255, 46));
            g.drawRoundedRectangle(bounds, 13.0f, 1.0f);

            auto textArea = bounds.reduced(12.0f, 0.0f);
            auto labelArea = textArea.removeFromLeft(72.0f);
            g.setColour(juce::Colour::fromRGBA(132, 196, 255, 166));
            g.setFont(juce::FontOptions(12.0f));
            g.drawText(label, labelArea.toNearestInt(), juce::Justification::centredLeft);

            g.setColour(juce::Colours::white);
            g.setFont(juce::FontOptions(13.5f));
            g.drawText(value, textArea.toNearestInt(), juce::Justification::centredLeft);

            g.setColour(juce::Colour::fromRGBA(84, 226, 255, static_cast<uint8_t>(18 + 18 * juicePulse)));
            g.fillRoundedRectangle(bounds.withTrimmedTop(bounds.getHeight() - 3.0f).reduced(1.0f, 0.0f), 6.0f);
        };

        const float gap = 10.0f;
        const float chipW = (infoRow.getWidth() - gap * 3.0f) / 4.0f;
        drawChip(infoRow.removeFromLeft(chipW), "Filled", juce::String(voxelCount));
        infoRow.removeFromLeft(gap);
        drawChip(infoRow.removeFromLeft(chipW), "Rotation", juce::String(camera.rotation));
        infoRow.removeFromLeft(gap);
        drawChip(infoRow.removeFromLeft(chipW), "Zoom", juce::String(camera.zoom, 2));
        infoRow.removeFromLeft(gap);
        drawChip(infoRow.removeFromLeft(chipW), "z1", noteNameForHeight(1));
        return;
    }

    auto inner = panel.reduced(18.0f, 14.0f);
    auto topRow = inner.removeFromTop(36.0f);
    auto modePill = topRow.removeFromLeft(152.0f);
    topRow.removeFromLeft(12.0f);
    auto slabChip = topRow.removeFromLeft(168.0f);
    topRow.removeFromLeft(12.0f);
    auto detailChip = topRow;

    g.setColour(juce::Colour::fromRGBA(90, 236, 255, 28));
    g.fillRoundedRectangle(modePill, 14.0f);
    g.setColour(juce::Colour::fromRGBA(126, 240, 255, 146));
    g.drawRoundedRectangle(modePill, 14.0f, 1.4f);
    g.setColour(juce::Colours::white);
    g.setFont(juce::FontOptions(15.0f));
    g.drawText(modeLabel, modePill.toNearestInt(), juce::Justification::centred);

    g.setColour(juce::Colour::fromRGBA(12, 22, 52, 210));
    g.fillRoundedRectangle(slabChip, 14.0f);
    g.setColour(juce::Colour::fromRGBA(104, 190, 255, 64));
    g.drawRoundedRectangle(slabChip, 14.0f, 1.0f);
    g.setColour(juce::Colour::fromRGBA(198, 228, 255, 222));
    g.setFont(juce::FontOptions(13.5f));
    g.drawText(labelForSlab(isolatedSlab), slabChip.reduced(14.0f, 0.0f).toNearestInt(), juce::Justification::centredLeft);

    auto drawStat = [&] (juce::Rectangle<float> bounds, const juce::String& label, const juce::String& value)
    {
        g.setColour(juce::Colour::fromRGBA(12, 22, 52, 210));
        g.fillRoundedRectangle(bounds, 13.0f);
        g.setColour(juce::Colour::fromRGBA(102, 182, 255, 46));
        g.drawRoundedRectangle(bounds, 13.0f, 1.0f);
        auto statInner = bounds.reduced(10.0f, 5.0f);
        g.setColour(juce::Colour::fromRGBA(152, 216, 255, 170));
        g.setFont(juce::FontOptions(11.0f));
        g.drawText(label, statInner.removeFromTop(12.0f).toNearestInt(), juce::Justification::centredLeft);
        g.setColour(juce::Colours::white);
        g.setFont(juce::FontOptions(14.5f));
        g.drawText(value, statInner.toNearestInt(), juce::Justification::centredLeft);
    };

    g.setColour(juce::Colour::fromRGBA(12, 22, 52, 210));
    g.fillRoundedRectangle(detailChip, 14.0f);
    g.setColour(juce::Colour::fromRGBA(102, 182, 255, 46));
    g.drawRoundedRectangle(detailChip, 14.0f, 1.0f);
    g.setColour(juce::Colours::white);
    g.setFont(juce::FontOptions(14.0f));
    g.drawFittedText("Enter performance   WASD pan   Wheel zoom   Mouse snap   Click place   Right-click remove   Arrows move   [ ] height   C clear slab   Esc back",
                     detailChip.reduced(14.0f, 0.0f).toNearestInt(),
                     juce::Justification::centredLeft,
                     1);

    inner.removeFromTop(12.0f);

    auto statRow = inner.removeFromTop(56.0f);
    const float statGap = 10.0f;
    const float statWidth = (statRow.getWidth() - statGap * 3.0f) / 4.0f;
    drawStat(statRow.removeFromLeft(statWidth), "SYNTH", synthName());
    statRow.removeFromLeft(statGap);
    drawStat(statRow.removeFromLeft(statWidth), "DRUMS", drumModeName());
    statRow.removeFromLeft(statGap);
    drawStat(statRow.removeFromLeft(statWidth), "KEY", keyName());
    statRow.removeFromLeft(statGap);
    drawStat(statRow.removeFromLeft(statWidth), "SCALE", scaleName());

    inner.removeFromTop(10.0f);

    auto infoRow = inner.removeFromTop(28.0f);
    auto drawInfoChip = [&] (juce::Rectangle<float> bounds, const juce::String& text)
    {
        g.setColour(juce::Colour::fromRGBA(12, 22, 52, 198));
        g.fillRoundedRectangle(bounds, 12.0f);
        g.setColour(juce::Colour::fromRGBA(102, 182, 255, 42));
        g.drawRoundedRectangle(bounds, 12.0f, 1.0f);
        g.setColour(juce::Colour::fromRGBA(216, 232, 255, 226));
        g.setFont(juce::FontOptions(12.5f));
        g.drawText(text, bounds.reduced(12.0f, 0.0f).toNearestInt(), juce::Justification::centredLeft);
    };

    const float infoGap = 8.0f;
    const float infoWidth = (infoRow.getWidth() - infoGap * 2.0f) / 3.0f;
    drawInfoChip(infoRow.removeFromLeft(infoWidth),
                 "Cursor  x" + juce::String(editCursor.x) + "  y" + juce::String(editCursor.y) + "  z" + juce::String(editCursor.z));
    infoRow.removeFromLeft(infoGap);
    drawInfoChip(infoRow.removeFromLeft(infoWidth),
                 "Note  " + noteNameForHeight(editCursor.z));
    infoRow.removeFromLeft(infoGap);
    drawInfoChip(infoRow.removeFromLeft(infoWidth),
                 "M synth   B drums   K key   L scale   U quantize   C clear");
}

void MainComponent::drawBackdrop(juce::Graphics& g, juce::Rectangle<float> area)
{
    const float pulse = 0.5f + 0.5f * static_cast<float>(std::sin(juce::Time::getMillisecondCounterHiRes() * 0.0013));
    const float beatPulse = visualBeatPulse;
    const float barPulse = visualBarPulse;
    juce::ColourGradient bg(juce::Colour::fromRGB(4, 8, 24),
                            area.getCentreX(), area.getY(),
                            juce::Colour::fromRGB(14, 28, 82),
                            area.getCentreX(), area.getBottom(),
                            false);
    g.setGradientFill(bg);
    g.fillAll();

    g.setColour(juce::Colour::fromRGBA(62, 128, 255, static_cast<uint8_t>(34 + 18 * pulse)));
    g.fillEllipse(area.withSizeKeepingCentre(area.getWidth() * 0.82f, area.getHeight() * 0.56f)
                      .translated(0.0f, area.getHeight() * 0.08f));
    g.setColour(juce::Colour::fromRGBA(68, 232, 255, static_cast<uint8_t>(22 + 18 * pulse)));
    g.fillEllipse(area.withSizeKeepingCentre(area.getWidth() * 0.46f, area.getHeight() * 0.30f)
                      .translated(area.getWidth() * 0.10f, -area.getHeight() * 0.08f));

    juce::Path rings;
    const auto centre = area.getCentre();
    for (int i = 0; i < 6; ++i)
    {
        const float w = area.getWidth() * (0.20f + i * 0.12f);
        const float h = area.getHeight() * (0.12f + i * 0.08f);
        rings.addEllipse(juce::Rectangle<float>(w, h).withCentre({ centre.x, centre.y + area.getHeight() * 0.18f }));
    }
    g.setColour(juce::Colour::fromRGBA(120, 180, 255, static_cast<uint8_t>(14 + 10 * pulse)));
    g.strokePath(rings, juce::PathStrokeType(1.2f));

    juce::Path grid;
    constexpr int cols = 10;
    constexpr int rows = 7;
    for (int i = 0; i <= cols; ++i)
    {
        const float x = area.getX() + area.getWidth() * (static_cast<float>(i) / static_cast<float>(cols));
        grid.startNewSubPath(x, area.getCentreY() - 20.0f);
        grid.lineTo(centre.x + (x - centre.x) * 0.18f, area.getBottom());
    }
    for (int j = 0; j <= rows; ++j)
    {
        const float t = static_cast<float>(j) / static_cast<float>(rows);
        const float y = area.getCentreY() + t * t * area.getHeight() * 0.42f;
        grid.startNewSubPath(area.getX(), y);
        grid.lineTo(area.getRight(), y);
    }
    g.setColour(juce::Colour::fromRGBA(112, 164, 255, 12));
    g.strokePath(grid, juce::PathStrokeType(1.0f));

    g.setColour(juce::Colour::fromRGBA(160, 220, 255, static_cast<uint8_t>(8 + 8 * pulse)));
    g.drawLine(area.getX() + 32.0f, area.getY() + 112.0f, area.getRight() - 32.0f, area.getY() + 112.0f, 1.0f);

    juce::Path beatRings;
    const auto ringCentre = area.getCentre();
    for (int i = 0; i < 2; ++i)
    {
        const float expand = 1.0f + beatPulse * (0.12f + 0.06f * static_cast<float>(i));
        const float w = area.getWidth() * (0.34f + 0.16f * static_cast<float>(i)) * expand;
        const float h = area.getHeight() * (0.16f + 0.08f * static_cast<float>(i)) * expand;
        beatRings.addEllipse(juce::Rectangle<float>(w, h).withCentre({ ringCentre.x, ringCentre.y + area.getHeight() * 0.18f }));
    }
    g.setColour(juce::Colour::fromRGBA(170, 228, 255, static_cast<uint8_t>(8 + 26 * beatPulse + 16 * barPulse)));
    g.strokePath(beatRings, juce::PathStrokeType(1.0f + 1.2f * beatPulse));

    for (int i = 0; i < 24; ++i)
    {
        const float t = static_cast<float>(i) / 24.0f;
        const float x = area.getX() + area.getWidth() * std::fmod(0.11f * i + 0.02f * pulse, 1.0f);
        const float y = area.getY() + area.getHeight() * (0.08f + t * 0.72f);
        const float size = 1.4f + 1.8f * std::fmod(t * 7.0f + pulse, 1.0f);
        g.setColour(juce::Colour::fromRGBA(190, 230, 255, static_cast<uint8_t>(16 + 22 * std::fmod(t * 9.0f + pulse, 1.0f))));
        g.fillEllipse(juce::Rectangle<float>(size, size).withCentre({ x, y }));
    }
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

juce::String MainComponent::snakeTriggerModeName() const
{
    return snakeTriggerModeToString(snakeTriggerMode);
}

juce::String MainComponent::performanceAgentModeName() const
{
    return performanceAgentModeToString(performanceAgentMode);
}

int MainComponent::slabIndex(const SlabSelection& slab) const
{
    if (! slab.isValid())
        return 0;

    return juce::jlimit(0, 15, slab.floor * 4 + slab.quadrant);
}

void MainComponent::applyPerformancePresetForSlab(const SlabSelection& slab)
{
    if (! slab.isValid())
        return;

    const int index = slabIndex(slab);
    performanceAgentMode = slabPerformanceModes[static_cast<size_t>(index)];
    bpm = slabStartingTempos[static_cast<size_t>(index)];
    resetPerformanceAgents();
}

bool MainComponent::performanceTrackAt(juce::Point<int> cell) const
{
    return std::find_if(performanceTracks.begin(), performanceTracks.end(),
                        [cell] (const TrackPiece& track) { return track.cell == cell; }) != performanceTracks.end();
}

bool MainComponent::performanceTrackHorizontalAt(juce::Point<int> cell) const
{
    const auto it = std::find_if(performanceTracks.begin(), performanceTracks.end(),
                                 [cell] (const TrackPiece& track) { return track.cell == cell; });
    return it != performanceTracks.end() ? it->horizontal : true;
}

bool MainComponent::performanceOrbitCenterAt(juce::Point<int> cell) const
{
    return std::find(performanceOrbitCenters.begin(), performanceOrbitCenters.end(), cell) != performanceOrbitCenters.end();
}
