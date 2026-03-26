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
constexpr int isolatedBuildMaxHeight = 20;
constexpr int tetrisLayerCount = 13;
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

int boolToInt(bool value)
{
    return value ? 1 : 0;
}

int tetrominoTypeCount()
{
    return 7;
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

    if (screenMode == ScreenMode::title)
    {
        drawWireframeGrid(g, bounds.reduced(18.0f, 120.0f));
        drawTitleScreen(g, bounds);
        return;
    }

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
    if (isolatedSlab.isValid() && (performanceMode
                                   || isolatedBuildMode == IsolatedBuildMode::tetrisTopDown
                                   || isolatedBuildMode == IsolatedBuildMode::stampLibraryTopDown))
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
    if (screenMode == ScreenMode::title)
    {
        const auto nextAction = titleActionAt(event.position, getLocalBounds().toFloat());
        if (nextAction != hoveredTitleAction)
        {
            hoveredTitleAction = nextAction;
            repaint();
        }
        return;
    }

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

        if (isolatedBuildMode == IsolatedBuildMode::tetrisTopDown)
        {
            return;
        }

        if (isolatedBuildMode == IsolatedBuildMode::cellularAutomataTopDown)
        {
            auto bounds = getLocalBounds().toFloat();
            bounds.removeFromTop(112.0f);
            const auto nextCell = performanceCellAtPosition(event.position, bounds.reduced(12.0f));
            if (nextCell != automataHoverCell)
            {
                automataHoverCell = nextCell;
                if (automataHoverCell.has_value())
                    editCursor = { automataHoverCell->x, automataHoverCell->y, slabZStart(isolatedSlab) + automataBuildLayer, true };
                repaint();
            }
            return;
        }

        if (isolatedBuildMode == IsolatedBuildMode::stampLibraryTopDown)
        {
            auto bounds = getLocalBounds().toFloat();
            bounds.removeFromTop(112.0f);
            const auto nextCell = performanceCellAtPosition(event.position, bounds.reduced(12.0f));
            if (nextCell != stampHoverCell)
            {
                stampHoverCell = nextCell;
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
    if (screenMode == ScreenMode::title && hoveredTitleAction != TitleAction::none)
    {
        hoveredTitleAction = TitleAction::none;
        repaint();
    }

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
    if (screenMode == ScreenMode::title)
    {
        switch (titleActionAt(event.position, getLocalBounds().toFloat()))
        {
            case TitleAction::newWorld: enterWorldFromTitle(true); break;
            case TitleAction::saveWorld: showSaveDialog(); break;
            case TitleAction::loadWorld: showLoadDialog(); break;
            case TitleAction::none: break;
        }
        return;
    }

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
        const bool readOnlyTetrisPreview = tetrisVariantForSlab(isolatedSlab) != TetrisVariant::none
                                        && isolatedBuildMode == IsolatedBuildMode::cursor3D;
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

        if (isolatedBuildMode == IsolatedBuildMode::tetrisTopDown)
        {
            const auto clickedCell = performanceCellAtPosition(event.position, gridArea);
            if (clickedCell.has_value())
            {
                if (! tetrisPiece.active)
                    spawnTetrisPiece(false);

                tetrisPiece.anchor = *clickedCell;
                clampTetrisPieceToSlab(tetrisPiece);
                const bool remove = event.mods.isRightButtonDown() || event.mods.isCtrlDown();
                placeTetrisPiece(! remove);
                repaint();
                return;
            }
        }

        if (isolatedBuildMode == IsolatedBuildMode::cellularAutomataTopDown)
        {
            const auto clickedCell = performanceCellAtPosition(event.position, gridArea);
            if (clickedCell.has_value())
            {
                automataHoverCell = clickedCell;
                const bool remove = event.mods.isRightButtonDown() || event.mods.isCtrlDown();
                toggleAutomataCell(*clickedCell, ! remove);
                repaint();
                return;
            }
        }

        if (isolatedBuildMode == IsolatedBuildMode::stampLibraryTopDown)
        {
            const auto clickedCell = performanceCellAtPosition(event.position, gridArea);
            if (clickedCell.has_value())
            {
                stampHoverCell = clickedCell;
                if (stampCaptureMode)
                {
                    if (! stampCaptureAnchor.has_value())
                    {
                        stampCaptureAnchor = *clickedCell;
                    }
                    else
                    {
                        const auto xMin = juce::jmin(stampCaptureAnchor->x, clickedCell->x);
                        const auto yMin = juce::jmin(stampCaptureAnchor->y, clickedCell->y);
                        const auto xMax = juce::jmax(stampCaptureAnchor->x, clickedCell->x);
                        const auto yMax = juce::jmax(stampCaptureAnchor->y, clickedCell->y);
                        captureStampFromSelection({ xMin, yMin, xMax - xMin + 1, yMax - yMin + 1 });
                        stampCaptureMode = false;
                        stampCaptureAnchor.reset();
                    }
                    repaint();
                    return;
                }

                if (! stampLibrary.empty())
                {
                    const bool remove = event.mods.isRightButtonDown() || event.mods.isCtrlDown();
                    applyStampAtCell(stampLibrary[static_cast<size_t>(juce::jlimit(0, static_cast<int>(stampLibrary.size()) - 1, stampLibraryIndex))],
                                     *clickedCell,
                                     slabZStart(isolatedSlab) + stampBaseLayer,
                                     stampRotation,
                                     ! remove);
                }
                repaint();
                return;
            }
        }

        if (! readOnlyTetrisPreview && editCursor.active && cellInSelectedSlab(editCursor.x, editCursor.y, isolatedSlab))
        {
            const bool remove = event.mods.isRightButtonDown() || event.mods.isCtrlDown();
            applyEditPlacement(! remove);
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
        isolatedBuildMode = IsolatedBuildMode::cursor3D;
        tetrisPiece = {};
        nextTetrisType = TetrominoType::L;
        tetrisBuildLayer = 0;
        tetrisRotatePerLayerSession = false;
        tetrisGravityTick = 0;
        stampLibrary.clear();
        stampLibraryIndex = 0;
        stampRotation = 0;
        stampBaseLayer = 0;
        stampHoverCell.reset();
        stampCaptureMode = false;
        stampCaptureAnchor.reset();
        repaint();
        return;
    }

    if (slab.isValid())
    {
        isolatedSlab = slab;
        hoveredSlab = slab;
        resetEditCursor();
        editPlacementHeight = 1;
       editChordType = EditChordType::single;
        isolatedBuildMode = slabNumber(slab) >= 5 && slabNumber(slab) <= 8
                              ? IsolatedBuildMode::cursor3D
                              : slabNumber(slab) >= 9 && slabNumber(slab) <= 12
                                  ? IsolatedBuildMode::cellularAutomataTopDown
                              : slabNumber(slab) == 4
                              ? IsolatedBuildMode::cursor3D
                              : slabNumber(slab) <= 4
                                  ? IsolatedBuildMode::cursor3D
                                  : juce::Random::getSystemRandom().nextInt(4) == 0
                                      ? IsolatedBuildMode::stampLibraryTopDown
                                      : IsolatedBuildMode::cursor3D;
        performanceMode = false;
        tetrisPiece = {};
        nextTetrisType = TetrominoType::L;
        tetrisBuildLayer = 0;
        tetrisRotatePerLayerSession = tetrisVariantForSlab(slab) == TetrisVariant::standard
                                        ? juce::Random::getSystemRandom().nextBool()
                                        : false;
        tetrisGravityTick = 0;
        tetrisGravityFrames = 20;
        rouletteMirrorPlacement = false;
        rouletteRotatePerLayer = false;
        automataBuildLayer = 0;
        automataHoverCell = juce::Point<int>(editCursor.x, editCursor.y);
        rebuildStampLibrary();
        stampCaptureMode = false;
        stampCaptureAnchor.reset();
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
        if (isolatedBuildMode == IsolatedBuildMode::stampLibraryTopDown)
            stampHoverCell = juce::Point<int>(editCursor.x, editCursor.y);
        if (isolatedBuildMode == IsolatedBuildMode::cellularAutomataTopDown)
            editCursor.z = slabZStart(isolatedSlab) + automataBuildLayer;
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
    const int activeSlabNumber = isolatedSlab.isValid() ? slabNumber(isolatedSlab) : 0;
    const bool tetrisChiptuneMode = isolatedSlab.isValid()
                                 && isolatedBuildMode == IsolatedBuildMode::tetrisTopDown
                                 && ! performanceMode;
    const bool simCityBuildMode = isolatedSlab.isValid()
                               && ! performanceMode
                               && isolatedBuildMode == IsolatedBuildMode::cursor3D
                               && activeSlabNumber >= 1
                               && activeSlabNumber <= 4;
    const bool automataBuildMode = isolatedSlab.isValid()
                                && ! performanceMode
                                && isolatedBuildMode == IsolatedBuildMode::cellularAutomataTopDown
                                && activeSlabNumber >= 9
                                && activeSlabNumber <= 12;

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

        if (tetrisChiptuneMode)
        {
            auto addSynthEvent = [this, &midi, sampleOffset, &bufferToFill] (int midiNote, float velocity, float lengthSeconds)
            {
                midi.addEvent(juce::MidiMessage::noteOn(1, midiNote, juce::jlimit(0.0f, 1.0f, velocity * 0.80f)),
                              juce::jlimit(0, juce::jmax(0, bufferToFill.numSamples - 1), sampleOffset));
                pendingNoteOffs.push_back({ midiNote, lengthSeconds });
            };

            static constexpr std::array<int, 16> leadPatternA {
                76, -1, 71, -1, 72, -1, 74, -1,
                71, -1, 67, -1, 64, -1, 67, -1
            };
            static constexpr std::array<int, 16> leadPatternB {
                74, -1, 72, -1, 71, -1, 67, -1,
                64, -1, 67, -1, 71, -1, 72, -1
            };
            static constexpr std::array<int, 16> leadPatternC {
                76, -1, 79, -1, 77, -1, 76, -1,
                74, -1, 72, -1, 71, -1, 72, -1
            };
            static constexpr std::array<int, 16> leadPatternD {
                74, -1, 71, -1, 67, -1, 69, -1,
                71, -1, 72, -1, 74, -1, 76, -1
            };
            static constexpr std::array<int, 16> bassPatternA {
                40, -1, -1, -1, 47, -1, -1, -1,
                45, -1, -1, -1, 43, -1, -1, -1
            };
            static constexpr std::array<int, 16> bassPatternB {
                43, -1, -1, -1, 40, -1, -1, -1,
                47, -1, -1, -1, 45, -1, -1, -1
            };
            static constexpr std::array<int, 16> destructLeadPattern {
                76, 74, -1, 71, 72, -1, 67, 69,
                71, -1, 67, 64, 66, -1, 62, 64
            };
            static constexpr std::array<int, 16> destructBassPattern {
                35, -1, 35, -1, 42, -1, 42, -1,
                40, -1, 40, -1, 38, -1, 38, -1
            };
            static constexpr std::array<int, 16> fractureLeadPattern {
                79, -1, 76, 74, -1, 72, 71, -1,
                74, 76, -1, 79, 83, -1, 81, 79
            };
            static constexpr std::array<int, 16> fractureBassPattern {
                40, -1, -1, 47, -1, 45, -1, -1,
                43, -1, -1, 50, -1, 47, -1, -1
            };
            static constexpr std::array<int, 8> arpPattern {
                88, 83, 79, 83, 86, 81, 78, 81
            };
            static constexpr std::array<int, 8> fractureArpPattern {
                91, 86, 83, 79, 88, 84, 81, 76
            };
            static constexpr std::array<int, 16> rouletteLeadMirror {
                79, -1, 76, -1, 72, -1, 74, -1,
                71, -1, 67, -1, 69, -1, 71, -1
            };
            static constexpr std::array<int, 16> rouletteLeadGravity {
                84, 79, -1, 76, 74, -1, 72, 71,
                -1, 74, 76, -1, 79, 81, -1, 83
            };
            static constexpr std::array<int, 16> rouletteLeadRotate {
                76, -1, 83, -1, 79, -1, 74, -1,
                81, -1, 77, -1, 72, -1, 69, -1
            };
            static constexpr std::array<int, 16> rouletteBassPattern {
                40, -1, 47, -1, 45, -1, 52, -1,
                43, -1, 50, -1, 47, -1, 45, -1
            };

            const auto tetrisVariant = tetrisVariantForSlab(isolatedSlab);
            const int processPhase = beatBarIndex % 8;
            const int leadShift = processPhase % 4;
            const int bassShift = (beatBarIndex / 2) % 4;
            const int additiveWindow = 4 + processPhase;
            const bool addUpperPulse = processPhase >= 3;
            const bool addLowPedal = processPhase >= 5;
            const auto& classicLeadPattern = beatBarIndex % 4 == 0 ? leadPatternA
                                             : beatBarIndex % 4 == 1 ? leadPatternB
                                             : beatBarIndex % 4 == 2 ? leadPatternC
                                                                      : leadPatternD;
            const auto& classicBassPattern = (beatBarIndex % 2 == 0) ? bassPatternA : bassPatternB;

            switch (tetrisVariant)
            {
                case TetrisVariant::destruct:
                {
                    const int leadIndex = (step + leadShift) % 16;
                    if (step < additiveWindow)
                    {
                        if (const int leadNote = destructLeadPattern[static_cast<size_t>(leadIndex)]; leadNote >= 0)
                            addSynthEvent(leadNote, (step % 4 == 0) ? 0.42f : 0.28f, 0.08f);
                    }

                    const int bassIndex = (step + bassShift) % 16;
                    if (const int bassNote = destructBassPattern[static_cast<size_t>(bassIndex)]; bassNote >= 0)
                        addSynthEvent(bassNote, 0.22f, 0.12f);

                    if (addUpperPulse && (step == 3 || step == 7 || step == 11 || step == 15))
                    {
                        const int stab = destructLeadPattern[static_cast<size_t>((leadIndex + 1) % 16)];
                        if (stab >= 0)
                            addSynthEvent(stab - 12, 0.11f, 0.05f);
                    }

                    if (addLowPedal && step == 0)
                    {
                        addSynthEvent(35, 0.12f, 0.32f);
                    }
                    break;
                }

                case TetrisVariant::fracture:
                {
                    const int leadIndex = (step + leadShift) % 16;
                    if (step < additiveWindow)
                    {
                        if (const int leadNote = fractureLeadPattern[static_cast<size_t>(leadIndex)]; leadNote >= 0)
                            addSynthEvent(leadNote, 0.28f, 0.10f);
                    }

                    const int bassIndex = (step + bassShift) % 16;
                    if (const int bassNote = fractureBassPattern[static_cast<size_t>(bassIndex)]; bassNote >= 0)
                        addSynthEvent(bassNote, 0.19f, 0.14f);

                    const int arpIndex = (step + beatBarIndex * 2 + leadShift) % static_cast<int>(fractureArpPattern.size());
                    if ((step % 2) == 0 || addUpperPulse)
                        addSynthEvent(fractureArpPattern[static_cast<size_t>(arpIndex)], 0.09f, 0.05f);
                    break;
                }

                case TetrisVariant::roulette:
                {
                    const auto& rouletteLeadPattern = rouletteMirrorPlacement ? rouletteLeadMirror
                                                      : rouletteRotatePerLayer ? rouletteLeadRotate
                                                                               : rouletteLeadGravity;
                    const float leadVelocity = tetrisGravityFrames <= 12 ? 0.42f
                                               : tetrisGravityFrames >= 28 ? 0.28f
                                                                           : 0.35f;

                    const int leadIndex = (step + leadShift + (rouletteMirrorPlacement ? 1 : 0)) % 16;
                    if (step < additiveWindow)
                    {
                        if (const int leadNote = rouletteLeadPattern[static_cast<size_t>(leadIndex)]; leadNote >= 0)
                            addSynthEvent(leadNote + (rouletteRotatePerLayer ? 2 : 0), leadVelocity, 0.09f);
                    }

                    const int bassIndex = (step + bassShift) % 16;
                    if (const int bassNote = rouletteBassPattern[static_cast<size_t>(bassIndex)]; bassNote >= 0)
                        addSynthEvent(bassNote + (rouletteMirrorPlacement ? 5 : 0), 0.24f, 0.15f);

                    if ((step % 2) == 1 || addUpperPulse)
                    {
                        const int arpIndex = (step / 2 + beatBarIndex + leadShift + (rouletteMirrorPlacement ? 1 : 0))
                                           % static_cast<int>(arpPattern.size());
                        addSynthEvent(arpPattern[static_cast<size_t>(arpIndex)] + (rouletteRotatePerLayer ? 3 : 0),
                                      0.10f,
                                      0.05f);
                    }
                    break;
                }

                case TetrisVariant::standard:
                case TetrisVariant::none:
                default:
                {
                    const int leadIndex = (step + leadShift) % 16;
                    if (step < additiveWindow)
                    {
                        if (const int leadNote = classicLeadPattern[static_cast<size_t>(leadIndex)]; leadNote >= 0)
                            addSynthEvent(leadNote, 0.34f, 0.11f);
                    }

                    const int bassIndex = (step + bassShift) % 16;
                    if (const int bassNote = classicBassPattern[static_cast<size_t>(bassIndex)]; bassNote >= 0)
                        addSynthEvent(bassNote, 0.24f, 0.16f);

                    if ((step % 2) == 1 || addUpperPulse)
                    {
                        const int arpIndex = (step / 2 + beatBarIndex + leadShift) % static_cast<int>(arpPattern.size());
                        addSynthEvent(arpPattern[static_cast<size_t>(arpIndex)], 0.12f, 0.06f);
                    }

                    if (addLowPedal && step == 0)
                    {
                        addSynthEvent(classicBassPattern[0] - 12, 0.10f, 0.28f);
                    }
                    break;
                }
            }
        }

        if (simCityBuildMode)
        {
            auto addSynthEvent = [this, &midi, sampleOffset, &bufferToFill] (int midiNote, float velocity, float lengthSeconds)
            {
                midi.addEvent(juce::MidiMessage::noteOn(1, midiNote, juce::jlimit(0.0f, 1.0f, velocity * 1.18f)),
                              juce::jlimit(0, juce::jmax(0, bufferToFill.numSamples - 1), sampleOffset));
                pendingNoteOffs.push_back({ midiNote, lengthSeconds });
            };

            static constexpr std::array<int, 16> island1Lead {
                67, -1, -1, -1, 72, -1, -1, -1,
                71, -1, -1, -1, 69, -1, -1, -1
            };
            static constexpr std::array<int, 16> island2Lead {
                64, -1, -1, -1, 67, -1, -1, -1,
                66, -1, -1, -1, 62, -1, -1, -1
            };
            static constexpr std::array<int, 16> island3Lead {
                62, -1, -1, -1, 67, -1, -1, -1,
                71, -1, -1, -1, 69, -1, -1, -1
            };
            static constexpr std::array<int, 16> island4Lead {
                64, -1, -1, -1, 69, -1, -1, -1,
                72, -1, -1, -1, 67, -1, -1, -1
            };
            static constexpr std::array<int, 16> bassPatternLight {
                36, -1, -1, -1, -1, -1, -1, -1,
                43, -1, -1, -1, -1, -1, -1, -1
            };
            static constexpr std::array<int, 16> bassPatternDark {
                31, -1, -1, -1, -1, -1, -1, -1,
                38, -1, -1, -1, -1, -1, -1, -1
            };
            static constexpr std::array<int, 8> padIsland1 { 79, 74, 71, 74, 76, 72, 69, 72 };
            static constexpr std::array<int, 8> padIsland2 { 72, 67, 64, 67, 71, 66, 62, 66 };
            static constexpr std::array<int, 8> padIsland3 { 74, 69, 66, 69, 71, 67, 64, 67 };
            static constexpr std::array<int, 8> padIsland4 { 76, 72, 69, 72, 79, 74, 71, 74 };
            static constexpr std::array<int, 4> chordRoots1 { 55, 52, 50, 48 };
            static constexpr std::array<int, 4> chordRoots2 { 48, 45, 43, 41 };
            static constexpr std::array<int, 4> chordRoots3 { 46, 43, 41, 38 };
            static constexpr std::array<int, 4> chordRoots4 { 52, 48, 45, 50 };
            static constexpr std::array<std::array<int, 3>, 4> chordIntervals1 {{
                {{ 0, 3, 10 }}, {{ 0, 5, 10 }}, {{ 0, 3, 8 }}, {{ 0, 3, 10 }}
            }};
            static constexpr std::array<std::array<int, 3>, 4> chordIntervals2 {{
                {{ 0, 3, 10 }}, {{ 0, 3, 8 }}, {{ 0, 5, 10 }}, {{ 0, 3, 10 }}
            }};
            static constexpr std::array<std::array<int, 3>, 4> chordIntervals3 {{
                {{ 0, 3, 8 }}, {{ 0, 3, 10 }}, {{ 0, 5, 10 }}, {{ 0, 3, 8 }}
            }};
            static constexpr std::array<std::array<int, 3>, 4> chordIntervals4 {{
                {{ 0, 5, 10 }}, {{ 0, 3, 10 }}, {{ 0, 3, 8 }}, {{ 0, 5, 10 }}
            }};

            const auto& leadPattern = activeSlabNumber == 1 ? island1Lead
                                      : activeSlabNumber == 2 ? island2Lead
                                      : activeSlabNumber == 3 ? island3Lead
                                                               : island4Lead;
            const auto& padPattern = activeSlabNumber == 1 ? padIsland1
                                     : activeSlabNumber == 2 ? padIsland2
                                     : activeSlabNumber == 3 ? padIsland3
                                                              : padIsland4;
            const auto& bassPattern = (activeSlabNumber == 2 || activeSlabNumber == 3) ? bassPatternDark
                                                                                        : bassPatternLight;
            const auto& chordRoots = activeSlabNumber == 1 ? chordRoots1
                                     : activeSlabNumber == 2 ? chordRoots2
                                     : activeSlabNumber == 3 ? chordRoots3
                                                              : chordRoots4;
            const auto& chordIntervals = activeSlabNumber == 1 ? chordIntervals1
                                         : activeSlabNumber == 2 ? chordIntervals2
                                         : activeSlabNumber == 3 ? chordIntervals3
                                                                  : chordIntervals4;
            const bool darkIsland = activeSlabNumber == 2 || activeSlabNumber == 3;
            const int processPhase = beatBarIndex % 8;
            const int leadShift = processPhase % 4;
            const int padShift = (beatBarIndex / 2) % 4;
            const int additiveWindow = 3 + processPhase;
            const bool gentleBassStep = step == 0 || (processPhase >= 6 && step == 8);
            const bool padStep = step == 4 || (processPhase >= 5 && step == 12);
            const bool echoLead = processPhase >= 4;
            const bool lowPedal = processPhase >= 6;
            const bool chordStep = step == 0 || (processPhase >= 4 && step == 8);

            const int leadIndex = (step + leadShift) % 16;
            if (step < additiveWindow && (step % 4) == 0)
            {
                if (const int leadNote = leadPattern[static_cast<size_t>(leadIndex)]; leadNote >= 0)
                    addSynthEvent(leadNote - 12, darkIsland ? 0.10f : 0.13f, darkIsland ? 0.58f : 0.48f);
            }

            if (gentleBassStep)
            {
                const int bassIndex = (step + padShift) % 16;
                if (const int bassNote = bassPattern[static_cast<size_t>(bassIndex)]; bassNote >= 0)
                    addSynthEvent(bassNote - 5, darkIsland ? 0.08f : 0.10f, darkIsland ? 0.78f : 0.62f);
            }

            if (padStep)
            {
                const int padIndex = ((step == 4 ? 0 : step == 8 ? 2 : 4) + beatBarIndex + padShift)
                                   % static_cast<int>(padPattern.size());
                addSynthEvent(padPattern[static_cast<size_t>(padIndex)] - 12,
                              darkIsland ? 0.05f : 0.06f,
                              darkIsland ? 0.92f : 0.76f);
                addSynthEvent(padPattern[static_cast<size_t>((padIndex + 1) % static_cast<int>(padPattern.size()))] - 24,
                              darkIsland ? 0.04f : 0.05f,
                              darkIsland ? 0.92f : 0.76f);
            }

            if (echoLead && (step == 6 || step == 14))
            {
                const int echoIndex = (leadIndex + 1) % 16;
                if (const int echoNote = leadPattern[static_cast<size_t>(echoIndex)]; echoNote >= 0)
                    addSynthEvent(echoNote - 24, darkIsland ? 0.05f : 0.06f, 0.34f);
            }

            if (lowPedal && step == 0)
            {
                addSynthEvent((darkIsland ? 26 : 31) - (processPhase >= 7 ? 5 : 0), 0.05f, 1.10f);
            }

            if (chordStep)
            {
                const int chordIndex = ((step == 0 ? 0 : 2) + beatBarIndex) % static_cast<int>(chordRoots.size());
                const int root = chordRoots[static_cast<size_t>(chordIndex)] - (darkIsland ? 12 : 7);
                const auto& intervals = chordIntervals[static_cast<size_t>(chordIndex)];
                addSynthEvent(root, darkIsland ? 0.045f : 0.05f, darkIsland ? 1.45f : 1.20f);
                addSynthEvent(root + intervals[1], darkIsland ? 0.038f : 0.043f, darkIsland ? 1.45f : 1.20f);
                addSynthEvent(root + intervals[2], darkIsland ? 0.032f : 0.037f, darkIsland ? 1.45f : 1.20f);
            }
        }

        if (automataBuildMode)
        {
            auto addSynthEvent = [this, &midi, sampleOffset, &bufferToFill] (int midiNote, float velocity, float lengthSeconds)
            {
                midi.addEvent(juce::MidiMessage::noteOn(1, midiNote, juce::jlimit(0.0f, 1.0f, velocity * 0.82f)),
                              juce::jlimit(0, juce::jmax(0, bufferToFill.numSamples - 1), sampleOffset));
                pendingNoteOffs.push_back({ midiNote, lengthSeconds });
            };

            static constexpr std::array<int, 16> lifeLead {
                72, -1, -1, 76, -1, -1, 79, -1,
                76, -1, -1, 74, -1, -1, 71, -1
            };
            static constexpr std::array<int, 16> lifeBass {
                43, -1, -1, -1, 50, -1, -1, -1,
                47, -1, -1, -1, 45, -1, -1, -1
            };
            static constexpr std::array<int, 8> lifePulse { 84, 79, 76, 79, 83, 79, 74, 79 };

            static constexpr std::array<int, 16> coralLead {
                67, -1, 71, -1, 74, -1, 76, -1,
                79, -1, 76, -1, 74, -1, 71, -1
            };
            static constexpr std::array<int, 16> coralBass {
                36, -1, -1, -1, 43, -1, -1, -1,
                41, -1, -1, -1, 38, -1, -1, -1
            };
            static constexpr std::array<int, 8> coralPulse { 79, 83, 86, 83, 88, 84, 81, 84 };

            static constexpr std::array<int, 16> fredkinLead {
                79, 74, -1, 71, 76, -1, 72, 67,
                -1, 74, 79, -1, 83, 76, -1, 72
            };
            static constexpr std::array<int, 16> fredkinBass {
                31, -1, 43, -1, 36, -1, 48, -1,
                33, -1, 45, -1, 38, -1, 50, -1
            };
            static constexpr std::array<int, 8> fredkinPulse { 91, 84, 79, 84, 88, 81, 76, 81 };

            static constexpr std::array<int, 16> dayNightLead {
                71, -1, 74, 76, -1, 79, -1, 83,
                81, -1, 78, 76, -1, 74, -1, 71
            };
            static constexpr std::array<int, 16> dayNightBass {
                38, -1, -1, 45, -1, -1, 50, -1,
                43, -1, -1, 47, -1, -1, 52, -1
            };
            static constexpr std::array<int, 8> dayNightPulse { 86, 83, 79, 83, 90, 86, 81, 86 };

            const auto variant = automataVariantForSlab(isolatedSlab);
            const int processPhase = beatBarIndex % 8;
            const int leadShift = processPhase % 4;
            const int bassShift = (beatBarIndex / 2) % 4;
            const int additiveWindow = 4 + processPhase;
            const bool phaseEcho = processPhase >= 3;
            const bool highPulse = processPhase >= 5;
            const bool lowPedal = processPhase >= 6;

            const std::array<int, 16>* leadPattern = &lifeLead;
            const std::array<int, 16>* bassPattern = &lifeBass;
            const std::array<int, 8>* pulsePattern = &lifePulse;
            float leadVelocity = 0.18f;
            float bassVelocity = 0.13f;
            float pulseVelocity = 0.07f;
            float leadLength = 0.16f;
            float bassLength = 0.34f;
            float pulseLength = 0.08f;
            int pedalNote = 31;

            switch (variant)
            {
                case AutomataVariant::coral:
                    leadPattern = &coralLead;
                    bassPattern = &coralBass;
                    pulsePattern = &coralPulse;
                    leadVelocity = 0.15f;
                    bassVelocity = 0.14f;
                    pulseVelocity = 0.06f;
                    leadLength = 0.22f;
                    bassLength = 0.42f;
                    pulseLength = 0.10f;
                    pedalNote = 29;
                    break;
                case AutomataVariant::fredkin:
                    leadPattern = &fredkinLead;
                    bassPattern = &fredkinBass;
                    pulsePattern = &fredkinPulse;
                    leadVelocity = 0.20f;
                    bassVelocity = 0.12f;
                    pulseVelocity = 0.06f;
                    leadLength = 0.10f;
                    bassLength = 0.24f;
                    pulseLength = 0.06f;
                    pedalNote = 26;
                    break;
                case AutomataVariant::dayNight:
                    leadPattern = &dayNightLead;
                    bassPattern = &dayNightBass;
                    pulsePattern = &dayNightPulse;
                    leadVelocity = 0.17f;
                    bassVelocity = 0.14f;
                    pulseVelocity = 0.08f;
                    leadLength = 0.18f;
                    bassLength = 0.36f;
                    pulseLength = 0.09f;
                    pedalNote = 33;
                    break;
                case AutomataVariant::life:
                case AutomataVariant::none:
                default:
                    break;
            }

            const int leadIndex = (step + leadShift) % 16;
            if (step < additiveWindow)
            {
                if (const int leadNote = (*leadPattern)[static_cast<size_t>(leadIndex)]; leadNote >= 0)
                    addSynthEvent(leadNote, leadVelocity, leadLength);
            }

            const int bassIndex = (step + bassShift) % 16;
            if ((step % 4) == 0 || variant == AutomataVariant::fredkin)
            {
                if (const int bassNote = (*bassPattern)[static_cast<size_t>(bassIndex)]; bassNote >= 0)
                    addSynthEvent(bassNote, bassVelocity, bassLength);
            }

            if ((step % 2) == 0 || highPulse)
            {
                const int pulseIndex = (step / 2 + beatBarIndex + leadShift) % static_cast<int>(pulsePattern->size());
                addSynthEvent((*pulsePattern)[static_cast<size_t>(pulseIndex)], pulseVelocity, pulseLength);
            }

            if (phaseEcho && (step == 6 || step == 14))
            {
                const int echoIndex = (leadIndex + 1) % 16;
                if (const int echoNote = (*leadPattern)[static_cast<size_t>(echoIndex)]; echoNote >= 0)
                    addSynthEvent(echoNote - 12, pulseVelocity, 0.12f);
            }

            if (lowPedal && step == 0)
                addSynthEvent(pedalNote, 0.05f, variant == AutomataVariant::coral ? 0.92f : 0.72f);
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

    const auto previousSynthEngine = synthEngine;
    if (tetrisChiptuneMode || simCityBuildMode || automataBuildMode)
        synthEngine = SynthEngine::chipPulse;

    synth.renderNextBlock(*bufferToFill.buffer, midi, bufferToFill.startSample, bufferToFill.numSamples);

    if (tetrisChiptuneMode || simCityBuildMode || automataBuildMode)
        synthEngine = previousSynthEngine;

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
    const auto modifiers = key.getModifiers();
    const auto keyChar = static_cast<juce_wchar>(key.getTextCharacter());
    const auto lowerChar = juce::CharacterFunctions::toLowerCase(keyChar);
    const auto keyCode = juce::CharacterFunctions::toLowerCase(static_cast<juce_wchar>(key.getKeyCode()));
    const bool commandShortcutDown = modifiers.isCommandDown() || modifiers.isCtrlDown();
    const bool saveShortcut = commandShortcutDown && keyCode == 's';
    const bool loadShortcut = commandShortcutDown && keyCode == 'o';

    if (screenMode == ScreenMode::title)
    {
        if (key == juce::KeyPress::returnKey || key == juce::KeyPress::spaceKey || lowerChar == 'n')
        {
            enterWorldFromTitle(true);
            return true;
        }

        if (keyCode == 's' && ! commandShortcutDown)
        {
            showSaveDialog();
            return true;
        }

        if ((keyCode == 'l' && ! commandShortcutDown) || loadShortcut)
        {
            showLoadDialog();
            return true;
        }
    }

    if (saveShortcut)
    {
        showSaveDialog();
        return true;
    }

    if (loadShortcut)
    {
        showLoadDialog();
        return true;
    }

    if (isolatedSlab.isValid())
    {
        const bool readOnlyTetrisPreview = tetrisVariantForSlab(isolatedSlab) != TetrisVariant::none
                                        && isolatedBuildMode == IsolatedBuildMode::cursor3D;
        if (key == juce::KeyPress::returnKey)
        {
            if (isolatedBuildMode == IsolatedBuildMode::tetrisTopDown && ! performanceMode)
            {
                tetrisBuildLayer = juce::jlimit(0, tetrisLayerCount - 1, tetrisBuildLayer + 1);
                if (tetrisRotatePerLayerSession || (tetrisVariantForSlab(isolatedSlab) == TetrisVariant::roulette && rouletteRotatePerLayer))
                    rotateIsolatedSlabQuarterTurn();
                if (editCursor.active)
                    editCursor.z = slabZStart(isolatedSlab) + tetrisBuildLayer;
                if (tetrisPiece.active)
                    tetrisPiece.z = slabZStart(isolatedSlab) + tetrisBuildLayer;
                else
                    spawnTetrisPiece(true);
            }
            else if (isolatedBuildMode == IsolatedBuildMode::cellularAutomataTopDown && ! performanceMode)
            {
                advanceAutomataLayer();
            }
            else
            {
                performanceMode = ! performanceMode;
                if (performanceMode && performanceSnakes.empty() && performanceAutomataCells.empty())
                    setPerformanceSnakeCount(performanceAgentCount);
            }
            repaint();
            return true;
        }
        if (key == juce::KeyPress::tabKey && ! performanceMode)
        {
            if (slabNumber(isolatedSlab) >= 5 && slabNumber(isolatedSlab) <= 8)
                isolatedBuildMode = isolatedBuildMode == IsolatedBuildMode::tetrisTopDown
                                      ? IsolatedBuildMode::cursor3D
                                      : IsolatedBuildMode::tetrisTopDown;
            else if (slabNumber(isolatedSlab) >= 9 && slabNumber(isolatedSlab) <= 12)
                isolatedBuildMode = IsolatedBuildMode::cellularAutomataTopDown;
            else if (slabNumber(isolatedSlab) == 4)
                isolatedBuildMode = isolatedBuildMode == IsolatedBuildMode::stampLibraryTopDown
                                      ? IsolatedBuildMode::cursor3D
                                      : IsolatedBuildMode::stampLibraryTopDown;
            else if (slabNumber(isolatedSlab) <= 4)
                isolatedBuildMode = IsolatedBuildMode::cursor3D;
            else
                isolatedBuildMode = isolatedBuildMode == IsolatedBuildMode::cursor3D
                                      ? IsolatedBuildMode::tetrisTopDown
                                      : isolatedBuildMode == IsolatedBuildMode::tetrisTopDown
                                          ? IsolatedBuildMode::stampLibraryTopDown
                                          : IsolatedBuildMode::cursor3D;
            if (isolatedBuildMode == IsolatedBuildMode::tetrisTopDown)
            {
                tetrisBuildLayer = 0;
                tetrisRotatePerLayerSession = tetrisVariantForSlab(isolatedSlab) == TetrisVariant::standard
                                                ? juce::Random::getSystemRandom().nextBool()
                                                : false;
                tetrisGravityFrames = 20;
                rouletteMirrorPlacement = false;
                rouletteRotatePerLayer = false;
                editCursor.z = slabZStart(isolatedSlab) + tetrisBuildLayer;
                spawnTetrisPiece(true);
            }
            else if (isolatedBuildMode == IsolatedBuildMode::stampLibraryTopDown)
            {
                rebuildStampLibrary();
                stampHoverCell = juce::Point<int>(editCursor.x, editCursor.y);
            }
            else if (isolatedBuildMode == IsolatedBuildMode::cellularAutomataTopDown)
            {
                automataBuildLayer = 0;
                automataHoverCell = juce::Point<int>(editCursor.x, editCursor.y);
                editCursor.z = slabZStart(isolatedSlab) + automataBuildLayer;
            }
            repaint();
            return true;
        }
        if (key == juce::KeyPress::escapeKey) { isolatedSlab = {}; hoveredSlab = {}; editCursor = {}; isolatedBuildMode = IsolatedBuildMode::cursor3D; tetrisPiece = {}; nextTetrisType = TetrominoType::L; tetrisBuildLayer = 0; tetrisRotatePerLayerSession = false; tetrisGravityTick = 0; tetrisGravityFrames = 20; rouletteMirrorPlacement = false; rouletteRotatePerLayer = false; stampLibrary.clear(); stampLibraryIndex = 0; stampRotation = 0; stampBaseLayer = 0; stampHoverCell.reset(); stampCaptureMode = false; stampCaptureAnchor.reset(); automataBuildLayer = 0; automataHoverCell.reset(); performanceMode = false; performanceRegionMode = 2; performanceSnakes.clear(); performanceDiscs.clear(); performanceTracks.clear(); performanceOrbitCenters.clear(); performanceAutomataCells.clear(); performanceFlashes.clear(); performanceHoverCell.reset(); performanceSelection = {}; performancePlacementMode = PerformancePlacementMode::selectOnly; repaint(); return true; }

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

        if (isolatedBuildMode == IsolatedBuildMode::tetrisTopDown)
        {
            if (key == juce::KeyPress::leftKey) { moveTetrisPiece(-1, 0, 0); repaint(); return true; }
            if (key == juce::KeyPress::rightKey) { moveTetrisPiece(1, 0, 0); repaint(); return true; }
            if (key == juce::KeyPress::upKey) { moveTetrisPiece(0, -1, 0); repaint(); return true; }
            if (key == juce::KeyPress::downKey) { moveTetrisPiece(0, 1, 0); repaint(); return true; }
            if (key == juce::KeyPress::pageUpKey || key == juce::KeyPress(']')) { tetrisBuildLayer = juce::jlimit(0, tetrisLayerCount - 1, tetrisBuildLayer + 1); tetrisPiece.z = slabZStart(isolatedSlab) + tetrisBuildLayer; repaint(); return true; }
            if (key == juce::KeyPress::pageDownKey || key == juce::KeyPress('[')) { tetrisBuildLayer = juce::jlimit(0, tetrisLayerCount - 1, tetrisBuildLayer - 1); tetrisPiece.z = slabZStart(isolatedSlab) + tetrisBuildLayer; repaint(); return true; }
            if (key == juce::KeyPress('1')) { editPlacementHeight = 1; repaint(); return true; }
            if (key == juce::KeyPress('2')) { editPlacementHeight = 2; repaint(); return true; }
            if (key == juce::KeyPress('3')) { editPlacementHeight = 3; repaint(); return true; }
            if (key == juce::KeyPress('4')) { editPlacementHeight = 4; repaint(); return true; }
            if (key == juce::KeyPress('v')) { editChordType = static_cast<EditChordType>((static_cast<int>(editChordType) + 1) % 8); repaint(); return true; }
            if (key == juce::KeyPress('r')) { rotateTetrisPiece(); repaint(); return true; }
            if (key == juce::KeyPress('n')) { spawnTetrisPiece(true); repaint(); return true; }
            if (key == juce::KeyPress('s')) { softDropTetrisPiece(); repaint(); return true; }
            if (key == juce::KeyPress::spaceKey) { hardDropTetrisPiece(); repaint(); return true; }
            if (key == juce::KeyPress('p')) { placeTetrisPiece(true); repaint(); return true; }
            if (key == juce::KeyPress::deleteKey || key == juce::KeyPress::backspaceKey || key == juce::KeyPress('x')) { placeTetrisPiece(false); repaint(); return true; }
            if (key == juce::KeyPress('c')) { clearIsolatedSlab(); repaint(); return true; }
            return true;
        }

        if (isolatedBuildMode == IsolatedBuildMode::stampLibraryTopDown)
        {
            if (key == juce::KeyPress::leftKey && stampHoverCell.has_value()) { stampHoverCell = juce::Point<int>(stampHoverCell->x - 1, stampHoverCell->y); repaint(); return true; }
            if (key == juce::KeyPress::rightKey && stampHoverCell.has_value()) { stampHoverCell = juce::Point<int>(stampHoverCell->x + 1, stampHoverCell->y); repaint(); return true; }
            if (key == juce::KeyPress::upKey && stampHoverCell.has_value()) { stampHoverCell = juce::Point<int>(stampHoverCell->x, stampHoverCell->y - 1); repaint(); return true; }
            if (key == juce::KeyPress::downKey && stampHoverCell.has_value()) { stampHoverCell = juce::Point<int>(stampHoverCell->x, stampHoverCell->y + 1); repaint(); return true; }
            if (key == juce::KeyPress::pageUpKey || key == juce::KeyPress(']')) { stampBaseLayer = juce::jlimit(0, isolatedBuildMaxHeight - 1, stampBaseLayer + 1); repaint(); return true; }
            if (key == juce::KeyPress::pageDownKey || key == juce::KeyPress('[')) { stampBaseLayer = juce::jlimit(0, isolatedBuildMaxHeight - 1, stampBaseLayer - 1); repaint(); return true; }
            if (key == juce::KeyPress('s')) { stampCaptureMode = ! stampCaptureMode; stampCaptureAnchor.reset(); repaint(); return true; }
            if (key == juce::KeyPress('n')) { if (! stampLibrary.empty()) stampLibraryIndex = (stampLibraryIndex + 1) % static_cast<int>(stampLibrary.size()); repaint(); return true; }
            if (key == juce::KeyPress('b')) { if (! stampLibrary.empty()) stampLibraryIndex = (stampLibraryIndex + static_cast<int>(stampLibrary.size()) - 1) % static_cast<int>(stampLibrary.size()); repaint(); return true; }
            if (key == juce::KeyPress('r')) { stampRotation = (stampRotation + 1) % 4; repaint(); return true; }
            if (key == juce::KeyPress('p') && stampHoverCell.has_value() && ! stampLibrary.empty()) { applyStampAtCell(stampLibrary[static_cast<size_t>(stampLibraryIndex)], *stampHoverCell, slabZStart(isolatedSlab) + stampBaseLayer, stampRotation, true); repaint(); return true; }
            if ((key == juce::KeyPress::deleteKey || key == juce::KeyPress::backspaceKey || key == juce::KeyPress('x')) && stampHoverCell.has_value() && ! stampLibrary.empty()) { applyStampAtCell(stampLibrary[static_cast<size_t>(stampLibraryIndex)], *stampHoverCell, slabZStart(isolatedSlab) + stampBaseLayer, stampRotation, false); repaint(); return true; }
            if (key == juce::KeyPress('c')) { clearIsolatedSlab(); repaint(); return true; }
            return true;
        }

        if (isolatedBuildMode == IsolatedBuildMode::cellularAutomataTopDown)
        {
            if (key == juce::KeyPress::leftKey && automataHoverCell.has_value()) { automataHoverCell = juce::Point<int>(automataHoverCell->x - 1, automataHoverCell->y); repaint(); return true; }
            if (key == juce::KeyPress::rightKey && automataHoverCell.has_value()) { automataHoverCell = juce::Point<int>(automataHoverCell->x + 1, automataHoverCell->y); repaint(); return true; }
            if (key == juce::KeyPress::upKey && automataHoverCell.has_value()) { automataHoverCell = juce::Point<int>(automataHoverCell->x, automataHoverCell->y - 1); repaint(); return true; }
            if (key == juce::KeyPress::downKey && automataHoverCell.has_value()) { automataHoverCell = juce::Point<int>(automataHoverCell->x, automataHoverCell->y + 1); repaint(); return true; }
            if (key == juce::KeyPress::pageUpKey || key == juce::KeyPress(']')) { automataBuildLayer = juce::jlimit(0, isolatedBuildMaxHeight - 1, automataBuildLayer + 1); if (automataHoverCell.has_value()) editCursor = { automataHoverCell->x, automataHoverCell->y, slabZStart(isolatedSlab) + automataBuildLayer, true }; repaint(); return true; }
            if (key == juce::KeyPress::pageDownKey || key == juce::KeyPress('[')) { automataBuildLayer = juce::jlimit(0, isolatedBuildMaxHeight - 1, automataBuildLayer - 1); if (automataHoverCell.has_value()) editCursor = { automataHoverCell->x, automataHoverCell->y, slabZStart(isolatedSlab) + automataBuildLayer, true }; repaint(); return true; }
            if (key == juce::KeyPress('n')) { randomiseAutomataSeed(); repaint(); return true; }
            if (key == juce::KeyPress('p') && automataHoverCell.has_value()) { toggleAutomataCell(*automataHoverCell, true); repaint(); return true; }
            if ((key == juce::KeyPress::deleteKey || key == juce::KeyPress::backspaceKey || key == juce::KeyPress('x')) && automataHoverCell.has_value()) { toggleAutomataCell(*automataHoverCell, false); repaint(); return true; }
            if (key == juce::KeyPress('c')) { clearIsolatedSlab(); automataBuildLayer = 0; repaint(); return true; }
            return true;
        }

        if (key == juce::KeyPress::leftKey) { moveEditCursor(-1, 0, 0); repaint(); return true; }
        if (key == juce::KeyPress::rightKey) { moveEditCursor(1, 0, 0); repaint(); return true; }
        if (key == juce::KeyPress::upKey) { moveEditCursor(0, -1, 0); repaint(); return true; }
        if (key == juce::KeyPress::downKey) { moveEditCursor(0, 1, 0); repaint(); return true; }
        if (key == juce::KeyPress::pageUpKey || key == juce::KeyPress(']')) { moveEditCursor(0, 0, 1); repaint(); return true; }
        if (key == juce::KeyPress::pageDownKey || key == juce::KeyPress('[')) { moveEditCursor(0, 0, -1); repaint(); return true; }
        if (key == juce::KeyPress('1')) { editPlacementHeight = 1; repaint(); return true; }
        if (key == juce::KeyPress('2')) { editPlacementHeight = 2; repaint(); return true; }
        if (key == juce::KeyPress('3')) { editPlacementHeight = 3; repaint(); return true; }
        if (key == juce::KeyPress('4')) { editPlacementHeight = 4; repaint(); return true; }
        if (key == juce::KeyPress('v'))
        {
            editChordType = static_cast<EditChordType>((static_cast<int>(editChordType) + 1) % 8);
            repaint();
            return true;
        }
        if (key == juce::KeyPress('p'))
        {
            if (! readOnlyTetrisPreview && editCursor.active)
                applyEditPlacement(true);
            repaint();
            return true;
        }
        if (key == juce::KeyPress('c'))
        {
            if (! readOnlyTetrisPreview)
                clearIsolatedSlab();
            repaint();
            return true;
        }
        if (key == juce::KeyPress::deleteKey || key == juce::KeyPress::backspaceKey || key == juce::KeyPress('x'))
        {
            if (! readOnlyTetrisPreview && editCursor.active)
                applyEditPlacement(false);
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

    if (isolatedSlab.isValid() && isolatedBuildMode == IsolatedBuildMode::tetrisTopDown && ! performanceMode)
    {
        ++tetrisGravityTick;
        if (tetrisGravityTick >= tetrisGravityFrames)
        {
            tetrisGravityTick = 0;
            advanceTetrisGravity();
            needsRepaint = true;
        }
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
    editPlacementHeight = 1;
    editChordType = EditChordType::single;
    isolatedBuildMode = IsolatedBuildMode::cursor3D;
    tetrisPiece = {};
    nextTetrisType = TetrominoType::L;
    tetrisBuildLayer = 0;
    tetrisRotatePerLayerSession = false;
    tetrisGravityTick = 0;
    tetrisGravityFrames = 20;
    rouletteMirrorPlacement = false;
    rouletteRotatePerLayer = false;
    stampLibrary.clear();
    stampLibraryIndex = 0;
    stampRotation = 0;
    stampBaseLayer = 0;
    stampHoverCell.reset();
    stampCaptureMode = false;
    stampCaptureAnchor.reset();
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
    editPlacementHeight = 1;
    editChordType = EditChordType::single;
    isolatedBuildMode = IsolatedBuildMode::cursor3D;
    tetrisPiece = {};
    nextTetrisType = TetrominoType::L;
    tetrisBuildLayer = 0;
    tetrisRotatePerLayerSession = false;
    tetrisGravityTick = 0;
    tetrisGravityFrames = 20;
    rouletteMirrorPlacement = false;
    rouletteRotatePerLayer = false;
    stampLibrary.clear();
    stampLibraryIndex = 0;
    stampRotation = 0;
    stampBaseLayer = 0;
    stampHoverCell.reset();
    stampCaptureMode = false;
    stampCaptureAnchor.reset();
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
    if (isolatedSlab.isValid())
        return z - slabZStart(isolatedSlab);

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

int MainComponent::slabZStart(const SlabSelection& slab) const
{
    return slab.floor * floorBandHeight;
}

int MainComponent::slabZEndExclusive(const SlabSelection& slab) const
{
    if (! slab.isValid())
        return 0;

    const int zStart = slabZStart(slab);
    const bool isCurrentIsolatedSlab = isolatedSlab.isValid()
                                    && slab.quadrant == isolatedSlab.quadrant
                                    && slab.floor == isolatedSlab.floor;
    const int localHeight = isCurrentIsolatedSlab ? isolatedBuildMaxHeight : floorBandHeight;
    return juce::jlimit(0, gridHeight, zStart + localHeight);
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

    return z >= slabZStart(slab) && z < slabZEndExclusive(slab) && quadrantForCell(x, y) == slab.quadrant;
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

    const int zMin = slabZStart(isolatedSlab);
    const int zMax = slabZEndExclusive(isolatedSlab) - 1;
    const int bottomLayer = renderBaseZForLayer(cursor.z);
    int highestZ = cursor.z;
    for (int octave = 0; octave < juce::jmax(1, editPlacementHeight); ++octave)
        for (const int interval : editChordIntervals())
            if (const int z = cursor.z + octave * 12 + interval; z >= zMin && z <= zMax)
                highestZ = juce::jmax(highestZ, z);

    const int topLayer = renderBaseZForLayer(highestZ);
    const auto aTop = projectCellCorner(cursor.x,     cursor.y,     topLayer + 1, cursor.x, cursor.y, area);
    const auto bTop = projectCellCorner(cursor.x + 1, cursor.y,     topLayer + 1, cursor.x, cursor.y, area);
    const auto cTop = projectCellCorner(cursor.x + 1, cursor.y + 1, topLayer + 1, cursor.x, cursor.y, area);
    const auto dTop = projectCellCorner(cursor.x,     cursor.y + 1, topLayer + 1, cursor.x, cursor.y, area);
    const auto aBottom = projectCellCorner(cursor.x,     cursor.y,     bottomLayer, cursor.x, cursor.y, area);
    const auto bBottom = projectCellCorner(cursor.x + 1, cursor.y,     bottomLayer, cursor.x, cursor.y, area);
    const auto cBottom = projectCellCorner(cursor.x + 1, cursor.y + 1, bottomLayer, cursor.x, cursor.y, area);
    const auto dBottom = projectCellCorner(cursor.x,     cursor.y + 1, bottomLayer, cursor.x, cursor.y, area);

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

    bestCell = canonicalBuildCellForSlab(bestCell, isolatedSlab);

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
    const int width = x1 - x0;
    const int height = y1 - y0;
    switch (buildRuleForSlab(isolatedSlab))
    {
        case IsolatedBuildRule::mirror:
            editCursor.x = x0 + juce::jmax(0, width / 4);
            editCursor.y = y0 + height / 2;
            break;
        case IsolatedBuildRule::kaleidoscope:
            editCursor.x = x0 + juce::jmax(0, width / 4);
            editCursor.y = y0 + juce::jmax(0, height / 4);
            break;
        case IsolatedBuildRule::standard:
        case IsolatedBuildRule::stampClone:
            editCursor.x = x0 + width / 2;
            editCursor.y = y0 + height / 2;
            break;
    }
    editCursor.z = juce::jlimit(0, gridHeight - 1, slabZStart(isolatedSlab));
    editCursor.active = true;
    spawnTetrisPiece(false);
}

void MainComponent::moveEditCursor(int dx, int dy, int dz)
{
    if (! isolatedSlab.isValid())
        return;

    if (! editCursor.active)
        resetEditCursor();

    int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    quadrantBounds(isolatedSlab.quadrant, x0, y0, x1, y1);
    const int width = x1 - x0;
    const int height = y1 - y0;
    int maxX = x1 - 1;
    int maxY = y1 - 1;
    switch (buildRuleForSlab(isolatedSlab))
    {
        case IsolatedBuildRule::mirror:
            maxX = x0 + juce::jmax(1, width / 2) - 1;
            break;
        case IsolatedBuildRule::kaleidoscope:
            maxX = x0 + juce::jmax(1, width / 2) - 1;
            maxY = y0 + juce::jmax(1, height / 2) - 1;
            break;
        case IsolatedBuildRule::standard:
        case IsolatedBuildRule::stampClone:
            break;
    }
    editCursor.x = juce::jlimit(x0, maxX, editCursor.x + dx);
    editCursor.y = juce::jlimit(y0, maxY, editCursor.y + dy);

    const int zMin = slabZStart(isolatedSlab);
    const int zMax = slabZEndExclusive(isolatedSlab) - 1;
    editCursor.z = juce::jlimit(zMin, zMax, editCursor.z + dz);
    editCursor.active = true;
}

void MainComponent::applyEditPlacement(bool filled)
{
    if (! isolatedSlab.isValid() || ! editCursor.active || ! cellInSelectedSlab(editCursor.x, editCursor.y, isolatedSlab))
        return;

    applyEditPlacementAtCell(editCursor.x, editCursor.y, editCursor.z, filled);
}

MainComponent::IsolatedBuildRule MainComponent::buildRuleForSlab(const SlabSelection& slab) const
{
    switch (slabNumber(slab))
    {
        case 2: return IsolatedBuildRule::mirror;
        case 3: return IsolatedBuildRule::kaleidoscope;
        case 4: return IsolatedBuildRule::stampClone;
        default: return IsolatedBuildRule::standard;
    }
}

juce::String MainComponent::buildRuleName(IsolatedBuildRule rule) const
{
    switch (rule)
    {
        case IsolatedBuildRule::standard: return "Standard";
        case IsolatedBuildRule::mirror: return "Mirror";
        case IsolatedBuildRule::kaleidoscope: return "Kaleidoscope";
        case IsolatedBuildRule::stampClone: return "Stamp + Clone";
    }

    return "Standard";
}

MainComponent::TetrisVariant MainComponent::tetrisVariantForSlab(const SlabSelection& slab) const
{
    switch (slabNumber(slab))
    {
        case 5: return TetrisVariant::standard;
        case 6: return TetrisVariant::destruct;
        case 7: return TetrisVariant::fracture;
        case 8: return TetrisVariant::roulette;
        default: return TetrisVariant::none;
    }
}

juce::String MainComponent::tetrisVariantName(TetrisVariant variant) const
{
    switch (variant)
    {
        case TetrisVariant::none: return "None";
        case TetrisVariant::standard: return "Tetris";
        case TetrisVariant::destruct: return "Destruct Tetris";
        case TetrisVariant::fracture: return "Fracture Tetris";
        case TetrisVariant::roulette: return "Roulette Tetris";
    }

    return "Tetris";
}

MainComponent::AutomataVariant MainComponent::automataVariantForSlab(const SlabSelection& slab) const
{
    switch (slabNumber(slab))
    {
        case 9: return AutomataVariant::life;
        case 10: return AutomataVariant::coral;
        case 11: return AutomataVariant::fredkin;
        case 12: return AutomataVariant::dayNight;
        default: return AutomataVariant::none;
    }
}

juce::String MainComponent::automataVariantName(AutomataVariant variant) const
{
    switch (variant)
    {
        case AutomataVariant::life: return "Life Build";
        case AutomataVariant::coral: return "Coral Build";
        case AutomataVariant::fredkin: return "Fredkin Build";
        case AutomataVariant::dayNight: return "Day & Night Build";
        case AutomataVariant::none:
        default: return "Automata Build";
    }
}

juce::Point<int> MainComponent::canonicalBuildCellForSlab(juce::Point<int> cell, const SlabSelection& slab) const
{
    if (! slab.isValid())
        return cell;

    int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    quadrantBounds(slab.quadrant, x0, y0, x1, y1);
    const int width = x1 - x0;
    const int height = y1 - y0;
    const int halfW = juce::jmax(1, width / 2);
    const int halfH = juce::jmax(1, height / 2);

    cell.x = juce::jlimit(x0, x1 - 1, cell.x);
    cell.y = juce::jlimit(y0, y1 - 1, cell.y);

    switch (buildRuleForSlab(slab))
    {
        case IsolatedBuildRule::mirror:
            if (cell.x >= x0 + halfW)
                cell.x = x0 + (width - 1 - (cell.x - x0));
            break;
        case IsolatedBuildRule::kaleidoscope:
        {
            int localX = cell.x - x0;
            int localY = cell.y - y0;
            const bool right = localX >= halfW;
            const bool bottom = localY >= halfH;

            if (! right && ! bottom)
                break;
            if (right && ! bottom)
            {
                const int rx = localX - halfW;
                const int ry = localY;
                localX = ry;
                localY = halfW - 1 - rx;
            }
            else if (right && bottom)
            {
                const int rx = localX - halfW;
                const int ry = localY - halfH;
                localX = halfW - 1 - rx;
                localY = halfH - 1 - ry;
            }
            else
            {
                const int rx = localX;
                const int ry = localY - halfH;
                localX = halfH - 1 - ry;
                localY = rx;
            }

            cell.x = x0 + juce::jlimit(0, halfW - 1, localX);
            cell.y = y0 + juce::jlimit(0, halfH - 1, localY);
            break;
        }
        case IsolatedBuildRule::standard:
        case IsolatedBuildRule::stampClone:
            break;
    }

    return cell;
}

std::vector<juce::Point<int>> MainComponent::placementCellsForSourceCell(juce::Point<int> sourceCell, const SlabSelection& slab) const
{
    std::vector<juce::Point<int>> cells;
    if (! slab.isValid())
        return cells;

    int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    quadrantBounds(slab.quadrant, x0, y0, x1, y1);
    const int width = x1 - x0;
    const int height = y1 - y0;
    const int halfW = juce::jmax(1, width / 2);
    const int halfH = juce::jmax(1, height / 2);
    sourceCell = canonicalBuildCellForSlab(sourceCell, slab);

    auto addUnique = [&] (juce::Point<int> cell)
    {
        if (! cellInSelectedSlab(cell.x, cell.y, slab))
            return;
        if (std::find(cells.begin(), cells.end(), cell) == cells.end())
            cells.push_back(cell);
    };

    switch (buildRuleForSlab(slab))
    {
        case IsolatedBuildRule::mirror:
        {
            const int localX = sourceCell.x - x0;
            addUnique(sourceCell);
            addUnique({ x0 + halfW + (halfW - 1 - localX), sourceCell.y });
            break;
        }
        case IsolatedBuildRule::kaleidoscope:
        {
            const int dx = sourceCell.x - x0;
            const int dy = sourceCell.y - y0;
            addUnique({ x0 + dx, y0 + dy });
            addUnique({ x0 + halfW + (halfW - 1 - dy), y0 + dx });
            addUnique({ x0 + halfW + (halfW - 1 - dx), y0 + halfH + (halfH - 1 - dy) });
            addUnique({ x0 + dy, y0 + halfH + (halfH - 1 - dx) });
            break;
        }
        case IsolatedBuildRule::standard:
        case IsolatedBuildRule::stampClone:
            addUnique(sourceCell);
            break;
    }

    return cells;
}

void MainComponent::applyEditPlacementAtCell(int x, int y, int z, bool filled)
{
    if (! isolatedSlab.isValid() || ! cellInSelectedSlab(x, y, isolatedSlab))
        return;

    const int zMin = slabZStart(isolatedSlab);
    const int zMax = slabZEndExclusive(isolatedSlab) - 1;
    const int stackHeight = juce::jmax(1, editPlacementHeight);
    const auto intervals = editChordIntervals();

    for (const auto& cell : placementCellsForSourceCell({ x, y }, isolatedSlab))
        for (int octave = 0; octave < stackHeight; ++octave)
            for (const int interval : intervals)
            {
                const int noteZ = z + octave * 12 + interval;
                if (noteZ >= zMin && noteZ <= zMax)
                    setVoxel(cell.x, cell.y, noteZ, filled);
            }
}

std::vector<int> MainComponent::editChordIntervals() const
{
    switch (editChordType)
    {
        case EditChordType::single: return { 0 };
        case EditChordType::power: return { 0, 7 };
        case EditChordType::majorTriad: return { 0, 4, 7 };
        case EditChordType::minorTriad: return { 0, 3, 7 };
        case EditChordType::sus2: return { 0, 2, 7 };
        case EditChordType::sus4: return { 0, 5, 7 };
        case EditChordType::majorSeventh: return { 0, 4, 7, 11 };
        case EditChordType::minorSeventh: return { 0, 3, 7, 10 };
    }

    return { 0 };
}

juce::String MainComponent::editChordTypeName() const
{
    switch (editChordType)
    {
        case EditChordType::single: return "Single";
        case EditChordType::power: return "Power";
        case EditChordType::majorTriad: return "Major";
        case EditChordType::minorTriad: return "Minor";
        case EditChordType::sus2: return "Sus2";
        case EditChordType::sus4: return "Sus4";
        case EditChordType::majorSeventh: return "Maj7";
        case EditChordType::minorSeventh: return "Min7";
    }

    return "Single";
}

juce::String MainComponent::pitchClassName(int semitone) const
{
    static constexpr std::array<const char*, 12> names {
        "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"
    };

    return names[static_cast<size_t>((semitone % 12 + 12) % 12)];
}

juce::String MainComponent::currentEditChordName() const
{
    if (! isolatedSlab.isValid())
        return editChordTypeName();

    const bool usingTetris = isolatedBuildMode == IsolatedBuildMode::tetrisTopDown && tetrisPiece.active;
    const bool usingStamp = isolatedBuildMode == IsolatedBuildMode::stampLibraryTopDown;
    if (! usingTetris && ! usingStamp && ! editCursor.active)
        return editChordTypeName();

    const int localZ = (usingTetris ? tetrisPiece.z : usingStamp ? (slabZStart(isolatedSlab) + stampBaseLayer) : editCursor.z) - slabZStart(isolatedSlab);
    juce::String quality;
    switch (editChordType)
    {
        case EditChordType::single: quality = ""; break;
        case EditChordType::power: quality = "5"; break;
        case EditChordType::majorTriad: quality = "Maj"; break;
        case EditChordType::minorTriad: quality = "Min"; break;
        case EditChordType::sus2: quality = "Sus2"; break;
        case EditChordType::sus4: quality = "Sus4"; break;
        case EditChordType::majorSeventh: quality = "Maj7"; break;
        case EditChordType::minorSeventh: quality = "Min7"; break;
    }

    juce::String name = pitchClassName(localZ);
    if (quality.isNotEmpty())
        name << " " << quality;
    if (editPlacementHeight > 1)
        name << " x" << editPlacementHeight;
    return name;
}

juce::String MainComponent::isolatedBuildModeName() const
{
    switch (isolatedBuildMode)
    {
        case IsolatedBuildMode::cursor3D:
            return isolatedSlab.isValid() ? buildRuleName(buildRuleForSlab(isolatedSlab)) : "3D Cursor";
        case IsolatedBuildMode::tetrisTopDown:
            return isolatedSlab.isValid() ? tetrisVariantName(tetrisVariantForSlab(isolatedSlab)) : "Tetris Topdown";
        case IsolatedBuildMode::stampLibraryTopDown: return "Stamp Library";
        case IsolatedBuildMode::cellularAutomataTopDown:
            return isolatedSlab.isValid() ? automataVariantName(automataVariantForSlab(isolatedSlab)) : "Automata Build";
    }

    return "3D Cursor";
}

juce::String MainComponent::tetrominoTypeName(TetrominoType type) const
{
    switch (type)
    {
        case TetrominoType::I: return "I";
        case TetrominoType::O: return "O";
        case TetrominoType::T: return "T";
        case TetrominoType::L: return "L";
        case TetrominoType::J: return "J";
        case TetrominoType::S: return "S";
        case TetrominoType::Z: return "Z";
    }

    return "T";
}

std::array<juce::Point<int>, 4> MainComponent::tetrominoOffsets(TetrominoType type, int rotation) const
{
    const int r = ((rotation % 4) + 4) % 4;
    switch (type)
    {
        case TetrominoType::I:
            return (r % 2) == 0
                     ? std::array<juce::Point<int>, 4> {{ { 0, 0 }, { 1, 0 }, { 2, 0 }, { 3, 0 } }}
                     : std::array<juce::Point<int>, 4> {{ { 0, 0 }, { 0, 1 }, { 0, 2 }, { 0, 3 } }};
        case TetrominoType::O:
            return {{ { 0, 0 }, { 1, 0 }, { 0, 1 }, { 1, 1 } }};
        case TetrominoType::T:
            switch (r)
            {
                case 0: return {{ { 0, 0 }, { 1, 0 }, { 2, 0 }, { 1, 1 } }};
                case 1: return {{ { 1, 0 }, { 1, 1 }, { 1, 2 }, { 0, 1 } }};
                case 2: return {{ { 1, 0 }, { 0, 1 }, { 1, 1 }, { 2, 1 } }};
                default: return {{ { 0, 0 }, { 0, 1 }, { 0, 2 }, { 1, 1 } }};
            }
        case TetrominoType::L:
            switch (r)
            {
                case 0: return {{ { 0, 0 }, { 0, 1 }, { 0, 2 }, { 1, 2 } }};
                case 1: return {{ { 0, 0 }, { 1, 0 }, { 2, 0 }, { 0, 1 } }};
                case 2: return {{ { 0, 0 }, { 1, 0 }, { 1, 1 }, { 1, 2 } }};
                default: return {{ { 2, 0 }, { 0, 1 }, { 1, 1 }, { 2, 1 } }};
            }
        case TetrominoType::J:
            switch (r)
            {
                case 0: return {{ { 1, 0 }, { 1, 1 }, { 1, 2 }, { 0, 2 } }};
                case 1: return {{ { 0, 0 }, { 0, 1 }, { 1, 1 }, { 2, 1 } }};
                case 2: return {{ { 0, 0 }, { 1, 0 }, { 0, 1 }, { 0, 2 } }};
                default: return {{ { 0, 0 }, { 1, 0 }, { 2, 0 }, { 2, 1 } }};
            }
        case TetrominoType::S:
            return (r % 2) == 0
                     ? std::array<juce::Point<int>, 4> {{ { 1, 0 }, { 2, 0 }, { 0, 1 }, { 1, 1 } }}
                     : std::array<juce::Point<int>, 4> {{ { 0, 0 }, { 0, 1 }, { 1, 1 }, { 1, 2 } }};
        case TetrominoType::Z:
            return (r % 2) == 0
                     ? std::array<juce::Point<int>, 4> {{ { 0, 0 }, { 1, 0 }, { 1, 1 }, { 2, 1 } }}
                     : std::array<juce::Point<int>, 4> {{ { 1, 0 }, { 0, 1 }, { 1, 1 }, { 0, 2 } }};
    }

    return {{ { 0, 0 }, { 1, 0 }, { 2, 0 }, { 1, 1 } }};
}

void MainComponent::clampTetrisPieceToSlab(TetrisPiece& piece) const
{
    if (! isolatedSlab.isValid())
        return;

    int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    quadrantBounds(isolatedSlab.quadrant, x0, y0, x1, y1);

    const auto offsets = tetrominoOffsets(piece.type, piece.rotation);
    int minDx = offsets[0].x, maxDx = offsets[0].x;
    int minDy = offsets[0].y, maxDy = offsets[0].y;
    for (const auto& offset : offsets)
    {
        minDx = juce::jmin(minDx, offset.x);
        maxDx = juce::jmax(maxDx, offset.x);
        minDy = juce::jmin(minDy, offset.y);
        maxDy = juce::jmax(maxDy, offset.y);
    }

    piece.anchor.x = juce::jlimit(x0 - minDx, x1 - 1 - maxDx, piece.anchor.x);
    piece.anchor.y = juce::jlimit(y0 - maxDy - 1, y1 - 1 - maxDy, piece.anchor.y);
    piece.z = juce::jlimit(slabZStart(isolatedSlab), slabZEndExclusive(isolatedSlab) - 1, piece.z);
}

bool MainComponent::tetrisPieceFits(const TetrisPiece& piece) const
{
    if (! isolatedSlab.isValid())
        return false;

    int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    quadrantBounds(isolatedSlab.quadrant, x0, y0, x1, y1);

    const int zMin = slabZStart(isolatedSlab);
    const int zMax = slabZEndExclusive(isolatedSlab) - 1;
    const auto intervals = editChordIntervals();
    const int stackHeight = juce::jmax(1, editPlacementHeight);

    for (const auto& offset : tetrominoOffsets(piece.type, piece.rotation))
    {
        const int x = piece.anchor.x + offset.x;
        const int y = piece.anchor.y + offset.y;
        if (x < x0 || x >= x1 || y >= y1)
            return false;

        for (int octave = 0; octave < stackHeight; ++octave)
            for (const int interval : intervals)
                if (const int noteZ = piece.z + octave * 12 + interval; noteZ < zMin || noteZ > zMax)
                    return false;
    }

    for (const auto& cell : tetrisPlacementCells(piece))
        if (cell.x < x0 || cell.x >= x1 || cell.y >= y1)
            return false;

    return true;
}

bool MainComponent::tetrisPieceCollidesWithVoxels(const TetrisPiece& piece) const
{
    if (! isolatedSlab.isValid())
        return false;

    int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    quadrantBounds(isolatedSlab.quadrant, x0, y0, x1, y1);

    const int zMin = slabZStart(isolatedSlab);
    const int zMax = slabZEndExclusive(isolatedSlab) - 1;
    const auto intervals = editChordIntervals();
    const int stackHeight = juce::jmax(1, editPlacementHeight);

    for (const auto& cell : tetrisPlacementCells(piece))
    {
        const int x = cell.x;
        const int y = cell.y;
        if (y < y0 || x < x0 || x >= x1 || y >= y1)
            continue;

        for (int octave = 0; octave < stackHeight; ++octave)
        {
            for (const int interval : intervals)
            {
                const int noteZ = piece.z + octave * 12 + interval;
                if (noteZ < zMin || noteZ > zMax)
                    continue;
                if (hasVoxel(x, y, noteZ))
                    return true;
            }
        }
    }

    return false;
}

MainComponent::TetrominoType MainComponent::randomTetrominoType() const
{
    juce::Random rng;
    return static_cast<TetrominoType>(rng.nextInt(tetrominoTypeCount()));
}

void MainComponent::spawnTetrisPiece(bool randomizeType)
{
    if (! isolatedSlab.isValid())
        return;

    int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    quadrantBounds(isolatedSlab.quadrant, x0, y0, x1, y1);

    if (! tetrisPiece.active)
    {
        tetrisPiece.type = randomTetrominoType();
        nextTetrisType = randomTetrominoType();
    }
    else if (randomizeType)
    {
        tetrisPiece.type = nextTetrisType;
        nextTetrisType = randomTetrominoType();
    }

    tetrisPiece.rotation = 0;

    const auto offsets = tetrominoOffsets(tetrisPiece.type, tetrisPiece.rotation);
    int maxDy = offsets[0].y;
    for (const auto& offset : offsets)
        maxDy = juce::jmax(maxDy, offset.y);

    tetrisPiece.anchor = { x0 + (x1 - x0) / 2 - 1, y0 - maxDy - 1 };
    tetrisPiece.z = slabZStart(isolatedSlab) + juce::jlimit(0, tetrisLayerCount - 1, tetrisBuildLayer);
    tetrisPiece.active = true;
    clampTetrisPieceToSlab(tetrisPiece);
    tetrisGravityTick = 0;
    if (tetrisVariantForSlab(isolatedSlab) == TetrisVariant::roulette)
        applyRouletteRuleOnNewPiece();
}

void MainComponent::moveTetrisPiece(int dx, int dy, int dz)
{
    if (! isolatedSlab.isValid())
        return;

    if (! tetrisPiece.active)
        spawnTetrisPiece(false);

    TetrisPiece moved = tetrisPiece;
    moved.anchor += juce::Point<int>(dx, dy);
    moved.z += dz;
    clampTetrisPieceToSlab(moved);
    if (tetrisPieceFits(moved) && (shouldTetrisPieceDestroy(moved) || ! tetrisPieceCollidesWithVoxels(moved)))
        tetrisPiece = moved;
}

std::vector<juce::Point<int>> MainComponent::tetrisPlacementCells(const TetrisPiece& piece) const
{
    std::vector<juce::Point<int>> cells;
    if (! isolatedSlab.isValid())
        return cells;

    int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    quadrantBounds(isolatedSlab.quadrant, x0, y0, x1, y1);

    for (const auto& offset : tetrominoOffsets(piece.type, piece.rotation))
    {
        const auto baseCell = juce::Point<int>(piece.anchor.x + offset.x, piece.anchor.y + offset.y);
        if (isolatedSlab.isValid() && tetrisVariantForSlab(isolatedSlab) == TetrisVariant::roulette && rouletteMirrorPlacement)
        {
            if (baseCell.x >= x0 && baseCell.x < x1 && baseCell.y >= y0 && baseCell.y < y1)
            {
                const int localX = baseCell.x - x0;
                const int mirroredX = x0 + (x1 - x0 - 1 - localX);
                const std::array<juce::Point<int>, 2> mirroredCells { baseCell, juce::Point<int>(mirroredX, baseCell.y) };
                for (const auto& cell : mirroredCells)
                    if (std::find(cells.begin(), cells.end(), cell) == cells.end())
                        cells.push_back(cell);
            }
        }
        else
        {
            if (std::find(cells.begin(), cells.end(), baseCell) == cells.end())
                cells.push_back(baseCell);
        }
    }

    return cells;
}

bool MainComponent::shouldTetrisPieceDestroy(const TetrisPiece& piece) const
{
    if (! isolatedSlab.isValid())
        return false;

    if (tetrisVariantForSlab(isolatedSlab) != TetrisVariant::destruct)
        return false;

    return piece.type == TetrominoType::I || piece.type == TetrominoType::J || piece.type == TetrominoType::Z;
}

void MainComponent::rotateTetrisPiece()
{
    if (! isolatedSlab.isValid())
        return;

    if (! tetrisPiece.active)
        spawnTetrisPiece(false);

    TetrisPiece rotated = tetrisPiece;
    rotated.rotation = (rotated.rotation + 1) % 4;
    clampTetrisPieceToSlab(rotated);
    if (tetrisPieceFits(rotated) && (shouldTetrisPieceDestroy(rotated) || ! tetrisPieceCollidesWithVoxels(rotated)))
        tetrisPiece = rotated;
}

void MainComponent::placeTetrisPiece(bool filled)
{
    if (! isolatedSlab.isValid())
        return;

    if (! tetrisPiece.active)
        spawnTetrisPiece(false);

    if (! tetrisPieceFits(tetrisPiece))
        return;

    int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    quadrantBounds(isolatedSlab.quadrant, x0, y0, x1, y1);
    for (const auto& offset : tetrominoOffsets(tetrisPiece.type, tetrisPiece.rotation))
        if (tetrisPiece.anchor.y + offset.y < y0)
            return;

    const bool destructivePlacement = filled && shouldTetrisPieceDestroy(tetrisPiece);
    if (filled && ! destructivePlacement && tetrisPieceCollidesWithVoxels(tetrisPiece))
        return;

    for (const auto& cell : tetrisPlacementCells(tetrisPiece))
        applyEditPlacementAtCell(cell.x, cell.y, tetrisPiece.z, destructivePlacement ? false : filled);

    editCursor.x = tetrisPiece.anchor.x;
    editCursor.y = tetrisPiece.anchor.y;
    editCursor.z = tetrisPiece.z;
    editCursor.active = true;
    if (filled && tetrisVariantForSlab(isolatedSlab) == TetrisVariant::fracture)
        applyFractureToCurrentLayer();
    spawnTetrisPiece(true);
}

void MainComponent::advanceTetrisGravity()
{
    if (! isolatedSlab.isValid() || isolatedBuildMode != IsolatedBuildMode::tetrisTopDown || performanceMode)
        return;

    if (! tetrisPiece.active)
    {
        spawnTetrisPiece(true);
        return;
    }

    TetrisPiece dropped = tetrisPiece;
    dropped.anchor.y += 1;
    clampTetrisPieceToSlab(dropped);
    const bool destructive = shouldTetrisPieceDestroy(tetrisPiece);
    const bool droppedFits = dropped.anchor.y != tetrisPiece.anchor.y && tetrisPieceFits(dropped);
    const bool droppedCollides = droppedFits && tetrisPieceCollidesWithVoxels(dropped);
    if (droppedFits && ! droppedCollides)
    {
        tetrisPiece = dropped;
        return;
    }

    if (destructive && droppedFits && droppedCollides)
    {
        tetrisPiece = dropped;
        placeTetrisPiece(true);
        return;
    }

    if (tetrisPieceFits(tetrisPiece) && (destructive || ! tetrisPieceCollidesWithVoxels(tetrisPiece)))
        placeTetrisPiece(true);
}

void MainComponent::applyFractureToCurrentLayer()
{
    if (! isolatedSlab.isValid())
        return;

    int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    quadrantBounds(isolatedSlab.quadrant, x0, y0, x1, y1);
    const int width = x1 - x0;
    const int height = y1 - y0;
    const int z = slabZStart(isolatedSlab) + juce::jlimit(0, tetrisLayerCount - 1, tetrisBuildLayer);

    int filledCells = 0;
    for (int y = y0; y < y1; ++y)
        for (int x = x0; x < x1; ++x)
            if (hasVoxel(x, y, z))
                ++filledCells;

    const float occupancy = static_cast<float>(filledCells) / static_cast<float>(juce::jmax(1, width * height));
    if (occupancy < 0.35f)
        return;

    std::vector<juce::Point<int>> layerCells;
    for (int y = y0; y < y1; ++y)
        for (int x = x0; x < x1; ++x)
            if (hasVoxel(x, y, z))
                layerCells.push_back({ x, y });

    for (const auto& cell : layerCells)
        setVoxel(cell.x, cell.y, z, false);

    const int halfW = juce::jmax(1, width / 2);
    const int halfH = juce::jmax(1, height / 2);
    for (const auto& cell : layerCells)
    {
        int dx = 0;
        int dy = 0;
        if (cell.x < x0 + halfW) dx = -1; else dx = 1;
        if (cell.y < y0 + halfH) dy = -1; else dy = 1;
        setVoxel(juce::jlimit(x0, x1 - 1, cell.x + dx), juce::jlimit(y0, y1 - 1, cell.y + dy), z, true);
    }
}

void MainComponent::applyRouletteRuleOnNewPiece()
{
    const int rule = juce::Random::getSystemRandom().nextInt(3);
    switch (rule)
    {
        case 0:
            tetrisGravityFrames = juce::Random::getSystemRandom().nextBool() ? 10 : 28;
            break;
        case 1:
            scale = static_cast<ScaleType>(juce::Random::getSystemRandom().nextInt(5));
            break;
        case 2:
            rouletteMirrorPlacement = juce::Random::getSystemRandom().nextBool();
            rouletteRotatePerLayer = juce::Random::getSystemRandom().nextBool();
            break;
        default:
            break;
    }
}

void MainComponent::softDropTetrisPiece()
{
    if (! tetrisPiece.active)
        spawnTetrisPiece(true);

    TetrisPiece dropped = tetrisPiece;
    dropped.anchor.y += 1;
    clampTetrisPieceToSlab(dropped);
    const bool destructive = shouldTetrisPieceDestroy(tetrisPiece);
    const bool droppedFits = dropped.anchor.y != tetrisPiece.anchor.y && tetrisPieceFits(dropped);
    const bool droppedCollides = droppedFits && tetrisPieceCollidesWithVoxels(dropped);
    if (droppedFits && ! droppedCollides)
        tetrisPiece = dropped;
    else if (destructive && droppedFits && droppedCollides)
    {
        tetrisPiece = dropped;
        placeTetrisPiece(true);
    }
    else if (tetrisPieceFits(tetrisPiece) && (destructive || ! tetrisPieceCollidesWithVoxels(tetrisPiece)))
        placeTetrisPiece(true);
}

void MainComponent::hardDropTetrisPiece()
{
    if (! isolatedSlab.isValid())
        return;

    if (! tetrisPiece.active)
        spawnTetrisPiece(true);

    const bool destructive = shouldTetrisPieceDestroy(tetrisPiece);
    while (true)
    {
        TetrisPiece dropped = tetrisPiece;
        dropped.anchor.y += 1;
        clampTetrisPieceToSlab(dropped);
        if (dropped.anchor.y == tetrisPiece.anchor.y || ! tetrisPieceFits(dropped))
            break;
        if (destructive && tetrisPieceCollidesWithVoxels(dropped))
        {
            tetrisPiece = dropped;
            break;
        }
        if (! destructive && tetrisPieceCollidesWithVoxels(dropped))
            break;
        tetrisPiece = dropped;
    }

    if (tetrisPieceFits(tetrisPiece) && (destructive || ! tetrisPieceCollidesWithVoxels(tetrisPiece)))
        placeTetrisPiece(true);
}

void MainComponent::toggleAutomataCell(juce::Point<int> cell, bool filled)
{
    if (! isolatedSlab.isValid() || ! cellInSelectedSlab(cell.x, cell.y, isolatedSlab))
        return;

    const int z = slabZStart(isolatedSlab) + juce::jlimit(0, isolatedBuildMaxHeight - 1, automataBuildLayer);
    applyEditPlacementAtCell(cell.x, cell.y, z, filled);
    automataHoverCell = cell;
    editCursor = { cell.x, cell.y, z, true };
}

int MainComponent::automataNeighbourCount(const std::vector<juce::Point<int>>& aliveCells, juce::Point<int> cell) const
{
    int count = 0;
    for (int dy = -1; dy <= 1; ++dy)
        for (int dx = -1; dx <= 1; ++dx)
        {
            if (dx == 0 && dy == 0)
                continue;
            if (std::find(aliveCells.begin(), aliveCells.end(), juce::Point<int>(cell.x + dx, cell.y + dy)) != aliveCells.end())
                ++count;
        }
    return count;
}

void MainComponent::randomiseAutomataSeed()
{
    if (! isolatedSlab.isValid())
        return;

    int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    quadrantBounds(isolatedSlab.quadrant, x0, y0, x1, y1);
    const int z = slabZStart(isolatedSlab) + juce::jlimit(0, isolatedBuildMaxHeight - 1, automataBuildLayer);
    float density = 0.22f;
    switch (automataVariantForSlab(isolatedSlab))
    {
        case AutomataVariant::life:
            density = 0.22f;
            break;
        case AutomataVariant::coral:
            density = 0.34f;
            break;
        case AutomataVariant::fredkin:
            density = 0.16f;
            break;
        case AutomataVariant::dayNight:
            density = 0.28f;
            break;
        case AutomataVariant::none:
        default:
            density = 0.22f;
            break;
    }

    for (int y = y0; y < y1; ++y)
        for (int x = x0; x < x1; ++x)
            setVoxel(x, y, z, juce::Random::getSystemRandom().nextFloat() < density);
    rebuildFilledVoxelCache();
}

void MainComponent::advanceAutomataLayer()
{
    if (! isolatedSlab.isValid())
        return;

    int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    quadrantBounds(isolatedSlab.quadrant, x0, y0, x1, y1);

    const int currentLayer = juce::jlimit(0, isolatedBuildMaxHeight - 1, automataBuildLayer);
    if (currentLayer >= isolatedBuildMaxHeight - 1)
        return;

    const int zCurrent = slabZStart(isolatedSlab) + currentLayer;
    const int zNext = slabZStart(isolatedSlab) + currentLayer + 1;
    std::vector<juce::Point<int>> aliveCells;
    for (int y = y0; y < y1; ++y)
        for (int x = x0; x < x1; ++x)
            if (hasVoxel(x, y, zCurrent))
                aliveCells.push_back({ x, y });

    for (int y = y0; y < y1; ++y)
        for (int x = x0; x < x1; ++x)
            setVoxel(x, y, zNext, false);

    const auto variant = automataVariantForSlab(isolatedSlab);
    for (int y = y0; y < y1; ++y)
    {
        for (int x = x0; x < x1; ++x)
        {
            const auto cell = juce::Point<int>(x, y);
            const bool alive = std::find(aliveCells.begin(), aliveCells.end(), cell) != aliveCells.end();
            const int neighbours = automataNeighbourCount(aliveCells, cell);

            bool nextAlive = false;
            switch (variant)
            {
                case AutomataVariant::life:
                    nextAlive = neighbours == 3 || (alive && neighbours == 2);
                    break;
                case AutomataVariant::coral:
                    nextAlive = neighbours == 3 || (alive && neighbours >= 4 && neighbours <= 8);
                    break;
                case AutomataVariant::fredkin:
                    nextAlive = (neighbours % 2) == 1;
                    break;
                case AutomataVariant::dayNight:
                    nextAlive = neighbours == 3 || neighbours == 6 || neighbours == 7 || neighbours == 8
                             || (alive && (neighbours == 3 || neighbours == 4 || neighbours == 6 || neighbours == 7 || neighbours == 8));
                    break;
                case AutomataVariant::none:
                default:
                    nextAlive = neighbours == 3 || (alive && neighbours == 2);
                    break;
            }

            if (nextAlive)
                setVoxel(x, y, zNext, true);
        }
    }

    rebuildFilledVoxelCache();
    automataBuildLayer = currentLayer + 1;
    if (automataHoverCell.has_value())
        editCursor = { automataHoverCell->x, automataHoverCell->y, zNext, true };
}

void MainComponent::rotateIsolatedSlabQuarterTurn()
{
    if (! isolatedSlab.isValid())
        return;

    int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    quadrantBounds(isolatedSlab.quadrant, x0, y0, x1, y1);

    const int width = x1 - x0;
    const int height = y1 - y0;
    if (width != height)
        return;

    const int zStart = slabZStart(isolatedSlab);
    const int zEnd = juce::jmin(slabZEndExclusive(isolatedSlab), zStart + tetrisLayerCount);
    std::vector<uint8_t> rotated(static_cast<size_t>(width * height * (zEnd - zStart)), 0u);

    auto rotatedIndex = [width, height, zStart, x0, y0] (int x, int y, int z)
    {
        const int localX = x - x0;
        const int localY = y - y0;
        const int localZ = z - zStart;
        return static_cast<size_t>((localZ * height + localY) * width + localX);
    };

    for (int z = zStart; z < zEnd; ++z)
    {
        for (int y = y0; y < y1; ++y)
        {
            for (int x = x0; x < x1; ++x)
            {
                if (! hasVoxel(x, y, z))
                    continue;

                const int localX = x - x0;
                const int localY = y - y0;
                const int rotatedX = x0 + (height - 1 - localY);
                const int rotatedY = y0 + localX;
                rotated[rotatedIndex(rotatedX, rotatedY, z)] = 1u;
            }
        }
    }

    for (int z = zStart; z < zEnd; ++z)
        for (int y = y0; y < y1; ++y)
            for (int x = x0; x < x1; ++x)
                setVoxel(x, y, z, false);

    for (int z = zStart; z < zEnd; ++z)
        for (int y = y0; y < y1; ++y)
            for (int x = x0; x < x1; ++x)
                if (rotated[rotatedIndex(x, y, z)] != 0u)
                    setVoxel(x, y, z, true);

    if (editCursor.active && cellInSelectedSlab(editCursor.x, editCursor.y, isolatedSlab))
    {
        const int localX = editCursor.x - x0;
        const int localY = editCursor.y - y0;
        editCursor.x = x0 + (height - 1 - localY);
        editCursor.y = y0 + localX;
    }

    if (tetrisPiece.active)
    {
        const int localX = tetrisPiece.anchor.x - x0;
        const int localY = tetrisPiece.anchor.y - y0;
        tetrisPiece.anchor.x = x0 + (height - 1 - localY);
        tetrisPiece.anchor.y = y0 + localX;
        tetrisPiece.rotation = (tetrisPiece.rotation + 1) % 4;
        clampTetrisPieceToSlab(tetrisPiece);
    }
}

void MainComponent::clearIsolatedSlab()
{
    if (! isolatedSlab.isValid())
        return;

    int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    quadrantBounds(isolatedSlab.quadrant, x0, y0, x1, y1);
    const int zMin = slabZStart(isolatedSlab);
    const int zMax = slabZEndExclusive(isolatedSlab);

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

void MainComponent::rebuildFilledVoxelCache()
{
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
}

bool MainComponent::saveStateToFile(const juce::File& targetFile)
{
    auto file = targetFile;
    if (file.getFileExtension() != ".drd")
        file = file.withFileExtension(".drd");

    auto root = std::make_unique<juce::XmlElement>("drd");
    root->setAttribute("version", 1);
    root->setAttribute("gridWidth", gridWidth);
    root->setAttribute("gridDepth", gridDepth);
    root->setAttribute("gridHeight", gridHeight);

    auto* cameraXml = root->createNewChildElement("camera");
    cameraXml->setAttribute("rotation", camera.rotation);
    cameraXml->setAttribute("zoom", static_cast<double>(camera.zoom));
    cameraXml->setAttribute("targetZoom", static_cast<double>(targetZoom));
    cameraXml->setAttribute("heightScale", static_cast<double>(camera.heightScale));
    cameraXml->setAttribute("panX", static_cast<double>(camera.panX));
    cameraXml->setAttribute("panY", static_cast<double>(camera.panY));

    auto* stateXml = root->createNewChildElement("state");
    stateXml->setAttribute("layoutMode", static_cast<int>(layoutMode));
    stateXml->setAttribute("performanceMode", boolToInt(performanceMode));
    stateXml->setAttribute("performanceRegionMode", performanceRegionMode);
    stateXml->setAttribute("performanceAgentCount", performanceAgentCount);
    stateXml->setAttribute("performanceAgentMode", static_cast<int>(performanceAgentMode));
    stateXml->setAttribute("performanceTrackHorizontal", boolToInt(performanceTrackHorizontal));
    stateXml->setAttribute("performancePlacementMode", static_cast<int>(performancePlacementMode));
    stateXml->setAttribute("performanceTick", performanceTick);
    stateXml->setAttribute("synthEngine", static_cast<int>(synthEngine));
    stateXml->setAttribute("drumMode", static_cast<int>(drumMode));
    stateXml->setAttribute("snakeTriggerMode", static_cast<int>(snakeTriggerMode));
    stateXml->setAttribute("scale", static_cast<int>(scale));
    stateXml->setAttribute("keyRoot", keyRoot);
    stateXml->setAttribute("quantizeToScale", boolToInt(quantizeToScale));
    stateXml->setAttribute("editPlacementHeight", editPlacementHeight);
    stateXml->setAttribute("editChordType", static_cast<int>(editChordType));
    stateXml->setAttribute("isolatedBuildMode", static_cast<int>(isolatedBuildMode));
    stateXml->setAttribute("nextTetrisType", static_cast<int>(nextTetrisType));
    stateXml->setAttribute("tetrisBuildLayer", tetrisBuildLayer);
    stateXml->setAttribute("tetrisGravityTick", tetrisGravityTick);
    stateXml->setAttribute("tetrisGravityFrames", tetrisGravityFrames);
    stateXml->setAttribute("rouletteMirrorPlacement", boolToInt(rouletteMirrorPlacement));
    stateXml->setAttribute("rouletteRotatePerLayer", boolToInt(rouletteRotatePerLayer));
    stateXml->setAttribute("stampLibraryIndex", stampLibraryIndex);
    stateXml->setAttribute("stampRotation", stampRotation);
    stateXml->setAttribute("stampBaseLayer", stampBaseLayer);
    stateXml->setAttribute("stampCaptureMode", boolToInt(stampCaptureMode));
    stateXml->setAttribute("automataBuildLayer", automataBuildLayer);
    stateXml->setAttribute("bpm", bpm);
    stateXml->setAttribute("beatStepAccumulator", beatStepAccumulator);
    stateXml->setAttribute("beatStepIndex", beatStepIndex);
    stateXml->setAttribute("beatBarIndex", beatBarIndex);
    stateXml->setAttribute("selectedDirX", performanceSelectedDirection.x);
    stateXml->setAttribute("selectedDirY", performanceSelectedDirection.y);

    auto* slabXml = root->createNewChildElement("slabState");
    slabXml->setAttribute("hoveredQuadrant", hoveredSlab.quadrant);
    slabXml->setAttribute("hoveredFloor", hoveredSlab.floor);
    slabXml->setAttribute("isolatedQuadrant", isolatedSlab.quadrant);
    slabXml->setAttribute("isolatedFloor", isolatedSlab.floor);
    slabXml->setAttribute("cursorX", editCursor.x);
    slabXml->setAttribute("cursorY", editCursor.y);
    slabXml->setAttribute("cursorZ", editCursor.z);
    slabXml->setAttribute("cursorActive", boolToInt(editCursor.active));
    slabXml->setAttribute("hoverCellX", performanceHoverCell.has_value() ? performanceHoverCell->x : -1);
    slabXml->setAttribute("hoverCellY", performanceHoverCell.has_value() ? performanceHoverCell->y : -1);
    slabXml->setAttribute("selectionKind", static_cast<int>(performanceSelection.kind));
    slabXml->setAttribute("selectionX", performanceSelection.isValid() ? performanceSelection.cell.x : -1);
    slabXml->setAttribute("selectionY", performanceSelection.isValid() ? performanceSelection.cell.y : -1);
    slabXml->setAttribute("tetrisType", static_cast<int>(tetrisPiece.type));
    slabXml->setAttribute("tetrisRotation", tetrisPiece.rotation);
    slabXml->setAttribute("tetrisAnchorX", tetrisPiece.anchor.x);
    slabXml->setAttribute("tetrisAnchorY", tetrisPiece.anchor.y);
    slabXml->setAttribute("tetrisZ", tetrisPiece.z);
    slabXml->setAttribute("tetrisActive", boolToInt(tetrisPiece.active));

    auto* presetsXml = root->createNewChildElement("slabPresets");
    for (size_t i = 0; i < slabPerformanceModes.size(); ++i)
    {
        auto* presetXml = presetsXml->createNewChildElement("slab");
        presetXml->setAttribute("index", static_cast<int>(i));
        presetXml->setAttribute("mode", static_cast<int>(slabPerformanceModes[i]));
        presetXml->setAttribute("tempo", slabStartingTempos[i]);
    }

    auto* voxelsXml = root->createNewChildElement("voxels");
    voxelsXml->setAttribute("count", static_cast<int>(filledVoxels.size()));
    for (const auto& voxel : filledVoxels)
    {
        auto* voxelXml = voxelsXml->createNewChildElement("voxel");
        voxelXml->setAttribute("x", static_cast<int>(voxel.x));
        voxelXml->setAttribute("y", static_cast<int>(voxel.y));
        voxelXml->setAttribute("z", static_cast<int>(voxel.z));
    }

    auto* snakesXml = root->createNewChildElement("snakes");
    for (const auto& snake : performanceSnakes)
    {
        auto* snakeXml = snakesXml->createNewChildElement("snake");
        snakeXml->setAttribute("dirX", snake.direction.x);
        snakeXml->setAttribute("dirY", snake.direction.y);
        snakeXml->setAttribute("colour", static_cast<int>(snake.colour.getARGB()));
        snakeXml->setAttribute("orbitIndex", snake.orbitIndex);
        snakeXml->setAttribute("clockwise", boolToInt(snake.clockwise));
        for (const auto& segment : snake.body)
        {
            auto* segXml = snakeXml->createNewChildElement("segment");
            segXml->setAttribute("x", segment.x);
            segXml->setAttribute("y", segment.y);
        }
    }

    auto* discsXml = root->createNewChildElement("discs");
    for (const auto& disc : performanceDiscs)
    {
        auto* discXml = discsXml->createNewChildElement("disc");
        discXml->setAttribute("x", disc.cell.x);
        discXml->setAttribute("y", disc.cell.y);
        discXml->setAttribute("dirX", disc.direction.x);
        discXml->setAttribute("dirY", disc.direction.y);
    }

    auto* tracksXml = root->createNewChildElement("tracks");
    for (const auto& track : performanceTracks)
    {
        auto* trackXml = tracksXml->createNewChildElement("track");
        trackXml->setAttribute("x", track.cell.x);
        trackXml->setAttribute("y", track.cell.y);
        trackXml->setAttribute("horizontal", boolToInt(track.horizontal));
    }

    auto* orbitXml = root->createNewChildElement("orbitCenters");
    for (const auto& centre : performanceOrbitCenters)
    {
        auto* centreXml = orbitXml->createNewChildElement("center");
        centreXml->setAttribute("x", centre.x);
        centreXml->setAttribute("y", centre.y);
    }

    auto* automataXml = root->createNewChildElement("automataCells");
    for (const auto& cell : performanceAutomataCells)
    {
        auto* cellXml = automataXml->createNewChildElement("cell");
        cellXml->setAttribute("x", cell.x);
        cellXml->setAttribute("y", cell.y);
    }

    return root->writeTo(file);
}

bool MainComponent::loadStateFromFile(const juce::File& file)
{
    auto parsed = juce::XmlDocument::parse(file);
    if (parsed == nullptr || ! parsed->hasTagName("drd"))
        return false;

    std::fill(voxels.begin(), voxels.end(), 0u);
    filledVoxels.clear();
    performanceSnakes.clear();
    performanceDiscs.clear();
    performanceTracks.clear();
    performanceOrbitCenters.clear();
    performanceAutomataCells.clear();
    performanceFlashes.clear();
    pendingNoteOffs.clear();
    pendingBeatNoteOffs.clear();

    if (auto* cameraXml = parsed->getChildByName("camera"))
    {
        camera.rotation = cameraXml->getIntAttribute("rotation", 0);
        camera.zoom = static_cast<float>(cameraXml->getDoubleAttribute("zoom", 1.0));
        targetZoom = static_cast<float>(cameraXml->getDoubleAttribute("targetZoom", camera.zoom));
        camera.heightScale = static_cast<float>(cameraXml->getDoubleAttribute("heightScale", 1.0));
        camera.panX = static_cast<float>(cameraXml->getDoubleAttribute("panX", 0.0));
        camera.panY = static_cast<float>(cameraXml->getDoubleAttribute("panY", 0.0));
    }

    if (auto* stateXml = parsed->getChildByName("state"))
    {
        layoutMode = static_cast<LayoutMode>(juce::jlimit(0, 2, stateXml->getIntAttribute("layoutMode", 0)));
        performanceMode = stateXml->getBoolAttribute("performanceMode", false);
        performanceRegionMode = juce::jlimit(0, 2, stateXml->getIntAttribute("performanceRegionMode", 2));
        performanceAgentCount = juce::jlimit(0, 8, stateXml->getIntAttribute("performanceAgentCount", 1));
        performanceAgentMode = static_cast<PerformanceAgentMode>(juce::jlimit(0, 3, stateXml->getIntAttribute("performanceAgentMode", 0)));
        performanceTrackHorizontal = stateXml->getBoolAttribute("performanceTrackHorizontal", true);
        performancePlacementMode = static_cast<PerformancePlacementMode>(juce::jlimit(0, 2, stateXml->getIntAttribute("performancePlacementMode", 0)));
        performanceTick = stateXml->getIntAttribute("performanceTick", 0);
        synthEngine = static_cast<SynthEngine>(juce::jlimit(0, 4, stateXml->getIntAttribute("synthEngine", 0)));
        drumMode = static_cast<DrumMode>(juce::jlimit(0, 4, stateXml->getIntAttribute("drumMode", 0)));
        snakeTriggerMode = static_cast<SnakeTriggerMode>(juce::jlimit(0, 1, stateXml->getIntAttribute("snakeTriggerMode", 0)));
        scale = static_cast<ScaleType>(juce::jlimit(0, 4, stateXml->getIntAttribute("scale", 2)));
        keyRoot = juce::jlimit(0, 11, stateXml->getIntAttribute("keyRoot", 0));
        quantizeToScale = stateXml->getBoolAttribute("quantizeToScale", true);
        editPlacementHeight = juce::jlimit(1, 4, stateXml->getIntAttribute("editPlacementHeight", 1));
        editChordType = static_cast<EditChordType>(juce::jlimit(0, 7, stateXml->getIntAttribute("editChordType", 0)));
        isolatedBuildMode = static_cast<IsolatedBuildMode>(juce::jlimit(0, 3, stateXml->getIntAttribute("isolatedBuildMode", 0)));
        nextTetrisType = static_cast<TetrominoType>(juce::jlimit(0, tetrominoTypeCount() - 1, stateXml->getIntAttribute("nextTetrisType", static_cast<int>(TetrominoType::L))));
        tetrisBuildLayer = juce::jlimit(0, tetrisLayerCount - 1, stateXml->getIntAttribute("tetrisBuildLayer", 0));
        tetrisGravityTick = juce::jmax(0, stateXml->getIntAttribute("tetrisGravityTick", 0));
        tetrisGravityFrames = juce::jlimit(6, 40, stateXml->getIntAttribute("tetrisGravityFrames", 20));
        rouletteMirrorPlacement = stateXml->getBoolAttribute("rouletteMirrorPlacement", false);
        rouletteRotatePerLayer = stateXml->getBoolAttribute("rouletteRotatePerLayer", false);
        stampLibraryIndex = juce::jmax(0, stateXml->getIntAttribute("stampLibraryIndex", 0));
        stampRotation = ((stateXml->getIntAttribute("stampRotation", 0) % 4) + 4) % 4;
        stampBaseLayer = juce::jlimit(0, isolatedBuildMaxHeight - 1, stateXml->getIntAttribute("stampBaseLayer", 0));
        stampCaptureMode = stateXml->getBoolAttribute("stampCaptureMode", false);
        automataBuildLayer = juce::jlimit(0, isolatedBuildMaxHeight - 1, stateXml->getIntAttribute("automataBuildLayer", 0));
        bpm = juce::jlimit(60.0, 220.0, stateXml->getDoubleAttribute("bpm", 168.0));
        beatStepAccumulator = stateXml->getDoubleAttribute("beatStepAccumulator", 0.0);
        beatStepIndex = stateXml->getIntAttribute("beatStepIndex", 0);
        beatBarIndex = stateXml->getIntAttribute("beatBarIndex", 0);
        performanceSelectedDirection = { stateXml->getIntAttribute("selectedDirX", 1),
                                         stateXml->getIntAttribute("selectedDirY", 0) };
    }

    if (auto* slabXml = parsed->getChildByName("slabState"))
    {
        hoveredSlab = { slabXml->getIntAttribute("hoveredQuadrant", -1), slabXml->getIntAttribute("hoveredFloor", -1) };
        isolatedSlab = { slabXml->getIntAttribute("isolatedQuadrant", -1), slabXml->getIntAttribute("isolatedFloor", -1) };
        editCursor = { slabXml->getIntAttribute("cursorX", 0),
                       slabXml->getIntAttribute("cursorY", 0),
                       slabXml->getIntAttribute("cursorZ", 0),
                       slabXml->getBoolAttribute("cursorActive", false) };
        const int hoverX = slabXml->getIntAttribute("hoverCellX", -1);
        const int hoverY = slabXml->getIntAttribute("hoverCellY", -1);
        performanceHoverCell = (hoverX >= 0 && hoverY >= 0) ? std::optional<juce::Point<int>>(juce::Point<int>(hoverX, hoverY)) : std::nullopt;
        performanceSelection.kind = static_cast<PerformanceSelection::Kind>(juce::jlimit(0, 2, slabXml->getIntAttribute("selectionKind", 0)));
        performanceSelection.cell = { slabXml->getIntAttribute("selectionX", -1),
                                      slabXml->getIntAttribute("selectionY", -1) };
        tetrisPiece.type = static_cast<TetrominoType>(juce::jlimit(0, tetrominoTypeCount() - 1, slabXml->getIntAttribute("tetrisType", 2)));
        tetrisPiece.rotation = juce::jlimit(0, 3, slabXml->getIntAttribute("tetrisRotation", 0));
        tetrisPiece.anchor = { slabXml->getIntAttribute("tetrisAnchorX", 0),
                               slabXml->getIntAttribute("tetrisAnchorY", 0) };
        tetrisPiece.z = slabXml->getIntAttribute("tetrisZ", 0);
        tetrisPiece.active = slabXml->getBoolAttribute("tetrisActive", false);
    }

    if (auto* presetsXml = parsed->getChildByName("slabPresets"))
    {
        forEachXmlChildElementWithTagName(*presetsXml, presetXml, "slab")
        {
            const int index = juce::jlimit(0, 15, presetXml->getIntAttribute("index", 0));
            slabPerformanceModes[static_cast<size_t>(index)] = static_cast<PerformanceAgentMode>(juce::jlimit(0, 3, presetXml->getIntAttribute("mode", 0)));
            slabStartingTempos[static_cast<size_t>(index)] = presetXml->getDoubleAttribute("tempo", 168.0);
        }
    }

    if (auto* voxelsXml = parsed->getChildByName("voxels"))
    {
        forEachXmlChildElementWithTagName(*voxelsXml, voxelXml, "voxel")
        {
            const int x = voxelXml->getIntAttribute("x", -1);
            const int y = voxelXml->getIntAttribute("y", -1);
            const int z = voxelXml->getIntAttribute("z", -1);
            if (x >= 0 && x < gridWidth && y >= 0 && y < gridDepth && z >= 0 && z < gridHeight)
                voxels[voxelIndex(x, y, z)] = 1u;
        }
    }

    if (auto* snakesXml = parsed->getChildByName("snakes"))
    {
        forEachXmlChildElementWithTagName(*snakesXml, snakeXml, "snake")
        {
            Snake snake;
            snake.direction = { snakeXml->getIntAttribute("dirX", 1), snakeXml->getIntAttribute("dirY", 0) };
            snake.colour = juce::Colour(static_cast<juce::uint32>(snakeXml->getIntAttribute("colour", juce::Colours::white.getARGB())));
            snake.orbitIndex = snakeXml->getIntAttribute("orbitIndex", 0);
            snake.clockwise = snakeXml->getBoolAttribute("clockwise", true);
            forEachXmlChildElementWithTagName(*snakeXml, segmentXml, "segment")
                snake.body.push_back({ segmentXml->getIntAttribute("x", 0), segmentXml->getIntAttribute("y", 0) });
            performanceSnakes.push_back(std::move(snake));
        }
    }

    if (auto* discsXml = parsed->getChildByName("discs"))
    {
        forEachXmlChildElementWithTagName(*discsXml, discXml, "disc")
            performanceDiscs.push_back({ { discXml->getIntAttribute("x", 0), discXml->getIntAttribute("y", 0) },
                                         { discXml->getIntAttribute("dirX", 1), discXml->getIntAttribute("dirY", 0) } });
    }

    if (auto* tracksXml = parsed->getChildByName("tracks"))
    {
        forEachXmlChildElementWithTagName(*tracksXml, trackXml, "track")
            performanceTracks.push_back({ { trackXml->getIntAttribute("x", 0), trackXml->getIntAttribute("y", 0) },
                                          trackXml->getBoolAttribute("horizontal", true) });
    }

    if (auto* orbitXml = parsed->getChildByName("orbitCenters"))
    {
        forEachXmlChildElementWithTagName(*orbitXml, centreXml, "center")
            performanceOrbitCenters.push_back({ centreXml->getIntAttribute("x", 0), centreXml->getIntAttribute("y", 0) });
    }

    if (auto* automataXml = parsed->getChildByName("automataCells"))
    {
        forEachXmlChildElementWithTagName(*automataXml, cellXml, "cell")
            performanceAutomataCells.push_back({ cellXml->getIntAttribute("x", 0), cellXml->getIntAttribute("y", 0) });
    }

    visualStepCounter.store(beatStepIndex, std::memory_order_relaxed);
    visualBarCounter.store(beatBarIndex, std::memory_order_relaxed);
    visualBeatPulse = 0.0f;
    visualBarPulse = 0.0f;
    visualBarSweep = 0.0f;
    performanceBeatEnergy = 0.0f;
    if (isolatedSlab.isValid() && isolatedBuildMode == IsolatedBuildMode::tetrisTopDown)
    {
        tetrisPiece.z = slabZStart(isolatedSlab) + tetrisBuildLayer;
        if (tetrisPiece.active)
            clampTetrisPieceToSlab(tetrisPiece);
        else
            spawnTetrisPiece(false);
    }
    if (isolatedSlab.isValid() && isolatedBuildMode == IsolatedBuildMode::stampLibraryTopDown)
    {
        rebuildStampLibrary();
        stampLibraryIndex = juce::jlimit(0, static_cast<int>(stampLibrary.size()) - 1, stampLibraryIndex);
        stampHoverCell = juce::Point<int>(editCursor.x, editCursor.y);
        stampCaptureAnchor.reset();
    }
    rebuildFilledVoxelCache();
    return true;
}

void MainComponent::showSaveDialog()
{
    activeFileChooser = std::make_unique<juce::FileChooser>("Save .drd state",
                                                            juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile("KlangKunstWorld.drd"),
                                                            "*.drd");
    juce::Component::SafePointer<MainComponent> safeThis(this);
    activeFileChooser->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles,
                                   [safeThis] (const juce::FileChooser& chooser)
                                   {
                                       if (auto* self = safeThis.getComponent())
                                       {
                                           const auto file = chooser.getResult();
                                           if (file != juce::File{} && ! self->saveStateToFile(file))
                                               juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon, "Save Failed", "Could not save the .drd file.");
                                           self->activeFileChooser.reset();
                                       }
                                   });
}

void MainComponent::showLoadDialog()
{
    activeFileChooser = std::make_unique<juce::FileChooser>("Load .drd state",
                                                            juce::File::getSpecialLocation(juce::File::userDocumentsDirectory),
                                                            "*.drd");
    juce::Component::SafePointer<MainComponent> safeThis(this);
    activeFileChooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                                   [safeThis] (const juce::FileChooser& chooser)
                                   {
                                       if (auto* self = safeThis.getComponent())
                                       {
                                           const auto file = chooser.getResult();
                                           if (file != juce::File{})
                                           {
                                               if (! self->loadStateFromFile(file))
                                                   juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon, "Load Failed", "Could not load the .drd file.");
                                               else
                                                {
                                                    self->screenMode = ScreenMode::world;
                                                    self->hoveredTitleAction = TitleAction::none;
                                                   self->repaint();
                                                }
                                           }
                                           self->activeFileChooser.reset();
                                       }
                                   });
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

    const int zStart = slabZStart(isolatedSlab);
    const int zEnd = slabZEndExclusive(isolatedSlab);
    const juce::ScopedLock sl(synthLock);

    int triggered = 0;
    for (int z = zStart; z < zEnd; ++z)
    {
        if (! hasVoxel(cell.x, cell.y, z))
            continue;

        const int midiNote = midiNoteForHeight(z);
        const float velocity = juce::jlimit(0.12f, 0.78f, 0.28f + 0.032f * static_cast<float>(z - zStart));
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
    buffer.addEvent(juce::MidiMessage::noteOn(1, midiNote, juce::jlimit(0.0f, 1.0f, velocity * 0.86f)), onOffset);

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

int MainComponent::slabNumber(const SlabSelection& slab) const
{
    return slab.isValid() ? slabIndex(slab) + 1 : 0;
}

juce::String MainComponent::labelForSlab(const SlabSelection& slab) const
{
    if (! slab.isValid())
        return {};

    return "Island " + juce::String(slabNumber(slab));
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
        zMin = 0;
        zMax = slabZEndExclusive(isolatedSlab) - slabZStart(isolatedSlab);
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

    const int zStart = slabZStart(isolatedSlab);
    const int zEnd = slabZEndExclusive(isolatedSlab);

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

void MainComponent::drawTetrisBuildView(juce::Graphics& g, juce::Rectangle<float> area)
{
    if (! isolatedSlab.isValid())
        return;

    int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    quadrantBounds(isolatedSlab.quadrant, x0, y0, x1, y1);

    const int slabWidth = x1 - x0;
    const int slabHeight = y1 - y0;
    const auto board = performanceBoardBounds(area);
    const float tileSize = board.getWidth() / static_cast<float>(slabWidth);
    const float pulse = 0.5f + 0.5f * static_cast<float>(std::sin(juce::Time::getMillisecondCounterHiRes() * 0.0032));

    g.setColour(juce::Colour::fromRGBA(6, 11, 30, 236));
    g.fillRoundedRectangle(board.expanded(26.0f), 28.0f);
    g.setColour(juce::Colour::fromRGBA(74, 144, 255, 72));
    g.drawRoundedRectangle(board.expanded(26.0f), 28.0f, 1.8f);

    juce::ColourGradient boardGlow(juce::Colour::fromRGBA(42, 102, 212, 102),
                                   board.getCentreX(), board.getCentreY(),
                                   juce::Colour::fromRGBA(8, 13, 36, 0),
                                   board.getCentreX(), board.getBottom() + 80.0f,
                                   true);
    g.setGradientFill(boardGlow);
    g.fillEllipse(board.expanded(96.0f, 82.0f));

    const int zStart = slabZStart(isolatedSlab);
    const int zEnd = slabZEndExclusive(isolatedSlab);
    const int activeLayerZ = zStart + juce::jlimit(0, tetrisLayerCount - 1, tetrisBuildLayer);
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
            g.setColour(juce::Colour::fromRGBA(8, 14, 34, 214));
            g.fillRect(cell);

            struct LayerSlice
            {
                juce::Colour colour;
                bool active = false;
            };

            std::vector<LayerSlice> slices;
            slices.reserve(static_cast<size_t>(zEnd - zStart));
            for (int z = zStart; z < zEnd; ++z)
                if (hasVoxel(worldX, worldY, z))
                    slices.push_back({ colourForHeight(z), z == activeLayerZ });

            if (! slices.empty())
            {
                auto innerCell = cell.reduced(2.0f);
                const float sliceHeight = innerCell.getHeight() / static_cast<float>(slices.size());
                for (size_t i = 0; i < slices.size(); ++i)
                {
                    auto sliceBounds = juce::Rectangle<float>(innerCell.getX(),
                                                              innerCell.getY() + sliceHeight * static_cast<float>(i),
                                                              innerCell.getWidth(),
                                                              juce::jmax(1.0f, sliceHeight + 0.5f));
                    auto colour = slices[i].colour;
                    if (slices[i].active)
                        colour = colour.withMultipliedBrightness(1.12f);
                    else
                        colour = colour.interpolatedWith(juce::Colour::greyLevel(colour.getPerceivedBrightness()), 0.88f)
                                       .withMultipliedBrightness(0.52f)
                                       .withMultipliedAlpha(0.55f);
                    g.setColour(colour);
                    g.fillRect(sliceBounds);
                    if (slices[i].active)
                    {
                        g.setColour(juce::Colour::fromRGBA(255, 255, 255, 72));
                        g.drawRect(sliceBounds, 1.0f);
                    }
                }
            }

            g.setColour(juce::Colour::fromRGBA(88, 122, 214, 32));
            g.drawRect(cell, 1.0f);
        }
    }

    if (! tetrisPiece.active)
        spawnTetrisPiece(false);

    const bool pieceFits = tetrisPieceFits(tetrisPiece) && ! tetrisPieceCollidesWithVoxels(tetrisPiece);
    const auto pieceColour = pieceFits ? juce::Colour::fromRGBA(84, 238, 255, 210)
                                       : juce::Colour::fromRGBA(255, 116, 116, 214);
    const auto pieceGlow = pieceFits ? juce::Colour::fromRGBA(84, 238, 255, static_cast<uint8_t>(44 + 24 * pulse))
                                     : juce::Colour::fromRGBA(255, 116, 116, static_cast<uint8_t>(44 + 24 * pulse));
    for (const auto& offset : tetrominoOffsets(tetrisPiece.type, tetrisPiece.rotation))
    {
        const int worldX = tetrisPiece.anchor.x + offset.x;
        const int worldY = tetrisPiece.anchor.y + offset.y;
        if (! cellInSelectedSlab(worldX, worldY, isolatedSlab))
            continue;

        const int localX = worldX - x0;
        const int localY = worldY - y0;
        auto cell = juce::Rectangle<float>(board.getX() + static_cast<float>(localX) * tileSize,
                                           board.getY() + static_cast<float>(localY) * tileSize,
                                           tileSize,
                                           tileSize).reduced(1.5f);
        g.setColour(pieceGlow);
        g.fillRoundedRectangle(cell.expanded(4.0f), 6.0f);
        g.setColour(pieceColour.withAlpha(0.26f));
        g.fillRoundedRectangle(cell, 5.0f);
        g.setColour(pieceColour);
        g.drawRoundedRectangle(cell, 5.0f, 2.3f);
        g.setColour(juce::Colours::white.withAlpha(0.82f));
        g.drawLine(cell.getX() + 4.0f, cell.getY() + 4.0f, cell.getRight() - 4.0f, cell.getBottom() - 4.0f, 1.2f);
        g.drawLine(cell.getRight() - 4.0f, cell.getY() + 4.0f, cell.getX() + 4.0f, cell.getBottom() - 4.0f, 1.2f);
    }

    auto pieceTag = juce::Rectangle<float>(156.0f, 28.0f)
                        .withCentre({ board.getCentreX(), board.getY() - 24.0f });
    g.setColour(juce::Colour::fromRGBA(8, 14, 34, 228));
    g.fillRoundedRectangle(pieceTag, 8.0f);
    g.setColour(pieceColour.withAlpha(0.9f));
    g.drawRoundedRectangle(pieceTag, 8.0f, 1.5f);
    g.setColour(juce::Colours::white);
    g.setFont(juce::FontOptions(13.0f));
    g.drawFittedText("Piece " + tetrominoTypeName(tetrisPiece.type) + "  layer " + juce::String(tetrisBuildLayer),
                     pieceTag.toNearestInt(),
                     juce::Justification::centred,
                     1);

    auto previewPanel = juce::Rectangle<float>(board.getRight() + 34.0f, board.getY() + 10.0f, 112.0f, 112.0f);
    previewPanel = previewPanel.withRight(juce::jmin(area.getRight() - 12.0f, previewPanel.getRight()));
    g.setColour(juce::Colour::fromRGBA(8, 14, 34, 226));
    g.fillRoundedRectangle(previewPanel, 14.0f);
    g.setColour(juce::Colour::fromRGBA(84, 238, 255, 88));
    g.drawRoundedRectangle(previewPanel, 14.0f, 1.4f);
    g.setColour(juce::Colour::fromRGBA(170, 214, 255, 170));
    g.setFont(juce::FontOptions(12.0f));
    g.drawText("NEXT", previewPanel.removeFromTop(24.0f).toNearestInt(), juce::Justification::centred);
    auto previewArea = previewPanel.reduced(18.0f);
    const auto previewOffsets = tetrominoOffsets(nextTetrisType, 0);
    int minX = previewOffsets[0].x, maxX = previewOffsets[0].x, minY = previewOffsets[0].y, maxY = previewOffsets[0].y;
    for (const auto& offset : previewOffsets)
    {
        minX = juce::jmin(minX, offset.x);
        maxX = juce::jmax(maxX, offset.x);
        minY = juce::jmin(minY, offset.y);
        maxY = juce::jmax(maxY, offset.y);
    }
    const float previewTile = juce::jmin(previewArea.getWidth() / static_cast<float>(maxX - minX + 2),
                                         previewArea.getHeight() / static_cast<float>(maxY - minY + 2));
    const float previewStartX = previewArea.getCentreX() - previewTile * static_cast<float>(maxX - minX + 1) * 0.5f;
    const float previewStartY = previewArea.getCentreY() - previewTile * static_cast<float>(maxY - minY + 1) * 0.5f;
    for (const auto& offset : previewOffsets)
    {
        auto cell = juce::Rectangle<float>(previewStartX + previewTile * static_cast<float>(offset.x - minX),
                                           previewStartY + previewTile * static_cast<float>(offset.y - minY),
                                           previewTile,
                                           previewTile).reduced(1.5f);
        g.setColour(juce::Colour::fromRGBA(84, 238, 255, 52));
        g.fillRoundedRectangle(cell.expanded(2.0f), 4.0f);
        g.setColour(juce::Colour::fromRGBA(84, 238, 255, 168));
        g.fillRoundedRectangle(cell, 4.0f);
        g.setColour(juce::Colours::white.withAlpha(0.84f));
        g.drawRoundedRectangle(cell, 4.0f, 1.1f);
    }
}

void MainComponent::rebuildStampLibrary()
{
    stampLibrary.clear();
    stampLibraryIndex = 0;
    stampRotation = 0;
    stampBaseLayer = 0;
    stampHoverCell.reset();

    std::vector<StampMotif> candidates;
    candidates.reserve(32);

    for (int quadrant = 0; quadrant < 4; ++quadrant)
    {
        int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
        quadrantBounds(quadrant, x0, y0, x1, y1);

        for (int floor = 0; floor < 4; ++floor)
        {
            const int zStart = floor * floorBandHeight;
            const int zEnd = juce::jmin(gridHeight, zStart + floorBandHeight);

            for (int startY = y0; startY <= y1 - 4; startY += 5)
            {
                for (int startX = x0; startX <= x1 - 4; startX += 5)
                {
                    StampMotif motif;
                    motif.width = 4;
                    motif.height = 4;
                    motif.maxDz = 0;
                    for (int z = zStart; z < zEnd; ++z)
                    {
                        for (int y = startY; y < startY + 4; ++y)
                        {
                            for (int x = startX; x < startX + 4; ++x)
                            {
                                if (! hasVoxel(x, y, z))
                                    continue;

                                motif.voxels.push_back({ x - startX, y - startY, z - zStart });
                                motif.maxDz = juce::jmax(motif.maxDz, z - zStart);
                            }
                        }
                    }

                    if (motif.voxels.size() < 4 || motif.voxels.size() > 28)
                        continue;

                    motif.name = labelForSlab({ quadrant, floor }) + " " + juce::String(static_cast<int>(candidates.size()) + 1);
                    candidates.push_back(std::move(motif));
                }
            }
        }
    }

    std::sort(candidates.begin(), candidates.end(), [] (const StampMotif& a, const StampMotif& b)
    {
        return a.voxels.size() > b.voxels.size();
    });

    const int keepCount = juce::jmin(8, static_cast<int>(candidates.size()));
    for (int i = 0; i < keepCount; ++i)
        stampLibrary.push_back(candidates[static_cast<size_t>(i)]);

    if (stampLibrary.empty())
    {
        StampMotif fallback;
        fallback.name = "Block";
        fallback.width = 1;
        fallback.height = 1;
        fallback.maxDz = 0;
        fallback.voxels.push_back({ 0, 0, 0 });
        stampLibrary.push_back(std::move(fallback));
    }
}

bool MainComponent::captureStampFromSelection(juce::Rectangle<int> selection)
{
    if (! isolatedSlab.isValid() || selection.isEmpty())
        return false;

    int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    quadrantBounds(isolatedSlab.quadrant, x0, y0, x1, y1);
    const auto slabBounds = juce::Rectangle<int>(x0, y0, x1 - x0, y1 - y0);
    selection = selection.getIntersection(slabBounds);
    if (selection.isEmpty())
        return false;

    const int zStart = slabZStart(isolatedSlab);
    const int zEnd = slabZEndExclusive(isolatedSlab);
    int minZ = zEnd;
    int maxZ = zStart - 1;

    for (int z = zStart; z < zEnd; ++z)
        for (int y = selection.getY(); y < selection.getBottom(); ++y)
            for (int x = selection.getX(); x < selection.getRight(); ++x)
                if (hasVoxel(x, y, z))
                {
                    minZ = juce::jmin(minZ, z);
                    maxZ = juce::jmax(maxZ, z);
                }

    if (maxZ < minZ)
        return false;

    StampMotif motif;
    motif.name = "Selection";
    motif.width = selection.getWidth();
    motif.height = selection.getHeight();
    motif.maxDz = maxZ - minZ;

    for (int z = minZ; z <= maxZ; ++z)
        for (int y = selection.getY(); y < selection.getBottom(); ++y)
            for (int x = selection.getX(); x < selection.getRight(); ++x)
                if (hasVoxel(x, y, z))
                    motif.voxels.push_back({ x - selection.getX(), y - selection.getY(), z - minZ });

    if (! motif.isValid())
        return false;

    if (stampLibrary.empty())
        stampLibrary.push_back(motif);
    else
        stampLibrary[static_cast<size_t>(juce::jlimit(0, static_cast<int>(stampLibrary.size()) - 1, stampLibraryIndex))] = motif;

    stampRotation = 0;
    return true;
}

std::vector<MainComponent::StampVoxel> MainComponent::rotatedStampVoxels(const StampMotif& motif, int rotation, int& widthOut, int& heightOut) const
{
    const int r = ((rotation % 4) + 4) % 4;
    std::vector<StampVoxel> rotated;
    rotated.reserve(motif.voxels.size());

    for (const auto& voxel : motif.voxels)
    {
        StampVoxel out = voxel;
        switch (r)
        {
            case 0:
                out.dx = voxel.dx;
                out.dy = voxel.dy;
                break;
            case 1:
                out.dx = motif.height - 1 - voxel.dy;
                out.dy = voxel.dx;
                break;
            case 2:
                out.dx = motif.width - 1 - voxel.dx;
                out.dy = motif.height - 1 - voxel.dy;
                break;
            default:
                out.dx = voxel.dy;
                out.dy = motif.width - 1 - voxel.dx;
                break;
        }
        rotated.push_back(out);
    }

    widthOut = (r % 2) == 0 ? motif.width : motif.height;
    heightOut = (r % 2) == 0 ? motif.height : motif.width;
    return rotated;
}

bool MainComponent::stampFitsAtCell(const StampMotif& motif, juce::Point<int> cell, int baseZ, int rotation) const
{
    if (! isolatedSlab.isValid())
        return false;

    int width = 0, height = 0;
    const auto rotated = rotatedStampVoxels(motif, rotation, width, height);
    const int zMin = slabZStart(isolatedSlab);
    const int zMax = slabZEndExclusive(isolatedSlab) - 1;

    for (const auto& voxel : rotated)
    {
        const int x = cell.x + voxel.dx;
        const int y = cell.y + voxel.dy;
        const int z = baseZ + voxel.dz;
        if (! cellInSelectedSlab(x, y, isolatedSlab))
            return false;
        if (z < zMin || z > zMax)
            return false;
    }

    return true;
}

void MainComponent::applyStampAtCell(const StampMotif& motif, juce::Point<int> cell, int baseZ, int rotation, bool filled)
{
    if (! stampFitsAtCell(motif, cell, baseZ, rotation))
        return;

    int width = 0, height = 0;
    const auto rotated = rotatedStampVoxels(motif, rotation, width, height);
    for (const auto& voxel : rotated)
        setVoxel(cell.x + voxel.dx, cell.y + voxel.dy, baseZ + voxel.dz, filled);
}

void MainComponent::drawStampLibraryBuildView(juce::Graphics& g, juce::Rectangle<float> area)
{
    if (! isolatedSlab.isValid())
        return;

    int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    quadrantBounds(isolatedSlab.quadrant, x0, y0, x1, y1);

    const int slabWidth = x1 - x0;
    const int slabHeight = y1 - y0;
    const auto board = performanceBoardBounds(area);
    const float tileSize = board.getWidth() / static_cast<float>(slabWidth);
    const float pulse = 0.5f + 0.5f * static_cast<float>(std::sin(juce::Time::getMillisecondCounterHiRes() * 0.0032));
    const int zStart = slabZStart(isolatedSlab);
    const int zEnd = slabZEndExclusive(isolatedSlab);
    const int activeLayerZ = zStart + juce::jlimit(0, isolatedBuildMaxHeight - 1, stampBaseLayer);

    g.setColour(juce::Colour::fromRGBA(6, 11, 30, 236));
    g.fillRoundedRectangle(board.expanded(26.0f), 28.0f);
    g.setColour(juce::Colour::fromRGBA(74, 144, 255, 72));
    g.drawRoundedRectangle(board.expanded(26.0f), 28.0f, 1.8f);

    juce::ColourGradient boardGlow(juce::Colour::fromRGBA(42, 102, 212, 102),
                                   board.getCentreX(), board.getCentreY(),
                                   juce::Colour::fromRGBA(8, 13, 36, 0),
                                   board.getCentreX(), board.getBottom() + 80.0f,
                                   true);
    g.setGradientFill(boardGlow);
    g.fillEllipse(board.expanded(96.0f, 82.0f));

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
            g.setColour(juce::Colour::fromRGBA(8, 14, 34, 214));
            g.fillRect(cell);

            std::vector<juce::Colour> slices;
            for (int z = zStart; z < zEnd; ++z)
                if (hasVoxel(worldX, worldY, z))
                {
                    auto colour = colourForHeight(z);
                    if (z != activeLayerZ)
                        colour = colour.interpolatedWith(juce::Colour::greyLevel(colour.getPerceivedBrightness()), 0.84f)
                                       .withMultipliedBrightness(0.5f)
                                       .withMultipliedAlpha(0.55f);
                    slices.push_back(colour);
                }

            if (! slices.empty())
            {
                auto innerCell = cell.reduced(2.0f);
                const float sliceHeight = innerCell.getHeight() / static_cast<float>(slices.size());
                for (size_t i = 0; i < slices.size(); ++i)
                {
                    auto sliceBounds = juce::Rectangle<float>(innerCell.getX(),
                                                              innerCell.getY() + sliceHeight * static_cast<float>(i),
                                                              innerCell.getWidth(),
                                                              juce::jmax(1.0f, sliceHeight + 0.5f));
                    g.setColour(slices[i]);
                    g.fillRect(sliceBounds);
                }
            }

            g.setColour(juce::Colour::fromRGBA(88, 122, 214, 32));
            g.drawRect(cell, 1.0f);
        }
    }

    if (! stampLibrary.empty() && stampHoverCell.has_value())
    {
        const auto& motif = stampLibrary[static_cast<size_t>(juce::jlimit(0, static_cast<int>(stampLibrary.size()) - 1, stampLibraryIndex))];
        int rotatedWidth = 0, rotatedHeight = 0;
        const auto rotated = rotatedStampVoxels(motif, stampRotation, rotatedWidth, rotatedHeight);
        const bool fits = stampFitsAtCell(motif, *stampHoverCell, activeLayerZ, stampRotation);
        const auto previewColour = fits ? juce::Colour::fromRGBA(84, 238, 255, 188)
                                        : juce::Colour::fromRGBA(255, 116, 116, 188);

        for (const auto& voxel : rotated)
        {
            const int worldX = stampHoverCell->x + voxel.dx;
            const int worldY = stampHoverCell->y + voxel.dy;
            if (! cellInSelectedSlab(worldX, worldY, isolatedSlab))
                continue;

            auto cell = juce::Rectangle<float>(board.getX() + static_cast<float>(worldX - x0) * tileSize,
                                               board.getY() + static_cast<float>(worldY - y0) * tileSize,
                                               tileSize,
                                               tileSize).reduced(1.6f);
            g.setColour(previewColour.withAlpha(0.22f + 0.08f * pulse));
            g.fillRoundedRectangle(cell, 5.0f);
            g.setColour(previewColour);
            g.drawRoundedRectangle(cell, 5.0f, 2.0f);
        }
    }

    if (stampCaptureMode && stampHoverCell.has_value())
    {
        const auto start = stampCaptureAnchor.value_or(*stampHoverCell);
        const auto xMin = juce::jmin(start.x, stampHoverCell->x);
        const auto yMin = juce::jmin(start.y, stampHoverCell->y);
        const auto xMax = juce::jmax(start.x, stampHoverCell->x);
        const auto yMax = juce::jmax(start.y, stampHoverCell->y);
        auto selectionBounds = juce::Rectangle<float>(board.getX() + static_cast<float>(xMin - x0) * tileSize,
                                                      board.getY() + static_cast<float>(yMin - y0) * tileSize,
                                                      static_cast<float>(xMax - xMin + 1) * tileSize,
                                                      static_cast<float>(yMax - yMin + 1) * tileSize).reduced(1.0f);
        g.setColour(juce::Colour::fromRGBA(255, 214, 120, 28));
        g.fillRoundedRectangle(selectionBounds, 8.0f);
        g.setColour(juce::Colour::fromRGBA(255, 228, 168, 220));
        g.drawRoundedRectangle(selectionBounds, 8.0f, 2.2f);
    }

    auto infoTag = juce::Rectangle<float>(224.0f, 28.0f).withCentre({ board.getCentreX(), board.getY() - 24.0f });
    g.setColour(juce::Colour::fromRGBA(8, 14, 34, 228));
    g.fillRoundedRectangle(infoTag, 8.0f);
    g.setColour(juce::Colour::fromRGBA(84, 238, 255, 160));
    g.drawRoundedRectangle(infoTag, 8.0f, 1.5f);
    g.setColour(juce::Colours::white);
    g.setFont(juce::FontOptions(13.0f));
    const auto& motif = stampLibrary[static_cast<size_t>(juce::jlimit(0, static_cast<int>(stampLibrary.size()) - 1, stampLibraryIndex))];
    g.drawFittedText((stampCaptureMode ? "Capture source" : "Stamp " + motif.name) + "  layer " + juce::String(stampBaseLayer),
                     infoTag.toNearestInt(), juce::Justification::centred, 1);
}

void MainComponent::drawAutomataBuildView(juce::Graphics& g, juce::Rectangle<float> area)
{
    if (! isolatedSlab.isValid())
        return;

    int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    quadrantBounds(isolatedSlab.quadrant, x0, y0, x1, y1);

    const int slabWidth = x1 - x0;
    const int slabHeight = y1 - y0;
    const auto board = performanceBoardBounds(area);
    const float tileSize = board.getWidth() / static_cast<float>(slabWidth);
    const float pulse = 0.5f + 0.5f * static_cast<float>(std::sin(juce::Time::getMillisecondCounterHiRes() * 0.0032));
    const int zStart = slabZStart(isolatedSlab);
    const int zEnd = slabZEndExclusive(isolatedSlab);
    const int activeLayer = juce::jlimit(0, isolatedBuildMaxHeight - 1, automataBuildLayer);
    const int activeLayerZ = zStart + activeLayer;

    g.setColour(juce::Colour::fromRGBA(6, 11, 30, 236));
    g.fillRoundedRectangle(board.expanded(26.0f), 28.0f);
    g.setColour(juce::Colour::fromRGBA(74, 144, 255, 72));
    g.drawRoundedRectangle(board.expanded(26.0f), 28.0f, 1.8f);

    juce::ColourGradient boardGlow(juce::Colour::fromRGBA(42, 102, 212, 102),
                                   board.getCentreX(), board.getCentreY(),
                                   juce::Colour::fromRGBA(8, 13, 36, 0),
                                   board.getCentreX(), board.getBottom() + 80.0f,
                                   true);
    g.setGradientFill(boardGlow);
    g.fillEllipse(board.expanded(96.0f, 82.0f));

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
            g.setColour(juce::Colour::fromRGBA(8, 14, 34, 214));
            g.fillRect(cell);

            const bool activeFilled = hasVoxel(worldX, worldY, activeLayerZ);
            int otherLayerCount = 0;
            for (int z = zStart; z < zEnd; ++z)
                if (z != activeLayerZ && hasVoxel(worldX, worldY, z))
                    ++otherLayerCount;

            auto innerCell = cell.reduced(2.0f);
            if (otherLayerCount > 0)
            {
                const float dimInset = juce::jmin(5.0f, 1.6f + 0.4f * static_cast<float>(otherLayerCount - 1));
                auto dimmedCell = innerCell.reduced(dimInset);
                g.setColour(juce::Colour::fromRGBA(130, 144, 176, static_cast<uint8_t>(46 + juce::jmin(otherLayerCount, 4) * 20)));
                g.fillRoundedRectangle(dimmedCell, 5.0f);
                g.setColour(juce::Colour::fromRGBA(196, 208, 236, static_cast<uint8_t>(18 + juce::jmin(otherLayerCount, 4) * 10)));
                g.drawRoundedRectangle(dimmedCell, 5.0f, 1.0f);
            }

            if (activeFilled)
            {
                const auto activeColour = colourForHeight(activeLayerZ).withMultipliedBrightness(1.1f);
                g.setColour(activeColour.withAlpha(0.22f + 0.08f * pulse));
                g.fillRoundedRectangle(innerCell.expanded(3.0f), 7.0f);
                g.setColour(activeColour);
                g.fillRoundedRectangle(innerCell, 5.0f);
                g.setColour(juce::Colours::white.withAlpha(0.30f));
                g.drawRoundedRectangle(innerCell, 5.0f, 1.2f);
            }

            if (automataHoverCell.has_value() && automataHoverCell->x == worldX && automataHoverCell->y == worldY)
            {
                auto hoverCell = cell.reduced(1.4f);
                g.setColour(juce::Colour::fromRGBA(84, 238, 255, static_cast<uint8_t>(38 + 24 * pulse)));
                g.fillRoundedRectangle(hoverCell.expanded(4.0f), 7.0f);
                g.setColour(juce::Colour::fromRGBA(84, 238, 255, 220));
                g.drawRoundedRectangle(hoverCell, 7.0f, 2.2f);
            }

            g.setColour(juce::Colour::fromRGBA(88, 122, 214, 32));
            g.drawRect(cell, 1.0f);
        }
    }

    auto infoTag = juce::Rectangle<float>(248.0f, 28.0f).withCentre({ board.getCentreX(), board.getY() - 24.0f });
    g.setColour(juce::Colour::fromRGBA(8, 14, 34, 228));
    g.fillRoundedRectangle(infoTag, 8.0f);
    g.setColour(juce::Colour::fromRGBA(84, 238, 255, 160));
    g.drawRoundedRectangle(infoTag, 8.0f, 1.5f);
    g.setColour(juce::Colours::white);
    g.setFont(juce::FontOptions(13.0f));
    g.drawFittedText(automataVariantName(automataVariantForSlab(isolatedSlab)) + "  layer " + juce::String(activeLayer),
                     infoTag.toNearestInt(),
                     juce::Justification::centred,
                     1);
}

void MainComponent::drawWireframeGrid(juce::Graphics& g, juce::Rectangle<float> area)
{
    if (isolatedSlab.isValid() && performanceMode)
    {
        drawPerformanceView(g, area);
        return;
    }

    if (isolatedSlab.isValid() && isolatedBuildMode == IsolatedBuildMode::tetrisTopDown)
    {
        drawTetrisBuildView(g, area);
        return;
    }

    if (isolatedSlab.isValid() && isolatedBuildMode == IsolatedBuildMode::stampLibraryTopDown)
    {
        drawStampLibraryBuildView(g, area);
        return;
    }

    if (isolatedSlab.isValid() && isolatedBuildMode == IsolatedBuildMode::cellularAutomataTopDown)
    {
        drawAutomataBuildView(g, area);
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

            const int floorZ = isolatedSlab.isValid() ? 0
                               : layoutMode == LayoutMode::FourIslandsFourFloors
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

        const int baseZ = isolatedSlab.isValid() ? 0 : renderBaseZForLayer(0);
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
        auto hasRenderableVoxel = [this] (int vx, int vy, int vz, const std::optional<SlabSelection>& slabSelection)
        {
            if (! hasVoxel(vx, vy, vz))
                return false;
            if (slabSelection.has_value())
                return voxelInSelectedSlab(vx, vy, vz, *slabSelection);
            return true;
        };
        const int baseZ = renderBaseZForLayer(z);
        const auto colour = colourForHeight(z);
        const SlabSelection voxelSlab { quadrantForCell(x, y), z / floorBandHeight };
        const auto slabLift = hoverLiftForSlab(voxelSlab);
        const bool showTop = (z == gridHeight - 1)
                          || (! isolatedSlab.isValid() && layoutMode == LayoutMode::FourIslandsFourFloors && (z % floorBandHeight) == floorBandHeight - 1)
                          || ! hasRenderableVoxel(x, y, z + 1, isolatedSlab.isValid() ? std::optional<SlabSelection>(isolatedSlab) : std::nullopt);
        const bool showLeft = hasLeftFaceDirection
                            ? ! hasRenderableVoxel(x + leftFaceDirection.dx,
                                                   y + leftFaceDirection.dy,
                                                   z,
                                                   isolatedSlab.isValid() ? std::optional<SlabSelection>(isolatedSlab) : std::nullopt)
                            : true;
        const bool showRight = hasRightFaceDirection
                             ? ! hasRenderableVoxel(x + rightFaceDirection.dx,
                                                    y + rightFaceDirection.dy,
                                                    z,
                                                    isolatedSlab.isValid() ? std::optional<SlabSelection>(isolatedSlab) : std::nullopt)
                             : true;

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

            const int floorZ = isolatedSlab.isValid() ? 0
                               : layoutMode == LayoutMode::FourIslandsFourFloors
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
        int topCursorZ = editCursor.z;
        const int slabZMin = isolatedSlab.floor * floorBandHeight;
        const int slabZMax = slabZMin + floorBandHeight - 1;
        for (int octave = 0; octave < juce::jmax(1, editPlacementHeight); ++octave)
            for (const int interval : editChordIntervals())
                if (const int z = editCursor.z + octave * 12 + interval; z >= slabZMin && z <= slabZMax)
                    topCursorZ = juce::jmax(topCursorZ, z);
        const int cursorBaseZ = renderBaseZForLayer(topCursorZ);
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

        const auto mirroredCells = placementCellsForSourceCell({ editCursor.x, editCursor.y }, isolatedSlab);
        for (const auto& cell : mirroredCells)
        {
            if (cell.x == editCursor.x && cell.y == editCursor.y)
                continue;

            EditCursor ghostCursor = editCursor;
            ghostCursor.x = cell.x;
            ghostCursor.y = cell.y;
            auto ghost = cursorPath(ghostCursor, area);
            g.setColour(juce::Colour::fromRGBA(120, 232, 255, 36));
            g.fillPath(ghost);
            g.setColour(juce::Colour::fromRGBA(120, 232, 255, 150));
            g.strokePath(ghost, juce::PathStrokeType(2.0f));
        }

        auto zLabelBounds = juce::Rectangle<float>(92.0f, 22.0f)
                                .withCentre({ topCentre.x + 46.0f, topCentre.y - 12.0f });
        zLabelBounds = zLabelBounds.withY(juce::jmax(area.getY() + 8.0f, zLabelBounds.getY()));
        g.setColour(juce::Colour::fromRGBA(8, 14, 34, 232));
        g.fillRoundedRectangle(zLabelBounds, 6.0f);
        g.setColour(juce::Colour::fromFloatRGBA(0.35f, 0.96f, 1.0f, 0.88f));
        g.drawRoundedRectangle(zLabelBounds, 6.0f, 1.4f);
        g.setColour(juce::Colours::white);
        g.setFont(juce::FontOptions(12.5f));
        const int localCursorZ = editCursor.z - slabZMin;
        const int localTopCursorZ = topCursorZ - slabZMin;
        const auto stackLabel = editPlacementHeight > 1
                                  ? ("z " + juce::String(localCursorZ) + "-" + juce::String(localTopCursorZ))
                                  : ("z " + juce::String(localCursorZ));
        g.drawFittedText(stackLabel, zLabelBounds.toNearestInt(), juce::Justification::centred, 1);

        auto chordLabelBounds = juce::Rectangle<float>(144.0f, 22.0f)
                                    .withCentre({ topCentre.x + 74.0f, topCentre.y + 14.0f });
        chordLabelBounds = chordLabelBounds.withY(juce::jmax(area.getY() + 34.0f, chordLabelBounds.getY()));
        g.setColour(juce::Colour::fromRGBA(8, 14, 34, 232));
        g.fillRoundedRectangle(chordLabelBounds, 6.0f);
        g.setColour(juce::Colour::fromRGBA(255, 194, 96, 210));
        g.drawRoundedRectangle(chordLabelBounds, 6.0f, 1.3f);
        g.setColour(juce::Colours::white);
        g.setFont(juce::FontOptions(12.0f));
        g.drawFittedText(currentEditChordName(), chordLabelBounds.toNearestInt(), juce::Justification::centred, 1);
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
    if (screenMode == ScreenMode::title)
        return;

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

    const auto modeLabel = isolatedSlab.isValid() ? (performanceMode ? "PERFORMANCE"
                                                                     : (isolatedBuildMode == IsolatedBuildMode::tetrisTopDown ? "TETRIS BUILD"
                                                                                                                               : isolatedBuildMode == IsolatedBuildMode::stampLibraryTopDown ? "STAMP LIBRARY"
                                                                                                                                 : isolatedBuildMode == IsolatedBuildMode::cellularAutomataTopDown ? "AUTOMATA BUILD"
                                                                                                                                                                                     : "EDIT VIEW"))
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
    const auto currentRule = buildRuleForSlab(isolatedSlab);
    const bool readOnlyTetrisPreview = tetrisVariantForSlab(isolatedSlab) != TetrisVariant::none
                                    && isolatedBuildMode == IsolatedBuildMode::cursor3D;
    const auto detailText = isolatedBuildMode == IsolatedBuildMode::tetrisTopDown
                              ? (tetrisVariantForSlab(isolatedSlab) == TetrisVariant::destruct
                                     ? "Destructive pieces carve on impact   Enter next layer   Mouse aim   Arrows move   S drop   Space hard drop   R rotate   [ ] layer   1-4 layers   V chord type   N reroll   Esc back"
                                     : tetrisVariantForSlab(isolatedSlab) == TetrisVariant::fracture
                                         ? "Dense layers fracture outward   Enter next layer   Mouse aim   Arrows move   S drop   Space hard drop   R rotate   [ ] layer   1-4 layers   V chord type   N reroll   Esc back"
                                         : tetrisVariantForSlab(isolatedSlab) == TetrisVariant::roulette
                                             ? "Every new piece mutates gravity, scale, or mirroring   Enter next layer   Mouse aim   Arrows move   S drop   Space hard drop   R rotate   [ ] layer   1-4 layers   V chord type   N reroll   Esc back"
                                             : "Enter next layer   Mouse aim   Arrows move   S drop   Space hard drop   R rotate   [ ] layer   1-4 layers   V chord type   N reroll   Esc back")
                              : isolatedBuildMode == IsolatedBuildMode::cellularAutomataTopDown
                                  ? "Click seed/erase   Arrows move   Enter evolve   [ ] layer   N random seed   P seed   X delete   C clear   Esc back"
                              : isolatedBuildMode == IsolatedBuildMode::stampLibraryTopDown
                                  ? "Tab 3D mode   S capture source   Click twice to mark source   Click stamp/remove   N/B motif   R rotate   [ ] layer   P stamp   C clear   Esc back"
                              : readOnlyTetrisPreview
                                  ? "Read-only isometric preview   Tab enter topdown tetris   Enter performance   WASD pan   Wheel zoom   Arrows inspect   [ ] height   Esc back"
                              : currentRule == IsolatedBuildRule::mirror
                                  ? "Build in the left half   Right half mirrors live   Enter performance   WASD pan   Wheel zoom   Mouse snap   Click place/remove   Arrows move   [ ] height   1-4 layers   V chord type   C clear   Esc back"
                                  : currentRule == IsolatedBuildRule::kaleidoscope
                                      ? "Build in the top-left quarter   Other quarters rotate and mirror   Enter performance   WASD pan   Wheel zoom   Mouse snap   Click place/remove   Arrows move   [ ] height   1-4 layers   V chord type   C clear   Esc back"
                              : currentRule == IsolatedBuildRule::stampClone
                                  ? "Tab stamp library   Enter performance   WASD pan   Wheel zoom   Mouse snap   Click place/remove   Arrows move   [ ] height   1-4 layers   V chord type   C clear   Esc back"
                                  : "Tab Tetris mode   Enter performance   WASD pan   Wheel zoom   Mouse snap   Click place/remove   Arrows move   [ ] height   1-4 layers   V chord type   C clear   Esc back";
    g.drawFittedText(detailText,
                     detailChip.reduced(14.0f, 0.0f).toNearestInt(),
                     juce::Justification::centredLeft,
                     1);

    inner.removeFromTop(12.0f);

    auto statRow = inner.removeFromTop(56.0f);
    const float statGap = 10.0f;
    const float statWidth = (statRow.getWidth() - statGap * 4.0f) / 5.0f;
    drawStat(statRow.removeFromLeft(statWidth), "BUILD", isolatedBuildModeName());
    statRow.removeFromLeft(statGap);
    drawStat(statRow.removeFromLeft(statWidth),
             isolatedBuildMode == IsolatedBuildMode::cellularAutomataTopDown ? "RULE" : "PLACE",
             isolatedBuildMode == IsolatedBuildMode::cellularAutomataTopDown ? automataVariantName(automataVariantForSlab(isolatedSlab))
                                                                             : editChordTypeName());
    statRow.removeFromLeft(statGap);
    drawStat(statRow.removeFromLeft(statWidth), "KEY", keyName());
    statRow.removeFromLeft(statGap);
    drawStat(statRow.removeFromLeft(statWidth), "SCALE", scaleName());
    statRow.removeFromLeft(statGap);
    drawStat(statRow.removeFromLeft(statWidth),
             isolatedBuildMode == IsolatedBuildMode::tetrisTopDown ? "PIECE"
                 : isolatedBuildMode == IsolatedBuildMode::stampLibraryTopDown ? "STAMP"
                 : isolatedBuildMode == IsolatedBuildMode::cellularAutomataTopDown ? "SYNTH"
                 : "SYNTH",
             isolatedBuildMode == IsolatedBuildMode::tetrisTopDown ? tetrominoTypeName(tetrisPiece.type)
                 : isolatedBuildMode == IsolatedBuildMode::stampLibraryTopDown
                     ? (stampCaptureMode ? "Capture Source"
                                         : (stampLibrary.empty() ? "None" : stampLibrary[static_cast<size_t>(juce::jlimit(0, static_cast<int>(stampLibrary.size()) - 1, stampLibraryIndex))].name))
                     : synthName());

    inner.removeFromTop(10.0f);

    auto chordRow = inner.removeFromTop(34.0f);
    auto drawChordChip = [&] (juce::Rectangle<float> bounds, const juce::String& text, bool active)
    {
        g.setColour(active ? juce::Colour::fromRGBA(255, 168, 84, 72)
                           : juce::Colour::fromRGBA(12, 22, 52, 182));
        g.fillRoundedRectangle(bounds, 12.0f);
        g.setColour(active ? juce::Colour::fromRGBA(255, 212, 140, 188)
                           : juce::Colour::fromRGBA(102, 182, 255, 38));
        g.drawRoundedRectangle(bounds, 12.0f, active ? 1.5f : 1.0f);
        g.setColour(juce::Colours::white);
        g.setFont(juce::FontOptions(active ? 12.5f : 12.0f));
        g.drawFittedText(text, bounds.reduced(10.0f, 0.0f).toNearestInt(), juce::Justification::centred, 1);
    };
    auto chordLabelForType = [] (EditChordType type) -> juce::String
    {
        switch (type)
        {
            case EditChordType::single: return "Single";
            case EditChordType::power: return "Power";
            case EditChordType::majorTriad: return "Major";
            case EditChordType::minorTriad: return "Minor";
            case EditChordType::sus2: return "Sus2";
            case EditChordType::sus4: return "Sus4";
            case EditChordType::majorSeventh: return "Maj7";
            case EditChordType::minorSeventh: return "Min7";
        }

        return "Single";
    };

    const std::array<EditChordType, 8> chordTypes {
        EditChordType::single, EditChordType::power, EditChordType::majorTriad, EditChordType::minorTriad,
        EditChordType::sus2, EditChordType::sus4, EditChordType::majorSeventh, EditChordType::minorSeventh
    };
    const float chordGap = 6.0f;
    const float chordWidth = (chordRow.getWidth() - chordGap * 7.0f) / 8.0f;
    for (size_t i = 0; i < chordTypes.size(); ++i)
    {
        auto chip = chordRow.removeFromLeft(chordWidth);
        if (i + 1 < chordTypes.size())
            chordRow.removeFromLeft(chordGap);
        drawChordChip(chip, chordLabelForType(chordTypes[i]), chordTypes[i] == editChordType);
    }

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
    const float infoWidth = (infoRow.getWidth() - infoGap * 3.0f) / 4.0f;
    const int slabBaseZ = slabZStart(isolatedSlab);
    const int displayZ = isolatedBuildMode == IsolatedBuildMode::tetrisTopDown ? (slabBaseZ + tetrisBuildLayer)
                       : isolatedBuildMode == IsolatedBuildMode::stampLibraryTopDown ? (slabBaseZ + stampBaseLayer)
                       : isolatedBuildMode == IsolatedBuildMode::cellularAutomataTopDown ? (slabBaseZ + automataBuildLayer)
                       : editCursor.z;
    const int localCursorZ = displayZ - slabBaseZ;
    drawInfoChip(infoRow.removeFromLeft(infoWidth),
                 isolatedBuildMode == IsolatedBuildMode::tetrisTopDown
                     ? ("Piece  x" + juce::String(tetrisPiece.anchor.x) + "  y" + juce::String(tetrisPiece.anchor.y) + "  layer " + juce::String(localCursorZ))
                     : isolatedBuildMode == IsolatedBuildMode::cellularAutomataTopDown
                         ? ("Seed  " + (automataHoverCell.has_value() ? ("x" + juce::String(automataHoverCell->x) + "  y" + juce::String(automataHoverCell->y)) : juce::String("hover board")) + "  layer " + juce::String(localCursorZ))
                     : isolatedBuildMode == IsolatedBuildMode::stampLibraryTopDown
                         ? ("Stamp  " + (stampHoverCell.has_value() ? ("x" + juce::String(stampHoverCell->x) + "  y" + juce::String(stampHoverCell->y)) : juce::String("hover board")) + "  layer " + juce::String(localCursorZ))
                     : ("Cursor  x" + juce::String(editCursor.x) + "  y" + juce::String(editCursor.y) + "  z" + juce::String(localCursorZ)));
    infoRow.removeFromLeft(infoGap);
    drawInfoChip(infoRow.removeFromLeft(infoWidth),
                 "Root  " + pitchClassName(localCursorZ));
    infoRow.removeFromLeft(infoGap);
    drawInfoChip(infoRow.removeFromLeft(infoWidth),
                 isolatedBuildMode == IsolatedBuildMode::cellularAutomataTopDown
                     ? ("Born  " + automataVariantName(automataVariantForSlab(isolatedSlab)))
                     : ("Layers  " + juce::String(editPlacementHeight) + " octaves"));
    infoRow.removeFromLeft(infoGap);
    drawInfoChip(infoRow.removeFromLeft(infoWidth),
                 isolatedBuildMode == IsolatedBuildMode::tetrisTopDown
                     ? ("Next  " + tetrominoTypeName(nextTetrisType) + "   " + currentEditChordName() + (tetrisPieceFits(tetrisPiece) ? "" : "  blocked"))
                     : isolatedBuildMode == IsolatedBuildMode::cellularAutomataTopDown
                         ? ("Next  evolve to z" + juce::String(juce::jmin(isolatedBuildMaxHeight - 1, localCursorZ + 1)) + "   " + currentEditChordName())
                     : isolatedBuildMode == IsolatedBuildMode::stampLibraryTopDown
                         ? (stampCaptureMode
                               ? (stampCaptureAnchor.has_value() ? "Capture  choose opposite corner" : "Capture  click first corner")
                               : ("Next  " + juce::String(stampLibraryIndex + 1) + "/" + juce::String(static_cast<int>(stampLibrary.size())) + "   " + currentEditChordName()))
                         : readOnlyTetrisPreview
                             ? ("Mode  " + tetrisVariantName(tetrisVariantForSlab(isolatedSlab)))
                         : ("Name  " + currentEditChordName()));

    inner.removeFromTop(8.0f);
    auto footerRow = inner.removeFromTop(28.0f);
    drawInfoChip(footerRow,
                 isolatedBuildMode == IsolatedBuildMode::tetrisTopDown
                     ? (tetrisVariantForSlab(isolatedSlab) == TetrisVariant::roulette
                            ? ("P place now   X delete   S soft drop   Space hard drop   grav " + juce::String(tetrisGravityFrames) + "   " + (rouletteMirrorPlacement ? "mirror on" : "mirror off"))
                            : "P place now   X delete   S soft drop   Space hard drop   M/B/K/L/U sound")
                     : isolatedBuildMode == IsolatedBuildMode::cellularAutomataTopDown
                         ? "Click seed   X delete   N randomise   Enter evolve next layer   M/B/K/L/U sound"
                     : isolatedBuildMode == IsolatedBuildMode::stampLibraryTopDown
                         ? "S capture   P stamp   X delete stamp   N/B browse   R rotate   M/B/K/L/U sound"
                         : readOnlyTetrisPreview
                             ? "Tab enter tetris   Enter performance   Read-only 3D preview"
                         : "P place   X delete   M/B/K/L/U sound");
}

juce::Rectangle<float> MainComponent::titleCardBounds(juce::Rectangle<float> area) const
{
    return area.withSizeKeepingCentre(900.0f, 400.0f).translated(0.0f, -16.0f);
}

juce::Rectangle<float> MainComponent::titleButtonBounds(juce::Rectangle<float> area, int index) const
{
    auto row = titleCardBounds(area).reduced(34.0f, 28.0f);
    row.removeFromTop(206.0f);
    const float gap = 18.0f;
    const float buttonWidth = (row.getWidth() - gap * 2.0f) / 3.0f;
    row.removeFromLeft((buttonWidth + gap) * static_cast<float>(index));
    return row.removeFromLeft(buttonWidth).removeFromTop(110.0f);
}

MainComponent::TitleAction MainComponent::titleActionAt(juce::Point<float> position, juce::Rectangle<float> area) const
{
    for (int i = 0; i < 3; ++i)
        if (titleButtonBounds(area, i).contains(position))
            return static_cast<TitleAction>(i + 1);

    return TitleAction::none;
}

juce::String MainComponent::titleActionLabel(TitleAction action) const
{
    switch (action)
    {
        case TitleAction::newWorld: return "NEW";
        case TitleAction::saveWorld: return "SAVE";
        case TitleAction::loadWorld: return "LOAD";
        case TitleAction::none: break;
    }

    return {};
}

void MainComponent::enterWorldFromTitle(bool regenerateWorld)
{
    if (regenerateWorld)
        randomiseVoxels();

    screenMode = ScreenMode::world;
    hoveredTitleAction = TitleAction::none;
    if (isolatedSlab.isValid() && isolatedBuildMode == IsolatedBuildMode::tetrisTopDown && ! tetrisPiece.active)
        spawnTetrisPiece(false);
    repaint();
}

void MainComponent::drawTitleScreen(juce::Graphics& g, juce::Rectangle<float> area)
{
    auto card = titleCardBounds(area);
    g.setColour(juce::Colour::fromRGBA(75, 136, 255, 20));
    g.fillRoundedRectangle(card.expanded(22.0f, 18.0f), 34.0f);

    juce::ColourGradient fill(juce::Colour::fromRGBA(7, 12, 34, 234),
                              card.getX(), card.getY(),
                              juce::Colour::fromRGBA(18, 30, 70, 226),
                              card.getRight(), card.getBottom(),
                              false);
    g.setGradientFill(fill);
    g.fillRoundedRectangle(card, 28.0f);
    g.setColour(juce::Colour::fromRGBA(122, 214, 255, 94));
    g.drawRoundedRectangle(card, 28.0f, 1.7f);

    auto inner = card.reduced(34.0f, 28.0f);
    auto titleArea = inner.removeFromTop(72.0f);
    auto subtitleArea = inner.removeFromTop(58.0f);
    auto statsArea = inner.removeFromTop(56.0f);
    inner.removeFromTop(20.0f);
    auto buttonArea = inner.removeFromTop(110.0f);
    inner.removeFromTop(18.0f);
    auto hintArea = inner.removeFromTop(26.0f);

    g.setColour(juce::Colours::white);
    g.setFont(juce::FontOptions(46.0f));
    g.drawText("KlangKunstWorld", titleArea.toNearestInt(), juce::Justification::centred);

    g.setColour(juce::Colour::fromRGBA(214, 228, 255, 228));
    g.setFont(juce::FontOptions(18.0f));
    g.drawFittedText("Sculpt a voxel world, split it into islands and floors, then dive into compact musical performance spaces.",
                     subtitleArea.toNearestInt(), juce::Justification::centred, 2);

    auto drawStat = [&] (juce::Rectangle<float> bounds, const juce::String& label, const juce::String& value)
    {
        g.setColour(juce::Colour::fromRGBA(11, 20, 46, 204));
        g.fillRoundedRectangle(bounds, 14.0f);
        g.setColour(juce::Colour::fromRGBA(102, 182, 255, 44));
        g.drawRoundedRectangle(bounds, 14.0f, 1.0f);
        auto statInner = bounds.reduced(12.0f, 8.0f);
        g.setColour(juce::Colour::fromRGBA(150, 210, 255, 168));
        g.setFont(juce::FontOptions(11.0f));
        g.drawText(label, statInner.removeFromTop(12.0f).toNearestInt(), juce::Justification::centredLeft);
        g.setColour(juce::Colours::white);
        g.setFont(juce::FontOptions(15.0f));
        g.drawText(value, statInner.toNearestInt(), juce::Justification::centredLeft);
    };

    const float statGap = 12.0f;
    const float statWidth = (statsArea.getWidth() - statGap * 2.0f) / 3.0f;
    drawStat(statsArea.removeFromLeft(statWidth), "WORLD", "128 x 128 x 48");
    statsArea.removeFromLeft(statGap);
    drawStat(statsArea.removeFromLeft(statWidth), "SAVES", ".drd");
    statsArea.removeFromLeft(statGap);
    drawStat(statsArea.removeFromLeft(statWidth), "FLOW", "Build / Perform");

    const float buttonGap = 18.0f;
    const float buttonWidth = (buttonArea.getWidth() - buttonGap * 2.0f) / 3.0f;
    const std::array<TitleAction, 3> actions { TitleAction::newWorld, TitleAction::saveWorld, TitleAction::loadWorld };
    const std::array<juce::String, 3> captions {
        "Generate a fresh world and enter build mode",
        "Write the current world and systems to a .drd",
        "Load a previously saved .drd session"
    };

    for (size_t i = 0; i < actions.size(); ++i)
    {
        auto button = buttonArea.removeFromLeft(buttonWidth);
        if (i + 1 < actions.size())
            buttonArea.removeFromLeft(buttonGap);

        const bool hovered = hoveredTitleAction == actions[i];
        const float pulse = hovered ? (0.5f + 0.5f * static_cast<float>(std::sin(juce::Time::getMillisecondCounterHiRes() * 0.006))) : 0.0f;

        g.setColour(juce::Colour::fromRGBA(12, 22, 54, hovered ? 238 : 214));
        g.fillRoundedRectangle(button, 18.0f);
        g.setColour(hovered ? juce::Colour::fromRGBA(146, 244, 255, static_cast<uint8_t>(178 + 40 * pulse))
                            : juce::Colour::fromRGBA(108, 182, 255, 64));
        g.drawRoundedRectangle(button, 18.0f, hovered ? 2.2f : 1.2f);
        g.setColour(hovered ? juce::Colour::fromRGBA(82, 232, 255, static_cast<uint8_t>(26 + 28 * pulse))
                            : juce::Colour::fromRGBA(58, 138, 255, 14));
        g.fillRoundedRectangle(button.reduced(4.0f, 4.0f), 15.0f);

        auto buttonInner = button.reduced(18.0f, 14.0f);
        g.setColour(juce::Colours::white);
        g.setFont(juce::FontOptions(22.0f));
        g.drawText(titleActionLabel(actions[i]),
                   buttonInner.removeFromTop(28.0f).toNearestInt(),
                   juce::Justification::centredLeft);
        g.setColour(juce::Colour::fromRGBA(194, 224, 255, 214));
        g.setFont(juce::FontOptions(14.5f));
        g.drawFittedText(captions[i], buttonInner.toNearestInt(), juce::Justification::centredLeft, 2);
    }

    g.setColour(juce::Colour::fromRGBA(198, 220, 255, 182));
    g.setFont(juce::FontOptions(13.0f));
    g.drawText("Enter or N starts a new world   S saves   L loads",
               hintArea.toNearestInt(),
               juce::Justification::centred);
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
