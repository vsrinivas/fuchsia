// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddk/binding.h>
#include <ddk/device.h>
#include <ddk/protocol/intel-hda-codec.h>
#include <ddk/protocol/intel-hda-dsp.h>
#include <ddktl/device.h>
#include <fbl/mutex.h>
#include <fbl/unique_ptr.h>
#include <fbl/vmo_mapper.h>

#include <sync/completion.h>
#include <stdint.h>
#include <string.h>
#include <threads.h>

#include <intel-hda/utils/intel-audio-dsp-ipc.h>
#include <intel-hda/utils/intel-hda-registers.h>
#include <intel-hda/utils/utils.h>

#include "debug-logging.h"
#include "intel-dsp-ipc.h"

namespace audio {
namespace intel_hda {

class IntelAudioDsp;
using IntelAudioDspDeviceType = ddk::Device<IntelAudioDsp, ddk::Unbindable>;

class IntelAudioDsp : public IntelAudioDspDeviceType {
public:
    static fbl::unique_ptr<IntelAudioDsp> Create(zx_device_t* hda_dev);

    const char*  log_prefix() const { return log_prefix_; }

    // Mailbox constants
    static constexpr size_t MAILBOX_SIZE = 0x1000;

    // IPC helper methods
    void SendIpcMessage(const IpcMessage& message) {
        // HIPCIE must be programmed before setting HIPCI.BUSY
        REG_WR(&regs()->hipcie, message.extension);
        REG_WR(&regs()->hipci, message.primary | ADSP_REG_HIPCI_BUSY);
    }
    void IpcMailboxWrite(const void* data, size_t size) {
        mailbox_out_.Write(data, size);
    }
    void IpcMailboxRead(void* data, size_t size) {
        mailbox_in_.Read(data, size);
    }

    zx_status_t DriverBind() __WARN_UNUSED_RESULT;
    void        DeviceShutdown();

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

    zx_status_t GetModulesInfo();
    zx_status_t SetupPipelines();
    zx_status_t RunPipeline(uint8_t pipeline_id);

    bool IsCoreEnabled(uint8_t core_mask);

    zx_status_t ResetCore(uint8_t core_mask);
    zx_status_t UnResetCore(uint8_t core_mask);
    zx_status_t PowerDownCore(uint8_t core_mask);
    zx_status_t PowerUpCore(uint8_t core_mask);
    void        RunCore(uint8_t core_mask);

    void EnableInterrupts();

    // Interrupt handler.
    void ProcessIrq();

    // Debug
    void DumpFirmwareConfig(const TLVHeader* config, size_t length);
    void DumpHardwareConfig(const TLVHeader* config, size_t length);
    void DumpModulesInfo(const ModuleEntry* info, uint32_t count);
    void DumpPipelineListInfo(const PipelineListInfo* info);
    void DumpPipelineProps(const PipelineProps* props);

    enum class State : uint8_t {
        START,
        INITIALIZING,  // init thread running
        OPERATING,
        SHUT_DOWN,
        ERROR = 0xFF,
    };
    State state_ = State::START;

    // IPC
    IntelDspIpc ipc_;

    // IPC Mailboxes
    class Mailbox {
    public:
        void Initialize(void* base, size_t size) {
            base_ = base;
            size_ = size;
        }

        size_t size() const { return size_; }

        void Write(const void* data, size_t size) {
            // It is the caller's responsibility to ensure size fits in the mailbox.
            ZX_DEBUG_ASSERT(size <= size_);
            memcpy(base_, data, size);
        }
        void Read(void* data, size_t size) {
            // It is the caller's responsibility to ensure size fits in the mailbox.
            ZX_DEBUG_ASSERT(size <= size_);
            memcpy(data, base_, size);
        }
    private:
        void*  base_;
        size_t size_;
    };
    Mailbox mailbox_in_;
    Mailbox mailbox_out_;

    // Module IDs
    enum Module {
        COPIER,
        MIXIN,
        MIXOUT,
        MODULE_COUNT,
    };
    static constexpr uint16_t MODULE_ID_INVALID = 0xFFFF;
    uint16_t module_ids_[to_underlying(Module::MODULE_COUNT)];

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
