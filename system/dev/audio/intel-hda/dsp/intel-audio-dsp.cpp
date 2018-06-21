// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fbl/alloc_checker.h>
#include <fbl/auto_call.h>
#include <fbl/auto_lock.h>
#include <string.h>

#include "intel-audio-dsp.h"
#include "intel-dsp-code-loader.h"

namespace audio {
namespace intel_hda {

namespace {

// ADSP SRAM windows
constexpr size_t SKL_ADSP_SRAM0_OFFSET  = 0x8000; // Shared between Skylake and Kabylake
constexpr size_t SKL_ADSP_SRAM1_OFFSET  = 0xA000;

// Mailbox offsets
constexpr size_t ADSP_MAILBOX_IN_OFFSET = 0x1000; // Section 5.5 Offset from SRAM0

constexpr const char* ADSP_FIRMWARE_PATH = "/boot/lib/firmware/dsp_fw_kbl_v3266.bin";

constexpr uint32_t EXT_MANIFEST_HDR_MAGIC = 0x31454124;

constexpr zx_time_t INTEL_ADSP_TIMEOUT_NSEC              = ZX_MSEC( 50); // 50mS Arbitrary
constexpr zx_time_t INTEL_ADSP_POLL_NSEC                 = ZX_USEC(500); // 500uS Arbitrary
constexpr zx_time_t INTEL_ADSP_ROM_INIT_TIMEOUT_NSEC     = ZX_SEC (  1); // 1S Arbitrary
constexpr zx_time_t INTEL_ADSP_BASE_FW_INIT_TIMEOUT_NSEC = ZX_SEC (  3); // 3S Arbitrary
constexpr zx_time_t INTEL_ADSP_POLL_FW_NSEC              = ZX_MSEC(  1); //.1mS Arbitrary
}  // anon namespace

struct skl_adspfw_ext_manifest_hdr_t {
    uint32_t id;
    uint32_t len;
    uint32_t version_major;
    uint32_t version_minor;
    uint32_t entries;
} __PACKED;

fbl::RefPtr<IntelAudioDsp> IntelAudioDsp::Create() {
    fbl::AllocChecker ac;
    auto ret = fbl::AdoptRef(new (&ac) IntelAudioDsp());
    if (!ac.check()) {
        GLOBAL_LOG(ERROR, "Out of memory attempting to allocate IHDA DSP\n");
        return nullptr;
    }
    return ret;
}

IntelAudioDsp::IntelAudioDsp()
    : ipc_(*this) {
    for (auto& id : module_ids_) { id = MODULE_ID_INVALID; }
    snprintf(log_prefix_, sizeof(log_prefix_), "IHDA DSP (unknown BDF)");
}

adsp_fw_registers_t* IntelAudioDsp::fw_regs() const {
    return reinterpret_cast<adsp_fw_registers_t*>(static_cast<uint8_t*>(mapped_regs_.start()) +
                                                  SKL_ADSP_SRAM0_OFFSET);
}

zx_status_t IntelAudioDsp::DriverBind(zx_device_t* hda_dev) {
    // IntelHDACodecDriverBase initialization. Do first so the parent reference is set.
    zx_status_t res = Bind(hda_dev, "intel-sst-dsp");

    res = SetupDspDevice();
    if (res != ZX_OK) {
        return res;
    }

    res = ParseNhlt();
    if (res != ZX_OK) {
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

    return ZX_OK;
}

zx_status_t IntelAudioDsp::SetupDspDevice() {
    zx_status_t res = device_get_protocol(codec_device(), ZX_PROTOCOL_IHDA_DSP,
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

    ipc_.SetLogPrefix(log_prefix_);

    // Fetch the bar which holds the Audio DSP registers.
    zx::vmo bar_vmo;
    size_t bar_size;
    res = ihda_dsp_get_mmio(&ihda_dsp_, bar_vmo.reset_and_get_address(), &bar_size);
    if (res != ZX_OK) {
        LOG(ERROR, "Failed to fetch DSP register VMO (err %u)\n", res);
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

    // Initialize mailboxes
    uint8_t* mapped_base = static_cast<uint8_t*>(mapped_regs_.start());
    mailbox_in_.Initialize(static_cast<void*>(mapped_base + SKL_ADSP_SRAM0_OFFSET +
                                              ADSP_MAILBOX_IN_OFFSET), MAILBOX_SIZE);
    mailbox_out_.Initialize(static_cast<void*>(mapped_base + SKL_ADSP_SRAM1_OFFSET),
                            MAILBOX_SIZE);

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

    return ZX_OK;
}

zx_status_t IntelAudioDsp::ParseNhlt() {
    size_t size = 0;
    zx_status_t res = device_get_metadata(codec_device(), MD_KEY_NHLT,
                                          nhlt_buf_, sizeof(nhlt_buf_), &size);
    if (res != ZX_OK) {
        LOG(ERROR, "Failed to fetch NHLT (res %d)\n", res);
        return res;
    }

    nhlt_table_t* nhlt = reinterpret_cast<nhlt_table_t*>(nhlt_buf_);

    // Sanity check
    if (size < sizeof(*nhlt)) {
        LOG(ERROR, "NHLT too small (%zu bytes)\n", size);
        return ZX_ERR_INTERNAL;
    }

    static_assert(sizeof(nhlt->header.signature) >= ACPI_NAME_SIZE, "");
    static_assert(sizeof(ACPI_NHLT_SIGNATURE) >= ACPI_NAME_SIZE, "");

    if (memcmp(nhlt->header.signature, ACPI_NHLT_SIGNATURE, ACPI_NAME_SIZE)) {
        LOG(ERROR, "Invalid NHLT signature\n");
        return ZX_ERR_INTERNAL;
    }

    uint8_t count = nhlt->endpoint_desc_count;
    if (count > I2S_CONFIG_MAX) {
        LOG(INFO, "Too many NHLT endpoints (max %zu, got %u), "
                  "only the first %zu will be processed\n",
                  I2S_CONFIG_MAX, count, I2S_CONFIG_MAX);
        count = I2S_CONFIG_MAX;
    }

    // Extract the PCM formats and I2S config blob
    size_t i = 0;
    size_t desc_offset = reinterpret_cast<uint8_t*>(nhlt->endpoints) - nhlt_buf_;
    while (count--) {
        auto desc = reinterpret_cast<nhlt_descriptor_t*>(nhlt_buf_ + desc_offset);

        // Sanity check
        if ((desc_offset + desc->length) > size) {
            LOG(ERROR, "NHLT endpoint descriptor out of bounds\n");
            return ZX_ERR_INTERNAL;
        }

        size_t length = static_cast<size_t>(desc->length);
        if (length < sizeof(*desc)) {
            LOG(ERROR, "Short NHLT descriptor\n");
            return ZX_ERR_INTERNAL;
        }
        length -= sizeof(*desc);

        // Only care about SSP endpoints
        if (desc->link_type != NHLT_LINK_TYPE_SSP) {
            continue;
        }

        // Make sure there is enough room for formats_configs
        if (length < desc->config.capabilities_size + sizeof(formats_config_t)) {
            LOG(ERROR, "NHLT endpoint descriptor too short (specific_config too long)\n");
            return ZX_ERR_INTERNAL;
        }
        length -= desc->config.capabilities_size + sizeof(formats_config_t);

        // Must have at least one format
        auto formats = reinterpret_cast<const formats_config_t*>(
                nhlt_buf_ + desc_offset + sizeof(*desc) + desc->config.capabilities_size
        );
        if (formats->format_config_count == 0) {
            continue;
        }

        // Iterate the formats and check lengths
        const format_config_t* format = formats->format_configs;
        for (uint8_t j = 0; j < formats->format_config_count; j++) {
            size_t format_length = sizeof(*format) + format->config.capabilities_size;
            if (length < format_length) {
                LOG(ERROR, "Invalid NHLT endpoint desciptor format too short\n");
                return ZX_ERR_INTERNAL;
            }
            length -= format_length;
            format = reinterpret_cast<const format_config_t*>(
                    reinterpret_cast<const uint8_t*>(format) + format_length
            );
        }
        if (length != 0) {
            LOG(ERROR, "Invalid NHLT endpoint descriptor length\n");
            return ZX_ERR_INTERNAL;
        }

        i2s_configs_[i++] = { desc->virtual_bus_id, desc->direction, formats };

        desc_offset += desc->length;
    }

    LOG(TRACE, "parse success, found %zu formats\n", i);

    return ZX_OK;
}

void IntelAudioDsp::DeviceShutdown() {
    if (state_ == State::INITIALIZING) {
        thrd_join(init_thread_, NULL);
    }

    // Order is important below.
    // Disable Audio DSP and interrupt
    ihda_dsp_irq_disable(&ihda_dsp_);
    ihda_dsp_disable(&ihda_dsp_);


    // Reset and power down the DSP.
    ResetCore(ADSP_REG_ADSPCS_CORE0_MASK);
    PowerDownCore(ADSP_REG_ADSPCS_CORE0_MASK);

    ipc_.Shutdown();

    state_ = State::SHUT_DOWN;
}

int IntelAudioDsp::InitThread() {
    zx_status_t st = ZX_OK;
    auto cleanup = fbl::MakeAutoCall([this]() {
        DeviceShutdown();
    });

    // Enable Audio DSP
    ihda_dsp_enable(&ihda_dsp_);

    // The HW loads the DSP base firmware from ROM during the initialization,
    // when the Tensilica Core is out of reset, but halted.
    st = Boot();
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

    // Setup pipelines
    st = GetModulesInfo();
    if (st != ZX_OK) {
        LOG(ERROR, "Error getting DSP modules info\n");
        return -1;
    }
    st = SetupPipelines();
    if (st != ZX_OK) {
        LOG(ERROR, "Error initializing DSP pipelines\n");
        return -1;
    }

    // Create and publish streams.
    st = CreateAndStartStreams();
    if (st != ZX_OK) {
        LOG(ERROR, "Error starting DSP streams\n");
        return -1;
    }

    cleanup.cancel();
    return 0;
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

zx_status_t IntelAudioDsp::GetModulesInfo() {
    uint8_t data[MAILBOX_SIZE];
    IntelDspIpc::Txn txn(nullptr, 0, data, sizeof(data));
    ipc_.LargeConfigGet(&txn, 0, 0, to_underlying(BaseFWParamType::MODULES_INFO), sizeof(data));

    if (txn.success()) {
        auto info = reinterpret_cast<const ModulesInfo*>(txn.rx_data);
        uint32_t count = info->module_count;

        ZX_DEBUG_ASSERT(txn.rx_actual >= sizeof(ModulesInfo) + (count * sizeof(ModuleEntry)));

        static constexpr const char* MODULE_NAMES[] = {
            [COPIER] = "COPIER",
            [MIXIN]  = "MIXIN",
            [MIXOUT] = "MIXOUT",
        };
        static_assert(countof(MODULE_NAMES) == countof(module_ids_), "invalid module id count\n");

        for (uint32_t i = 0; i < count; i++) {
            for (size_t j = 0; j < countof(MODULE_NAMES); j++) {
                if (!strncmp(reinterpret_cast<const char*>(info->module_info[i].name),
                             MODULE_NAMES[j], strlen(MODULE_NAMES[j]))) {
                    if (module_ids_[j] == MODULE_ID_INVALID) {
                        module_ids_[j] = info->module_info[i].module_id;
                    } else {
                        LOG(ERROR, "Found duplicate module id %hu\n",
                                   info->module_info[i].module_id);
                    }
                }
            }
        }
    }


    return txn.success() ? ZX_OK : ZX_ERR_INTERNAL;
}

zx_status_t IntelAudioDsp::StripFirmware(const zx::vmo& fw, void* out, size_t* size_inout) {
    ZX_DEBUG_ASSERT(out != nullptr);
    ZX_DEBUG_ASSERT(size_inout != nullptr);

    // Check for extended manifest
    skl_adspfw_ext_manifest_hdr_t hdr;
    zx_status_t st = fw.read(&hdr, 0, sizeof(hdr));
    if (st != ZX_OK) {
        return st;
    }

    // If the firmware contains an extended manifest, it must be stripped
    // before loading to the DSP.
    uint32_t offset = 0;
    if (hdr.id == EXT_MANIFEST_HDR_MAGIC) {
        offset = hdr.len;
    }

    // Always copy the firmware to simplify the code.
    size_t bytes = *size_inout - offset;
    if (*size_inout < bytes) {
        return ZX_ERR_BUFFER_TOO_SMALL;
    }

    *size_inout = bytes;
    return fw.read(out, offset, bytes);
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
    st = load_firmware(codec_device(), ADSP_FIRMWARE_PATH, fw_vmo.reset_and_get_address(),
                       &fw_size);
    if (st != ZX_OK) {
        LOG(ERROR, "Error fetching firmware (err %d)\n", st);
        return st;
    }

    // The max length of the firmware is 256 pages, assuming a fully distinguous VMO.
    constexpr size_t MAX_FW_BYTES = PAGE_SIZE * IntelDspCodeLoader::MAX_BDL_LENGTH;
    if (fw_size > MAX_FW_BYTES) {
        LOG(ERROR, "DSP firmware is too big (0x%zx bytes > 0x%zx bytes)\n", fw_size, MAX_FW_BYTES);
        return ZX_ERR_INVALID_ARGS;
    }

    // Create and map a VMO to copy the firmware into. The firmware must be copied to
    // a new VMO because BDL addresses must be 128-byte aligned, and the presence
    // of the extended manifest header will guarantee un-alignment.
    // This VMO is mapped once and thrown away after firmware loading, so map it
    // into the root VMAR so we don't need to allocate more space in DriverVmars::registers().
    constexpr uint32_t CPU_MAP_FLAGS = ZX_VM_FLAG_PERM_READ | ZX_VM_FLAG_PERM_WRITE;
    zx::vmo stripped_vmo;
    vmo_utils::VmoMapper stripped_fw;
    st = stripped_fw.CreateAndMap(fw_size, CPU_MAP_FLAGS, nullptr, &stripped_vmo);
    if (st != ZX_OK) {
        LOG(ERROR, "Error creating DSP firmware VMO (err %d)\n", st);
        return st;
    }

    size_t stripped_size = fw_size;
    st = StripFirmware(fw_vmo, stripped_fw.start(), &stripped_size);
    if (st != ZX_OK) {
        LOG(ERROR, "Error stripping DSP firmware (err %d)\n", st);
        return st;
    }

    // Pin this VMO and grant the controller access to it.  The controller
    // should only need read access to the firmware.
    constexpr uint32_t DSP_MAP_FLAGS = ZX_BTI_PERM_READ;
    PinnedVmo pinned_fw;
    st = pinned_fw.Pin(stripped_vmo, hda_bti_->initiator(), DSP_MAP_FLAGS);
    if (st != ZX_OK) {
        LOG(ERROR, "Failed to pin pages for DSP firmware (res %d)\n", st);
        return st;
    }

    // Transfer firmware to DSP
    st = loader.TransferFirmware(pinned_fw, stripped_size);
    if (st != ZX_OK) {
        return st;
    }

    // Wait for firwmare boot
    // Read FW_STATUS first... Polling this field seems to affect something in the DSP.
    // If we wait for the FW Ready IPC first, sometimes FW_STATUS will not equal
    // ADSP_FW_STATUS_STATE_ENTER_BASE_FW when this times out, but if we then poll
    // FW_STATUS the value will transition to the expected value.
    st = WaitCondition(INTEL_ADSP_BASE_FW_INIT_TIMEOUT_NSEC,
                       INTEL_ADSP_POLL_FW_NSEC,
                       [this]() -> bool {
                           return ((REG_RD(&fw_regs()->fw_status) &
                                    ADSP_FW_STATUS_STATE_MASK) ==
                                  ADSP_FW_STATUS_STATE_ENTER_BASE_FW);
                       });
    if (st != ZX_OK) {
        LOG(ERROR, "Error waiting for DSP base firmware entry (err %d, fw_status = 0x%08x)\n",
                   st, REG_RD(&fw_regs()->fw_status));
        return st;
    }

    // Stop the DMA
    loader.StopTransfer();

    // Now check whether we received the FW Ready IPC. Receiving this IPC indicates the
    // IPC system is ready. Both FW_STATUS = ADSP_FW_STATUS_STATE_ENTER_BASE_FW and
    // receiving the IPC are required for the DSP to be operational.
    st = ipc_.WaitForFirmwareReady(INTEL_ADSP_BASE_FW_INIT_TIMEOUT_NSEC);
    if (st != ZX_OK) {
        LOG(ERROR, "Error waiting for FW Ready IPC (err %d, fw_status = 0x%08x)\n",
                   st, REG_RD(&fw_regs()->fw_status));
        return st;
    }

    return ZX_OK;
}

zx_status_t IntelAudioDsp::RunPipeline(uint8_t pipeline_id) {
    // Pipeline must be paused before starting
    zx_status_t st = ipc_.SetPipelineState(pipeline_id, PipelineState::PAUSED, true);
    if (st != ZX_OK) {
        return st;
    }
    return ipc_.SetPipelineState(pipeline_id, PipelineState::RUNNING, true);
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
                                     ADSP_REG_ADSPCS_CPA(core_mask)) == 0;
                         });
}

zx_status_t IntelAudioDsp::PowerUpCore(uint8_t core_mask) {
    REG_SET_BITS(&regs()->adspcs, ADSP_REG_ADSPCS_SPA(core_mask));
    return WaitCondition(INTEL_ADSP_TIMEOUT_NSEC,
                         INTEL_ADSP_POLL_NSEC,
                         [this, &core_mask]() -> bool {
                             return (REG_RD(&regs()->adspcs) & ADSP_REG_ADSPCS_CPA(core_mask)) != 0;
                         });
}

void IntelAudioDsp::RunCore(uint8_t core_mask) {
    REG_CLR_BITS(&regs()->adspcs, ADSP_REG_ADSPCS_CSTALL(core_mask));
}

void IntelAudioDsp::EnableInterrupts() {
    REG_SET_BITS(&regs()->adspic, ADSP_REG_ADSPIC_CLDMA | ADSP_REG_ADSPIC_IPC);
    REG_SET_BITS(&regs()->hipcctl, ADSP_REG_HIPCCTL_IPCTDIE | ADSP_REG_HIPCCTL_IPCTBIE);
}

void IntelAudioDsp::ProcessIrq() {
    uint32_t adspis = REG_RD(&regs()->adspis);
    if (adspis & ADSP_REG_ADSPIC_CLDMA) {
        LOG(TRACE, "Got CLDMA irq\n");
        uint32_t w = REG_RD(&regs()->cldma.stream.ctl_sts.w);
        REG_WR(&regs()->cldma.stream.ctl_sts.w, w);
    }
    if (adspis & ADSP_REG_ADSPIC_IPC) {
        IpcMessage message(REG_RD(&regs()->hipct), REG_RD(&regs()->hipcte));
        if (message.primary & ADSP_REG_HIPCT_BUSY) {
            if (state_ != State::OPERATING) {
                LOG(WARN, "Got IRQ when device is not operating (state %u)\n", to_underlying(state_));
            } else {
                // Process the incoming message
                ipc_.ProcessIpc(message);
            }

            // Ack the IRQ after reading mailboxes.
            REG_SET_BITS(&regs()->hipct, ADSP_REG_HIPCT_BUSY);
        }

        // Ack the IPC target done IRQ
        uint32_t val = REG_RD(&regs()->hipcie);
        if (val & ADSP_REG_HIPCIE_DONE) {
            REG_WR(&regs()->hipcie, val);
        }
    }
}

}  // namespace intel_hda
}  // namespace audio

extern "C" {
zx_status_t ihda_dsp_init_hook(void** out_ctx) {
    return ZX_OK;
}

zx_status_t ihda_dsp_bind_hook(void* ctx, zx_device_t* hda_dev) {
    auto dev = ::audio::intel_hda::IntelAudioDsp::Create();
    if (!dev) {
        return ZX_ERR_NO_MEMORY;
    }
    zx_status_t st = dev->DriverBind(hda_dev);
    if (st == ZX_OK) {
        // devmgr is now in charge of the memory for dev
        void* ptr __UNUSED = dev.leak_ref();
    }
    return st;
}

void ihda_dsp_release_hook(void* ctx) {
}
}  // extern "C"
