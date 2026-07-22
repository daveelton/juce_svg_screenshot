// ============================================================================
// Part of the ursa_svg_screenshot JUCE module — MIT licensed, see LICENSE
// at the module root. Copyright (c) 2026 UrsaDSP / Perambulum Ltd.
// ============================================================================

#pragma once

#include <functional>

/**
 * Drop-in hotkeys for the vector exporters: add one of these to an editor
 * (or any component that receives key events) and press F10 for an SVG of
 * the target (F11 for a PDF on macOS). Files land in the output directory
 * as manual_<timestamp>.svg/.pdf. The whole integration:
 *
 *   exportKeys = std::make_unique<UrsaVectorExportKeys>(mainUi, outputDir);
 *   editor.addKeyListener(exportKeys.get());   // remove in the destructor
 *
 * The optional allowExport predicate gates the hotkeys at runtime (e.g.
 * behind a "debug features" user setting when the module is compiled into
 * production builds); when empty the keys are always active.
 */
class UrsaVectorExportKeys : public juce::KeyListener
{
public:
    UrsaVectorExportKeys(juce::Component& target_, const juce::File& outputDir_,
                         std::function<bool()> allowExport_ = {})
        : target(target_), outputDir(outputDir_), allowExport(std::move(allowExport_))
    {
    }

    bool keyPressed(const juce::KeyPress& key, juce::Component* /*originatingComponent*/) override
    {
        if (allowExport != nullptr && ! allowExport())
            return false;

        if (key == juce::KeyPress::F10Key)
        {
            UrsaSvgExporter::exportComponent(target, timestampedFile("svg"));
            return true;
        }

#if JUCE_MAC
        if (key == juce::KeyPress::F11Key)
        {
            UrsaPdfExporter::exportComponent(target, timestampedFile("pdf"));
            return true;
        }
#endif

        return false;
    }

private:
    juce::File timestampedFile(const juce::String& extension) const
    {
        outputDir.createDirectory();
        auto timestamp = juce::Time::getCurrentTime().formatted("%Y%m%d_%H%M%S");
        return outputDir.getChildFile("manual_" + timestamp + "." + extension);
    }

    juce::Component& target;
    juce::File outputDir;
    std::function<bool()> allowExport;

    JUCE_DECLARE_NON_COPYABLE(UrsaVectorExportKeys)
};
