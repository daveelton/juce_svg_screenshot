/*
    Copyright 2018 Nicole Alassandro

    Permission is hereby granted, free of charge, to any person obtaining a copy
    of this software and associated documentation files (the "Software"), to
    deal in the Software without restriction, including without limitation the
    rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
    sell copies of the Software, and to permit persons to whom the Software is
    furnished to do so, subject to the following conditions:

    The above copyright notice and this permission notice shall be included in
    all copies or substantial portions of the Software.

    THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
    IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
    FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
    AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
    LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
    FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
    IN THE SOFTWARE.
*/

namespace
{
    // Font::getStringWidth is deprecated in JUCE 8; keep one guard here
    // instead of at every measurement site.
    inline int svgStringWidth (const juce::Font& f, const juce::String& s)
    {
       #if JUCE_MAJOR_VERSION >= 8
        return juce::GlyphArrangement::getStringWidthInt (f, s);
       #else
        return f.getStringWidth (s);
       #endif
    }
} // namespace

LowLevelGraphicsSVGRenderer::LowLevelGraphicsSVGRenderer(
    juce::XmlElement* svgDocument,
    int totalWidth,
    int totalHeight)
{
    stateStack.add(new SavedState());
    state = stateStack.getLast();

    state->clipRegions.add(juce::Rectangle<float>((float) totalWidth, (float) totalHeight));
    state->clipPath = state->clipRegions.toPath();
    state->clipGroup = nullptr;

    resampleQuality = juce::Graphics::mediumResamplingQuality;

    document = svgDocument;

    // XmlElements that don't have the proper name or that already have children
    // will yield unusable or undefined results
    jassert(document->getTagName().toLowerCase() == "svg");
    jassert(document->getNumChildElements() == 0);

    document->setAttribute("xmlns", "http://www.w3.org/2000/svg");
    document->setAttribute("xmlns:xlink", "http://www.w3.org/1999/xlink");

    document->setAttribute("width", totalWidth);
    document->setAttribute("height", totalHeight);

    document->createNewChildElement("defs");
}

#pragma mark -
// =============================================================================

bool LowLevelGraphicsSVGRenderer::isVectorDevice() const
{
    return true;
}

#if JUCE_MAJOR_VERSION >= 8
float LowLevelGraphicsSVGRenderer::getPhysicalPixelScaleFactor() const // JUCE 8 port: const
#else
float LowLevelGraphicsSVGRenderer::getPhysicalPixelScaleFactor()
#endif
{
    return 1.0f;
}

#pragma mark -
// =============================================================================

// fix (CTM overhaul): the original kept origin offsets separate from the
// transform and never applied the accumulated transform to emitted geometry
// (only to clips) — so anything painted after Graphics::addTransform (moving
// value bubbles, Drawable::drawWithin logos, ...) landed at untransformed
// coordinates while its clip was correct, and browsers clipped it to nothing.
// Now a single CTM (state->transform, origin folded in) maps every draw call
// to device space, geometry is emitted in device coordinates, and the clip —
// stored in device space — is left alone by later transform changes.

void LowLevelGraphicsSVGRenderer::setOrigin(juce::Point<int> p)
{
    // moves the origin within the CURRENT user space: a leading translation
    state->transform = juce::AffineTransform::translation((float) p.x, (float) p.y)
                           .followedBy(state->transform);
}

void LowLevelGraphicsSVGRenderer::addTransform(const juce::AffineTransform& t)
{
    // t maps the new user space into the current one, so it composes ahead
    // of the existing CTM (the original composed it behind, backwards)
    state->transform = t.followedBy(state->transform);
}

#pragma mark -
// =============================================================================

bool LowLevelGraphicsSVGRenderer::clipToRectangle(const juce::Rectangle<int>& r)
{
    // transformedBy is the bounding box of the transformed corners — exact
    // for the translate/scale CTMs component painting produces; conservative
    // (over-large, never over-clipping... the reverse) under rotation/shear
    state->clipRegions.clipTo(r.toFloat().transformedBy(state->transform));

    setClip(state->clipRegions.toPath());

    return !isClipEmpty();
}

bool LowLevelGraphicsSVGRenderer::clipToRectangleList(
    const juce::RectangleList<int>& r)
{
    juce::RectangleList<float> deviceRects;

    for (auto& rect : r)
        deviceRects.add(rect.toFloat().transformedBy(state->transform));

    state->clipRegions.clipTo(deviceRects);

    setClip(state->clipRegions.toPath());

    return !isClipEmpty();
}

void LowLevelGraphicsSVGRenderer::excludeClipRectangle(
    const juce::Rectangle<int>& r)
{
    state->clipRegions.subtract(r.toFloat().transformedBy(state->transform));

    setClip(state->clipRegions.toPath());
}

void LowLevelGraphicsSVGRenderer::clipToPath(
    const juce::Path& p,
    const juce::AffineTransform& t)
{
    auto temp = p;
    temp.applyTransform(deviceTransform(t));

    // keep the rect-list tracker roughly in sync so later rectangle clips
    // still intersect with this (upstream REPLACE semantics otherwise kept)
    state->clipRegions.clear();
    state->clipRegions.add(temp.getBounds());

    setClip(temp);
}

void LowLevelGraphicsSVGRenderer::clipToImageAlpha(
    const juce::Image& i,
    const juce::AffineTransform& t)
{
    juce::Image maskImage(i);

    if (i.getFormat() != juce::Image::SingleChannel)
        maskImage = i.convertedToFormat(juce::Image::SingleChannel);

    auto defs = document->getChildByName("defs");
    auto maskRef = juce::String::formatted(
        "#Mask%d",
        defs->getNumChildElements());

    auto mask = defs->createNewChildElement("mask");
    mask->setAttribute("id", maskRef.replace("#", ""));

    auto image = mask->createNewChildElement("image");
    // fix (JUCE 8 port): same placement correction as drawImage
    image->setAttribute("x", 0);
    image->setAttribute("y", 0);
    image->setAttribute("width", i.getWidth());
    image->setAttribute("height", i.getHeight());

    image->setAttribute("image-rendering", writeImageQuality());

    const auto placed = deviceTransform(t);
    if (!placed.isIdentity())
        image->setAttribute("transform", writeTransform(placed));

    juce::MemoryOutputStream out;
    juce::PNGImageFormat png;
    png.writeImageToStream(i, out);

    auto base64Data = juce::Base64::toBase64(out.getData(), out.getDataSize());

    image->setAttribute("xlink:href", "data:image/png;base64," + base64Data);

    state->clipGroup = document->createNewChildElement("g");
    state->clipGroup->setAttribute("mask", "url(" + maskRef + ")");
}

bool LowLevelGraphicsSVGRenderer::clipRegionIntersects(
    const juce::Rectangle<int>& r)
{
    auto rect = r.toFloat().transformedBy(state->transform);
    return state->clipPath.getBounds().intersects(rect);
}

juce::Rectangle<int> LowLevelGraphicsSVGRenderer::getClipBounds() const
{
    // the clip lives in device space; callers expect user-space bounds
    return state->clipPath.getBounds()
        .transformedBy(state->transform.inverted())
        .toNearestInt();
}

bool LowLevelGraphicsSVGRenderer::isClipEmpty() const
{
    return state->clipPath.isEmpty();
}

#pragma mark -
// =============================================================================

void LowLevelGraphicsSVGRenderer::saveState()
{
    stateStack.add(new SavedState(*stateStack.getLast()));
    state = stateStack.getLast();
}

void LowLevelGraphicsSVGRenderer::restoreState()
{
    jassert(stateStack.size() > 0);
    stateStack.removeLast();
    state = stateStack.getLast();
}

#pragma mark -
// =============================================================================

void LowLevelGraphicsSVGRenderer::beginTransparencyLayer(float opacity)
{
    state->fillType.setOpacity(opacity);
}

void LowLevelGraphicsSVGRenderer::endTransparencyLayer()
{
    state->fillType.setOpacity(1.0f);
}

void LowLevelGraphicsSVGRenderer::setFill(const juce::FillType& fill)
{
    state->fillType = fill;

    if (fill.isGradient())
    {
        auto defs = document->getChildElement(0);
        jassert(defs);

        juce::String gradientType = (fill.gradient->isRadial)
                                        ? "radialGradient"
                                        : "linearGradient";

        auto e = defs->createNewChildElement(gradientType);

        state->gradientRef = juce::String::formatted(
            "#Gradient%d",
            defs->getNumChildElements());

        e->setAttribute("id", state->gradientRef.replace("#", ""));

        e->setAttribute("gradientUnits", "userSpaceOnUse");

        // geometry is emitted in device space, so the gradient must be too:
        // bake a pure-translation CTM into the points, otherwise emit the
        // CTM as gradientTransform
        auto point1 = fill.gradient->point1;
        auto point2 = fill.gradient->point2;

        if (state->transform.isOnlyTranslation())
        {
            point1 = point1.translated(state->transform.getTranslationX(),
                                       state->transform.getTranslationY());
            point2 = point2.translated(state->transform.getTranslationX(),
                                       state->transform.getTranslationY());
        }
        else if (!state->transform.isIdentity())
        {
            e->setAttribute(
                "gradientTransform",
                writeTransform(state->transform));
        }

        if (fill.gradient->isRadial)
        {
            // fix: JUCE's point2 is the rim point that defines the radius,
            // NOT an SVG focal point — writing it into fx/fy collapsed the
            // stop ramp toward the rim (crescent shadows, squashed glows)
            e->setAttribute("cx", truncateFloat(point1.x));
            e->setAttribute("cy", truncateFloat(point1.y));
            e->setAttribute("r", truncateFloat(point1.getDistanceFrom(point2)));
        }
        else
        {
            e->setAttribute("x1", truncateFloat(point1.x));
            e->setAttribute("y1", truncateFloat(point1.y));
            e->setAttribute("x2", truncateFloat(point2.x));
            e->setAttribute("y2", truncateFloat(point2.y));
        }

        auto prevRef = getPreviousGradientRef(fill.gradient.get()); // JUCE 8 port: FillType::gradient is a unique_ptr now

        if (prevRef.isNotEmpty())
        {
            e->setAttribute("xlink:href", prevRef);
        }
        else
        {
            for (int i = 0; i < fill.gradient->getNumColours(); ++i)
            {
                auto stop = e->createNewChildElement("stop");
                stop->setAttribute(
                    "offset",
                    truncateFloat((float) fill.gradient->getColourPosition(i)));

                stop->setAttribute(
                    "stop-color",
                    writeColour(fill.gradient->getColour(i)));

                stop->setAttribute(
                    "stop-opacity",
                    truncateFloat(fill.gradient->getColour(i).getFloatAlpha()));
            }
        }
    }
    else
    {
        state->gradientRef = "";
    }
}

void LowLevelGraphicsSVGRenderer::setOpacity(float opacity)
{
    state->fillType.setOpacity(opacity);
}

void LowLevelGraphicsSVGRenderer::setInterpolationQuality(
    juce::Graphics::ResamplingQuality quality)
{
    resampleQuality = quality;
}

#pragma mark -
// =============================================================================

void LowLevelGraphicsSVGRenderer::fillRect(
    const juce::Rectangle<int>& r,
    bool replaceExistingContents)
{
    // TODO: Utilize replaceExistingContents
    fillRect(r.toFloat());
}

void LowLevelGraphicsSVGRenderer::fillRect(const juce::Rectangle<float>& r)
{
    // under rotation/shear a rect element can't represent the result
    if (!ctmIsAxisAligned())
    {
        juce::Path p;
        p.addRectangle(r);
        fillPath(p, juce::AffineTransform());
        return;
    }

    const auto deviceRect = r.transformedBy(state->transform);

    if (shouldSkipDrawing(deviceRect))
        return;

    juce::XmlElement* rect;

    if (state->clipGroup)
        rect = state->clipGroup->createNewChildElement("rect");
    else
        rect = document->createNewChildElement("rect");

    rect->setAttribute("fill", writeFill());
    rect->setAttribute(
        "fill-opacity",
        truncateFloat(state->fillType.getOpacity()));

    rect->setAttribute("x", truncateFloat(deviceRect.getX()));
    rect->setAttribute("y", truncateFloat(deviceRect.getY()));
    rect->setAttribute("width", truncateFloat(deviceRect.getWidth()));
    rect->setAttribute("height", truncateFloat(deviceRect.getHeight()));

    applyTags(rect);
}

void LowLevelGraphicsSVGRenderer::fillRectList(
    const juce::RectangleList<float>& r)
{
    fillPath(r.toPath(), juce::AffineTransform());
}

void LowLevelGraphicsSVGRenderer::fillPath(
    const juce::Path& p,
    const juce::AffineTransform& t)
{
    auto temp = p;
    temp.applyTransform(deviceTransform(t));

    if (shouldSkipDrawing(temp.getBounds()))
        return;

    juce::XmlElement* path;

    if (state->clipGroup)
        path = state->clipGroup->createNewChildElement("path");
    else
        path = document->createNewChildElement("path");

    juce::String d = temp.toString().removeCharacters("a");
    path->setAttribute("d", d.toUpperCase());

    path->setAttribute("fill", writeFill());
    path->setAttribute(
        "fill-opacity",
        truncateFloat(state->fillType.getOpacity()));

    if (!p.isUsingNonZeroWinding())
        path->setAttribute("fill-rule", "evenodd");

    applyTags(path);
}

void LowLevelGraphicsSVGRenderer::drawImage(
    const juce::Image& i,
    const juce::AffineTransform& t)
{
    // t maps image pixel space into user space; deviceTransform then carries
    // it the rest of the way into document space
    const auto placed = deviceTransform(t);

    const auto deviceBounds = i.getBounds().toFloat().transformedBy(placed);

    if (state->fillType.getOpacity() <= 0.0f
        || !state->clipPath.getBounds().intersects(deviceBounds))
        return;

    juce::XmlElement* image;

    if (state->clipGroup)
        image = state->clipGroup->createNewChildElement("image");
    else
        image = document->createNewChildElement("image");

    image->setAttribute("x", 0);
    image->setAttribute("y", 0);
    image->setAttribute("width", i.getWidth());
    image->setAttribute("height", i.getHeight());

    image->setAttribute("image-rendering", writeImageQuality());

    if (!placed.isIdentity())
        image->setAttribute("transform", writeTransform(placed));

    juce::MemoryOutputStream out;
    juce::PNGImageFormat png;
    png.writeImageToStream(i, out);

    auto base64Data = juce::Base64::toBase64(out.getData(), out.getDataSize());
    image->setAttribute("xlink:href", "data:image/png;base64," + base64Data);

    applyTags(image);
}

void LowLevelGraphicsSVGRenderer::drawLine(const juce::Line<float>& l)
{
    // endpoints transform exactly under any affine map, so emit device-space
    // coordinates directly (the original added offsets AND wrote the
    // transform attribute — applying the translation twice)
    const auto start = l.getStart().transformedBy(state->transform);
    const auto end = l.getEnd().transformedBy(state->transform);

    if (shouldSkipDrawing(juce::Rectangle<float>(start, end).expanded(1.0f)))
        return;

    juce::XmlElement* line;

    if (state->clipGroup)
        line = state->clipGroup->createNewChildElement("line");
    else
        line = document->createNewChildElement("line");

    line->setAttribute("x1", truncateFloat(start.x));
    line->setAttribute("y1", truncateFloat(start.y));
    line->setAttribute("x2", truncateFloat(end.x));
    line->setAttribute("y2", truncateFloat(end.y));

    line->setAttribute("stroke", writeFill());
    line->setAttribute(
        "stroke-opacity",
        truncateFloat(state->fillType.getOpacity()));

    applyTags(line);
}

#pragma mark -
// =============================================================================

void LowLevelGraphicsSVGRenderer::setFont(const juce::Font& f)
{
    state->font = f;
}

const juce::Font& LowLevelGraphicsSVGRenderer::getFont()
{
    return state->font;
}

#if JUCE_MAJOR_VERSION >= 8
void LowLevelGraphicsSVGRenderer::drawGlyphs(
    juce::Span<const juce::uint16> glyphNumbers,
    juce::Span<const juce::Point<float>> positions,
    const juce::AffineTransform& t)
{
    // JUCE 8 port: replaces the removed per-glyph drawGlyph override.
    // getOutlineForGlyph still returns outlines normalised to height 1.0,
    // so the original scale-by-font transform applies per glyph, plus the
    // glyph's position ahead of the caller's transform.
    jassert(glyphNumbers.size() == positions.size());

    juce::Font& f = state->font;

    auto glyphScale = juce::AffineTransform::scale(
        f.getHeight() * f.getHorizontalScale(),
        f.getHeight());

    for (size_t i = 0; i < glyphNumbers.size(); ++i)
    {
        juce::Path p;
        f.getTypefacePtr()->getOutlineForGlyph(f.getMetricsKind(), glyphNumbers[i], p);

        p.applyTransform(
            glyphScale
                .translated(positions[i])
                .followedBy(t));

        fillPath(p, juce::AffineTransform());
    }
}
#else
void LowLevelGraphicsSVGRenderer::drawGlyph(int glyphNumber, const juce::AffineTransform& t)
{
    // pre-JUCE 8: the caller's transform already carries the glyph position;
    // outlines are normalised to height 1.0, so scale by the font first.
    juce::Font& f = state->font;

    juce::Path p;
    f.getTypefacePtr()->getOutlineForGlyph(glyphNumber, p);

    p.applyTransform(
        juce::AffineTransform::scale(
            f.getHeight() * f.getHorizontalScale(),
            f.getHeight())
            .followedBy(t));

    fillPath(p, juce::AffineTransform());
}
#endif

#pragma mark -
// =============================================================================

void LowLevelGraphicsSVGRenderer::drawSingleLineText(
    const juce::String& t,
    int startX,
    int baselineY,
    juce::Justification justification)
{
    juce::XmlElement* text;

    if (state->clipGroup)
        text = state->clipGroup->createNewChildElement("text");
    else
        text = document->createNewChildElement("text");

    auto f = state->font;
    auto tf = f.getTypefacePtr();

    text->setAttribute("x", startX);
    text->setAttribute("y", baselineY - f.getHeight());
    text->setAttribute("font-family", tf->getName());
    text->setAttribute("font-style", tf->getStyle());
    text->setAttribute("font-size", f.getHeight());
    text->setAttribute("fill", writeFill());

    if (justification.testFlags(justification.left))
        text->setAttribute("text-anchor", "start");

    else if (justification.testFlags(justification.horizontallyCentred))
        text->setAttribute("text-anchor", "middle");

    else if (justification.testFlags(justification.right))
        text->setAttribute("text-anchor", "end");

    else
        text->setAttribute("text-anchor", "inherited");

    if (!state->transform.isIdentity())
        text->setAttribute("transform", writeTransform(state->transform));

    text->addTextElement(t);

    applyTags(text);
}

void LowLevelGraphicsSVGRenderer::drawMultiLineText(
    const juce::String& t,
    int startX,
    int baselineY,
    int maximumLineWidth)
{
    juce::XmlElement* text;

    if (state->clipGroup)
        text = state->clipGroup->createNewChildElement("text");
    else
        text = document->createNewChildElement("text");

    auto f = state->font;
    auto tf = f.getTypefacePtr();

    text->setAttribute("x", startX);
    text->setAttribute("y", baselineY - f.getHeight());
    text->setAttribute("font-family", tf->getName());
    text->setAttribute("font-style", tf->getStyle());
    text->setAttribute("font-size", f.getHeight());
    text->setAttribute("fill", writeFill());

    if (!state->transform.isIdentity())
        text->setAttribute("transform", writeTransform(state->transform));

    auto t2 = t;

    while (t2.isNotEmpty())
    {
        int len = svgStringWidth(f, t2);

        if (len > maximumLineWidth)
        {
            auto line = t2;

            int i = 0;
            while (svgStringWidth(f, line) > maximumLineWidth)
            {
                line = line.dropLastCharacters(1);
                i++;
            }

            auto tspan = text->createNewChildElement("tspan");
            tspan->setAttribute("x", startX);
            tspan->setAttribute("y", baselineY);
            tspan->addTextElement(line);

            t2 = t2.substring(i);

            baselineY += (int) f.getHeight();
        }
        else
        {
            auto tspan = text->createNewChildElement("tspan");
            tspan->setAttribute("x", startX);
            tspan->setAttribute("y", baselineY);
            tspan->addTextElement(t2);

            t2 = "";
        }
    }

    applyTags(text);
}

void LowLevelGraphicsSVGRenderer::drawText(
    const juce::String& t,
    int x,
    int y,
    int width,
    int height,
    juce::Justification justification,
    bool useEllipsesIfTooBig)
{
    juce::XmlElement* text;

    if (state->clipGroup)
        text = state->clipGroup->createNewChildElement("text");
    else
        text = document->createNewChildElement("text");

    auto f = state->font;
    auto tf = f.getTypefacePtr();

    applyTextPos(text, x, y, width, height, justification);

    text->setAttribute("font-family", tf->getName());
    text->setAttribute("font-style", tf->getStyle());
    text->setAttribute("font-size", f.getHeight());
    text->setAttribute("fill", writeFill());

    if (!state->transform.isIdentity())
        text->setAttribute("transform", writeTransform(state->transform));

    auto t2 = t;

    auto len = svgStringWidth(f, t);
    if (len > width)
    {
        juce::String ellipses = (useEllipsesIfTooBig)
                                    ? juce::String(juce::CharPointer_UTF8("\xe2\x80\xa6"))
                                    : "";

        while (svgStringWidth(f, t2 + ellipses) > width)
        {
            t2 = t2.dropLastCharacters(1);
        }

        t2 += ellipses;
    }

    text->addTextElement(t2);

    applyTags(text);
}

void LowLevelGraphicsSVGRenderer::drawText(
    const juce::String& t,
    juce::Rectangle<int> area,
    juce::Justification justification,
    bool useEllipsesIfTooBig)
{
    drawText(
        t,
        area.getX(),
        area.getY(),
        area.getWidth(),
        area.getHeight(),
        justification,
        useEllipsesIfTooBig);
}

void LowLevelGraphicsSVGRenderer::drawText(
    const juce::String& t,
    juce::Rectangle<float> area,
    juce::Justification justification,
    bool useEllipsesIfTooBig)
{
    drawText(
        t,
        (int) area.getX(),
        (int) area.getY(),
        (int) area.getWidth(),
        (int) area.getHeight(),
        justification,
        useEllipsesIfTooBig);
}

void LowLevelGraphicsSVGRenderer::drawFittedText(
    const juce::String& t,
    int x,
    int y,
    int width,
    int height,
    juce::Justification justification,
    int maximumNumberOfLines,
    float minimumHorizontalScale)
{
    juce::XmlElement* text;

    if (state->clipGroup)
        text = state->clipGroup->createNewChildElement("text");
    else
        text = document->createNewChildElement("text");

    auto f = state->font;
    auto tf = f.getTypefacePtr();

    applyTextPos(text, x, y, width, height, justification);

    // TODO: support minimumHorizontalScale values
    if (minimumHorizontalScale == 0.0f)
    {
        text->setAttribute("textLength", width);
        text->setAttribute("lengthAdjust", "spacingAndGlyphs");
    }

    text->setAttribute("font-family", tf->getName());
    text->setAttribute("font-style", tf->getStyle());
    text->setAttribute("font-size", f.getHeight());
    text->setAttribute("fill", writeFill());

    auto t2 = t;

    if (maximumNumberOfLines > 1)
    {
        while (t2.isNotEmpty())
        {
            int len = t2.length();

            if (len > width)
            {
                auto line = t2;

                int i = 0;
                while (svgStringWidth(f, line) > width)
                {
                    line = line.dropLastCharacters(1);
                    i++;
                }

                auto tspan = text->createNewChildElement("tspan");
                tspan->setAttribute("x", x);
                tspan->setAttribute("y", y);
                tspan->setAttribute("text-anchor", "auto");
                tspan->setAttribute("dominant-baseline", "auto");
                tspan->addTextElement(line);

                t2 = t2.substring(i);

                y += (int) f.getHeight();
            }
            else
            {
                auto tspan = text->createNewChildElement("tspan");
                tspan->setAttribute("x", x);
                tspan->setAttribute("y", y);
                tspan->setAttribute("text-anchor", "auto");
                tspan->setAttribute("dominant-baseline", "auto");
                tspan->addTextElement(t2);

                t2 = "";
            }
        }
    }
    else
    {
        auto len = svgStringWidth(f, t2);
        if (len > width)
        {
            while (svgStringWidth(f, t2) > width)
            {
                t2 = t2.dropLastCharacters(1);
            }
        }
        text->addTextElement(t2);
    }

    applyTags(text);
}

void LowLevelGraphicsSVGRenderer::drawFittedText(
    const juce::String& t,
    juce::Rectangle<int> area,
    juce::Justification justification,
    int maximumNumberOfLines,
    float minimumHorizontalScale)
{
    drawFittedText(
        t,
        area.getX(),
        area.getY(),
        area.getWidth(),
        area.getHeight(),
        justification,
        maximumNumberOfLines,
        minimumHorizontalScale);
}

#pragma mark -
// =============================================================================

void LowLevelGraphicsSVGRenderer::pushGroup(const juce::String& groupID)
{
    if (!state->clipGroup)
        state->clipGroup = document->createNewChildElement("g");
    else
        state->clipGroup = state->clipGroup->createNewChildElement("g");

    state->clipGroup->setAttribute("id", groupID);
}

void LowLevelGraphicsSVGRenderer::popGroup()
{
    jassert(state->clipGroup);

    if (state->clipGroup->hasAttribute("id"))
    {
        auto temp = state->clipGroup;

        state->clipGroup = document->findParentElementOf(state->clipGroup);

        if (temp->getNumChildElements() == 0)
            state->clipGroup->removeChildElement(temp, true);
    }
    else
    {
        jassertfalse; // More popGroup() calls than pushGroup()!
    }
}

void LowLevelGraphicsSVGRenderer::setTags(const juce::StringPairArray& s)
{
    state->tags = s;
}

void LowLevelGraphicsSVGRenderer::clearTags()
{
    state->tags.clear();
}

#pragma mark -
// =============================================================================

juce::AffineTransform LowLevelGraphicsSVGRenderer::deviceTransform(
    const juce::AffineTransform& t) const
{
    return t.followedBy(state->transform);
}

bool LowLevelGraphicsSVGRenderer::ctmIsAxisAligned() const
{
    return state->transform.mat01 == 0.0f && state->transform.mat10 == 0.0f;
}

bool LowLevelGraphicsSVGRenderer::shouldSkipDrawing(
    const juce::Rectangle<float>& deviceBounds) const
{
    if (state->fillType.isInvisible())
        return true;

    return !state->clipPath.getBounds().intersects(deviceBounds);
}

juce::String LowLevelGraphicsSVGRenderer::truncateFloat(float value)
{
    auto string = juce::String(value, 2);

    while (string.getLastCharacters(1) == "." || (string.getLastCharacters(1) == "0" && string.contains(".")))
        string = string.dropLastCharacters(1);

    return string;
}

juce::String LowLevelGraphicsSVGRenderer::getPreviousGradientRef(
    juce::ColourGradient* g)
{
    if (previousGradients.size() == 0)
    {
        jassert(state->gradientRef.isNotEmpty());

        GradientRef newRef;
        newRef.gradient = *g;
        newRef.ref = state->gradientRef;

        previousGradients.add(newRef);
        return "";
    }

    for (auto r : previousGradients)
    {
        auto previousGradient = &r.gradient;

        if (previousGradient->getNumColours() != g->getNumColours())
            continue;

        bool match = false;

        for (int i = 0; i < previousGradient->getNumColours(); ++i)
        {
            auto colour = previousGradient->getColour(i);
            auto pos = previousGradient->getColourPosition(i);

            if (g->getColour(i) != colour)
                break;

            if (g->getColourPosition(i) != pos)
                break;

            match = true;
        }

        if (match)
            return r.ref;
    }

    jassert(state->gradientRef.isNotEmpty());

    GradientRef newRef;
    newRef.gradient = *g;
    newRef.ref = state->gradientRef;

    previousGradients.add(newRef);
    return "";
}

juce::String LowLevelGraphicsSVGRenderer::writeTransform(
    const juce::AffineTransform& t)
{
    // fix (JUCE 8 port): SVG matrix(a,b,c,d,e,f) is column-major
    // (xx, yx, xy, yy, tx, ty); the original wrote JUCE's row-major member
    // order, putting translation in the shear slot and 0 in yy — a
    // degenerate matrix that viewers discard, leaving images unplaced
    return juce::String::formatted(
        "matrix(%f,%f,%f,%f,%f,%f)",
        t.mat00,
        t.mat10,
        t.mat01,
        t.mat11,
        t.mat02,
        t.mat12);
}

juce::String LowLevelGraphicsSVGRenderer::writeColour(const juce::Colour& c)
{
    return juce::String::formatted(
        "rgb(%d,%d,%d)",
        c.getRed(),
        c.getGreen(),
        c.getBlue());
}

juce::String LowLevelGraphicsSVGRenderer::writeFill()
{
    if (state->fillType.isGradient())
        return "url(" + state->gradientRef + ")";
    else
        return writeColour(state->fillType.colour);
}

juce::String LowLevelGraphicsSVGRenderer::writeImageQuality()
{
    switch (resampleQuality)
    {
        case juce::Graphics::lowResamplingQuality:
            return "optimizeSpeed";

        case juce::Graphics::mediumResamplingQuality:
            return "auto";

        case juce::Graphics::highResamplingQuality:
            return "optimizeQuality";
    }
}

void LowLevelGraphicsSVGRenderer::applyTags(juce::XmlElement* e)
{
    if (state->tags.size() == 0)
        return;

    auto keys = state->tags.getAllKeys();
    auto values = state->tags.getAllValues();

    for (int i = 0; i < state->tags.size(); ++i)
        e->setAttribute(keys[i], values[i]);
}

#pragma mark -
// =============================================================================

void LowLevelGraphicsSVGRenderer::applyTextPos(
    juce::XmlElement* text,
    int x,
    int y,
    const int width,
    const int height,
    const juce::Justification& j)
{
    if (j.testFlags(j.horizontallyCentred))
    {
        text->setAttribute("text-anchor", "middle");
        x += width / 2;
    }
    else if (j.testFlags(j.right))
    {
        text->setAttribute("text-anchor", "end");
        x += width;
    }
    else
    {
        text->setAttribute("text-anchor", "start");
    }

    if (j.testFlags(j.verticallyCentred))
    {
        text->setAttribute("dominant-baseline", "central");
        y += height / 2;
    }
    else if (j.testFlags(j.bottom))
    {
        text->setAttribute("dominant-baseline", "ideographic");
        y += height;
    }
    else
    {
        text->setAttribute("dominant-baseline", "hanging");
    }

    text->setAttribute("x", x);
    text->setAttribute("y", y);
}

void LowLevelGraphicsSVGRenderer::setClip(const juce::Path& p)
{
    // p is already in device space, same as all emitted geometry — no
    // transform attribute (the original re-applied the CTM here, shifting
    // the clip away from the geometry it was meant to clip)
    state->clipPath = p;

    auto defs = document->getChildByName("defs");
    auto clipRef = juce::String::formatted(
        "#ClipPath%d",
        defs->getNumChildElements());

    auto clipPath = defs->createNewChildElement("clipPath");
    clipPath->setAttribute("id", clipRef.replace("#", ""));

    auto path = clipPath->createNewChildElement("path");
    path->setAttribute("d", state->clipPath.toString().toUpperCase());

    if (!state->clipGroup)
        state->clipGroup = document->createNewChildElement("g");

    state->clipGroup = state->clipGroup->createNewChildElement("g");

    state->clipGroup->setAttribute("clip-path", "url(" + clipRef + ")");
}
