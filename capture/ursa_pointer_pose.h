// ============================================================================
// Part of the ursa_svg_screenshot JUCE module — MIT licensed, see LICENSE
// at the module root. Copyright (c) 2026 UrsaDSP / Perambulum Ltd.
// ============================================================================

#pragma once

/**
 * The capture rig's pointer and focus poses (docs/CaptureRig.md): the pointer
 * over a component, a held button, keyboard focus — delivered the way the OS
 * delivers them, so the widgets need no test-only flags.
 *
 * Mouse events go into the window's peer, the same entry point the native
 * layer uses: JUCE's enter/exit tracking, isMouseOver, tooltips and
 * double-click detection all run for real.
 */
class UrsaPointerPose
{
public:
    explicit UrsaPointerPose(juce::Component& uiRoot_) : uiRoot(uiRoot_) {}

    /** Component lookup by componentID, depth first from root. */
    static juce::Component* findById(juce::Component* root, const juce::String& id)
    {
        if (root == nullptr)
            return nullptr;

        if (root->getComponentID() == id)
            return root;

        for (auto* child : root->getChildren())
            if (auto* found = findById(child, id))
                return found;

        return nullptr;
    }

    /** macOS drops a mouse event whose screen point another app's window
        covers (the peer asks the window server what is topmost there), and
        keyboard focus needs the window active — so the window is raised a
        beat before every pointer or focus pose. A background app can't make
        itself frontmost, so the window floats above the others instead;
        releaseWindow puts it back. Keyboard focus still wants the app active. */
    void raiseWindow()
    {
        juce::Process::makeForegroundProcess();

        if (auto* top = uiRoot.getTopLevelComponent())
        {
            windowWasAlwaysOnTop = top->isAlwaysOnTop();
            top->setAlwaysOnTop(true);
            top->toFront(true);
        }
    }

    /** Undo raiseWindow — the window stops floating over other apps. */
    void releaseWindow()
    {
        if (auto* top = uiRoot.getTopLevelComponent())
            top->setAlwaysOnTop(windowWasAlwaysOnTop);
    }

    /** Put the pointer over a component. x/y are component-local; negative
        means the component's centre. */
    bool hover(juce::Component* target, int x, int y)
    {
        if (target == nullptr || !target->isShowing())
            return false;

        const auto local = (x < 0 || y < 0) ? target->getLocalBounds().getCentre().toFloat()
                                            : juce::Point<float>(float(x), float(y));
        return sendPointerEvent(target->localPointToGlobal(local), juce::ModifierKeys::noModifiers);
    }

    /** Hover, then hold the left button down. The release is delivered by
        release() well outside the component, so no click fires. */
    bool press(juce::Component* target)
    {
        if (!hover(target, -1, -1))
            return false;

        pointerDown = true;
        return sendPointerEvent(target->localPointToGlobal(target->getLocalBounds().getCentre().toFloat()),
                                juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier));
    }

    /** Undo hover/press: any held button is released away from every control,
        and the pointer leaves the window, so nothing stays hovered. */
    void release()
    {
        if (pointerDown)
        {
            sendPointerEvent(awayPoint(), juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier));
            pointerDown = false;
        }

        park();
    }

    /** Tell JUCE the pointer has left the window, so no hover highlight or
        tooltip shows in a capture. The real cursor doesn't move: JUCE takes
        it back from the next real mouse movement. */
    void park()
    {
        sendPointerEvent(awayPoint(), juce::ModifierKeys::noModifiers);
    }

    bool focus(juce::Component* target)
    {
        if (target == nullptr || !target->isShowing())
            return false;

        target->grabKeyboardFocus();
        return true;
    }

    /** Give keyboard focus away. With discardEdit, a type-in in progress is
        first sent Escape — a text entry's own cancel route, so no value is
        committed. */
    void clearFocus(bool discardEdit)
    {
        if (auto* focused = juce::Component::getCurrentlyFocusedComponent())
        {
            if (discardEdit)
                focused->keyPressed(juce::KeyPress(juce::KeyPress::escapeKey));

            focused->giveAwayKeyboardFocus();
        }
    }

    /** One raw mouse event at a screen position, for host poses that click
        (UrsaCaptureHost::poseEditFocus). */
    bool sendPointerEvent(juce::Point<float> screenPos, juce::ModifierKeys mods)
    {
        auto* peer = uiRoot.getPeer();
        if (peer == nullptr)
            return false;

        peer->handleMouseEvent(juce::MouseInputSource::InputSourceType::mouse,
                               peer->globalToLocal(screenPos),
                               mods,
                               juce::MouseInputSource::defaultPressure,
                               juce::MouseInputSource::defaultOrientation,
                               juce::Time::currentTimeMillis());
        return true;
    }

private:
    juce::Point<float> awayPoint() const
    {
        return uiRoot.localPointToGlobal(juce::Point<float>(-64.0f, -64.0f));
    }

    juce::Component& uiRoot;
    bool pointerDown { false };
    bool windowWasAlwaysOnTop { false };

    JUCE_DECLARE_NON_COPYABLE(UrsaPointerPose)
};
