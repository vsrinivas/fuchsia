// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddk/protocol/display-controller.h>
#include <ddktl/device.h>
#include <hwreg/mmio.h>
#include <lib/edid/edid.h>
#include <region-alloc/region-alloc.h>
#include <lib/zx/vmo.h>

#include "gtt.h"
#include "power.h"
#include "registers-ddi.h"
#include "registers-pipe.h"
#include "registers-transcoder.h"

namespace i915 {

class Controller;
class DisplayDevice;

// Thread safe weak-ref to the DisplayDevice, because the backlight device
// lifecycle is managed by devmgr but the DisplayDevice lifecycle is managed
// by the display controller class.
typedef struct display_ref {
    mtx_t mtx;
    DisplayDevice* display_device __TA_GUARDED(mtx);
} display_ref_t;


class DisplayDevice {
public:
    DisplayDevice(Controller* device, uint64_t id,
                  registers::Ddi ddi, registers::Trans trans, registers::Pipe pipe);
    virtual ~DisplayDevice();

    void ApplyConfiguration(const display_config_t* config, registers::pipe_arming_regs* regs);
    void ClearConfig();

    bool Init();
    bool Resume();
    // Method to allow the display device to handle hotplug events. Returns
    // true if the device can handle the event without disconnecting. Otherwise
    // the device will be removed.
    virtual bool HandleHotplug(bool long_pulse) { return false; }

    uint64_t id() const { return id_; }
    registers::Ddi ddi() const { return ddi_; }
    registers::Pipe pipe() const { return pipe_; }
    registers::Trans trans() const { return trans_; }
    Controller* controller() { return controller_; }
    const edid::Edid& edid() { return edid_; }

    uint32_t width() const { return info_.v_addressable; }
    uint32_t height() const { return info_.h_addressable; }
    uint32_t format() const { return ZX_PIXEL_FORMAT_ARGB_8888; }

    virtual bool HasBacklight() { return false; }
    virtual void SetBacklightState(bool power, uint8_t brightness) {}
    virtual void GetBacklightState(bool* power, uint8_t* brightness) {}

    virtual bool CheckDisplayLimits(const display_config_t* config) = 0;

protected:
    // Queries the DisplayDevice to see if there is a supported display attached. If
    // there is, then returns true and populates |edid| and |info|.
    virtual bool QueryDevice(edid::Edid* edid) = 0;
    // Configures the hardware to display a framebuffer at the preferred resolution.
    virtual bool ConfigureDdi() = 0;

    hwreg::RegisterIo* mmio_space() const;
    const display_mode_t& mode() const { return info_; }

private:
    void ResetPipe();
    bool ResetTrans();
    bool ResetDdi();

    void ConfigurePrimaryPlane(uint32_t plane_num, const primary_layer_t* primary,
                               bool enable_csc, bool* scaler_1_claimed,
                               registers::pipe_arming_regs* regs);
    void ConfigureCursorPlane(const cursor_layer_t* cursor, bool enable_csc,
                              registers::pipe_arming_regs* regs);
    void SetColorConversionOffsets(bool preoffsets, const float vals[3]);

    // Borrowed reference to Controller instance
    Controller* controller_;

    uint64_t id_;
    registers::Ddi ddi_;
    registers::Trans trans_;
    registers::Pipe pipe_;

    PowerWellRef ddi_power_;
    PowerWellRef pipe_power_;

    bool inited_ = false;
    display_mode_t info_ = {};
    edid::Edid edid_;

    // For any scaled planes, this contains the (1-based) index of the active scaler
    uint32_t scaled_planes_[registers::kPipeCount][registers::kImagePlaneCount] = {};

    zx_device_t* backlight_device_ = nullptr;
    display_ref_t* display_ref_ = nullptr;
};

} // namespace i915
