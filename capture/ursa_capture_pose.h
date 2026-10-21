// ============================================================================
// Part of the ursa_svg_screenshot JUCE module — MIT licensed, see LICENSE
// at the module root. Copyright (c) 2026 UrsaDSP / Perambulum Ltd.
// ============================================================================

#pragma once

/**
 * The capture rig's processor-side poses (docs/CaptureRig.md): a parameter
 * arriving the way host automation arrives, the plugin's saved state, and the
 * editor's size.
 */
class UrsaCapturePose
{
public:
    UrsaCapturePose(juce::AudioProcessorEditor& editor_, UrsaCaptureHost& host_, juce::File stateDir_)
        : editor(editor_), host(host_), stateDir(std::move(stateDir_))
    {
    }

    //==========================================================================
    // Parameters — through the host-facing parameter, not a control

    /** Set a parameter by its host id from a normalised 0-1 value, the way an
        automation lane does: the UI is seen reacting to a change it did not
        originate. Returns false for an unknown id. */
    bool setParamNormalised(const juce::String& paramId, float normalised)
    {
        auto* param = findHostParam(paramId);
        if (param == nullptr)
            return false;

        param->setValueNotifyingHost(juce::jlimit(0.0f, 1.0f, normalised));
        return true;
    }

    /** As setParamNormalised, but from the parameter's own text ("2.5 s",
        "On", "Plate") so a script needn't know the normalisation. */
    bool setParamFromText(const juce::String& paramId, const juce::String& text)
    {
        auto* param = findHostParam(paramId);
        if (param == nullptr)
            return false;

        param->setValueNotifyingHost(juce::jlimit(0.0f, 1.0f, param->getValueForText(text)));
        return true;
    }

    //==========================================================================
    // Saved state — getStateInformation / setStateInformation without a DAW

    bool saveState(const juce::String& name)
    {
        juce::MemoryBlock block;
        editor.getAudioProcessor()->getStateInformation(block);
        return stateFile(name).replaceWithData(block.getData(), block.getSize());
    }

    bool loadState(const juce::String& name)
    {
        juce::MemoryBlock block;
        if (!stateFile(name).loadFileAsData(block))
            return false;

        editor.getAudioProcessor()->setStateInformation(block.getData(), static_cast<int>(block.getSize()));
        return true;
    }

    //==========================================================================
    // Editor size — "min", "max", "default" or "<w>x<h>", clamped to the constrainer

    /** Resize the editor, remembering the size it had so restoreEditorSize can
        put it back: a standalone persists its window size between launches,
        so a capture script has to be able to leave it as it found it. */
    bool setEditorSize(const juce::String& spec)
    {
        auto* constrainer = editor.getConstrainer();
        if (constrainer == nullptr)
            return false;

        juce::Point<int> target;

        if (spec == "min")
            target = { constrainer->getMinimumWidth(), constrainer->getMinimumHeight() };
        else if (spec == "max")
            target = { constrainer->getMaximumWidth(), constrainer->getMaximumHeight() };
        else if (spec == "default")
        {
            const auto size = host.defaultEditorSize();
            if (!size.has_value())
                return false;
            target = *size;
        }
        else
        {
            const auto w = spec.upToFirstOccurrenceOf("x", false, true).getIntValue();
            const auto h = spec.fromLastOccurrenceOf("x", false, true).getIntValue();
            if (w <= 0 || h <= 0)
                return false;
            target = { juce::jlimit(constrainer->getMinimumWidth(), constrainer->getMaximumWidth(), w),
                       juce::jlimit(constrainer->getMinimumHeight(), constrainer->getMaximumHeight(), h) };
        }

        if (!sizeBeforePose.has_value())
            sizeBeforePose = juce::Point<int> { editor.getWidth(), editor.getHeight() };

        editor.setSize(target.x, target.y);
        return true;
    }

    /** Put the editor back to the size it had before the pose. */
    void restoreEditorSize()
    {
        if (!sizeBeforePose.has_value())
            return;

        editor.setSize(sizeBeforePose->x, sizeBeforePose->y);
        sizeBeforePose.reset();
    }

private:
    juce::AudioProcessorParameterWithID* findHostParam(const juce::String& paramId) const
    {
        for (auto* p : editor.getAudioProcessor()->getParameters())
            if (auto* withId = dynamic_cast<juce::AudioProcessorParameterWithID*>(p))
                if (withId->paramID == paramId)
                    return withId;

        return nullptr;
    }

    juce::File stateFile(const juce::String& name) const
    {
        return stateDir.getChildFile(name + ".state");
    }

    juce::AudioProcessorEditor& editor;
    UrsaCaptureHost& host;
    juce::File stateDir;
    std::optional<juce::Point<int>> sizeBeforePose;

    JUCE_DECLARE_NON_COPYABLE(UrsaCapturePose)
};
