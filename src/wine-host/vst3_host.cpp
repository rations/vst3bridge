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
 * @file vst3_host.cpp
 * @brief Wine-side VST3 plugin loading implementation.
 */

#include "vst3_host.h"
#include "logger.h"

namespace vst3bridge {

// ============================================================================
// Destructor
// ============================================================================

VST3Host::~VST3Host() {
    unloadPlugin();
}

// ============================================================================
// loadPlugin
// ============================================================================

bool VST3Host::loadPlugin(const wchar_t* path) {
    if (!path || path[0] == L'\0') {
        LOG_ERROR("VST3Host::loadPlugin(): path is null or empty");
        return false;
    }

    if (module_) {
        LOG_WARN("VST3Host::loadPlugin(): a plugin is already loaded, unloading first");
        unloadPlugin();
    }

    // ---- 1. Load the DLL ----------------------------------------------------

    module_ = LoadLibraryW(path);
    if (!module_) {
        const DWORD err = GetLastError();
        LOG_ERROR("VST3Host: LoadLibraryW failed (error {})", static_cast<uint32_t>(err));
        return false;
    }

    LOG_INFO("VST3Host: loaded DLL successfully");

    // ---- 2. Resolve optional module lifecycle symbols -----------------------

    moduleInit_ = reinterpret_cast<ModuleInitProc>(
        GetProcAddress(module_, "ModuleInit"));
    moduleExit_ = reinterpret_cast<ModuleExitProc>(
        GetProcAddress(module_, "ModuleExit"));

    if (moduleInit_) {
        LOG_DEBUG("VST3Host: calling ModuleInit()");
        if (!moduleInit_()) {
            LOG_ERROR("VST3Host: ModuleInit() returned false");
            FreeLibrary(module_);
            module_     = nullptr;
            moduleInit_ = nullptr;
            moduleExit_ = nullptr;
            return false;
        }
    }

    // ---- 3. Get the GetPluginFactory entry point ----------------------------

    using GetFactoryProc = Steinberg::IPluginFactory* (PLUGIN_API*)();
    auto getFactoryProc = reinterpret_cast<GetFactoryProc>(
        GetProcAddress(module_, "GetPluginFactory"));

    if (!getFactoryProc) {
        LOG_ERROR("VST3Host: 'GetPluginFactory' symbol not found in DLL");
        unloadPlugin();
        return false;
    }

    // ---- 4. Call the entry point and validate the factory -------------------

    factory_ = getFactoryProc();
    if (!factory_) {
        LOG_ERROR("VST3Host: GetPluginFactory() returned nullptr");
        unloadPlugin();
        return false;
    }

    // The factory returned by GetPluginFactory() has its reference count
    // already at 1 (the ownership is transferred to us).  We do NOT call
    // addRef() here.

    // ---- 5. Query extended factory interfaces -------------------------------

    {
        Steinberg::TUID iid2;
        Steinberg::IPluginFactory2::iid.toTUID(iid2);
        void* ptr = nullptr;
        if (factory_->queryInterface(iid2, &ptr) == Steinberg::kResultOk) {
            factory2_ = static_cast<Steinberg::IPluginFactory2*>(ptr);
            // ptr already addRef()'d by queryInterface; we own that reference
            LOG_DEBUG("VST3Host: IPluginFactory2 available");
        }
    }

    {
        Steinberg::TUID iid3;
        Steinberg::IPluginFactory3::iid.toTUID(iid3);
        void* ptr = nullptr;
        if (factory_->queryInterface(iid3, &ptr) == Steinberg::kResultOk) {
            factory3_ = static_cast<Steinberg::IPluginFactory3*>(ptr);
            LOG_DEBUG("VST3Host: IPluginFactory3 available");
        }
    }

    // ---- 6. Log factory info for diagnostics --------------------------------

    Steinberg::PFactoryInfo fi;
    if (factory_->getFactoryInfo(&fi) == Steinberg::kResultOk) {
        LOG_INFO("VST3Host: vendor='{}' url='{}' classes={}",
                 fi.vendor,
                 fi.url,
                 factory_->countClasses());
    }

    return true;
}

// ============================================================================
// unloadPlugin
// ============================================================================

void VST3Host::unloadPlugin() {
    // Release extended factory interfaces first
    if (factory3_) {
        factory3_->release();
        factory3_ = nullptr;
    }
    if (factory2_) {
        factory2_->release();
        factory2_ = nullptr;
    }

    // Release the base factory
    if (factory_) {
        factory_->release();
        factory_ = nullptr;
    }

    // Call optional module exit before unloading
    if (moduleExit_) {
        LOG_DEBUG("VST3Host: calling ModuleExit()");
        moduleExit_();
        moduleExit_ = nullptr;
    }
    moduleInit_ = nullptr;

    // Unload the DLL
    if (module_) {
        FreeLibrary(module_);
        module_ = nullptr;
        LOG_INFO("VST3Host: plugin DLL unloaded");
    }
}

} // namespace vst3bridge
