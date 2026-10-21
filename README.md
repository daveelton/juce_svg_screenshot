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

## Scripted captures

`UrsaCaptureManager` services request files so a script can capture the
UI without anyone clicking through it. The design, and how to add it to
a plugin, are in `docs/CaptureRig.md`. A plugin's own views and request
fields are listed in that plugin's docs.

Write `<name>.request.json` into `<projectDir>/plugin/screenshots/`; the
running plugin writes `<name>.png` (or `.svg`/`.pdf`) beside it and
deletes the request. A failed pose rejects the request: a line in the
plugin log says why and the request file is deleted.

```json
{"mode": "all", "delay_ms": 200, "host": "standalone"}
{"mode": "all", "view": "<plugin view>", "scale": 2.0, "delay_ms": 800, "host": "standalone"}
{"mode": "component", "component_id": "<id>", "delay_ms": 100}
{"mode": "region", "x": 0, "y": 460, "w": 760, "h": 260, "delay_ms": 100}
{"mode": "all_when_visible", "component_id": "<id>", "delay_ms": 300}
{"mode": "svg", "component_id": "<id>", "delay_ms": 800}
{"mode": "svg", "trigger_id": "<id>", "delay_ms": 400}
{"mode": "pdf", "delay_ms": 300}
{"mode": "all", "set_params": {"<paramID>": 1.0}, "delay_ms": 700}
{"mode": "all", "set_params_text": {"<paramID>": "8.0 s"}, "delay_ms": 700}
{"mode": "all", "preset": "<display name>", "delay_ms": 1200}
{"mode": "all", "preset_file": "path/relative/to/repo.preset"}
{"mode": "all", "save_state": "before", "delay_ms": 200}
{"mode": "all", "load_state": "before", "delay_ms": 1200}
{"mode": "all", "hover_id": "<id>", "hover_x": 20, "hover_y": 20}
{"mode": "all", "press_id": "<id>", "delay_ms": 400}
{"mode": "all", "focus_id": "<id>", "delay_ms": 400}
{"mode": "all", "edit_id": "<id>", "delay_ms": 400}
{"mode": "component", "component_id": "plugin_editor", "editor_size": "min"}
```

**Modes.** `all` is the whole UI (the manager's `uiRoot`). `component`
is one component by `componentID`; `plugin_editor` is the editor itself,
the only component carrying a resize transform. `region` crops the whole
UI, in UI coordinates. `all_when_visible` captures the whole UI when
`component_id` appears, and only on arrival: it must be seen hidden
first, so queue one per tab and click through. `svg`/`pdf` are vector
exports of the whole UI or of `component_id`; with `trigger_id` they are
arrival-gated like `all_when_visible`. PDF is macOS only.

Every mode waits until its target is showing (hidden components paint
transparent), then for `delay_ms`. A request whose target never shows
waits indefinitely, which can be used on purpose: drop the request, then
switch to the right tab. `scale` scales raster output (`2.0` = @2x).
`all` and `region` composite open popup windows (menus) into the image;
`component` does not.

**Poses**, applied in this order. A request with any pose is serviced one
at a time in filename order, and `delay_ms` counts from the last pose.

| field | what it does | undone after the capture |
|---|---|---|
| `load_state` | `setStateInformation` from `plugin/screenshots/<name>.state` | no — it *is* the restore |
| `preset` / `preset_file` | the plugin's preset load path; display name, or a path (absolute or relative to the repo root) | no |
| `set_params` | `{paramID: 0-1}` through `setValueNotifyingHost`, so the UI reacts as to automation | no |
| `set_params_text` | `{paramID: "8.0 s"}`, parsed by the parameter's `getValueForText` | no |
| `view` | a named view the plugin defines | no |
| `editor_size` | `min`, `max`, `default` or `<w>x<h>`, clamped to the constrainer | yes |
| plugin fields | whatever the plugin's `UrsaCaptureHost` adds | as the plugin defines |
| `hover_id` (+ `hover_x`/`hover_y`, default the centre) | pointer over a component, through the window's peer | yes, the pointer leaves the window |
| `press_id` | hover and hold the left button; released outside, so no click fires | yes |
| `focus_id` | keyboard focus | yes |
| `edit_id` | the component's text entry open, if the plugin supports it | yes, discarded with Escape |
| `save_state` | `getStateInformation` to `plugin/screenshots/<name>.state`, applied last | n/a |

Parameter, preset and state poses are deliberately not undone; pair them
with `save_state`/`load_state` to leave the plugin as you found it. A
standalone persists its state and size between launches, so a relaunch
is not a reset.

**`host`**: `"standalone"` or `"daw"` pins a request to one kind of
instance; absent, any instance services it. Scripted captures should
always pass `"standalone"`: DAW sessions often hold stale builds, and the
symptom is byte-identical PNGs across captures that should differ.

**macOS limits.** The plugin's app must be the active application for
`hover_id`, `press_id`, `focus_id` and `edit_id`: the window server drops
mouse events aimed at a covered window and only the key window takes
focus. The rig floats the window, which is not enough on its own, so
someone must click the window first; a run from a shell captures the
idle UI. A posed popup menu closes within a few hundred ms unless the app
is active, so use a small `delay_ms` (~20). A resized editor with an open
popup can't be captured faithfully: the popup is composited at the
default zoom's size. `editor_size` is invisible in an `all` capture when
the editor zooms the UI with a transform; capture `plugin_editor`.

**Manual captures**: F12 saves a PNG of the whole UI, F10 an SVG, F11 a
PDF (macOS), as `plugin/screenshots/manual_<timestamp>.*`.

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
