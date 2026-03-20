#pragma once

#include <JuceHeader.h>

class MainComponent final : public juce::Component,
                            public juce::KeyListener,
                            public juce::Timer
{
public:
    MainComponent();

    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseWheelMove(const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel) override;
    void mouseMove(const juce::MouseEvent& event) override;
    void mouseExit(const juce::MouseEvent& event) override;
    void mouseUp(const juce::MouseEvent& event) override;

    bool keyPressed(const juce::KeyPress& key, juce::Component*) override;
    bool keyPressed(const juce::KeyPress& key) override;
    void timerCallback() override;

private:
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
    juce::Path slabPath(const SlabSelection& slab, juce::Rectangle<float> area) const;
    SlabSelection slabAtPosition(juce::Point<float> position, juce::Rectangle<float> area) const;
    bool voxelInSelectedSlab(int x, int y, int z, const SlabSelection& slab) const;
    bool cellInSelectedSlab(int x, int y, const SlabSelection& slab) const;
    juce::Path cursorPath(const EditCursor& cursor, juce::Rectangle<float> area) const;
    bool updateCursorFromPosition(juce::Point<float> position, juce::Rectangle<float> area);
    void resetEditCursor();
    void moveEditCursor(int dx, int dy, int dz);
    juce::Colour displayColourForVoxel(int x, int y, int z, juce::Colour base) const;
    float hoverLiftForSlab(const SlabSelection& slab) const;
    juce::String labelForSlab(const SlabSelection& slab) const;
    void drawWireframeGrid(juce::Graphics& g, juce::Rectangle<float> area);
    void drawHud(juce::Graphics& g, juce::Rectangle<float> area);
    void drawBackdrop(juce::Graphics& g, juce::Rectangle<float> area);
    void splitIntoFourIslands();
    void rotateCamera(int direction);
    void panCamera(float dx, float dy);
    void changeZoom(float factor);
    void changeHeightScale(float delta);
    juce::Point<int> rotateXY(int x, int y) const;
    int gridLineStep() const;
    juce::Colour colourForHeight(int z) const;
    juce::String noteNameForHeight(int z) const;

    Camera camera;
    float targetZoom = 1.0f;
    std::vector<uint8_t> voxels;
    std::vector<FilledVoxel> filledVoxels;
    int voxelCount = 0;
    LayoutMode layoutMode = LayoutMode::OneBoard;
    SlabSelection hoveredSlab;
    SlabSelection isolatedSlab;
    EditCursor editCursor;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainComponent)
};
