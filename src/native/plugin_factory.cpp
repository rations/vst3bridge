/*
 * Copyright (C) 2026
 * VST3 Bridge - Wine VST3 Host Bridge
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 */

/**
 * @file plugin_factory.cpp
 * @brief Native-side VST3 factory implementation
 * 
 * Proxies VST3 factory calls to the Wine host via IPC.
 */

#include "plugin_factory.h"
#include <cstring>
#include <iostream>

namespace vst3bridge {

using namespace Steinberg;

// Helper function to convert FIDString to TUID
static void stringToTUID(const char* str, TUID& tuid) {
    for (int i = 0; i < 16; ++i) {
        unsigned int byte = 0;
        if (sscanf(str + i * 2, "%2x", &byte) == 1) {
            tuid[i] = static_cast<int8>(byte);
        }
    }
}

PluginFactory::PluginFactory(std::shared_ptr<MessageSocket> socket)
    : socket_(socket) {
}

PluginFactory::~PluginFactory() {
    socket_->close();
}

tresult PLUGIN_API PluginFactory::getFactoryInfo(PFactoryInfo* info) {
    if (!info) return kInvalidArgument;

    // Send factory info request
    MsgRequestFactoryInfo request;
    if (!socket_->sendMessage(MsgType::RequestFactoryInfo, 
                              &request, sizeof(request))) {
        return kInternalError;
    }

    // Receive response
    MsgResponseFactoryInfo response;
    if (!socket_->receiveMessage(response)) {
        return kInternalError;
    }

    std::memcpy(info, &response.info, sizeof(PFactoryInfo));
    return kResultOk;
}

int32 PLUGIN_API PluginFactory::countClasses() {
    MsgRequestClassCount request;
    if (!socket_->sendMessage(MsgType::RequestClassCount,
                              &request, sizeof(request))) {
        return 0;
    }

    MsgResponseClassCount response;
    if (!socket_->receiveMessage(response)) {
        return 0;
    }

    return response.count;
}

tresult PLUGIN_API PluginFactory::getClassInfo(int32 index, PClassInfo* info) {
    if (!info || index < 0) return kInvalidArgument;

    MsgRequestClassInfo request;
    request.index = index;

    if (!socket_->sendMessage(MsgType::RequestClassInfo,
                              &request, sizeof(request))) {
        return kInternalError;
    }

    MsgResponseClassInfo response;
    if (!socket_->receiveMessage(response)) {
        return kInternalError;
    }

    if (response.success) {
        std::memcpy(info, &response.info, sizeof(PClassInfo));
        return kResultOk;
    }

    return kInvalidArgument;
}

tresult PLUGIN_API PluginFactory::createInstance(FIDString cid, FIDString _iid, void** obj) {
    if (!obj) return kInvalidArgument;
    *obj = nullptr;

    MsgRequestCreateInstance request;
    std::memcpy(request.cid, cid, sizeof(request.cid));
    std::memcpy(request.iid, _iid, sizeof(request.iid));

    if (!socket_->sendMessage(MsgType::CreateInstance,
                              &request, sizeof(request))) {
        return kInternalError;
    }

    MsgResponseCreateInstance response;
    if (!socket_->receiveMessage(response)) {
        return kInternalError;
    }

    if (!response.success) {
        return kNoInterface;
    }

    // Create native proxy for the instance
    // The Wine host returns an instance ID that we use to reference it
    auto proxy = std::make_unique<PluginProxy>(socket_, response.instance_id);

    // Query for requested interface
    TUID iid;
    stringToTUID(_iid, iid);
    tresult result = proxy->queryInterface(iid, obj);
    if (result == kResultOk) {
        // Object is referenced, release our local reference
        proxy.release();
    }

    return result;
}

tresult PLUGIN_API PluginFactory::getClassInfo2(int32 index, PClassInfo2* info) {
    if (!info || index < 0) return kInvalidArgument;

    MsgRequestClassInfo2 request;
    request.index = index;

    if (!socket_->sendMessage(MsgType::RequestClassInfo2,
                              &request, sizeof(request))) {
        return kInternalError;
    }

    MsgResponseClassInfo2 response;
    if (!socket_->receiveMessage(response)) {
        return kInternalError;
    }

    if (response.success) {
        std::memcpy(info, &response.info, sizeof(PClassInfo2));
        return kResultOk;
    }

    return kInvalidArgument;
}

tresult PLUGIN_API PluginFactory::getClassInfoUnicode(int32 index, PClassInfoW* info) {
    if (!info || index < 0) return kInvalidArgument;

    MsgRequestClassInfoW request;
    request.index = index;

    if (!socket_->sendMessage(MsgType::RequestClassInfoW,
                              &request, sizeof(request))) {
        return kInternalError;
    }

    MsgResponseClassInfoW response;
    if (!socket_->receiveMessage(response)) {
        return kInternalError;
    }

    if (response.success) {
        std::memcpy(info, &response.info, sizeof(PClassInfoW));
        return kResultOk;
    }

    return kInvalidArgument;
}

tresult PLUGIN_API PluginFactory::setHostContext(FUnknown* context) {
    // Store host context for later use
    host_context_ = context;
    if (host_context_) {
        host_context_->addRef();
    }

    // Notify Wine host
    MsgRequestSetHostContext request;
    // Context pointer is not valid in Wine, so we just notify
    // The Wine host will create its own host context proxy

    if (!socket_->sendMessage(MsgType::SetHostContext,
                              &request, sizeof(request))) {
        return kInternalError;
    }

    MsgResponseSetHostContext response;
    if (!socket_->receiveMessage(response)) {
        return kInternalError;
    }

    return response.success ? kResultOk : kInternalError;
}

// FUnknown implementation
tresult PLUGIN_API PluginFactory::queryInterface(const TUID _iid, void** obj) {
    if (!obj) return kInvalidArgument;
    *obj = nullptr;

    FUID requested = FUID::fromTUID(_iid);

    if (requested == IPluginFactory::iid || requested == FUnknown::iid) {
        *obj = static_cast<IPluginFactory*>(this);
        addRef();
        return kResultOk;
    }

    if (requested == IPluginFactory2::iid) {
        *obj = static_cast<IPluginFactory2*>(this);
        addRef();
        return kResultOk;
    }

    if (requested == IPluginFactory3::iid) {
        *obj = static_cast<IPluginFactory3*>(this);
        addRef();
        return kResultOk;
    }

    return kNoInterface;
}

uint32 PLUGIN_API PluginFactory::addRef() {
    return ++ref_count_;
}

uint32 PLUGIN_API PluginFactory::release() {
    uint32 result = --ref_count_;
    if (result == 0) {
        delete this;
    }
    return result;
}

} // namespace vst3bridge
