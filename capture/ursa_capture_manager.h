// ============================================================================
// Part of the ursa_svg_screenshot JUCE module — MIT licensed, see LICENSE
// at the module root. Copyright (c) 2026 UrsaDSP / Perambulum Ltd.
// ============================================================================

#pragma once

/**
 * The capture rig (docs/CaptureRig.md): services <name>.request.json files
 * dropped into <projectDir>/plugin/screenshots, writing <name>.png/.svg/.pdf
 * beside them, and adds the F10 (SVG), F11 (PDF, macOS) and F12 (PNG)
 * hotkeys. Build one in the editor's constructor once the UI exists:
 *
 *   captureManager = std::make_unique<UrsaCaptureManager>(*this, mainUi, captureHost, projectDir);
 *
 * uiRoot is the component that counts as "the whole UI" for "all" and
 * "region" captures and for component lookup; "plugin_editor" names the
 * editor itself.
 */
class UrsaCaptureManager : private juce::Timer,
                           public juce::KeyListener
{
public:
    UrsaCaptureManager(juce::AudioProcessorEditor& editor_,
                       juce::Component& uiRoot_,
                       UrsaCaptureHost& host_,
                       const juce::File& projectDir_)
        : editor(editor_),
          uiRoot(uiRoot_),
          host(host_),
          projectDir(projectDir_),
          screenshotDir(projectDir_.getChildFile("plugin/screenshots")),
          pose(editor_, host_, screenshotDir),
          pointer(uiRoot_),
          vectorExportKeys(uiRoot_, screenshotDir)
    {
        screenshotDir.createDirectory();

        editor.addKeyListener(&vectorExportKeys);
        editor.addKeyListener(this);

        scanForRequests();
        startTimerHz(ticksPerSecond);
    }

    ~UrsaCaptureManager() override
    {
        stopTimer();
        editor.removeKeyListener(this);
        editor.removeKeyListener(&vectorExportKeys);
    }

    bool keyPressed(const juce::KeyPress& key, juce::Component* /*originatingComponent*/) override
    {
        if (key == juce::KeyPress::F12Key)
        {
            captureManualPng();
            return true;
        }

        return false;
    }

private:
    static constexpr int ticksPerSecond = 48;

    // The window has to be raised before a pointer or focus pose: macOS drops
    // a mouse event whose screen point another app's window covers, and
    // keyboard focus needs the window active. The window server takes a
    // moment to agree.
    static constexpr int windowRaiseSettleMs = 400;

    void timerCallback() override
    {
        // rescan for new request files about once a second
        if (++tickCount >= ticksPerSecond)
        {
            tickCount = 0;
            scanForRequests();
        }
        processPendingRequests();
    }

    void captureManualPng()
    {
        auto timestamp = juce::Time::getCurrentTime().formatted("%Y%m%d_%H%M%S");
        auto image = uiRoot.createComponentSnapshot(uiRoot.getLocalBounds(), true, 1.0f);

        if (image.isValid())
            writePng(image, screenshotDir.getChildFile("manual_" + timestamp + ".png"));
    }

    void scanForRequests()
    {
        const bool isStandalone = editor.getAudioProcessor()->wrapperType == juce::AudioProcessor::wrapperType_Standalone;

        for (auto& file : screenshotDir.findChildFiles(juce::File::findFiles, false, "*.request.json"))
        {
            const bool alreadyPending = std::any_of(pendingRequests.begin(),
                                                    pendingRequests.end(),
                                                    [&](const UrsaCaptureRequest& r) { return r.requestFile == file; });
            if (alreadyPending)
                continue;

            auto json = juce::JSON::parse(file.loadFileAsString());
            if (!json.isObject())
                continue;

            // optional host guard: {"host": "standalone"} or {"host": "daw"}.
            // Scripted captures should pass "standalone" — DAW sessions often
            // hold stale plugin builds, and an unguarded request is serviced
            // by whichever instance polls first.
            const auto hostKind = json.getProperty("host", "").toString();
            if (hostKind.isNotEmpty() && (hostKind == "standalone") != isStandalone)
                continue;

            UrsaCaptureRequest req(file, json);
            req.pluginPose = host.hasPluginPose(json);
            pendingRequests.push_back(std::move(req));
        }

        // name order makes a numbered batch of posed requests deterministic
        std::sort(pendingRequests.begin(),
                  pendingRequests.end(),
                  [](const UrsaCaptureRequest& a, const UrsaCaptureRequest& b) { return a.name < b.name; });
    }

    void processPendingRequests()
    {
        auto now = juce::Time::currentTimeMillis();

        // posed requests are serviced one at a time in name order so a numbered
        // batch can't fight over the view, the pointer or the editor size
        juce::String activePoseName;
        for (const auto& r : pendingRequests)
        {
            if (r.hasPose())
            {
                activePoseName = r.name;
                break;
            }
        }

        for (auto it = pendingRequests.begin(); it != pendingRequests.end();)
        {
            auto& req = *it;

            if (req.hasPose())
            {
                if (req.name != activePoseName)
                {
                    ++it; // waits for its turn
                    continue;
                }

                if (!req.posed)
                {
                    // a pose that can't be applied (unknown view, unknown
                    // parameter id, missing preset): drop the request rather
                    // than requeue it forever
                    const auto dropRequest = [&]
                    {
                        undoTransientPoses(req);
                        req.requestFile.deleteFile();
                        it = pendingRequests.erase(it);
                        activePoseName = {};
                    };

                    if (req.poseStage == 0)
                    {
                        if (!applyStatePoses(req))
                        {
                            dropRequest();
                            continue;
                        }

                        req.poseStage = 1;
                        req.poseReadyAt = now + windowRaiseSettleMs;

                        if (req.hasPointerPose())
                        {
                            ++it; // the pointer poses land on a later tick
                            continue;
                        }
                    }
                    else if (now < req.poseReadyAt)
                    {
                        ++it;
                        continue;
                    }
                    else if (!applyPointerPoses(req))
                    {
                        dropRequest();
                        continue;
                    }

                    req.posed = true;
                    req.visibleSince = 0; // delay counts from the last pose
                }
            }

            juce::Component* targetComponent = nullptr;
            const bool targetVisible = resolveTarget(req, targetComponent);

            if (!targetVisible)
            {
                req.visibleSince = 0;
                ++it;
                continue;
            }

            if (req.visibleSince == 0)
            {
                req.visibleSince = now;

                // whatever the real mouse rests on (a highlighted readout, a
                // tooltip) must not show; the delay gives the UI time to
                // repaint without it
                if (!req.hasPointerPose())
                    pointer.park();
            }

            if (now - req.visibleSince < req.delayMs)
            {
                ++it;
                continue;
            }

            if (capture(req, targetComponent))
                req.requestFile.deleteFile();

            undoTransientPoses(req);

            if (req.name == activePoseName)
                activePoseName = {}; // the next posed request gets its turn next tick

            it = pendingRequests.erase(it);
        }
    }

    /** The component a request captures, and whether it may be captured yet.
        Hidden components paint transparent, so every mode waits for its
        target to show. Arrival-gated modes ("all_when_visible", and svg/pdf
        with trigger_id) capture the FULL UI once the trigger appears — and
        only on arrival: the trigger must be seen hidden first, so a request
        armed while its tab is already open waits for a leave-and-return. */
    bool resolveTarget(UrsaCaptureRequest& req, juce::Component*& target)
    {
        const auto showingTarget = [&](const juce::String& id)
        {
            target = findTarget(id);
            return target != nullptr && target->isShowing();
        };

        const auto arrivalGated = [&](const juce::String& triggerId)
        {
            auto* trigger = findTarget(triggerId);
            const bool triggerShowing = (trigger != nullptr && trigger->isShowing());
            if (!triggerShowing)
                req.seenHidden = true;
            target = &uiRoot;
            return req.seenHidden && triggerShowing;
        };

        if (req.mode == "all" || req.mode == "region")
        {
            target = &uiRoot;
            return uiRoot.isShowing();
        }

        if (req.mode == "component")
            return showingTarget(req.componentId);

        if (req.mode == "pdf" || req.mode == "svg")
        {
            if (req.triggerId.isNotEmpty())
                return arrivalGated(req.triggerId);

            if (req.componentId.isNotEmpty())
                return showingTarget(req.componentId);

            target = &uiRoot;
            return uiRoot.isShowing();
        }

        if (req.mode == "all_when_visible")
            return arrivalGated(req.componentId);

        return false;
    }

    bool capture(const UrsaCaptureRequest& req, juce::Component* target)
    {
        if (target == nullptr)
            return false;

        if (req.mode == "svg")
            return UrsaSvgExporter::exportComponent(*target, screenshotDir.getChildFile(req.name + ".svg"));

        if (req.mode == "pdf")
        {
#if JUCE_MAC
            return UrsaPdfExporter::exportComponent(*target, screenshotDir.getChildFile(req.name + ".pdf"));
#else
            return true; // macOS-only: swallow the request rather than requeue it forever
#endif
        }

        return capturePng(req, target);
    }

    // Poses are applied in one fixed order (docs/CaptureRig.md): saved state,
    // preset, parameters, view, editor size, the host's own poses, the window
    // raise, and finally save_state — so a saved state records everything
    // above it. Any failure rejects the whole request.
    bool applyStatePoses(UrsaCaptureRequest& req)
    {
        if (req.loadState.isString() && !pose.loadState(req.loadState.toString()))
            return reject(req, "no saved state '" + req.loadState.toString() + "'");

        if (req.preset.isString() && !host.loadPreset(req.preset.toString()))
            return reject(req, "no preset named '" + req.preset.toString() + "'");

        if (req.presetFile.isString() && !host.loadPresetFile(resolveProjectPath(req.presetFile.toString())))
            return reject(req, "no preset file '" + req.presetFile.toString() + "'");

        if (auto* params = req.setParams.getDynamicObject())
            for (const auto& p : params->getProperties())
                if (!pose.setParamNormalised(p.name.toString(), float(double(p.value))))
                    return reject(req, "unknown parameter id '" + p.name.toString() + "'");

        if (auto* params = req.setParamsText.getDynamicObject())
            for (const auto& p : params->getProperties())
                if (!pose.setParamFromText(p.name.toString(), p.value.toString()))
                    return reject(req, "unknown parameter id '" + p.name.toString() + "'");

        if (req.view.isNotEmpty() && !host.poseView(req.view))
            return reject(req, "unknown view '" + req.view + "'");

        if (req.editorSize.isNotEmpty() && !pose.setEditorSize(req.editorSize))
            return reject(req, "bad editor_size '" + req.editorSize + "'");

        if (req.pluginPose)
        {
            juce::String error;
            if (!host.applyPluginPoses(req.json, error))
                return reject(req, error);
        }

        if (req.hasPointerPose())
            pointer.raiseWindow();

        if (req.saveState.isString() && !pose.saveState(req.saveState.toString()))
            return reject(req, "could not write state '" + req.saveState.toString() + "'");

        return true;
    }

    // Stage two, once the raised window is really in front
    bool applyPointerPoses(UrsaCaptureRequest& req)
    {
        if (req.hoverId.isNotEmpty() && !pointer.hover(findTarget(req.hoverId), req.hoverX, req.hoverY))
            return reject(req, "can't hover '" + req.hoverId + "' (no such component, or hidden)");

        if (req.pressId.isNotEmpty() && !pointer.press(findTarget(req.pressId)))
            return reject(req, "can't press '" + req.pressId + "' (no such component, or hidden)");

        if (req.focusId.isNotEmpty() && !pointer.focus(findTarget(req.focusId)))
            return reject(req, "can't focus '" + req.focusId + "' (no such component, or hidden)");

        if (req.editId.isNotEmpty())
        {
            auto* target = findTarget(req.editId);
            if (target == nullptr || !target->isShowing() || !host.poseEditFocus(*target, pointer))
                return reject(req, "'" + req.editId + "' type-in did not open");
            req.editPosed = true;
        }

        return true;
    }

    // Everything a pose leaves behind that would outlive the capture: a held
    // button, a hover, an open type-in, a floated window, a resized editor,
    // and the host's own poses. The parameter, preset and state poses are
    // deliberately NOT undone — they are the point; a script restores them
    // with load_state.
    void undoTransientPoses(const UrsaCaptureRequest& req)
    {
        if (req.hoverId.isNotEmpty() || req.pressId.isNotEmpty() || req.editId.isNotEmpty())
            pointer.release();

        if (req.focusId.isNotEmpty() || req.editId.isNotEmpty())
            pointer.clearFocus(req.editPosed);

        if (req.hasPointerPose())
            pointer.releaseWindow();

        if (req.editorSize.isNotEmpty())
            pose.restoreEditorSize();

        if (req.pluginPose)
            host.undoPluginPoses(req.json);
    }

    juce::Component* findTarget(const juce::String& id)
    {
        // the editor is the one component outside uiRoot: it carries any
        // resize transform, so it is what a posed editor_size has to capture
        if (id == "plugin_editor")
            return &editor;

        return UrsaPointerPose::findById(&uiRoot, id);
    }

    juce::File resolveProjectPath(const juce::String& path) const
    {
        return juce::File::isAbsolutePath(path) ? juce::File(path) : projectDir.getChildFile(path);
    }

    bool reject(const UrsaCaptureRequest& req, const juce::String& reason)
    {
        juce::Logger::writeToLog("Screenshot request '" + req.name + "' rejected: " + reason);
        return false;
    }

    // A PopupMenu lives in its own desktop window, so it is outside uiRoot's
    // snapshot. Draw every other showing top-level window into the image at
    // its position relative to uiRoot.
    void compositeDesktopWindows(juce::Image& image, float scale)
    {
        auto& desktop = juce::Desktop::getInstance();
        auto* ownWindow = editor.getTopLevelComponent();

        juce::Graphics g(image);

        for (int i = 0; i < desktop.getNumComponents(); ++i)
        {
            auto* window = desktop.getComponent(i);

            if (window == nullptr || window == ownWindow || !window->isShowing())
                continue;

            auto snapshot = window->createComponentSnapshot(window->getLocalBounds(), true, scale);
            if (!snapshot.isValid())
                continue;

            const auto origin = uiRoot.getLocalPoint(nullptr, window->getScreenPosition()).toFloat() * scale;
            g.drawImageAt(snapshot, juce::roundToInt(origin.x), juce::roundToInt(origin.y));
        }
    }

    bool capturePng(const UrsaCaptureRequest& req, juce::Component* target)
    {
        juce::Image image;

        if (req.mode == "region")
        {
            // capture the whole UI then crop; region coordinates are UI-space,
            // the snapshot (and therefore the crop) is in scaled pixels
            auto fullImage = uiRoot.createComponentSnapshot(uiRoot.getLocalBounds(), true, req.scale);

            compositeDesktopWindows(fullImage, req.scale);

            auto cropBounds = (juce::Rectangle<int>(req.x, req.y, req.w, req.h).toFloat() * req.scale)
                                  .getSmallestIntegerContainer()
                                  .getIntersection(fullImage.getBounds());

            if (cropBounds.isEmpty())
                return false;

            image = fullImage.getClippedImage(cropBounds);
        }
        else
        {
            image = target->createComponentSnapshot(target->getLocalBounds(), true, req.scale);

            // only the whole-UI snapshot shares uiRoot's coordinates
            if (target == &uiRoot && image.isValid())
                compositeDesktopWindows(image, req.scale);
        }

        if (!image.isValid())
            return false;

        return writePng(image, screenshotDir.getChildFile(req.name + ".png"));
    }

    static bool writePng(const juce::Image& image, const juce::File& file)
    {
        auto stream = file.createOutputStream();
        if (stream == nullptr)
            return false;

        juce::PNGImageFormat pngFormat;
        return pngFormat.writeImageToStream(image, *stream);
    }

    juce::AudioProcessorEditor& editor;
    juce::Component& uiRoot;
    UrsaCaptureHost& host;
    juce::File projectDir;
    juce::File screenshotDir;
    UrsaCapturePose pose;
    UrsaPointerPose pointer;
    UrsaVectorExportKeys vectorExportKeys;
    std::vector<UrsaCaptureRequest> pendingRequests;
    int tickCount { 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(UrsaCaptureManager)
};
