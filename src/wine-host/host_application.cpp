/*
 * Copyright (C) 2026
 * VST3 Bridge - Wine VST3 Host Bridge
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 */

#include "host_application.h"
#include "logger.h"
#include <cstring>

namespace vst3bridge {

// ============================================================================
// Constants
// ============================================================================

/** Name reported to plugins via IHostApplication::getName(). */
static const char kHostName[] = "VST3 Bridge";

// ============================================================================
// FUnknown iid
// ============================================================================

const Steinberg::FUID HostApplication::iid(
        0xC2FCA94Du, 0x7B4E4A3Bu, 0xD5F8A921u, 0x4BE7D35Cu);

// ============================================================================
// Constructor
// ============================================================================

HostApplication::HostApplication()
    : refCount_(1)
{}

// ============================================================================
// FUnknown (addRef / release / queryInterface)
// ============================================================================

Steinberg::uint32 PLUGIN_API HostApplication::addRef() {
    return ++refCount_;
}

Steinberg::uint32 PLUGIN_API HostApplication::release() {
    Steinberg::uint32 r = --refCount_;
    if (r == 0) {
        delete this;
    }
    return r;
}

Steinberg::tresult PLUGIN_API HostApplication::queryInterface(
        const Steinberg::TUID _iid, void** obj)
{
    if (!obj) return Steinberg::kInvalidArgument;

    Steinberg::FUID requested = Steinberg::FUID::fromTUID(_iid);

    if (requested == Steinberg::FUnknown::iid ||
        requested == Steinberg::Vst::IHostApplication::iid ||
        requested == HostApplication::iid)
    {
        *obj = static_cast<Steinberg::Vst::IHostApplication*>(this);
        addRef();
        return Steinberg::kResultOk;
    }

    *obj = nullptr;
    return Steinberg::kNoInterface;
}

// ============================================================================
// IHostApplication
// ============================================================================

Steinberg::tresult PLUGIN_API HostApplication::getName(Steinberg::Vst::String128 name)
{
    if (!name) return Steinberg::kInvalidArgument;

    // Convert ASCII name to UTF-16 (host name is ASCII so direct cast is safe)
    const size_t len = std::char_traits<char>::length(kHostName);

    for (size_t i = 0; i < len && i < 127; ++i) {
        name[i] = static_cast<char16_t>(kHostName[i]);
    }
    name[std::min(len, static_cast<size_t>(127))] = u'\0';

    return Steinberg::kResultOk;
}

Steinberg::tresult PLUGIN_API HostApplication::createInstance(
        Steinberg::TUID  /*cid*/,
        Steinberg::TUID  /*_iid*/,
        void**           obj)
{
    // The bridge does not yet provide any host-created objects.
    // Well-behaved plugins handle kNotImplemented gracefully.
    if (obj) *obj = nullptr;
    return Steinberg::kNotImplemented;
}

} // namespace vst3bridge
