# Capture rig

Scripted UI captures for a JUCE plugin in a debug build. A script, or an
agent, drops a small JSON request file into a folder. The running plugin
poses itself as the request asks (a tab, a parameter value, a preset, a
hovered control, an editor size), waits, and writes a PNG, SVG or PDF
named after the request. That lets a change to the UI be checked from the
command line without anyone clicking through the plugin.

The rig started inside UrsaVerb. Everything that doesn't depend on a
particular plugin lives here, so every UrsaDSP plugin gets it and its
fixes; a plugin supplies only its own views and widget-specific poses
through one small interface.

## How the parts fit

```
 plugin/screenshots/<name>.request.json
              │  scanned about once a second
              ▼
 UrsaCaptureManager ── parses ──► UrsaCaptureRequest
   │  one posed request at a time, in filename order
   ├── UrsaCapturePose    parameters, saved state, editor size
   ├── UrsaPointerPose    window raise, hover, press, focus, component lookup
   ├── UrsaCaptureHost    the plugin: views, presets, type-in, its own fields
   │
   ▼  target visible + delay_ms elapsed
 PNG (popup windows composited in) │ SVG / PDF via the vector exporters
```

All the classes live in `capture/` and are compiled under the module's
`URSA_SVG_SCREENSHOT_ENABLED` flag, like the exporters.

- **`UrsaCaptureManager`** owns the rig. It is built by the editor with the
  editor, the component that counts as "the whole UI", the screenshots
  folder and the plugin's `UrsaCaptureHost`. It scans for requests,
  filters on `host`, sequences the poses, waits for visibility and the
  delay, captures, undoes the transient poses and deletes the request
  file. It owns a 48 Hz timer of its own and the F10/F11/F12 hotkeys
  (`UrsaVectorExportKeys` for F10/F11, a PNG for F12).
- **`UrsaCaptureRequest`** is one parsed request: the capture mode and
  target, the shared pose fields, and the request's JSON object, kept so
  the host can read fields only it knows.
- **`UrsaCapturePose`** applies the poses that go through the processor
  rather than the UI. Parameters are set through the host-facing parameter
  (`setValueNotifyingHost`), so the UI reacts the way it does to automation.
  Saved state goes through `getStateInformation`/`setStateInformation`.
  Editor size is set through the editor's constrainer and put back after
  the capture.
- **`UrsaPointerPose`** sends real mouse events into the window's peer, so
  JUCE delivers genuine enter/move/down events and `isMouseOver()` reads
  true without test-only flags on widgets. It also raises and floats the
  window for those poses, gives and clears keyboard focus, and finds
  components by `componentID` (`plugin_editor` is the editor itself).
- **`UrsaCaptureHost`** is the interface a plugin implements, all virtual
  with defaults that reject:
  - `poseView(name)`: switch to a named view (a tab, an overlay, a popover).
  - `loadPreset(displayName)` / `loadPresetFile(file)`: through the
    plugin's own preset browser path.
  - `poseEditFocus(target, pointer)`: open a control's text entry. The
    widget that does this is plugin-specific; the pointer pose is passed
    in so the host can click.
  - `defaultEditorSize()`: what `editor_size: "default"` means.
  - `hasPluginPose(request)`, `applyPluginPoses(request)`,
    `undoPluginPoses()`: request fields only this plugin understands
    (UrsaVerb's `key_grid_order`, `eq_menu_band`).

A plugin that implements none of the host methods still gets full-window,
component, region, SVG and PDF captures, plus parameter, state, size,
hover, press and focus poses.

## Behaviour a script can rely on

- Poses apply in one fixed order: saved state, preset, preset file,
  parameters (normalised, then text), view, editor size, the plugin's own
  poses, then (after the window has had 400 ms to come to the front)
  hover, press, focus, type-in, and last `save_state`, so a saved state
  records everything above it. `delay_ms` counts from the last pose.
- Any pose that can't be applied rejects the whole request. A log line
  gives the reason and the request file is deleted, so a bad request
  never waits forever.
- Pointer, focus, type-in, editor size and the plugin's poses are undone
  after the capture. Parameter, preset and state poses are not: they are
  the point of the request. Restore them with `load_state`.
- `host: "standalone"` or `"daw"` pins a request to one kind of instance,
  read from the processor's `wrapperType`. Scripts should always pass
  `"standalone"`: a DAW session may hold a stale build.

The full request format, with examples, is in the README.

## Known limitations

- On macOS the plugin's app must be the active application for pointer
  and focus poses. The window server drops mouse events aimed at a
  covered window and only the key window takes focus; floating the window
  is not enough. A script run from a shell captures the idle UI.
- A popup menu is its own desktop window. Full-window and region PNGs
  composite it in; a single-component capture does not. With a resized
  editor, the popup is composited at the default zoom's size.
- A posed popup menu closes within a few hundred ms unless the app is
  active, so such requests want a small `delay_ms`.
- `editor_size` is invisible in a capture of the UI component when the
  editor zooms it with a transform; capture `plugin_editor`.
- Two switches: the module's classes compile under
  `URSA_SVG_SCREENSHOT_ENABLED` (on with `JUCE_DEBUG`), and the plugin's
  own wiring under its `URSA_SCREENSHOT_CAPTURE`, set in Debug by CMake.
  Both are on in Debug; switching only the second on in another
  configuration fails to compile.

## Adding it to a plugin

1. Link the module and, for Debug, define `URSA_SCREENSHOT_CAPTURE=1`. Add
   the `Standalone` format, so captures don't need a DAW.
2. Implement an `UrsaCaptureHost` for the plugin (an empty one is valid).
3. Build an `UrsaCaptureManager` in the editor's constructor, under
   `URSA_SCREENSHOT_CAPTURE`, after the UI exists. Gitignore the
   screenshots folder.
4. Give controls stable `componentID`s; they become the capture API.

## How to check it works

Capture a batch of views with `"host": "standalone"` and confirm one PNG
per request. After any change to the rig itself, capture the same
numbered batch before and after and compare checksums (`md5`): views
without animation should match byte for byte. Byte-identical PNGs across
requests that should differ mean a stale instance serviced them.

## Users

- **UrsaVerb**: `UvrbCaptureHost` (`plugin/src/ui/uvrb_capture_host.h`)
  supplies its views, Key grid order, EQ menu, presets and value-readout
  type-in. Its `CLAUDE.md` lists its views and component ids.

## In progress

- [ ] Wire into the plugin template (`urpdefault`), so new plugins start
      with the rig.
