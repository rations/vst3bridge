/*
 * Copyright (C) 2026
 * VST3 Bridge - Wine VST3 Host Bridge
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 */

/**
 * @file plugin_instance.cpp
 * @brief VST3 plugin instance management for the Wine host.
 */

#include "plugin_instance.h"
#include "logger.h"
#include "istream_impl.h"
#include <atomic>
#include <cstring>

namespace vst3bridge {

// ============================================================================
// Static members
// ============================================================================

uint64_t PluginInstance::nextId_ = 1;

// ============================================================================
// Private constructor
// ============================================================================

PluginInstance::PluginInstance(uint64_t id) : id_(id) {}

// ============================================================================
// Destructor — release all interfaces in safe order
// ============================================================================

PluginInstance::~PluginInstance() {
    // Remove the view first if still attached
    if (view_) {
        view_->removed();
        view_->release();
        view_ = nullptr;
    }

    // Terminate controller (unless it is the same object as component)
    if (controller_ && !singleComponent_) {
        controller_->terminate();
        controller_->release();
        controller_ = nullptr;
    }

    // Terminate component (releases audioProc_ reference too)
    if (component_) {
        component_->terminate();
        component_->release();
        component_ = nullptr;
    }

    // audioProc_ is a query on component_, already released above
    if (audioProc_ && singleComponent_) {
        audioProc_->release();
    }
    audioProc_ = nullptr;

    if (controller_ && singleComponent_) {
        controller_->release();
        controller_ = nullptr;
    }
}

// ============================================================================
// create() — factory method
// ============================================================================

std::unique_ptr<PluginInstance> PluginInstance::create(
        Steinberg::IPluginFactory*   factory,
        const Steinberg::TUID        cid,
        Steinberg::Vst::IHostApplication* hostApp)
{
    if (!factory) {
        LOG_ERROR("PluginInstance::create(): factory is null");
        return nullptr;
    }

    auto inst = std::unique_ptr<PluginInstance>(
        new PluginInstance(nextId_++));

    // ---- 1. Create component ------------------------------------------------

    Steinberg::TUID iComponentId;
    Steinberg::Vst::IComponent::iid.toTUID(iComponentId);

    void* rawComponent = nullptr;
    Steinberg::tresult res = factory->createInstance(
            cid, iComponentId, &rawComponent);

    if (res != Steinberg::kResultOk || !rawComponent) {
        LOG_ERROR("PluginInstance: createInstance() failed (result {})",
                  static_cast<uint32_t>(res));
        return nullptr;
    }

    inst->component_ = static_cast<Steinberg::Vst::IComponent*>(rawComponent);
    // Ownership transferred; do NOT addRef here.

    // ---- 2. Query IAudioProcessor from the component -----------------------

    {
        Steinberg::TUID apId;
        Steinberg::Vst::IAudioProcessor::iid.toTUID(apId);
        void* ptr = nullptr;
        if (inst->component_->queryInterface(apId, &ptr) == Steinberg::kResultOk) {
            inst->audioProc_ = static_cast<Steinberg::Vst::IAudioProcessor*>(ptr);
            LOG_DEBUG("PluginInstance: IAudioProcessor obtained");
        } else {
            LOG_WARN("PluginInstance: IAudioProcessor not available (instrument only?)");
        }
    }

    // ---- 3. Find and create the IEditController ----------------------------

    // First try: controller is implemented by the same object.
    {
        Steinberg::TUID ecId;
        Steinberg::Vst::IEditController::iid.toTUID(ecId);
        void* ptr = nullptr;
        if (inst->component_->queryInterface(ecId, &ptr) == Steinberg::kResultOk) {
            inst->controller_ = static_cast<Steinberg::Vst::IEditController*>(ptr);
            inst->singleComponent_ = true;
            LOG_DEBUG("PluginInstance: single component/controller");
        }
    }

    // Second try: separate controller class returned by getControllerClassId().
    if (!inst->controller_) {
        Steinberg::TUID controllerClassTuid;
        Steinberg::TUID ecId;
        Steinberg::Vst::IEditController::iid.toTUID(ecId);

        if (inst->component_->getControllerClassId(controllerClassTuid) == Steinberg::kResultOk) {
            void* rawCtrl = nullptr;
            Steinberg::tresult cres = factory->createInstance(
                    controllerClassTuid, ecId, &rawCtrl);

            if (cres == Steinberg::kResultOk && rawCtrl) {
                inst->controller_ = static_cast<Steinberg::Vst::IEditController*>(rawCtrl);
                LOG_DEBUG("PluginInstance: separate controller instance created");
            } else {
                LOG_WARN("PluginInstance: could not create separate controller "
                         "(result {}), continuing without it",
                         static_cast<uint32_t>(cres));
            }
        }
    }

    // ---- 4. Initialize component (and controller if separate) --------------

    if (hostApp) {
        // Note: we pass FUnknown* (IHostApplication is-a FUnknown)
        res = inst->component_->initialize(
                static_cast<Steinberg::FUnknown*>(hostApp));
        if (res != Steinberg::kResultOk) {
            LOG_ERROR("PluginInstance: component->initialize() failed (result {})",
                      static_cast<uint32_t>(res));
            // Continue anyway; some plugins ignore initialize errors.
        }

        if (inst->controller_ && !inst->singleComponent_) {
            res = inst->controller_->initialize(
                    static_cast<Steinberg::FUnknown*>(hostApp));
            if (res != Steinberg::kResultOk) {
                LOG_WARN("PluginInstance: controller->initialize() failed (result {})",
                         static_cast<uint32_t>(res));
            }
        }
    }

    LOG_INFO("PluginInstance: created id={}", inst->id_);
    return inst;
}

// ============================================================================
// IPluginBase
// ============================================================================

Steinberg::tresult PluginInstance::initialize(Steinberg::Vst::IHostApplication* hostApp)
{
    if (!component_) return Steinberg::kInternalError;

    Steinberg::tresult res =
        component_->initialize(static_cast<Steinberg::FUnknown*>(hostApp));

    if (controller_ && !singleComponent_) {
        controller_->initialize(static_cast<Steinberg::FUnknown*>(hostApp));
    }

    return res;
}

Steinberg::tresult PluginInstance::terminate()
{
    Steinberg::tresult res = Steinberg::kResultOk;

    if (controller_ && !singleComponent_) {
        res = controller_->terminate();
    }
    if (component_) {
        res = component_->terminate();
    }
    return res;
}

// ============================================================================
// IComponent pass-throughs
// ============================================================================

Steinberg::tresult PluginInstance::getControllerClassId(Steinberg::FUID& classId)
{
    if (!component_) return Steinberg::kInternalError;
    Steinberg::TUID tuid;
    Steinberg::tresult res = component_->getControllerClassId(tuid);
    if (res == Steinberg::kResultOk)
        classId = Steinberg::FUID::fromTUID(tuid);
    return res;
}

Steinberg::tresult PluginInstance::setIoMode(Steinberg::Vst::IoMode mode)
{
    if (!component_) return Steinberg::kInternalError;
    return component_->setIoMode(mode);
}

Steinberg::int32 PluginInstance::getBusCount(
        Steinberg::Vst::MediaType type, Steinberg::Vst::BusDirection dir)
{
    if (!component_) return 0;
    return component_->getBusCount(type, dir);
}

Steinberg::tresult PluginInstance::getBusInfo(
        Steinberg::Vst::MediaType type, Steinberg::Vst::BusDirection dir,
        Steinberg::int32 index, Steinberg::Vst::BusInfo& bus)
{
    if (!component_) return Steinberg::kInternalError;
    return component_->getBusInfo(type, dir, index, bus);
}

Steinberg::tresult PluginInstance::activateBus(
        Steinberg::Vst::MediaType type, Steinberg::Vst::BusDirection dir,
        Steinberg::int32 index, bool state)
{
    if (!component_) return Steinberg::kInternalError;
    return component_->activateBus(type, dir, index, state);
}

Steinberg::tresult PluginInstance::setActive(bool state)
{
    if (!component_) return Steinberg::kInternalError;
    return component_->setActive(state);
}

// ============================================================================
// State management (IComponent)
// ============================================================================

Steinberg::tresult PluginInstance::setState(const std::vector<uint8_t>& data)
{
    if (!component_) return Steinberg::kInternalError;
    MemoryStream stream(data.data(), static_cast<Steinberg::int32>(data.size()));
    return component_->setState(&stream);
}

Steinberg::tresult PluginInstance::getState(std::vector<uint8_t>& data)
{
    if (!component_) return Steinberg::kInternalError;

    MemoryStream stream;
    Steinberg::tresult res = component_->getState(&stream);
    if (res == Steinberg::kResultOk) {
        data = stream.buffer();
    }
    return res;
}

Steinberg::tresult PluginInstance::setComponentState(const std::vector<uint8_t>& data)
{
    if (!controller_) return Steinberg::kNotImplemented;
    MemoryStream stream(data.data(), static_cast<Steinberg::int32>(data.size()));
    return controller_->setComponentState(&stream);
}

Steinberg::tresult PluginInstance::getComponentState(std::vector<uint8_t>& data)
{
    // "Component state" flows from component → controller.
    // First get the state from the component, then we send it to the controller.
    return getState(data);
}

// ============================================================================
// IAudioProcessor pass-throughs
// ============================================================================

Steinberg::tresult PluginInstance::setBusArrangements(
        const MsgRequestSetBusArrangements& msg)
{
    if (!audioProc_) return Steinberg::kNotImplemented;
    if (msg.num_ins > 8 || msg.num_outs > 8) return Steinberg::kInvalidArgument;

    return audioProc_->setBusArrangements(
        const_cast<Steinberg::Vst::SpeakerArrangement*>(msg.in_arr),
        msg.num_ins,
        const_cast<Steinberg::Vst::SpeakerArrangement*>(msg.out_arr),
        msg.num_outs);
}

Steinberg::tresult PluginInstance::getBusArrangement(
        Steinberg::Vst::BusDirection dir, Steinberg::int32 busIndex,
        Steinberg::Vst::SpeakerArrangement& arr)
{
    if (!audioProc_) return Steinberg::kNotImplemented;
    return audioProc_->getBusArrangement(dir, busIndex, arr);
}

Steinberg::tresult PluginInstance::canProcessSampleSize(Steinberg::int32 symbolicSize)
{
    if (!audioProc_) return Steinberg::kNotImplemented;
    return audioProc_->canProcessSampleSize(symbolicSize);
}

Steinberg::uint32 PluginInstance::getLatencySamples()
{
    if (!audioProc_) return 0;
    return audioProc_->getLatencySamples();
}

Steinberg::tresult PluginInstance::setupProcessing(Steinberg::Vst::ProcessSetup& setup)
{
    if (!audioProc_) return Steinberg::kNotImplemented;
    return audioProc_->setupProcessing(setup);
}

Steinberg::tresult PluginInstance::setProcessing(bool state)
{
    if (!audioProc_) return Steinberg::kNotImplemented;
    return audioProc_->setProcessing(state);
}

Steinberg::tresult PluginInstance::process(Steinberg::Vst::ProcessData& data)
{
    if (!audioProc_) return Steinberg::kNotImplemented;
    return audioProc_->process(data);
}

Steinberg::uint32 PluginInstance::getTailSamples()
{
    if (!audioProc_) return 0;
    return audioProc_->getTailSamples();
}

// ============================================================================
// IEditController pass-throughs
// ============================================================================

Steinberg::tresult PluginInstance::setControllerComponentState(
        const std::vector<uint8_t>& data)
{
    if (!controller_) return Steinberg::kNotImplemented;
    MemoryStream stream(data.data(), static_cast<Steinberg::int32>(data.size()));
    return controller_->setComponentState(&stream);
}

Steinberg::int32 PluginInstance::getParameterCount()
{
    if (!controller_) return 0;
    return controller_->getParameterCount();
}

Steinberg::tresult PluginInstance::getParameterInfo(
        Steinberg::int32 index, Steinberg::Vst::ParameterInfo& info)
{
    if (!controller_) return Steinberg::kNotImplemented;
    return controller_->getParameterInfo(index, info);
}

double PluginInstance::getParamNormalized(Steinberg::uint32 id)
{
    if (!controller_) return 0.0;
    return controller_->getParamNormalized(id);
}

Steinberg::tresult PluginInstance::setParamNormalized(
        Steinberg::uint32 id, double value)
{
    if (!controller_) return Steinberg::kNotImplemented;
    return controller_->setParamNormalized(id, value);
}

Steinberg::tresult PluginInstance::setComponentHandler(
        Steinberg::Vst::IComponentHandler* handler)
{
    if (!controller_) return Steinberg::kNotImplemented;
    return controller_->setComponentHandler(handler);
}

// ============================================================================
// IPlugView
// ============================================================================

bool PluginInstance::createView()
{
    if (!controller_) {
        LOG_ERROR("PluginInstance::createView(): no controller");
        return false;
    }

    if (view_) {
        // Already created; release the old one first.
        view_->removed();
        view_->release();
        view_ = nullptr;
    }

    view_ = controller_->createView(Steinberg::Vst::ViewType::kEditor);
    if (!view_) {
        LOG_WARN("PluginInstance::createView(): controller returned null view "
                 "(plugin may not have a GUI)");
        return false;
    }

    LOG_DEBUG("PluginInstance: view created");
    return true;
}

Steinberg::tresult PluginInstance::attachView(HWND hwnd)
{
    if (!view_) return Steinberg::kInternalError;
    return view_->attached(
        static_cast<void*>(hwnd),
        Steinberg::kPlatformTypeHWND);
}

Steinberg::tresult PluginInstance::removeView()
{
    if (!view_) return Steinberg::kResultOk;
    Steinberg::tresult res = view_->removed();
    view_->release();
    view_ = nullptr;
    return res;
}

Steinberg::tresult PluginInstance::getViewSize(Steinberg::ViewRect& rect)
{
    if (!view_) return Steinberg::kInternalError;
    return view_->getSize(&rect);
}

} // namespace vst3bridge
