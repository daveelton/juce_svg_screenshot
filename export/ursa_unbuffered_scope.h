// ============================================================================
// Part of the ursa_svg_screenshot JUCE module — MIT licensed, see LICENSE
// at the module root. Copyright (c) 2026 UrsaDSP / Perambulum Ltd.
// ============================================================================

#pragma once

/**
 * RAII scope for the vector exporters: temporarily un-buffers every
 * setBufferedToImage component in the tree so cached components render
 * their true vector drawing instead of a raster, restoring on
 * destruction. Message thread only.
 */
class UrsaUnbufferedScope
{
public:
    explicit UrsaUnbufferedScope(juce::Component& root)
    {
        collectBuffered(root);
        for (auto& c : buffered)
            c->setBufferedToImage(false);
    }

    ~UrsaUnbufferedScope()
    {
        // SafePointer: painting can churn components (list rows are
        // created and deleted during layout/paint)
        for (auto& c : buffered)
            if (c != nullptr)
                c->setBufferedToImage(true);
    }

private:
    void collectBuffered(juce::Component& comp)
    {
        if (comp.getCachedComponentImage() != nullptr)
            buffered.push_back(&comp);
        for (auto* child : comp.getChildren())
            collectBuffered(*child);
    }

    std::vector<juce::Component::SafePointer<juce::Component>> buffered;

    JUCE_DECLARE_NON_COPYABLE(UrsaUnbufferedScope)
};
