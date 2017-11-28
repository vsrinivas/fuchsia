// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddktl/device.h>
#include <ddktl/protocol/display.h>

#include <zx/vmo.h>

namespace i915 {

class Controller;

class DisplayDevice;
using DisplayDeviceType = ddk::Device<DisplayDevice>;

class DisplayDevice : public DisplayDeviceType, public ddk::DisplayProtocol<DisplayDevice> {
public:
    DisplayDevice(Controller* device);
    virtual ~DisplayDevice();

    void DdkRelease();

    zx_status_t SetMode(zx_display_info_t* info);
    zx_status_t GetMode(zx_display_info_t* info);
    zx_status_t GetFramebuffer(void** framebuffer);
    void Flush();

    bool Init();

    uintptr_t framebuffer() { return framebuffer_; }
    uint32_t framebuffer_size() { return framebuffer_size_; }
    const zx_display_info_t& info() { return info_; }

protected:
    virtual bool Init(zx_display_info_t* info) = 0;

private:
    // Borrowed reference to Controller instance
    Controller* controller_;

    uintptr_t framebuffer_;
    uint32_t framebuffer_size_;
    zx::vmo framebuffer_vmo_;

    zx_display_info_t info_;
};

} // namespace i915
