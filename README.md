# ursa_svg_screenshot

A JUCE module for exporting a component tree as **structured vector
graphics** for design handoff: SVG with per-component named groups (the
layer tree a designer sees mirrors your component tree), plus a
geometry-true PDF alternative on macOS. Debug tooling — compiled out of
release builds by default.

MIT licensed (see `LICENSE`). Includes significant parts of Nicole
Alassandro's MIT-licensed SVG renderer (`context/`, 2018), updated for
JUCE 8 by UrsaDSP; the export machinery (`export/`) is UrsaDSP's.

## Usage

```cmake
juce_add_module(path/to/ursa_svg_screenshot)
target_link_libraries(YourPlugin PRIVATE ursa_svg_screenshot)
```

```cpp
// one-shot exports
UrsaSvgExporter::exportComponent(mainUi, file.withFileExtension("svg"));
UrsaPdfExporter::exportComponent(mainUi, file.withFileExtension("pdf")); // macOS

// or hotkeys: F10 = SVG, F11 = PDF, saved as manual_<timestamp>.*
exportKeys = std::make_unique<UrsaVectorExportKeys>(mainUi, outputDir);
editor.addKeyListener(exportKeys.get());
```

Enabled when `JUCE_DEBUG` is on; override with
`URSA_SVG_SCREENSHOT_ENABLED=1/0`.

## What you get

- **SVG (primary)**: everything drawn with `Graphics` primitives becomes
  real vectors — including gradients with translucent stops
  (`stop-opacity`). Components render inside named `<g>` groups
  (componentID where set, else class name), single-child wrapper chains
  collapsed. Images (`drawImage`, buffered caches are bypassed during
  export) embed as base64 PNG. Text arrives as glyph outline paths.
- **PDF (macOS, secondary)**: JUCE's own CoreGraphics renderer into a
  `CGPDFContext` — production-faithful geometry and embedded-font text,
  but Quartz drops per-stop gradient alpha (PDF shadings can't express
  it), so translucent-stop gradients render opaque. Prefer the SVG.

## Provenance & local changes

`context/LowLevelGraphicsSVGRenderer.{h,cpp}` by Nicole Alassandro
(2018), vendored from an upstream JUCE module packaging that is no
longer available; treat this copy as canonical for our changes.
Changes are marked `JUCE 8 port` / `CTM overhaul` in the sources:

- JUCE 8 API: batched `drawGlyphs`, `FillType::gradient` as `unique_ptr`,
  `const` scale factor, two new pure virtuals.
- Fix: `writeTransform` wrote JUCE's row-major matrix into SVG's
  column-major `matrix(...)` slots — degenerate output that viewers
  discarded, leaving images unplaced.
- **CTM overhaul (July 2026)**: the renderer kept origin offsets separate
  from the accumulated transform and never applied the transform to
  emitted geometry (only to clips) — anything painted after
  `Graphics::addTransform` (value bubbles positioned by translation,
  `Drawable::drawWithin` logo art, ...) landed at untransformed
  coordinates while its clip was correct, so browsers clipped it to
  nothing. A single CTM now maps every draw call to device space:
  geometry, gradients, images and clips are all emitted in the same
  coordinate space, and `addTransform` composes in the correct order
  (`t.followedBy(existing)` — it was backwards).
- Fix: radial gradients wrote JUCE's `point2` (the rim point defining the
  radius) into SVG `fx`/`fy` as a focal point, collapsing the stop ramp
  toward the rim (crescent shadows, squashed glows). No focal point is
  emitted now.
- Emission culling: fully transparent fills and geometry whose device
  bounds miss the current clip are skipped — invisible on screen anyway,
  and emitting them corrupted consumers that measure group bounds
  (`getBBox` ignores `clip-path`).

Known upstream weak spots (unchanged): `clipToPath` replaces rather than
intersects, rotated/sheared rectangle clips are approximated by their
bounding box, and transparency layers are approximated as fill-opacity
rather than composited groups.
