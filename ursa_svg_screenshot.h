#pragma once

/*
 BEGIN_JUCE_MODULE_DECLARATION

  ID:               ursa_svg_screenshot
  vendor:           UrsaDSP
  version:          1.0.0
  name:             UrsaDSP UI capture and vector export
  description:      Debug tooling: scripted UI captures (PNG/SVG/PDF from request files) and export of a JUCE component tree as structured SVG (or macOS PDF) for design handoff. Includes Nicole Alassandro's MIT-licensed SVG renderer, updated for JUCE 8.
  website:          https://ursadsp.com
  license:          MIT
  minimumCppStandard: 17
  OSXFrameworks:    CoreGraphics
  iOSFrameworks:
  dependencies:     juce_gui_basics juce_audio_processors

 END_JUCE_MODULE_DECLARATION
 */

// MIT licensed — see LICENSE at the module root. Includes significant
// parts of Nicole Alassandro's MIT-licensed SVG renderer (context/),
// updated for JUCE 8; export machinery (export/) by UrsaDSP.
//
// Usage: UrsaSvgExporter::exportComponent(comp, file) for structured SVG
// (per-component named groups), UrsaPdfExporter for macOS PDF (geometry-
// true, but PDF shadings drop per-stop gradient alpha — prefer the SVG),
// or add an UrsaVectorExportKeys to your editor for F10/F11 hotkeys.
// The scripted capture rig (UrsaCaptureManager + a plugin's UrsaCaptureHost)
// is described in docs/CaptureRig.md.

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>

// enabled in debug builds by default; define to 1/0 to override
#ifndef URSA_SVG_SCREENSHOT_ENABLED
    #define URSA_SVG_SCREENSHOT_ENABLED JUCE_DEBUG
#endif

#if URSA_SVG_SCREENSHOT_ENABLED

    #include "context/LowLevelGraphicsSVGRenderer.h"
    #include "export/ursa_unbuffered_scope.h"
    #include "export/ursa_svg_exporter.h"
    #include "export/ursa_pdf_exporter.h"
    #include "export/ursa_vector_export_keys.h"

    #include "capture/ursa_pointer_pose.h"
    #include "capture/ursa_capture_host.h"
    #include "capture/ursa_capture_request.h"
    #include "capture/ursa_capture_pose.h"
    #include "capture/ursa_capture_manager.h"

#endif
