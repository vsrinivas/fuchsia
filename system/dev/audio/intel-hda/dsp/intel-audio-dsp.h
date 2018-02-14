// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddk/binding.h>
#include <ddk/device.h>
#include <ddk/protocol/intel-hda-codec.h>
#include <ddk/protocol/intel-hda-dsp.h>
#include <ddktl/device.h>
#include <fbl/unique_ptr.h>
#include <fbl/vmo_mapper.h>
#include <stdint.h>
#include <string.h>
#include <threads.h>

#include <intel-hda/utils/intel-hda-registers.h>
#include <intel-hda/utils/utils.h>

#include "debug-logging.h"

namespace audio {
namespace intel_hda {

class IntelHDAController;

class IntelAudioDsp;
using IntelAudioDspDeviceType = ddk::Device<IntelAudioDsp, ddk::Unbindable>;

class IntelAudioDsp : public IntelAudioDspDeviceType {
public:
    static fbl::unique_ptr<IntelAudioDsp> Create(zx_device_t* hda_dev);

    const char*  log_prefix() const { return log_prefix_; }

    zx_status_t DriverBind() __WARN_UNUSED_RESULT;

    void Shutdown();

    // Ddktl device interface implementation
    void DdkUnbind();
    void DdkRelease();

private:
    friend class fbl::unique_ptr<IntelAudioDsp>;

    IntelAudioDsp(zx_device_t* hda_dev);
    ~IntelAudioDsp() { }

    // Accessor for our mapped registers
    adsp_registers_t* regs() const {
        return reinterpret_cast<adsp_registers_t*>(mapped_regs_.start());
    }
    adsp_fw_registers_t* fw_regs() const;

    int InitThread();

    zx_status_t Boot();
    zx_status_t LoadFirmware();

    bool IsCoreEnabled(uint8_t core_mask);

    zx_status_t ResetCore(uint8_t core_mask);
    zx_status_t UnResetCore(uint8_t core_mask);
    zx_status_t PowerDownCore(uint8_t core_mask);
    zx_status_t PowerUpCore(uint8_t core_mask);
    void        RunCore(uint8_t core_mask);

    void EnableInterrupts();

    // Interrupt handler.
    void ProcessIrq();

    enum class State : uint8_t {
        START,
        INITIALIZING,  // init thread running
        OPERATING,
        SHUT_DOWN,
        ERROR = 0xFF,
    };
    State state_ = State::START;

    // Init thread
    thrd_t init_thread_;

    // Log prefix storage
    char log_prefix_[LOG_PREFIX_STORAGE] = { 0 };

    // Upstream HDA DSP protocol interface.
    ihda_dsp_protocol_t ihda_dsp_;

    // PCI registers
    fbl::VmoMapper mapped_regs_;

    // A reference to our controller's BTI. This is needed to load firmware to the DSP.
    fbl::RefPtr<RefCountedBti> hda_bti_;
};

}  // namespace intel_hda
}  // namespace audio
