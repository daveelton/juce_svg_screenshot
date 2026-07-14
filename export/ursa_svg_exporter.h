// ============================================================================
// Part of the ursa_svg_screenshot JUCE module — MIT licensed, see LICENSE
// at the module root. Copyright (c) 2026 UrsaDSP / Perambulum Ltd.
// ============================================================================

#pragma once

#include <map>
#include <typeinfo>
#if defined(__GNUG__)
    #include <cxxabi.h>
#endif

/**
 * Wraps every component's painting in a named SVG group for the duration
 * of an export, so the document mirrors the component tree. Works through
 * the CachedComponentImage hook: JUCE consults it instead of painting a
 * child's subtree, and our hook brackets the normal paint with the
 * renderer's pushGroup/popGroup (a ScopedSaveState around the paint keeps
 * the renderer's clip-group pointer balanced for the pop). Group names
 * come from the componentID where set, else the class name; repeats get a
 * numeric suffix (SVG ids must be unique).
 *
 * Installing the hook also displaces any buffered-to-image cache, which is
 * exactly what an export wants — cached components render their true
 * vector drawing instead of a raster (the PDF exporter, which has no
 * grouping, uses UrsaUnbufferedScope for the same effect). Destruction
 * restores buffering where it was on. Message thread only.
 */
class UrsaSvgGroupScope
{
public:
    explicit UrsaSvgGroupScope(juce::Component& root)
    {
        // the root itself needs no group — it IS the document
        for (auto* child : root.getChildren())
            install(*child);
    }

    ~UrsaSvgGroupScope()
    {
        for (auto& entry : installed)
        {
            // painting can churn components (list rows are created and
            // deleted during layout/paint), so entries may have died
            // mid-export — SafePointer nulls instead of dangling
            if (entry.component == nullptr)
                continue;

            // clear our hook first: setBufferedToImage asserts if it would
            // silently delete a custom CachedComponentImage
            entry.component->setCachedComponentImage(nullptr);
            if (entry.wasBuffered)
                entry.component->setBufferedToImage(true);
        }
    }

private:
    class GroupedPaint : public juce::CachedComponentImage
    {
    public:
        GroupedPaint(juce::Component& owner_, const juce::String& groupId_)
            : owner(owner_), groupId(groupId_)
        {
        }

        void paint(juce::Graphics& g) override
        {
            // only the SVG renderer understands groups; any other context
            // (screen repaints while the scope is installed, the PDF path)
            // just gets the plain unbuffered paint
            auto* svg = dynamic_cast<LowLevelGraphicsSVGRenderer*>(&g.getInternalContext());

            if (svg != nullptr)
                svg->pushGroup(groupId);
            {
                const juce::Graphics::ScopedSaveState ss(g);
                owner.paintEntireComponent(g, false);
            }
            if (svg != nullptr)
                svg->popGroup();
        }

        bool invalidateAll() override { return true; }
        bool invalidate(const juce::Rectangle<int>&) override { return true; }
        void releaseResources() override {}

    private:
        juce::Component& owner;
        const juce::String groupId;

        JUCE_DECLARE_NON_COPYABLE(GroupedPaint)
    };

    void install(juce::Component& comp)
    {
        installed.push_back({ &comp, comp.getCachedComponentImage() != nullptr });
        comp.setCachedComponentImage(new GroupedPaint(comp, uniqueGroupId(comp)));

        for (auto* child : comp.getChildren())
            install(*child);
    }

    juce::String uniqueGroupId(juce::Component& comp)
    {
        auto base = comp.getComponentID().isNotEmpty() ? comp.getComponentID()
                                                       : classNameFor(comp);
        base = base.retainCharacters(
            "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_-.");
        if (base.isEmpty())
            base = "component";

        const int count = ++nameCounts[base];
        return count == 1 ? base : base + "_" + juce::String(count);
    }

    static juce::String classNameFor(juce::Component& comp)
    {
#if defined(__GNUG__)
        int status = 0;
        char* demangled = abi::__cxa_demangle(typeid(comp).name(), nullptr, nullptr, &status);
        juce::String name(status == 0 && demangled != nullptr ? demangled : typeid(comp).name());
        std::free(demangled);
#else
        juce::String name(typeid(comp).name());
#endif

        // namespaces and template arguments don't help a designer
        if (name.contains("<"))
            name = name.upToFirstOccurrenceOf("<", false, false);
        if (name.contains("::"))
            name = name.fromLastOccurrenceOf("::", false, false);
        return name.trim();
    }

    struct Entry
    {
        juce::Component::SafePointer<juce::Component> component;
        bool wasBuffered;
    };

    std::vector<Entry> installed;
    std::map<juce::String, int> nameCounts;

    JUCE_DECLARE_NON_COPYABLE(UrsaSvgGroupScope)
};

/**
 * Debug-only vector export of a component tree to SVG, for designer
 * handoff — the primary vector format: unlike the PDF route, SVG carries
 * per-stop gradient alpha (stop-opacity), which this UI's styling leans
 * on (background in the module README). Paints through the vendored
 * LowLevelGraphicsSVGRenderer (vector/) with per-component named groups
 * (UrsaSvgGroupScope above), then collapses the paint recursion's
 * single-child wrapper chains so the layer tree a designer sees is the
 * component tree, not the clip plumbing. Glyphs arrive as outline paths.
 * Message thread only.
 */
class UrsaSvgExporter
{
public:
    static bool exportComponent(juce::Component& comp, const juce::File& outFile)
    {
        const auto w = comp.getWidth();
        const auto h = comp.getHeight();
        if (w <= 0 || h <= 0)
            return false;

        juce::XmlElement svg("svg");
        {
            LowLevelGraphicsSVGRenderer renderer(&svg, w, h);
            juce::Graphics g(renderer);

            const UrsaSvgGroupScope groups(comp);
            comp.paintEntireComponent(g, false);
        }

        tidyGroups(svg);

        outFile.deleteFile();
        return svg.writeTo(outFile);
    }

private:
    // prune empty <g>s and collapse single-child <g> chains, merging a
    // sole-child group into its parent. Merge only when at most ONE side
    // carries non-id attributes: combining, say, a parent's clip-path with
    // a child's transform onto one element would re-order their SVG
    // semantics (transform re-bases the clip's user space). ids merge as
    // "parent.child" so no component name is lost.
    static void tidyGroups(juce::XmlElement& e)
    {
        for (int i = e.getNumChildElements(); --i >= 0;)
        {
            auto* child = e.getChildElement(i);
            tidyGroups(*child);
            if (child->hasTagName("g") && child->getNumChildElements() == 0)
                e.removeChildElement(child, true);
        }

        if (!e.hasTagName("g"))
            return;

        while (e.getNumChildElements() == 1)
        {
            auto* only = e.getFirstChildElement();
            if (!only->hasTagName("g") || !canMerge(e, *only))
                break;

            for (int i = 0; i < only->getNumAttributes(); ++i)
            {
                const auto& name = only->getAttributeName(i);
                const auto& value = only->getAttributeValue(i);
                if (name == "id" && e.hasAttribute("id"))
                    e.setAttribute("id", e.getStringAttribute("id") + "." + value);
                else
                    e.setAttribute(name, value);
            }

            while (auto* grandchild = only->getFirstChildElement())
            {
                only->removeChildElement(grandchild, false);
                e.addChildElement(grandchild);
            }
            e.removeChildElement(only, true);
        }
    }

    static bool canMerge(const juce::XmlElement& parent, const juce::XmlElement& child)
    {
        return countNonIdAttributes(parent) == 0 || countNonIdAttributes(child) == 0;
    }

    static int countNonIdAttributes(const juce::XmlElement& e)
    {
        int count = 0;
        for (int i = 0; i < e.getNumAttributes(); ++i)
            count += e.getAttributeName(i) != "id" ? 1 : 0;
        return count;
    }

    UrsaSvgExporter() = delete;
};
