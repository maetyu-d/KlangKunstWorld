#pragma once

#include <JuceHeader.h>
#include <atomic>

class MainComponent final : public juce::AudioAppComponent,
                            public juce::KeyListener,
                            public juce::Timer
{
public:
    MainComponent();
    ~MainComponent() override;

    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseWheelMove(const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel) override;
    void mouseMove(const juce::MouseEvent& event) override;
    void mouseExit(const juce::MouseEvent& event) override;
    void mouseUp(const juce::MouseEvent& event) override;
    void prepareToPlay(int samplesPerBlockExpected, double sampleRate) override;
    void getNextAudioBlock(const juce::AudioSourceChannelInfo& bufferToFill) override;
    void releaseResources() override;

    bool keyPressed(const juce::KeyPress& key, juce::Component*) override;
    bool keyPressed(const juce::KeyPress& key) override;
    void timerCallback() override;

private:
public:
    static constexpr int gridWidth = 128;
    static constexpr int gridDepth = 128;
    static constexpr int gridHeight = 48;
    static constexpr float voxelFillRatio = 0.02f;

    enum class LayoutMode
    {
        OneBoard,
        FourIslands,
        FourIslandsFourFloors
    };

    enum class SynthEngine
    {
        digitalV4,
        fmGlass,
        velvetNoise,
        chipPulse,
        guitarPluck
    };

    enum class ScaleType
    {
        chromatic,
        major,
        minor,
        dorian,
        pentatonic
    };

    enum class DrumMode
    {
        reactiveBreakbeat,
        rezStraight,
        tightPulse,
        forwardStep,
        railLine
    };

    enum class SnakeTriggerMode
    {
        headOnly,
        wholeBody
    };

    enum class PerformanceAgentMode
    {
        snakes,
        trains,
        orbiters,
        automata
    };

    enum class EditChordType
    {
        single,
        power,
        majorTriad,
        minorTriad,
        sus2,
        sus4,
        majorSeventh,
        minorSeventh
    };

    enum class IsolatedBuildMode
    {
        cursor3D,
        tetrisTopDown,
        stampLibraryTopDown,
        cellularAutomataTopDown
    };

    enum class IsolatedBuildRule
    {
        standard,
        mirror,
        kaleidoscope,
        stampClone
    };

    enum class TetrisVariant
    {
        none,
        standard,
        destruct,
        fracture,
        roulette
    };

    enum class AutomataVariant
    {
        none,
        life,
        coral,
        fredkin,
        dayNight
    };

private:
    enum class ScreenMode
    {
        title,
        world
    };

    enum class TitleAction
    {
        none,
        newWorld,
        saveWorld,
        loadWorld
    };

    struct Camera
    {
        int rotation = 0;
        float zoom = 1.0f;
        float heightScale = 1.0f;
        float panX = 0.0f;
        float panY = 0.0f;
    };

    struct FilledVoxel
    {
        uint16_t x = 0;
        uint16_t y = 0;
        uint8_t z = 0;
    };

    struct SlabSelection
    {
        int quadrant = -1;
        int floor = -1;

        bool isValid() const { return quadrant >= 0 && floor >= 0; }
    };

    struct EditCursor
    {
        int x = 0;
        int y = 0;
        int z = 0;
        bool active = false;
    };

    enum class TetrominoType
    {
        I,
        O,
        T,
        L,
        J,
        S,
        Z
    };

    struct TetrisPiece
    {
        TetrominoType type = TetrominoType::T;
        int rotation = 0;
        juce::Point<int> anchor;
        int z = 0;
        bool active = false;
    };

    struct StampVoxel
    {
        int dx = 0;
        int dy = 0;
        int dz = 0;
    };

    struct StampMotif
    {
        juce::String name;
        int width = 1;
        int height = 1;
        int maxDz = 0;
        std::vector<StampVoxel> voxels;

        bool isValid() const { return ! voxels.empty(); }
    };

    struct Snake
    {
        std::vector<juce::Point<int>> body;
        juce::Point<int> direction { 1, 0 };
        juce::Colour colour;
        int orbitIndex = 0;
        bool clockwise = true;
    };

    struct ReflectorDisc
    {
        juce::Point<int> cell;
        juce::Point<int> direction { 1, 0 };
    };

    struct TrackPiece
    {
        juce::Point<int> cell;
        bool horizontal = true;
    };

    enum class PerformancePlacementMode
    {
        selectOnly,
        placeDisc,
        placeTrack
    };

    struct PerformanceSelection
    {
        enum class Kind
        {
            none,
            disc,
            track
        };

        Kind kind = Kind::none;
        juce::Point<int> cell;

        bool isValid() const { return kind != Kind::none; }
    };

    struct PerformanceFlash
    {
        juce::Point<int> cell;
        juce::Colour colour;
        float intensity = 1.0f;
        bool discFlash = false;
    };

    struct PendingNoteOff
    {
        int note = 60;
        float secondsRemaining = 0.2f;
    };

    class WaveVoice final : public juce::SynthesiserVoice
    {
    public:
        explicit WaveVoice(SynthEngine& engineRef) : engine(engineRef) {}

        bool canPlaySound(juce::SynthesiserSound* s) override;
        void startNote(int midiNoteNumber, float velocity, juce::SynthesiserSound*, int) override;
        void stopNote(float velocity, bool allowTailOff) override;
        void pitchWheelMoved(int) override {}
        void controllerMoved(int, int) override {}
        void renderNextBlock(juce::AudioBuffer<float>& outputBuffer, int startSample, int numSamples) override;

    private:
        SynthEngine& engine;
        juce::ADSR adsr;
        juce::ADSR::Parameters adsrParams;
        float level = 0.0f;
        double currentAngle = 0.0;
        double angleDelta = 0.0;
        double modAngle = 0.0;
        double modDelta = 0.0;
        double subAngle = 0.0;
        double subDelta = 0.0;
        float noteAgeSeconds = 0.0f;
        uint32_t noiseSeed = 0u;
        float sampleHoldValue = 0.0f;
        int sampleHoldCounter = 0;
        int sampleHoldPeriod = 1;
        float lpState = 0.0f;
        float hpState = 0.0f;
        bool percussionMode = false;
        int percussionType = 0;
        float noiseLP = 0.0f;
        float noiseHP = 0.0f;
        float lastNoise = 0.0f;
        int chipSfxType = 0;
        std::vector<float> ksDelay;
        int ksIndex = 0;
        float ksLast = 0.0f;
    };

    class WaveSound final : public juce::SynthesiserSound
    {
    public:
        bool appliesToNote(int) override { return true; }
        bool appliesToChannel(int) override { return true; }
    };

    void randomiseVoxels();
    bool hasVoxel(int x, int y, int z) const;
    void setVoxel(int x, int y, int z, bool filled);
    size_t voxelIndex(int x, int y, int z) const;
    juce::Point<int> islandOffsetForCell(int x, int y) const;
    int renderBaseZForLayer(int z) const;
    int renderedWorldHeight() const;
    juce::Point<float> projectCellCorner(int x, int y, int z, int cellX, int cellY, juce::Rectangle<float> area) const;
    juce::Point<float> projectionOffset(juce::Rectangle<float> area) const;
    juce::Point<float> projectPoint(int x, int y, int z, juce::Rectangle<float> area) const;
    int quadrantForCell(int x, int y) const;
    void quadrantBounds(int quadrant, int& x0, int& y0, int& x1, int& y1) const;
    int slabZStart(const SlabSelection& slab) const;
    int slabZEndExclusive(const SlabSelection& slab) const;
    juce::Path slabPath(const SlabSelection& slab, juce::Rectangle<float> area) const;
    SlabSelection slabAtPosition(juce::Point<float> position, juce::Rectangle<float> area) const;
    bool voxelInSelectedSlab(int x, int y, int z, const SlabSelection& slab) const;
    bool cellInSelectedSlab(int x, int y, const SlabSelection& slab) const;
    juce::Path cursorPath(const EditCursor& cursor, juce::Rectangle<float> area) const;
    bool updateCursorFromPosition(juce::Point<float> position, juce::Rectangle<float> area);
    void resetEditCursor();
    void moveEditCursor(int dx, int dy, int dz);
    void clearIsolatedSlab();
    void applyEditPlacement(bool filled);
    void applyEditPlacementAtCell(int x, int y, int z, bool filled);
    IsolatedBuildRule buildRuleForSlab(const SlabSelection& slab) const;
    juce::String buildRuleName(IsolatedBuildRule rule) const;
    TetrisVariant tetrisVariantForSlab(const SlabSelection& slab) const;
    juce::String tetrisVariantName(TetrisVariant variant) const;
    AutomataVariant automataVariantForSlab(const SlabSelection& slab) const;
    juce::String automataVariantName(AutomataVariant variant) const;
    std::vector<juce::Point<int>> tetrisPlacementCells(const TetrisPiece& piece) const;
    bool shouldTetrisPieceDestroy(const TetrisPiece& piece) const;
    void applyFractureToCurrentLayer();
    void applyRouletteRuleOnNewPiece();
    juce::Point<int> canonicalBuildCellForSlab(juce::Point<int> cell, const SlabSelection& slab) const;
    std::vector<juce::Point<int>> placementCellsForSourceCell(juce::Point<int> sourceCell, const SlabSelection& slab) const;
    std::vector<int> editChordIntervals() const;
    juce::String editChordTypeName() const;
    juce::String pitchClassName(int semitone) const;
    juce::String currentEditChordName() const;
    juce::String isolatedBuildModeName() const;
    juce::String tetrominoTypeName(TetrominoType type) const;
    std::array<juce::Point<int>, 4> tetrominoOffsets(TetrominoType type, int rotation) const;
    bool tetrisPieceFits(const TetrisPiece& piece) const;
    bool tetrisPieceCollidesWithVoxels(const TetrisPiece& piece) const;
    void clampTetrisPieceToSlab(TetrisPiece& piece) const;
    void spawnTetrisPiece(bool randomizeType);
    void moveTetrisPiece(int dx, int dy, int dz);
    void rotateTetrisPiece();
    void placeTetrisPiece(bool filled);
    void advanceTetrisGravity();
    void softDropTetrisPiece();
    void hardDropTetrisPiece();
    void advanceAutomataLayer();
    void randomiseAutomataSeed();
    void toggleAutomataCell(juce::Point<int> cell, bool filled);
    int automataNeighbourCount(const std::vector<juce::Point<int>>& aliveCells, juce::Point<int> cell) const;
    TetrominoType randomTetrominoType() const;
    void rotateIsolatedSlabQuarterTurn();
    void drawTetrisBuildView(juce::Graphics& g, juce::Rectangle<float> area);
    void rebuildStampLibrary();
    bool captureStampFromSelection(juce::Rectangle<int> selection);
    std::vector<StampVoxel> rotatedStampVoxels(const StampMotif& motif, int rotation, int& widthOut, int& heightOut) const;
    bool stampFitsAtCell(const StampMotif& motif, juce::Point<int> cell, int baseZ, int rotation) const;
    void applyStampAtCell(const StampMotif& motif, juce::Point<int> cell, int baseZ, int rotation, bool filled);
    void drawStampLibraryBuildView(juce::Graphics& g, juce::Rectangle<float> area);
    void drawAutomataBuildView(juce::Graphics& g, juce::Rectangle<float> area);
    void rebuildFilledVoxelCache();
    bool saveStateToFile(const juce::File& file);
    bool loadStateFromFile(const juce::File& file);
    void showSaveDialog();
    void showLoadDialog();
    int midiNoteForHeight(int z) const;
    void triggerPerformanceNotesAtCell(juce::Point<int> cell);
    void addBeatEvent(juce::MidiBuffer& buffer, int midiNote, float velocity, int sampleOffset, int blockSamples);
    juce::Colour displayColourForVoxel(int x, int y, int z, juce::Colour base) const;
    float hoverLiftForSlab(const SlabSelection& slab) const;
    int slabNumber(const SlabSelection& slab) const;
    juce::String labelForSlab(const SlabSelection& slab) const;
    juce::Rectangle<int> performanceRegionBounds() const;
    juce::Rectangle<float> performanceBoardBounds(juce::Rectangle<float> area) const;
    std::optional<juce::Point<int>> performanceCellAtPosition(juce::Point<float> position, juce::Rectangle<float> area) const;
    void setPerformanceSnakeCount(int count);
    void stepPerformanceAgents();
    void stepPerformanceSnakes();
    void stepPerformanceOrbiters();
    void stepPerformanceAutomata();
    void drawPerformanceView(juce::Graphics& g, juce::Rectangle<float> area);
    void drawPerformanceSidebar(juce::Graphics& g, juce::Rectangle<float> area);
    void drawWireframeGrid(juce::Graphics& g, juce::Rectangle<float> area);
    void drawHud(juce::Graphics& g, juce::Rectangle<float> area);
    void drawBackdrop(juce::Graphics& g, juce::Rectangle<float> area);
    void drawTitleScreen(juce::Graphics& g, juce::Rectangle<float> area);
    juce::Rectangle<float> titleCardBounds(juce::Rectangle<float> area) const;
    juce::Rectangle<float> titleButtonBounds(juce::Rectangle<float> area, int index) const;
    TitleAction titleActionAt(juce::Point<float> position, juce::Rectangle<float> area) const;
    juce::String titleActionLabel(TitleAction action) const;
    void enterWorldFromTitle(bool regenerateWorld);
    void splitIntoFourIslands();
    void rotateCamera(int direction);
    void panCamera(float dx, float dy);
    void changeZoom(float factor);
    void changeHeightScale(float delta);
    juce::Point<int> rotateXY(int x, int y) const;
    int gridLineStep() const;
    juce::Colour colourForHeight(int z) const;
    juce::String noteNameForHeight(int z) const;
    std::vector<int> currentScaleSteps() const;
    int quantizeMidiToScale(int midi) const;
    int quantizeMidiToCurrentScaleStrict(int midi) const;
    bool quantizeWorldToCurrentScale();
    juce::String keyName() const;
    juce::String scaleName() const;
    juce::String synthName() const;
    juce::String drumModeName() const;
    juce::String snakeTriggerModeName() const;
    juce::String performanceAgentModeName() const;
    int slabIndex(const SlabSelection& slab) const;
    void applyPerformancePresetForSlab(const SlabSelection& slab);
    bool performanceTrackAt(juce::Point<int> cell) const;
    bool performanceTrackHorizontalAt(juce::Point<int> cell) const;
    bool performanceOrbitCenterAt(juce::Point<int> cell) const;
    void resetPerformanceAgents();

    Camera camera;
    ScreenMode screenMode = ScreenMode::title;
    TitleAction hoveredTitleAction = TitleAction::none;
    float targetZoom = 1.0f;
    std::vector<uint8_t> voxels;
    std::vector<FilledVoxel> filledVoxels;
    int voxelCount = 0;
    LayoutMode layoutMode = LayoutMode::OneBoard;
    SlabSelection hoveredSlab;
    SlabSelection isolatedSlab;
    EditCursor editCursor;
    int editPlacementHeight = 1;
    EditChordType editChordType = EditChordType::single;
    IsolatedBuildMode isolatedBuildMode = IsolatedBuildMode::cursor3D;
    TetrisPiece tetrisPiece;
    TetrominoType nextTetrisType = TetrominoType::L;
    int tetrisBuildLayer = 0;
    bool tetrisRotatePerLayerSession = false;
    int tetrisGravityTick = 0;
    int tetrisGravityFrames = 20;
    bool rouletteMirrorPlacement = false;
    bool rouletteRotatePerLayer = false;
    std::vector<StampMotif> stampLibrary;
    int stampLibraryIndex = 0;
    int stampRotation = 0;
    int stampBaseLayer = 0;
    std::optional<juce::Point<int>> stampHoverCell;
    bool stampCaptureMode = false;
    std::optional<juce::Point<int>> stampCaptureAnchor;
    int automataBuildLayer = 0;
    std::optional<juce::Point<int>> automataHoverCell;
    bool performanceMode = false;
    int performanceRegionMode = 2;
    int performanceAgentCount = 1;
    PerformanceAgentMode performanceAgentMode = PerformanceAgentMode::snakes;
    std::array<PerformanceAgentMode, 16> slabPerformanceModes {};
    std::array<double, 16> slabStartingTempos {};
    std::vector<Snake> performanceSnakes;
    std::vector<ReflectorDisc> performanceDiscs;
    std::vector<TrackPiece> performanceTracks;
    std::vector<juce::Point<int>> performanceOrbitCenters;
    std::vector<juce::Point<int>> performanceAutomataCells;
    std::vector<PerformanceFlash> performanceFlashes;
    std::optional<juce::Point<int>> performanceHoverCell;
    juce::Point<int> performanceSelectedDirection { 1, 0 };
    bool performanceTrackHorizontal = true;
    PerformancePlacementMode performancePlacementMode = PerformancePlacementMode::selectOnly;
    PerformanceSelection performanceSelection;
    int performanceTick = 0;
    SynthEngine synthEngine = SynthEngine::digitalV4;
    DrumMode drumMode = DrumMode::reactiveBreakbeat;
    SnakeTriggerMode snakeTriggerMode = SnakeTriggerMode::headOnly;
    ScaleType scale = ScaleType::minor;
    int keyRoot = 0;
    bool quantizeToScale = true;
    juce::Synthesiser synth;
    juce::Synthesiser beatSynth;
    juce::CriticalSection synthLock;
    std::vector<PendingNoteOff> pendingNoteOffs;
    std::vector<PendingNoteOff> pendingBeatNoteOffs;
    std::unique_ptr<juce::FileChooser> activeFileChooser;
    double currentSampleRate = 44100.0;
    double bpm = 168.0;
    double beatStepAccumulator = 0.0;
    int beatStepIndex = 0;
    int beatBarIndex = 0;
    std::atomic<int> visualStepCounter { 0 };
    std::atomic<int> visualBarCounter { 0 };
    float visualBeatPulse = 0.0f;
    float visualBarPulse = 0.0f;
    float visualBarSweep = 0.0f;
    float performanceBeatEnergy = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainComponent)
};
