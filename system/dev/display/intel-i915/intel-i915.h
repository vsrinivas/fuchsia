// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#if __cplusplus

#include <ddk/protocol/pci.h>

#include <fbl/unique_ptr.h>
#include <fbl/vector.h>
#include <hwreg/mmio.h>
#include <threads.h>

#include "display-device.h"
#include "gtt.h"
#include "igd.h"
#include "power.h"
#include "registers.h"
#include "registers-ddi.h"
#include "registers-pipe.h"
#include "registers-transcoder.h"

namespace i915 {

class Controller;
using DeviceType = ddk::Device<Controller, ddk::Unbindable, ddk::Suspendable, ddk::Resumable>;

class Controller : public DeviceType {
public:
    Controller(zx_device_t* parent);
    ~Controller();

    void DdkUnbind();
    void DdkRelease();
    zx_status_t DdkSuspend(uint32_t reason);
    zx_status_t DdkResume(uint32_t reason);
    zx_status_t Bind(fbl::unique_ptr<i915::Controller>* controller_ptr);

    pci_protocol_t* pci() { return &pci_; }
    hwreg::RegisterIo* mmio_space() { return mmio_space_.get(); }
    Gtt* gtt() { return &gtt_; }
    uint16_t device_id() const { return device_id_; }
    const IgdOpRegion& igd_opregion() const { return igd_opregion_; }
    Power* power() { return &power_; }

    int IrqLoop();

    void ResetPipe(registers::Pipe pipe);
    bool ResetTrans(registers::Trans trans);
    bool ResetDdi(registers::Ddi ddi);

private:
    void EnableBacklight(bool enable);
    zx_status_t InitHotplug();
    void EnableHotplugInterrupts();
    zx_status_t InitDisplays();
    fbl::unique_ptr<DisplayDevice> InitDisplay(registers::Ddi ddi);
    zx_status_t AddDisplay(fbl::unique_ptr<DisplayDevice>&& display);
    bool BringUpDisplayEngine(bool resume);
    void AllocDisplayBuffers();
    void HandleHotplug(registers::Ddi ddi);

    Gtt gtt_;
    IgdOpRegion igd_opregion_;

    pci_protocol_t pci_;

    fbl::unique_ptr<hwreg::RegisterIo> mmio_space_;
    zx_handle_t regs_handle_;

    zx_handle_t irq_;
    thrd_t irq_thread_;

    // References to displays. References are owned by devmgr, but will always
    // be valid while they are in this vector.
    fbl::Vector<DisplayDevice*> display_devices_;

    Power power_;
    PowerWellRef cd_clk_power_well_;

    uint16_t device_id_;
    uint32_t flags_;

    // Various configuration values set by the BIOS which need to be carried across suspend.
    uint32_t pp_divisor_val_;
    uint32_t pp_off_delay_val_;
    uint32_t pp_on_delay_val_;
    uint32_t sblc_ctrl2_val_;
    uint32_t schicken1_val_;
    bool ddi_a_lane_capability_control_;
    bool sblc_polarity_;
};

} // namespace i915

#endif // __cplusplus

__BEGIN_CDECLS
zx_status_t intel_i915_bind(void* ctx, zx_device_t* parent);
__END_CDECLS
