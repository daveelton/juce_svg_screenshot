// ============================================================================
// Part of the ursa_svg_screenshot JUCE module — MIT licensed, see LICENSE
// at the module root. Copyright (c) 2026 UrsaDSP / Perambulum Ltd.
// ============================================================================

#pragma once

/**
 * What a plugin supplies to the capture rig (docs/CaptureRig.md): the poses
 * that depend on its own UI. Every method has a default that declines, and a
 * request using a declined pose is rejected with a log line — so an empty
 * subclass is a valid start.
 */
class UrsaCaptureHost
{
public:
    virtual ~UrsaCaptureHost() = default;

    /** The request's "view": switch to a named tab, overlay or popover. */
    virtual bool poseView(const juce::String& /*view*/) { return false; }

    /** "preset": load by the name the preset browser lists it under. */
    virtual bool loadPreset(const juce::String& /*displayName*/) { return false; }

    /** "preset_file": the path is already resolved against the project root. */
    virtual bool loadPresetFile(const juce::File& /*file*/) { return false; }

    /** "edit_id": open the target's text entry, the way a user does. The
        pointer pose is passed in so the host can click. */
    virtual bool poseEditFocus(juce::Component& /*target*/, UrsaPointerPose& /*pointer*/) { return false; }

    /** What "editor_size": "default" means; none rejects it. */
    virtual std::optional<juce::Point<int>> defaultEditorSize() const { return {}; }

    /** Request fields only this plugin understands. hasPluginPose tells the
        manager the request is posed (so it is sequenced with the others);
        applyPluginPoses runs after the editor size and before the pointer
        poses, and sets error on failure; undoPluginPoses runs after the
        capture. */
    virtual bool hasPluginPose(const juce::var& /*request*/) const { return false; }
    virtual bool applyPluginPoses(const juce::var& /*request*/, juce::String& /*error*/) { return true; }
    virtual void undoPluginPoses(const juce::var& /*request*/) {}
};
