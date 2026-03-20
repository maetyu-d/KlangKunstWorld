# KlangKunstWorldWireframe

A minimal JUCE desktop app that renders a `128 x 128 x 48` cube wireframe lattice in isometric view.

## Build

```bash
cd /Users/md/Downloads/KlangKunstWorld
cmake -S . -B build
cmake --build build -j 4
```

## Run

```bash
open /Users/md/Downloads/KlangKunstWorld/build/KlangKunstWorldWireframe_artefacts/KlangKunstWorldWireframe.app
```

## Controls

- `Q / E`: rotate the isometric camera
- `W / S`: zoom in / out
- `A / D`: reduce / increase vertical exaggeration
