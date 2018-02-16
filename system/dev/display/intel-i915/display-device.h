// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddktl/device.h>
#include <ddktl/protocol/display.h>
#include <hwreg/mmio.h>
#include <region-alloc/region-alloc.h>
#include <zx/vmo.h>

#include "edid.h"
#include "gtt.h"
#include "registers-ddi.h"
#include "registers-dpll.h"
#include "registers-pipe.h"
#include "registers-transcoder.h"

namespace i915 {

class Controller;

class DisplayDevice;
using DisplayDeviceType = ddk::Device<DisplayDevice>;

class DisplayDevice : public DisplayDeviceType, public ddk::DisplayProtocol<DisplayDevice> {
public:
    DisplayDevice(Controller* device, registers::Ddi ddi, registers::Dpll dpll,
                  registers::Trans trans, registers::Pipe pipe);
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
    registers::Trans trans() const { return trans_; }
    registers::Dpll dpll() const { return dpll_; }
    const Controller* controller() { return controller_; }

protected:
    virtual bool Init(zx_display_info_t* info) = 0;

    hwreg::RegisterIo* mmio_space() const;
    bool EnablePowerWell2();
    void ResetPipe();
    bool ResetTrans();
    bool ResetDdi();

private:
    // Borrowed reference to Controller instance
    Controller* controller_;

    registers::Ddi ddi_;
    registers::Dpll dpll_;
    registers::Trans trans_;
    registers::Pipe pipe_;

    uintptr_t framebuffer_;
    uint32_t framebuffer_size_;
    zx::vmo framebuffer_vmo_;
    fbl::unique_ptr<const GttRegion> fb_gfx_addr_;

    bool inited_;
    zx_display_info_t info_;
};

} // namespace i915
