// ============================================================================
// Part of the ursa_svg_screenshot JUCE module — MIT licensed, see LICENSE
// at the module root. Copyright (c) 2026 UrsaDSP / Perambulum Ltd.
// ============================================================================

#pragma once

#if JUCE_MAC

/**
 * Debug-only vector export of a component tree to PDF: paints through
 * JUCE's own CoreGraphics renderer wrapped in a CGPDFContext, so geometry
 * and text are production-faithful vectors. Known limitation (why the SVG
 * exporter is the primary format): PDF shading dictionaries carry no
 * per-stop alpha and Quartz silently drops it, so gradients with
 * translucent stops render opaque. Implementation lives in the module cpp
 * — it needs JUCE-internal headers that shouldn't leak to consumers.
 * Message thread only.
 */
class UrsaPdfExporter
{
public:
    static bool exportComponent(juce::Component& comp, const juce::File& outFile);

private:
    UrsaPdfExporter() = delete;
};

#endif // JUCE_MAC
