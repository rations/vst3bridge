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
 * @file gui_event_receiver.cpp
 * @brief GUI event receiver implementation for Wine side
 */

#include "gui_event_receiver.h"
#include "wine_socket_client.h"
#include <cstring>
#include <iostream>

namespace vst3bridge {

GUIEventReceiver::GUIEventReceiver(WineSocketClient* socket, HWND plugin_window)
    : socket_(socket)
    , plugin_window_(plugin_window) {
}

GUIEventReceiver::~GUIEventReceiver() {
    stop();
}

bool GUIEventReceiver::start() {
    if (running_.load()) {
        return true;
    }

    if (!plugin_window_ || !IsWindow(plugin_window_)) {
        std::cerr << "[GUIEventReceiver] Invalid plugin window\n";
        return false;
    }

    stop_requested_ = false;
    running_ = true;

    receive_thread_ = std::thread(&GUIEventReceiver::receiveLoop, this);
    return true;
}

void GUIEventReceiver::stop() {
    if (!running_.load()) {
        return;
    }

    stop_requested_ = true;

    if (receive_thread_.joinable()) {
        receive_thread_.join();
    }

    running_ = false;
}

void GUIEventReceiver::receiveLoop() {
    while (!stop_requested_.load()) {
        // Wait for GUI event message from native side
        GenericMessage msg;
        if (!socket_->receiveMessage(msg, 50)) {  // 50ms timeout
            continue;
        }

        // Process GUI event
        if (msg.payload.size() >= sizeof(GUIEventMessage)) {
            const auto* event = reinterpret_cast<const GUIEventMessage*>(msg.payload.data());
            processEvent(*event);
        }
    }
}

void GUIEventReceiver::processEvent(const GUIEventMessage& event) {
    if (!plugin_window_ || !IsWindow(plugin_window_)) {
        return;
    }

    switch (event.type) {
        case GUIEventType::MouseMove:
        case GUIEventType::MouseDown:
        case GUIEventType::MouseUp:
        case GUIEventType::MouseDoubleClick:
        case GUIEventType::MouseWheel:
            sendMouseEvent(event.data.mouse, event.type);
            break;

        case GUIEventType::KeyDown:
        case GUIEventType::KeyUp:
            sendKeyEvent(event.data.key, event.type);
            break;

        case GUIEventType::Resize: {
            // Send resize message to plugin window
            SetWindowPos(plugin_window_, nullptr, 0, 0,
                        static_cast<int>(event.data.resize.width),
                        static_cast<int>(event.data.resize.height),
                        SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
            break;
        }

        case GUIEventType::FocusIn: {
            SetFocus(plugin_window_);
            break;
        }

        case GUIEventType::FocusOut: {
            // Optionally clear focus
            break;
        }
    }
}

void GUIEventReceiver::sendMouseEvent(const MouseEvent& event, GUIEventType type) {
    if (!plugin_window_ || !IsWindow(plugin_window_)) {
        return;
    }

    // Update last known position
    last_mouse_x_ = event.x;
    last_mouse_y_ = event.y;

    POINT pt = { event.x, event.y };
    DWORD flags = modifiersToFlags(event.modifiers);

    switch (type) {
        case GUIEventType::MouseMove: {
            // Send WM_MOUSEMOVE
            LPARAM lParam = MAKELPARAM(event.x, event.y);
            PostMessage(plugin_window_, WM_MOUSEMOVE, flags, lParam);
            break;
        }

        case GUIEventType::MouseDown: {
            UINT msg;
            switch (event.button) {
                case MouseButton::Left: msg = WM_LBUTTONDOWN; break;
                case MouseButton::Right: msg = WM_RBUTTONDOWN; break;
                case MouseButton::Middle: msg = WM_MBUTTONDOWN; break;
                case MouseButton::XButton1: 
                case MouseButton::XButton2: msg = WM_XBUTTONDOWN; break;
                default: msg = WM_LBUTTONDOWN; break;
            }
            
            LPARAM lParam = MAKELPARAM(event.x, event.y);
            WPARAM wParam = flags | mouseButtonToFlag(event.button);
            if (event.button == MouseButton::XButton1 || event.button == MouseButton::XButton2) {
                wParam |= (event.button == MouseButton::XButton1) ? XBUTTON1 : XBUTTON2;
            }
            
            PostMessage(plugin_window_, msg, wParam, lParam);
            break;
        }

        case GUIEventType::MouseUp: {
            UINT msg;
            switch (event.button) {
                case MouseButton::Left: msg = WM_LBUTTONUP; break;
                case MouseButton::Right: msg = WM_RBUTTONUP; break;
                case MouseButton::Middle: msg = WM_MBUTTONUP; break;
                case MouseButton::XButton1: 
                case MouseButton::XButton2: msg = WM_XBUTTONUP; break;
                default: msg = WM_LBUTTONUP; break;
            }
            
            LPARAM lParam = MAKELPARAM(event.x, event.y);
            WPARAM wParam = flags | mouseButtonToFlag(event.button);
            if (event.button == MouseButton::XButton1 || event.button == MouseButton::XButton2) {
                wParam |= (event.button == MouseButton::XButton1) ? XBUTTON1 : XBUTTON2;
            }
            
            PostMessage(plugin_window_, msg, wParam, lParam);
            break;
        }

        case GUIEventType::MouseDoubleClick: {
            UINT msg;
            switch (event.button) {
                case MouseButton::Left: msg = WM_LBUTTONDBLCLK; break;
                case MouseButton::Right: msg = WM_RBUTTONDBLCLK; break;
                case MouseButton::Middle: msg = WM_MBUTTONDBLCLK; break;
                case MouseButton::XButton1: 
                case MouseButton::XButton2: msg = WM_XBUTTONDBLCLK; break;
                default: msg = WM_LBUTTONDBLCLK; break;
            }
            
            LPARAM lParam = MAKELPARAM(event.x, event.y);
            WPARAM wParam = flags | mouseButtonToFlag(event.button);
            if (event.button == MouseButton::XButton1 || event.button == MouseButton::XButton2) {
                wParam |= (event.button == MouseButton::XButton1) ? XBUTTON1 : XBUTTON2;
            }
            
            PostMessage(plugin_window_, msg, wParam, lParam);
            break;
        }

        case GUIEventType::MouseWheel: {
            // Send WM_MOUSEWHEEL or WM_MOUSEHWHEEL
            UINT msg = (event.wheel_axis == 1) ? WM_MOUSEHWHEEL : WM_MOUSEWHEEL;
            int delta = static_cast<int>(event.wheel_delta * WHEEL_DELTA);
            WPARAM wParam = MAKEWPARAM(flags, delta);
            LPARAM lParam = MAKELPARAM(event.x, event.y);
            
            PostMessage(plugin_window_, msg, wParam, lParam);
            break;
        }
    }
}

void GUIEventReceiver::sendKeyEvent(const KeyEvent& event, GUIEventType type) {
    if (!plugin_window_ || !IsWindow(plugin_window_)) {
        return;
    }

    UINT msg = (type == GUIEventType::KeyDown) ? WM_KEYDOWN : WM_KEYUP;
    
    // Create lParam
    LPARAM lParam = 1;  // Repeat count = 1
    lParam |= (event.scan_code << 16);  // Scan code
    lParam |= (1 << 24);  // Extended key (set for most keys)
    
    if (type == GUIEventType::KeyUp) {
        lParam |= (1 << 30);  // Previous key state (1 for up)
        lParam |= (1 << 31);  // Transition state (1 for up)
    }

    WPARAM wParam = event.key_code;
    
    PostMessage(plugin_window_, msg, wParam, lParam);

    // Also send WM_CHAR for text input if it's a key down event
    if (type == GUIEventType::KeyDown && event.character != 0) {
        PostMessage(plugin_window_, WM_CHAR, event.character, lParam);
    }
}

DWORD GUIEventReceiver::mouseButtonToFlag(MouseButton button) const {
    switch (button) {
        case MouseButton::Left: return MK_LBUTTON;
        case MouseButton::Right: return MK_RBUTTON;
        case MouseButton::Middle: return MK_MBUTTON;
        case MouseButton::XButton1: 
        case MouseButton::XButton2: return MK_XBUTTON1;  // Will add XBUTTON2 if needed
        default: return 0;
    }
}

DWORD GUIEventReceiver::modifiersToFlags(const ModifierKeys& modifiers) const {
    DWORD flags = 0;
    if (modifiers.shift) flags |= MK_SHIFT;
    if (modifiers.ctrl) flags |= MK_CONTROL;
    // Note: Alt and Super don't have direct MK_ flags in Windows
    // They would be handled differently or sent as separate messages
    return flags;
}

} // namespace vst3bridge
