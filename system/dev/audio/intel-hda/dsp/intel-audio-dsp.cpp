// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fbl/alloc_checker.h>

#include "intel-audio-dsp.h"
#include "intel-dsp-code-loader.h"

namespace audio {
namespace intel_hda {

namespace {

// ADSP SRAM windows
constexpr size_t SKL_ADSP_SRAM0_OFFSET  = 0x8000; // Shared between Skylake and Kabylake
__UNUSED constexpr size_t SKL_ADSP_SRAM1_OFFSET  = 0xA000;

// Mailbox offsets
__UNUSED constexpr size_t ADSP_MAILBOX_IN_OFFSET = 0x1000; // Section 5.5 Offset from SRAM0
__UNUSED constexpr size_t ADSP_MAILBOX_IN_SIZE   = 0x1000;
__UNUSED constexpr size_t ADSP_MAILBOX_OUT_SIZE  = 0x1000;

constexpr const char* ADSP_FIRMWARE_PATH = "/boot/lib/firmware/dsp_fw_kbl_v3266.bin";

constexpr zx_time_t INTEL_ADSP_TIMEOUT_NSEC              = ZX_MSEC( 50); // 50mS Arbitrary
constexpr zx_time_t INTEL_ADSP_POLL_NSEC                 = ZX_USEC(500); // 500uS Arbitrary
constexpr zx_time_t INTEL_ADSP_ROM_INIT_TIMEOUT_NSEC     = ZX_SEC (  1); // 1S Arbitrary
constexpr zx_time_t INTEL_ADSP_BASE_FW_INIT_TIMEOUT_NSEC = ZX_SEC (  3); // 3S Arbitrary
constexpr zx_time_t INTEL_ADSP_POLL_FW_NSEC              = ZX_MSEC(  1); //.1mS Arbitrary
}  // anon namespace

fbl::unique_ptr<IntelAudioDsp> IntelAudioDsp::Create(zx_device_t* hda_dev) {
    fbl::AllocChecker ac;
    fbl::unique_ptr<IntelAudioDsp> ret(new (&ac) IntelAudioDsp(hda_dev));
    if (!ac.check()) {
        GLOBAL_LOG(ERROR, "Out of memory attempting to allocate IHDA DSP\n");
        return nullptr;
    }
    return ret;
}

IntelAudioDsp::IntelAudioDsp(zx_device_t* hda_dev)
    : IntelAudioDspDeviceType(hda_dev) {

    snprintf(log_prefix_, sizeof(log_prefix_), "IHDA DSP (unknown BDF)");
}

adsp_fw_registers_t* IntelAudioDsp::fw_regs() const {
    return reinterpret_cast<adsp_fw_registers_t*>(static_cast<uint8_t*>(mapped_regs_.start()) +
                                                  SKL_ADSP_SRAM0_OFFSET);
}

zx_status_t IntelAudioDsp::DriverBind() {
    zx_status_t res = device_get_protocol(parent(), ZX_PROTOCOL_IHDA_DSP,
                                          reinterpret_cast<void*>(&ihda_dsp_));
    if (res != ZX_OK) {
        LOG(ERROR, "IHDA DSP device does not support IHDA DSP protocol (err %d)\n", res);
        return res;
    }

    zx_pcie_device_info_t hda_dev_info;
    ihda_dsp_get_dev_info(&ihda_dsp_, &hda_dev_info);
    snprintf(log_prefix_, sizeof(log_prefix_), "IHDA DSP %02x:%02x.%01x",
             hda_dev_info.bus_id,
             hda_dev_info.dev_id,
             hda_dev_info.func_id);

    // Fetch the bar which holds the Audio DSP registers.
    zx::vmo bar_vmo;
    size_t bar_size;
    res = ihda_dsp_get_mmio(&ihda_dsp_, bar_vmo.reset_and_get_address(), &bar_size);
    if (res != ZX_OK) {
        LOG(ERROR, "Failed to fetch DSP register VMO (err %d)\n", res);
        return res;
    }

    if (bar_size != sizeof(adsp_registers_t)) {
        LOG(ERROR, "Bad register window size (expected 0x%zx got 0x%zx)\n",
            sizeof(adsp_registers_t), bar_size);
        return res;
    }

    // Since this VMO provides access to our registers, make sure to set the
    // cache policy to UNCACHED_DEVICE
    res = bar_vmo.set_cache_policy(ZX_CACHE_POLICY_UNCACHED_DEVICE);
    if (res != ZX_OK) {
        LOG(ERROR, "Error attempting to set cache policy for PCI registers (res %d)\n", res);
        return res;
    }

    // Map the VMO in, make sure to put it in the same VMAR as the rest of our
    // registers.
    constexpr uint32_t CPU_MAP_FLAGS = ZX_VM_FLAG_PERM_READ | ZX_VM_FLAG_PERM_WRITE;
    res = mapped_regs_.Map(bar_vmo, 0, bar_size, CPU_MAP_FLAGS);
    if (res != ZX_OK) {
        LOG(ERROR, "Error attempting to map registers (res %d)\n", res);
        return res;
    }

    // Get bus transaction initiator
    zx::bti bti;
    res = ihda_dsp_get_bti(&ihda_dsp_, bti.reset_and_get_address());
    if (res != ZX_OK) {
        LOG(ERROR, "Failed to get BTI handle for IHDA DSP (res %d)\n", res);
        return res;
    }

    hda_bti_ = RefCountedBti::Create(fbl::move(bti));
    if (hda_bti_ == nullptr) {
        LOG(ERROR, "Out of memory while attempting to allocate BTI wrapper for IHDA DSP\n");
        return ZX_ERR_NO_MEMORY;
    }

    // Set IRQ handler and enable HDA interrupt.
    // Interrupts are still masked at the DSP level.
    res = ihda_dsp_irq_enable(&ihda_dsp_,
                              [](void* cookie) {
                                  auto thiz = static_cast<IntelAudioDsp*>(cookie);
                                  thiz->ProcessIrq();
                              }, this);
    if (res != ZX_OK) {
        LOG(ERROR, "Failed to set DSP interrupt callback (res %d)\n", res);
        return res;
    }

    state_ = State::INITIALIZING;

    // Perform hardware initializastion in a thread.
    int c11_res = thrd_create(
            &init_thread_,
            [](void* ctx) -> int { return static_cast<IntelAudioDsp*>(ctx)->InitThread(); },
            this);
    if (c11_res < 0) {
        LOG(ERROR, "Failed to create init thread (res = %d)\n", c11_res);
        state_ = State::ERROR;
        return ZX_ERR_INTERNAL;
    }

    // Add a device
    char dev_name[ZX_DEVICE_NAME_MAX] = { 0 };
    snprintf(dev_name, sizeof(dev_name), "intel-sst-dsp");

    res = DdkAdd(dev_name);
    if (res != ZX_OK) {
        LOG(ERROR, "Failed to add DSP device (err %d)\n", res);
        return res;
    }

    return ZX_OK;
}

void IntelAudioDsp::ProcessIrq() {
    ZX_DEBUG_ASSERT(state_ == State::OPERATING);

    // TODO(yky) Just ack the IRQ for now.
    REG_SET_BITS(&regs()->hipct, (1u << 31));
}

void IntelAudioDsp::Shutdown() {
    if (state_ == State::INITIALIZING) {
        thrd_join(init_thread_, NULL);
    }

    PowerDownCore(ADSP_REG_ADSPCS_CORE0_MASK);

    // Disable Audio DSP and interrupt
    ihda_dsp_irq_disable(&ihda_dsp_);
    ihda_dsp_disable(&ihda_dsp_);

    state_ = State::SHUT_DOWN;
}

void IntelAudioDsp::DdkUnbind() {
    Shutdown();
}

void IntelAudioDsp::DdkRelease() {
}

int IntelAudioDsp::InitThread() {
    // Enable Audio DSP
    ihda_dsp_enable(&ihda_dsp_);

    // The HW loads the DSP base firmware from ROM during the initialization,
    // when the Tensilica Core is out of reset, but halted.
    zx_status_t st = Boot();
    if (st != ZX_OK) {
        LOG(ERROR, "Error in DSP boot (err %d)\n", st);
        return -1;
    }

    // Wait for ROM initialization done
    st = WaitCondition(INTEL_ADSP_ROM_INIT_TIMEOUT_NSEC,
                       INTEL_ADSP_POLL_FW_NSEC,
                       [this]() -> bool {
                           return ((REG_RD(&fw_regs()->fw_status) & ADSP_FW_STATUS_STATE_MASK) ==
                                   ADSP_FW_STATUS_STATE_INITIALIZATION_DONE);
                       });
    if (st != ZX_OK) {
        LOG(ERROR, "Error waiting for DSP ROM init (err %d)\n", st);
        return -1;
    }

    state_ = State::OPERATING;
    EnableInterrupts();

    // Load DSP Firmware
    st = LoadFirmware();
    if (st != ZX_OK) {
        LOG(ERROR, "Error loading firmware (err %d)\n", st);
        return -1;
    }

    // DSP Firmware is now ready.
    LOG(INFO, "DSP firmware ready\n");
    return ZX_OK;
}

zx_status_t IntelAudioDsp::Boot() {
    zx_status_t st = ZX_OK;

    // Put core into reset
    if ((st = ResetCore(ADSP_REG_ADSPCS_CORE0_MASK)) != ZX_OK) {
        LOG(ERROR, "Error attempting to enter reset on core 0 (err %d)\n", st);
        return st;
    }

    // Power down core
    if ((st = PowerDownCore(ADSP_REG_ADSPCS_CORE0_MASK)) != ZX_OK) {
        LOG(ERROR, "Error attempting to power down core 0 (err %d)\n", st);
        return st;
    }

    // Power up core
    if ((st = PowerUpCore(ADSP_REG_ADSPCS_CORE0_MASK)) != ZX_OK) {
        LOG(ERROR, "Error attempting to power up core 0 (err %d)\n", st);
        return st;
    }

    // Take core out of reset
    if ((st = UnResetCore(ADSP_REG_ADSPCS_CORE0_MASK)) != ZX_OK) {
        LOG(ERROR, "Error attempting to take core 0 out of reset (err %d)\n", st);
        return st;
    }

    // Run core
    RunCore(ADSP_REG_ADSPCS_CORE0_MASK);
    if (!IsCoreEnabled(ADSP_REG_ADSPCS_CORE0_MASK)) {
        LOG(ERROR, "Failed to start core 0\n");
        ResetCore(ADSP_REG_ADSPCS_CORE0_MASK);
        return st;
    }

    LOG(TRACE, "DSP core 0 booted!\n");
    return ZX_OK;
}

zx_status_t IntelAudioDsp::LoadFirmware() {
    IntelDspCodeLoader loader(&regs()->cldma, hda_bti_);
    zx_status_t st = loader.Initialize();
    if (st != ZX_OK) {
        LOG(ERROR, "Error initializing firmware code loader (err %d)\n", st);
        return st;
    }

    // Get the VMO containing the firmware.
    zx::vmo fw_vmo;
    size_t fw_size;
    st = load_firmware(parent(), ADSP_FIRMWARE_PATH, fw_vmo.reset_and_get_address(), &fw_size);
    if (st != ZX_OK) {
        LOG(ERROR, "Error fetching firmware (err %d)\n", st);
        return st;
    }

    // Transfer firmware to DSP
    st = loader.TransferFirmware(fw_vmo, fw_size);
    if (st != ZX_OK) {
        return st;
    }

    // Wait for firwmare boot
    st = WaitCondition(INTEL_ADSP_BASE_FW_INIT_TIMEOUT_NSEC,
                       INTEL_ADSP_POLL_FW_NSEC,
                       [this]() -> bool {
                           return ((REG_RD(&fw_regs()->fw_status) &
                                    ADSP_FW_STATUS_STATE_MASK) ==
                                   ADSP_FW_STATUS_STATE_ENTER_BASE_FW);
                       });
    if (st != ZX_OK) {
        LOG(ERROR, "Error waiting for DSP base firmware entry (err %d)\n", st);
        LOG(ERROR, "FW_STATUS  0x%08x\n", REG_RD(&fw_regs()->fw_status));
        LOG(ERROR, "ERROR_CODE 0x%08x\n", REG_RD(&fw_regs()->error_code));
        return st;
    }

    return ZX_OK;
}

bool IntelAudioDsp::IsCoreEnabled(uint8_t core_mask) {
    uint32_t val = REG_RD(&regs()->adspcs);
    bool enabled = (val & ADSP_REG_ADSPCS_CPA(core_mask)) &&
                   (val & ADSP_REG_ADSPCS_SPA(core_mask)) &&
                   !(val & ADSP_REG_ADSPCS_CSTALL(core_mask)) &&
                   !(val & ADSP_REG_ADSPCS_CRST(core_mask));
    return enabled;
}

zx_status_t IntelAudioDsp::ResetCore(uint8_t core_mask) {
    // Stall cores
    REG_SET_BITS(&regs()->adspcs, ADSP_REG_ADSPCS_CSTALL(core_mask));

    // Put cores in reset
    REG_SET_BITS(&regs()->adspcs, ADSP_REG_ADSPCS_CRST(core_mask));

    // Wait for success
    return WaitCondition(INTEL_ADSP_TIMEOUT_NSEC,
                         INTEL_ADSP_POLL_NSEC,
                         [this, &core_mask]() -> bool {
                             return (REG_RD(&regs()->adspcs) &
                                     ADSP_REG_ADSPCS_CRST(core_mask)) != 0;
                         });
}

zx_status_t IntelAudioDsp::UnResetCore(uint8_t core_mask) {
    REG_CLR_BITS(&regs()->adspcs, ADSP_REG_ADSPCS_CRST(core_mask));
    return WaitCondition(INTEL_ADSP_TIMEOUT_NSEC,
                         INTEL_ADSP_POLL_NSEC,
                         [this, &core_mask]() -> bool {
                             return (REG_RD(&regs()->adspcs) &
                                     ADSP_REG_ADSPCS_CRST(core_mask)) == 0;
                         });
}

zx_status_t IntelAudioDsp::PowerDownCore(uint8_t core_mask) {
    REG_CLR_BITS(&regs()->adspcs, ADSP_REG_ADSPCS_SPA(core_mask));
    return WaitCondition(INTEL_ADSP_TIMEOUT_NSEC,
                         INTEL_ADSP_POLL_NSEC,
                         [this, &core_mask]() -> bool {
                             return (REG_RD(&regs()->adspcs) &
                                     ADSP_REG_ADSPCS_SPA(core_mask)) == 0;
                         });
}

zx_status_t IntelAudioDsp::PowerUpCore(uint8_t core_mask) {
    REG_SET_BITS(&regs()->adspcs, ADSP_REG_ADSPCS_SPA(core_mask));
    return WaitCondition(INTEL_ADSP_TIMEOUT_NSEC,
                         INTEL_ADSP_POLL_NSEC,
                         [this, &core_mask]() -> bool {
                             return (REG_RD(&regs()->adspcs) & ADSP_REG_ADSPCS_SPA(core_mask)) != 0;
                         });
}

void IntelAudioDsp::RunCore(uint8_t core_mask) {
    REG_CLR_BITS(&regs()->adspcs, ADSP_REG_ADSPCS_CSTALL(core_mask));
}

void IntelAudioDsp::EnableInterrupts() {
    REG_SET_BITS(&regs()->adspic, ADSP_REG_ADSPIC_IPC);
    REG_SET_BITS(&regs()->hipcctl, ADSP_REG_HIPCCTL_IPCTDIE | ADSP_REG_HIPCCTL_IPCTBIE);
}

}  // namespace intel_hda
}  // namespace audio

extern "C" {
zx_status_t ihda_dsp_init_hook(void** out_ctx) {
    return ZX_OK;
}

zx_status_t ihda_dsp_bind_hook(void* ctx, zx_device_t* hda_dev) {
    auto dev = ::audio::intel_hda::IntelAudioDsp::Create(hda_dev);
    if (!dev) {
        return ZX_ERR_NO_MEMORY;
    }
    zx_status_t st = dev->DriverBind();
    if (st == ZX_OK) {
        // devmgr is now in charge of the memory for dev
        void* ptr __UNUSED = dev.release();
    }
    return st;
}

void ihda_dsp_release_hook(void* ctx) {
}
}  // extern "C"
