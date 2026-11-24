---
name: design-svgs
description: Export a JUCE plugin's UI as SVGs ready to edit in a graphics package (Affinity, Illustrator, Figma) - every view captured through the ursa_svg_screenshot capture rig with text as editable text, then tidied into a named layer tree (tools/tidy_svg.py) and checked against the raw export. Use when the user invokes /design-svgs, or asks for the UI as editable SVGs, design SVGs, or SVGs to mark up with suggested changes. Optional args name the views to export (default all).
---

# Design SVGs

Produces one SVG per view of the plugin's UI, laid out for a designer: editable text, layers named
after the plugin's controls, no clip plumbing. The user typically marks them up in Affinity as
suggestions for later code changes, so what matters is that each file opens looking like the
plugin and that its layers are easy to find and change.

The pieces, all in this module (`ursa_svg_screenshot`): the capture rig takes `svg` requests with
`"svg_text": true` (README, request format); `tools/tidy_svg.py` rewrites the layer tree;
`tools/check_tidy.py` confirms the tidy changed nothing visible. Read the module README's "What you
get" before the first run in a session.

## 1. Find the plugin's side

From the plugin repo's CLAUDE.md (and the docs it points to), find:

- **Views**: the request `view` values the plugin supports (its tabs, overlays and popovers). An
  args list from the user narrows them.
- **How to launch the standalone**: a `.claude/runs.json` or `temp/runs.json` run for it means
  `devrun start <run>` (builds first). Otherwise the build and launch commands in CLAUDE.md.
- **Screenshots folder**: where request files go, usually `plugin/screenshots/`.
- **Sections file**: a `svg_tidy_sections.json` anywhere in the plugin repo outside `lib/`
  (search by name). It groups the top level into named sections. None means no sections, which is
  fine.

If CLAUDE.md lists no views, export the whole UI once with no `view` and say so.

## 2. Build and launch

Build and start the standalone. A capture is serviced by whichever plugin instance polls first, so
every request carries `"host": "standalone"`. If the build fails, stop and report it.

## 3. Export

Write one request per view into the screenshots folder, named so the files sort in tab order
(`svg_01_<view>`, ...):

```json
{"mode": "svg", "view": "<view>", "delay_ms": 800, "svg_text": true, "host": "standalone"}
```

Use about 800 ms on views with graphs, so asynchronous drawing settles. Each request file is
deleted when its SVG is written; wait for all of them (a minute or two). A request whose view
the plugin rejects is also deleted, with the reason in the plugin log, and writes no SVG.
Report any missing view with that reason; don't retry blindly.

Overlays a view opens can stay open for the next capture (a preset browser, say). Capture those
views last, in a second batch after the others have finished, and check later captures for one
left behind.

Move the SVGs to `temp/design-svgs/<YYYY-MM-DD>/raw/` in the plugin repo (`temp/` is gitignored).

## 4. Tidy

```bash
python3 lib/ursa_svg_screenshot/tools/tidy_svg.py \
    [--sections <path>/svg_tidy_sections.json] \
    --out temp/design-svgs/<date>/tidy temp/design-svgs/<date>/raw/*.svg
```

Without Pillow, drop shadows stay as SVG masks and the script says so; graphics packages draw those
inconsistently, so mention it.

## 5. Check

```bash
python3 lib/ursa_svg_screenshot/tools/check_tidy.py \
    temp/design-svgs/<date>/raw temp/design-svgs/<date>/tidy --keep temp/design-svgs/<date>/render
```

A few pixels differing by under about 10 levels is anti-aliasing. Hundreds of scattered pixels in a
dense graph come from merged rectangles and are invisible. Anything else, such as a region
differing or a large maximum, means the tidy changed the drawing: look at the two renders of that
file before reporting, and report it rather than hand over a file that looks wrong.

Look at one tidied render yourself (the most complex view) to confirm it looks like the plugin.

## 6. Report

Open with the folder of tidied SVGs (full path) and which views are in it. Then only what the user
needs to act on:

- **Fonts**: text uses the plugin's fonts by family name; they must be installed wherever the
  files are opened, or the app substitutes. Name the families (from the `font-family` values).
- **Views missing**, with the plugin's reason.
- **Check results**, only where something went beyond anti-aliasing.

Don't describe the layer structure unless asked; the module README covers it.
