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
#include "pipe.h"
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
    DisplayDevice(Controller* device, uint64_t id, registers::Ddi ddi);
    virtual ~DisplayDevice();

    bool AttachPipe(Pipe* pipe);
    void ApplyConfiguration(const display_config_t* config);

    bool Init();
    bool Resume();
    // Method to allow the display device to handle hotplug events. Returns
    // true if the device can handle the event without disconnecting. Otherwise
    // the device will be removed.
    virtual bool HandleHotplug(bool long_pulse) { return false; }

    uint64_t id() const { return id_; }
    registers::Ddi ddi() const { return ddi_; }
    Controller* controller() { return controller_; }
    const edid::Edid& edid() { return edid_; }

    Pipe* pipe() const { return pipe_; }

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

    // Attaching a pipe to a display or configuring a pipe after display mode change has
    // 3 steps. The second step is generic pipe configuration, whereas PipeConfigPreamble
    // and PipeConfigEpilogue are responsible for display-type-specific configuration that
    // must be done before and after the generic configuration.
    virtual bool PipeConfigPreamble(registers::Pipe pipe, registers::Trans trans) = 0;
    virtual bool PipeConfigEpilogue(registers::Pipe pipe, registers::Trans trans) = 0;

    hwreg::RegisterIo* mmio_space() const;
    const display_mode_t& mode() const { return info_; }

private:
    bool ResetDdi();

    // Borrowed reference to Controller instance
    Controller* controller_;

    uint64_t id_;
    registers::Ddi ddi_;

    Pipe* pipe_= nullptr;

    PowerWellRef ddi_power_;

    bool inited_ = false;
    display_mode_t info_ = {};
    edid::Edid edid_;

    zx_device_t* backlight_device_ = nullptr;
    display_ref_t* display_ref_ = nullptr;
};

} // namespace i915
