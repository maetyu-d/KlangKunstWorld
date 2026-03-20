#include "MainComponent.h"

#include <algorithm>
#include <array>
#include <limits>

namespace
{
constexpr float baseTileWidth = 12.0f;
constexpr float baseTileHeight = 6.0f;
constexpr float baseVerticalStep = baseTileHeight;
constexpr int boardInset = 26;
constexpr int islandGapSize = 20;
constexpr int floorBandHeight = 12;
constexpr int floorBandGap = 12;
}

MainComponent::MainComponent()
{
    voxels.resize(static_cast<size_t>(gridWidth * gridDepth * gridHeight), 0u);
    camera.zoom = 2.2f;
    targetZoom = camera.zoom;
    camera.heightScale = 1.0f;
    camera.panX = 0.0f;
    camera.panY = 0.0f;
    randomiseVoxels();
    setOpaque(true);
    setSize(1500, 980);
    setWantsKeyboardFocus(true);
    addKeyListener(this);
    startTimerHz(60);
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
        repaint();
    }
}

bool MainComponent::keyPressed(const juce::KeyPress& key, juce::Component*)
{
    return keyPressed(key);
}

bool MainComponent::keyPressed(const juce::KeyPress& key)
{
    if (isolatedSlab.isValid())
    {
        if (key == juce::KeyPress::escapeKey) { isolatedSlab = {}; hoveredSlab = {}; editCursor = {}; repaint(); return true; }
        if (key == juce::KeyPress::leftKey) { moveEditCursor(-1, 0, 0); repaint(); return true; }
        if (key == juce::KeyPress::rightKey) { moveEditCursor(1, 0, 0); repaint(); return true; }
        if (key == juce::KeyPress::upKey) { moveEditCursor(0, -1, 0); repaint(); return true; }
        if (key == juce::KeyPress::downKey) { moveEditCursor(0, 1, 0); repaint(); return true; }
        if (key == juce::KeyPress::pageUpKey || key == juce::KeyPress(']')) { moveEditCursor(0, 0, 1); repaint(); return true; }
        if (key == juce::KeyPress::pageDownKey || key == juce::KeyPress('[')) { moveEditCursor(0, 0, -1); repaint(); return true; }
        if (key == juce::KeyPress::returnKey || key == juce::KeyPress('p'))
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
    if (std::abs(delta) < 0.001f)
    {
        camera.zoom = targetZoom;
        return;
    }

    camera.zoom += delta * 0.28f;
    repaint();
}

void MainComponent::randomiseVoxels()
{
    juce::Random rng;
    layoutMode = LayoutMode::OneBoard;
    hoveredSlab = {};
    isolatedSlab = {};
    editCursor = {};
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

void MainComponent::drawWireframeGrid(juce::Graphics& g, juce::Rectangle<float> area)
{
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
                            ? "ISOLATED EDIT | WASD Pan | Wheel Zoom | Mouse snap | Click place | Right-click remove | Arrows move | [ ] height | Esc back"
                            : "BUILD MODE | WASD Pan | Wheel Zoom | Q/E Rotate | -/= Height | G View | R Randomise";
    g.drawText(modeText,
               area.removeFromTop(24.0f).reduced(10.0f, 0.0f).toNearestInt(),
               juce::Justification::centredLeft);

    g.setFont(juce::FontOptions(14.0f));
    juce::String detailText = "Random voxel field 128x128x48   z=0 no note   z=1 "
                            + noteNameForHeight(1)
                            + " chromatic upward   Filled " + juce::String(voxelCount)
                            + "   Note-colour mapping active";

    if (isolatedSlab.isValid() && editCursor.active)
    {
        detailText = "Editing " + labelForSlab(isolatedSlab)
                   + "   Cursor x" + juce::String(editCursor.x)
                   + " y" + juce::String(editCursor.y)
                   + " z" + juce::String(editCursor.z)
                   + "   Note " + noteNameForHeight(editCursor.z);
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

    return names[static_cast<size_t>((z - 1) % 12)];
}
