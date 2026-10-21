// ============================================================================
// Part of the ursa_svg_screenshot JUCE module — MIT licensed, see LICENSE
// at the module root. Copyright (c) 2026 UrsaDSP / Perambulum Ltd.
// ============================================================================

#pragma once

/**
 * One parsed <name>.request.json (format: README.md), plus the progress
 * UrsaCaptureManager keeps while servicing it. The JSON object is kept so the
 * plugin's UrsaCaptureHost can read fields only it knows.
 */
class UrsaCaptureRequest
{
public:
    UrsaCaptureRequest(const juce::File& file, const juce::var& json_)
        : requestFile(file), json(json_)
    {
        name = file.getFileName().upToFirstOccurrenceOf(".request.json", false, true);

        mode = json.getProperty("mode", "all").toString();
        delayMs = json.getProperty("delay_ms", 200);
        componentId = json.getProperty("component_id", "").toString();
        triggerId = json.getProperty("trigger_id", "").toString();
        view = json.getProperty("view", "").toString();
        scale = static_cast<float>(double(json.getProperty("scale", 1.0)));
        x = json.getProperty("x", 0);
        y = json.getProperty("y", 0);
        w = json.getProperty("w", 0);
        h = json.getProperty("h", 0);

        loadState = json.getProperty("load_state", juce::var());
        preset = json.getProperty("preset", juce::var());
        presetFile = json.getProperty("preset_file", juce::var());
        setParams = json.getProperty("set_params", juce::var());
        setParamsText = json.getProperty("set_params_text", juce::var());
        editorSize = json.getProperty("editor_size", "").toString();
        hoverId = json.getProperty("hover_id", "").toString();
        hoverX = json.getProperty("hover_x", -1);
        hoverY = json.getProperty("hover_y", -1);
        pressId = json.getProperty("press_id", "").toString();
        focusId = json.getProperty("focus_id", "").toString();
        editId = json.getProperty("edit_id", "").toString();
        saveState = json.getProperty("save_state", juce::var());
    }

    bool hasPose() const
    {
        return view.isNotEmpty() || loadState.isString() || preset.isString()
               || presetFile.isString() || setParams.isObject() || setParamsText.isObject()
               || editorSize.isNotEmpty() || hasPointerPose() || saveState.isString() || pluginPose;
    }

    // The pointer and focus poses need the window raised and the window
    // server caught up with it, so they run a beat after the rest.
    bool hasPointerPose() const
    {
        return hoverId.isNotEmpty() || pressId.isNotEmpty()
               || focusId.isNotEmpty() || editId.isNotEmpty();
    }

    juce::File requestFile;
    juce::var json;
    juce::String name; // <name>.request.json -> <name>

    juce::String mode;        // "all", "component", "region", "all_when_visible", "svg", "pdf" (macOS)
    juce::String componentId; // "component"/"svg"/"pdf" target; trigger for "all_when_visible"
    juce::String triggerId;   // svg/pdf only: arrival-gated FULL-UI export when this component shows
    juce::String view;
    float scale { 1.0f }; // raster modes only (2.0 = @2x)
    int x { 0 }, y { 0 }, w { 0 }, h { 0 }; // "region"
    int delayMs { 200 };

    // Shared poses, applied in this order (docs/CaptureRig.md); the host's
    // own fields go after editorSize.
    juce::var loadState;
    juce::var preset;
    juce::var presetFile;
    juce::var setParams;     // {paramID: normalised 0-1}
    juce::var setParamsText; // {paramID: "2.5 s"}
    juce::String editorSize; // min | max | default | <w>x<h>
    bool pluginPose { false }; // set by the manager from UrsaCaptureHost::hasPluginPose
    juce::String hoverId;
    int hoverX { -1 }, hoverY { -1 };
    juce::String pressId;
    juce::String focusId;
    juce::String editId;
    juce::var saveState; // applied last, so it records everything above

    // Progress while the manager services the request
    int64_t visibleSince { 0 }; // when the target became visible
    bool seenHidden { false };  // arrival-gated modes arm only after the trigger was seen hidden
    bool posed { false };       // the poses happened, the delay is counting
    int poseStage { 0 };        // 0: nothing applied; 1: waiting for the raised window, then the pointer
    int64_t poseReadyAt { 0 };  // when stage 1 may run
    bool editPosed { false };   // a type-in was opened, so clearing focus discards it
};
