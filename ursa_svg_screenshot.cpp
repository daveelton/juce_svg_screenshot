// ============================================================================
// Part of the ursa_svg_screenshot JUCE module — MIT licensed, see LICENSE
// at the module root. Copyright (c) 2026 UrsaDSP / Perambulum Ltd.
// ============================================================================

#include "ursa_svg_screenshot.h"

#if URSA_SVG_SCREENSHOT_ENABLED

    #include "context/LowLevelGraphicsSVGRenderer.cpp"

    #if JUCE_MAC

        // JUCE's production macOS renderer, wrapped around a CGPDFContext:
        // everything the plugin draws on screen comes out as vectors with
        // zero re-implementation drift. Internal JUCE headers — kept out
        // of the module header so they never leak to consumers; the
        // implementation is compiled inside the statically-linked
        // juce_graphics module, so only declarations are needed here.
        #include <CoreGraphics/CoreGraphics.h>

        #include <juce_core/native/juce_CFHelpers_mac.h>
        #include <juce_graphics/native/juce_CoreGraphicsContext_mac.h>

bool UrsaPdfExporter::exportComponent(juce::Component& comp, const juce::File& outFile)
{
    const auto w = comp.getWidth();
    const auto h = comp.getHeight();
    if (w <= 0 || h <= 0)
        return false;

    outFile.deleteFile();
    const juce::CFUniquePtr<CFStringRef> path(outFile.getFullPathName().toCFString());
    const juce::CFUniquePtr<CFURLRef> url(CFURLCreateWithFileSystemPath(kCFAllocatorDefault, path.get(), kCFURLPOSIXPathStyle, false));
    const juce::CFUniquePtr<CGDataConsumerRef> consumer(CGDataConsumerCreateWithURL(url.get()));
    if (consumer == nullptr)
        return false;

    // logical px map 1:1 to PDF points
    auto mediaBox = CGRectMake(0, 0, CGFloat(w), CGFloat(h));
    const juce::CFUniquePtr<CGContextRef> pdfContext(CGPDFContextCreate(consumer.get(), &mediaBox, nullptr));
    if (pdfContext == nullptr)
        return false;

    CGPDFContextBeginPage(pdfContext.get(), nullptr);
    {
        juce::CoreGraphicsContext context(pdfContext.get(), float(h));
        juce::Graphics g(context);

        const UrsaUnbufferedScope unbuffered(comp);
        comp.paintEntireComponent(g, false);
    }
    CGPDFContextEndPage(pdfContext.get());
    CGPDFContextClose(pdfContext.get());

    return outFile.existsAsFile();
}

    #endif // JUCE_MAC

#endif // URSA_SVG_SCREENSHOT_ENABLED
