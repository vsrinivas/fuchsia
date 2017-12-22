// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddktl/device.h>
#include <ddktl/protocol/display.h>
#include <hwreg/mmio.h>
#include <zx/vmo.h>

#include "edid.h"
#include "registers-ddi.h"
#include "registers-pipe.h"

namespace i915 {

class Controller;

class DisplayDevice;
using DisplayDeviceType = ddk::Device<DisplayDevice>;

class DisplayDevice : public DisplayDeviceType, public ddk::DisplayProtocol<DisplayDevice> {
public:
    DisplayDevice(Controller* device, registers::Ddi ddi, registers::Pipe pipe);
    virtual ~DisplayDevice();

    void DdkRelease();

    zx_status_t SetMode(zx_display_info_t* info);
    zx_status_t GetMode(zx_display_info_t* info);
    zx_status_t GetFramebuffer(void** framebuffer);
    void Flush();

    bool Init();

    const zx::vmo& framebuffer_vmo() const { return framebuffer_vmo_; }
    uint32_t framebuffer_size() const { return framebuffer_size_; }
    const zx_display_info_t& info() const { return info_; }
    registers::Ddi ddi() const { return ddi_; }
    registers::Pipe pipe() const { return pipe_; }
    int dpll() const {
        // Skip over dpll0 because changing it requires messing around with CDCLK
        return pipe_ + 1;
    }

protected:
    virtual bool Init(zx_display_info_t* info) = 0;

    hwreg::RegisterIo* mmio_space() const;
    bool EnablePowerWell2();
    bool ResetPipe();
    bool ResetDdi();

private:
    // Borrowed reference to Controller instance
    Controller* controller_;

    registers::Ddi ddi_;
    registers::Pipe pipe_;

    uintptr_t framebuffer_;
    uint32_t framebuffer_size_;
    zx::vmo framebuffer_vmo_;

    zx_display_info_t info_;
};

} // namespace i915
