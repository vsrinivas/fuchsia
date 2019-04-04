// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fbl/alloc_checker.h>
#include <fbl/auto_call.h>
#include <fbl/auto_lock.h>
#include <string.h>

#include <utility>

#include "intel-dsp-code-loader.h"
#include "intel-dsp.h"
#include "intel-hda-controller.h"

namespace audio {
namespace intel_hda {

namespace {

// ADSP SRAM windows
constexpr size_t SKL_ADSP_SRAM0_OFFSET  = 0x8000; // Shared between Skylake and Kabylake
constexpr size_t SKL_ADSP_SRAM1_OFFSET  = 0xA000;

// Mailbox offsets
constexpr size_t ADSP_MAILBOX_IN_OFFSET = 0x1000; // Section 5.5 Offset from SRAM0

constexpr const char* ADSP_FIRMWARE_PATH = "dsp_fw_kbl_v3420.bin";

constexpr uint32_t EXT_MANIFEST_HDR_MAGIC = 0x31454124;

constexpr zx_duration_t INTEL_ADSP_TIMEOUT_NSEC              = ZX_MSEC( 50); // 50mS Arbitrary
constexpr zx_duration_t INTEL_ADSP_POLL_NSEC                 = ZX_USEC(500); // 500uS Arbitrary
constexpr zx_duration_t INTEL_ADSP_ROM_INIT_TIMEOUT_NSEC     = ZX_SEC (  1); // 1S Arbitrary
constexpr zx_duration_t INTEL_ADSP_BASE_FW_INIT_TIMEOUT_NSEC = ZX_SEC (  3); // 3S Arbitrary
constexpr zx_duration_t INTEL_ADSP_POLL_FW_NSEC              = ZX_MSEC(  1); //.1mS Arbitrary
}  // anon namespace

struct skl_adspfw_ext_manifest_hdr_t {
    uint32_t id;
    uint32_t len;
    uint32_t version_major;
    uint32_t version_minor;
    uint32_t entries;
} __PACKED;

IntelDsp::IntelDsp(IntelHDAController* controller, hda_pp_registers_t* pp_regs,
                   const fbl::RefPtr<RefCountedBti>& pci_bti)
    : controller_(controller), pp_regs_(pp_regs), pci_bti_(pci_bti) {
    for (auto& id : module_ids_) {
        id = MODULE_ID_INVALID;
    }
    const auto& info = controller_->dev_info();
    snprintf(log_prefix_, sizeof(log_prefix_), "IHDA DSP %02x:%02x.%01x",
             info.bus_id, info.dev_id, info.func_id);
}

IntelDsp::~IntelDsp() {
    // Close all existing connections and synchronize with any client threads
    // who are currently processing requests.
    default_domain_->Deactivate();

    // Give any active streams we had back to our controller.
    IntelHDAStream::Tree streams;
    {
        fbl::AutoLock lock(&active_streams_lock_);
        streams.swap(active_streams_);
    }

    while (!streams.is_empty()) {
        controller_->ReturnStream(streams.pop_front());
    }
}

zx_status_t IntelDsp::Init(zx_device_t* dsp_dev) {
    default_domain_ = dispatcher::ExecutionDomain::Create();
    if (!default_domain_) {
        return ZX_ERR_NO_MEMORY;
    }

    zx_status_t res = Bind(dsp_dev, "intel-sst-dsp");
    if (res != ZX_OK) {
        return res;
    }

    res = SetupDspDevice();
    if (res != ZX_OK) {
        return res;
    }

    res = ParseNhlt();
    if (res != ZX_OK) {
        return res;
    }

    // Perform hardware initialization in a thread.
    state_ = State::INITIALIZING;
    int c11_res = thrd_create(
            &init_thread_,
            [](void* ctx) { return static_cast<IntelDsp*>(ctx)->InitThread(); },
            this);
    if (c11_res < 0) {
        LOG(ERROR, "Failed to create init thread (res = %d)\n", c11_res);
        state_ = State::ERROR;
        return ZX_ERR_INTERNAL;
    }

    return ZX_OK;
}

adsp_registers_t* IntelDsp::regs() const {
    return reinterpret_cast<adsp_registers_t*>(mapped_regs_.start());
}

adsp_fw_registers_t* IntelDsp::fw_regs() const {
    return reinterpret_cast<adsp_fw_registers_t*>(static_cast<uint8_t*>(mapped_regs_.start()) +
                                                  SKL_ADSP_SRAM0_OFFSET);
}

zx_status_t IntelDsp::CodecGetDispatcherChannel(zx_handle_t* remote_endpoint_out) {
    if (!remote_endpoint_out)
        return ZX_ERR_INVALID_ARGS;

    dispatcher::Channel::ProcessHandler phandler(
        [codec = fbl::WrapRefPtr(this)](dispatcher::Channel* channel) -> zx_status_t {
            OBTAIN_EXECUTION_DOMAIN_TOKEN(t, codec->default_domain_);
            return codec->ProcessClientRequest(channel, true);
        });

    dispatcher::Channel::ChannelClosedHandler chandler(
        [codec = fbl::WrapRefPtr(this)](const dispatcher::Channel* channel) -> void {
            OBTAIN_EXECUTION_DOMAIN_TOKEN(t, codec->default_domain_);
            codec->ProcessClientDeactivate(channel);
        });

    // Enter the driver channel lock.  If we have already connected to a codec
    // driver, simply fail the request.  Otherwise, attempt to build a driver channel
    // and activate it.
    fbl::AutoLock lock(&codec_driver_channel_lock_);

    if (codec_driver_channel_ != nullptr)
        return ZX_ERR_BAD_STATE;

    zx::channel client_channel;
    zx_status_t res;
    res = CreateAndActivateChannel(default_domain_,
                                   std::move(phandler),
                                   std::move(chandler),
                                   &codec_driver_channel_,
                                   &client_channel);
    if (res == ZX_OK) {
        // If things went well, release the reference to the remote endpoint
        // from the zx::channel instance into the unmanaged world of DDK
        // protocols.
        *remote_endpoint_out = client_channel.release();
    }

    return res;
}

#define PROCESS_CMD(_req_ack, _req_driver_chan, _ioctl, _payload, _handler) \
    case _ioctl:                                                            \
        if (req_size != sizeof(req._payload)) {                             \
            LOG(TRACE, "Bad " #_payload " request length (%u != %zu)\n",    \
                req_size, sizeof(req._payload));                            \
            return ZX_ERR_INVALID_ARGS;                                     \
        }                                                                   \
        if (_req_ack && (req.hdr.cmd & IHDA_NOACK_FLAG)) {                  \
            LOG(TRACE, "Cmd " #_payload                                     \
                       " requires acknowledgement, but the "                \
                       "NOACK flag was set!\n");                            \
            return ZX_ERR_INVALID_ARGS;                                     \
        }                                                                   \
        if (_req_driver_chan && !is_driver_channel) {                       \
            LOG(TRACE, "Cmd " #_payload                                     \
                       " requires a privileged driver channel.\n");         \
            return ZX_ERR_ACCESS_DENIED;                                    \
        }                                                                   \
        return _handler(channel, req._payload)
zx_status_t IntelDsp::ProcessClientRequest(dispatcher::Channel* channel,
                                           bool is_driver_channel) {
    zx_status_t res;
    uint32_t req_size;
    union {
        ihda_proto::CmdHdr hdr;
        ihda_proto::RequestStreamReq request_stream;
        ihda_proto::ReleaseStreamReq release_stream;
        ihda_proto::SetStreamFmtReq set_stream_fmt;
    } req;
    // TODO(johngro) : How large is too large?
    static_assert(sizeof(req) <= 256, "Request buffer is too large to hold on the stack!");

    // Read the client request.
    ZX_DEBUG_ASSERT(channel != nullptr);
    res = channel->Read(&req, sizeof(req), &req_size);
    if (res != ZX_OK) {
        LOG(TRACE, "Failed to read client request (res %d)\n", res);
        return res;
    }

    // Sanity checks.
    if (req_size < sizeof(req.hdr)) {
        LOG(TRACE, "Client request too small to contain header (%u < %zu)\n",
            req_size, sizeof(req.hdr));
        return ZX_ERR_INVALID_ARGS;
    }

    auto cmd_id = static_cast<ihda_cmd_t>(req.hdr.cmd & ~IHDA_NOACK_FLAG);
    if (req.hdr.transaction_id == IHDA_INVALID_TRANSACTION_ID) {
        LOG(TRACE, "Invalid transaction ID in client request 0x%04x\n", cmd_id);
        return ZX_ERR_INVALID_ARGS;
    }

    // Dispatch
    LOG(SPEW, "Client Request (cmd 0x%04x tid %u) len %u\n",
        req.hdr.cmd,
        req.hdr.transaction_id,
        req_size);

    switch (cmd_id) {
        PROCESS_CMD(true, true, IHDA_CODEC_REQUEST_STREAM, request_stream, ProcessRequestStream);
        PROCESS_CMD(false, true, IHDA_CODEC_RELEASE_STREAM, release_stream, ProcessReleaseStream);
        PROCESS_CMD(false, true, IHDA_CODEC_SET_STREAM_FORMAT, set_stream_fmt, ProcessSetStreamFmt);
    default:
        LOG(TRACE, "Unrecognized command ID 0x%04x\n", req.hdr.cmd);
        return ZX_ERR_INVALID_ARGS;
    }
}
#undef PROCESS_CMD

void IntelDsp::ProcessClientDeactivate(const dispatcher::Channel* channel) {
    ZX_DEBUG_ASSERT(channel != nullptr);

    // This should be the driver channel (client channels created with IOCTL do
    // not register a deactivate handler).  Start by releasing the internal
    // channel reference from within the codec_driver_channel_lock.
    {
        fbl::AutoLock lock(&codec_driver_channel_lock_);
        ZX_DEBUG_ASSERT(channel == codec_driver_channel_.get());
        codec_driver_channel_.reset();
    }

    // Return any DMA streams the codec driver had owned back to the controller.
    IntelHDAStream::Tree tmp;
    {
        fbl::AutoLock lock(&active_streams_lock_);
        tmp = std::move(active_streams_);
    }

    while (!tmp.is_empty()) {
        auto stream = tmp.pop_front();
        stream->Deactivate();
        controller_->ReturnStream(std::move(stream));
    }
}

zx_status_t IntelDsp::ProcessRequestStream(dispatcher::Channel* channel,
                                           const ihda_proto::RequestStreamReq& req) {
    ZX_DEBUG_ASSERT(channel != nullptr);

    ihda_proto::RequestStreamResp resp;
    resp.hdr = req.hdr;

    // Attempt to get a stream of the proper type.
    auto type = req.input
                    ? IntelHDAStream::Type::INPUT
                    : IntelHDAStream::Type::OUTPUT;
    auto stream = controller_->AllocateStream(type);

    if (stream != nullptr) {
        LOG(TRACE, "Decouple stream #%u\n", stream->id());
        // Decouple stream
        REG_SET_BITS<uint32_t>(&pp_regs_->ppctl, (1 << stream->dma_id()));

        // Success, send its ID and its tag back to the codec and add it to the
        // set of active streams owned by this codec.
        resp.result = ZX_OK;
        resp.stream_id = stream->id();
        resp.stream_tag = stream->tag();

        fbl::AutoLock lock(&active_streams_lock_);
        active_streams_.insert(std::move(stream));
    } else {
        // Failure; tell the codec that we are out of streams.
        resp.result = ZX_ERR_NO_MEMORY;
        resp.stream_id = 0;
        resp.stream_tag = 0;
    }

    return channel->Write(&resp, sizeof(resp));
}

zx_status_t IntelDsp::ProcessReleaseStream(dispatcher::Channel* channel,
                                           const ihda_proto::ReleaseStreamReq& req) {
    ZX_DEBUG_ASSERT(channel != nullptr);

    // Remove the stream from the active set.
    fbl::RefPtr<IntelHDAStream> stream;
    {
        fbl::AutoLock lock(&active_streams_lock_);
        stream = active_streams_.erase(req.stream_id);
    }

    // If the stream was not active, our codec driver has some sort of internal
    // inconsistency.  Hang up the phone on it.
    if (stream == nullptr)
        return ZX_ERR_BAD_STATE;

    LOG(TRACE, "Couple stream #%u\n", stream->id());

    // Couple stream
    REG_CLR_BITS<uint32_t>(&pp_regs_->ppctl, (1 << stream->dma_id()));

    // Give the stream back to the controller and (if an ack was requested) tell
    // our codec driver that things went well.
    stream->Deactivate();
    controller_->ReturnStream(std::move(stream));

    if (req.hdr.cmd & IHDA_NOACK_FLAG)
        return ZX_OK;

    ihda_proto::RequestStreamResp resp;
    resp.hdr = req.hdr;
    return channel->Write(&resp, sizeof(resp));
}

zx_status_t IntelDsp::ProcessSetStreamFmt(dispatcher::Channel* channel,
                                          const ihda_proto::SetStreamFmtReq& req) {
    ZX_DEBUG_ASSERT(channel != nullptr);

    // Sanity check the requested format.
    if (!StreamFormat(req.format).SanityCheck()) {
        LOG(TRACE, "Invalid encoded stream format 0x%04hx!\n", req.format);
        return ZX_ERR_INVALID_ARGS;
    }

    // Grab a reference to the stream from the active set.
    fbl::RefPtr<IntelHDAStream> stream;
    {
        fbl::AutoLock lock(&active_streams_lock_);
        auto iter = active_streams_.find(req.stream_id);
        if (iter.IsValid())
            stream = iter.CopyPointer();
    }

    // If the stream was not active, our codec driver has some sort of internal
    // inconsistency.  Hang up the phone on it.
    if (stream == nullptr)
        return ZX_ERR_BAD_STATE;

    // Set the stream format and assign the client channel to the stream.  If
    // this stream is already bound to a client, this will cause that connection
    // to be closed.
    zx::channel client_channel;
    zx_status_t res = stream->SetStreamFormat(default_domain_,
                                              req.format,
                                              &client_channel);
    if (res != ZX_OK) {
        LOG(TRACE, "Failed to set stream format 0x%04hx for stream %hu (res %d)\n",
            req.format, req.stream_id, res);
        return res;
    }

    // Send the channel back to the codec driver.
    ZX_DEBUG_ASSERT(client_channel.is_valid());
    ihda_proto::SetStreamFmtResp resp;
    resp.hdr = req.hdr;
    res = channel->Write(&resp, sizeof(resp), std::move(client_channel));

    if (res != ZX_OK)
        LOG(TRACE, "Failed to send stream channel back to codec driver (res %d)\n", res);

    return res;
}

zx_status_t IntelDsp::SetupDspDevice() {
    const zx_pcie_device_info_t& hda_dev_info = controller_->dev_info();
    snprintf(log_prefix_, sizeof(log_prefix_), "IHDA DSP %02x:%02x.%01x",
             hda_dev_info.bus_id,
             hda_dev_info.dev_id,
             hda_dev_info.func_id);
    ipc_.SetLogPrefix(log_prefix_);

    // Fetch the bar which holds the Audio DSP registers.
    zx::vmo bar_vmo;
    size_t bar_size;
    zx_status_t res = GetMmio(bar_vmo.reset_and_get_address(), &bar_size);
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
    constexpr uint32_t CPU_MAP_FLAGS = ZX_VM_PERM_READ | ZX_VM_PERM_WRITE;
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
    res = GetBti(bti.reset_and_get_address());
    if (res != ZX_OK) {
        LOG(ERROR, "Failed to get BTI handle for IHDA DSP (res %d)\n", res);
        return res;
    }

    pci_bti_ = RefCountedBti::Create(std::move(bti));
    if (pci_bti_ == nullptr) {
        LOG(ERROR, "Out of memory while attempting to allocate BTI wrapper for IHDA DSP\n");
        return ZX_ERR_NO_MEMORY;
    }

    // Enable HDA interrupt. Interrupts are still masked at the DSP level.
    IrqEnable();
    return ZX_OK;
}

zx_status_t IntelDsp::ParseNhlt() {
    size_t size = 0;
    zx_status_t res = device_get_metadata(codec_device(),
                                          *reinterpret_cast<const uint32_t*>(ACPI_NHLT_SIGNATURE),
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

void IntelDsp::DeviceShutdown() {
    if (state_ == State::INITIALIZING) {
        thrd_join(init_thread_, NULL);
    }

    // Order is important below.
    // Disable Audio DSP and interrupt
    IrqDisable();
    Disable();

    // Reset and power down the DSP.
    ResetCore(ADSP_REG_ADSPCS_CORE0_MASK);
    PowerDownCore(ADSP_REG_ADSPCS_CORE0_MASK);

    ipc_.Shutdown();

    state_ = State::SHUT_DOWN;
}

zx_status_t IntelDsp::Suspend(uint32_t flags) {
    switch (flags & DEVICE_SUSPEND_REASON_MASK) {
    case DEVICE_SUSPEND_FLAG_POWEROFF:
        DeviceShutdown();
        return ZX_OK;
    default:
        return ZX_ERR_NOT_SUPPORTED;
    }
}

int IntelDsp::InitThread() {
    auto cleanup = fbl::MakeAutoCall([this]() {
        DeviceShutdown();
    });

    // Enable Audio DSP
    Enable();

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

zx_status_t IntelDsp::Boot() {
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

zx_status_t IntelDsp::GetModulesInfo() {
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

zx_status_t IntelDsp::StripFirmware(const zx::vmo& fw, void* out, size_t* size_inout) {
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

zx_status_t IntelDsp::LoadFirmware() {
    IntelDspCodeLoader loader(&regs()->cldma, pci_bti_);
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
    constexpr uint32_t CPU_MAP_FLAGS = ZX_VM_PERM_READ | ZX_VM_PERM_WRITE;
    zx::vmo stripped_vmo;
    fzl::VmoMapper stripped_fw;
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
    fzl::PinnedVmo pinned_fw;
    st = pinned_fw.Pin(stripped_vmo, pci_bti_->initiator(), DSP_MAP_FLAGS);
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

zx_status_t IntelDsp::RunPipeline(uint8_t pipeline_id) {
    // Pipeline must be paused before starting
    zx_status_t st = ipc_.SetPipelineState(pipeline_id, PipelineState::PAUSED, true);
    if (st != ZX_OK) {
        return st;
    }
    return ipc_.SetPipelineState(pipeline_id, PipelineState::RUNNING, true);
}

bool IntelDsp::IsCoreEnabled(uint8_t core_mask) {
    uint32_t val = REG_RD(&regs()->adspcs);
    bool enabled = (val & ADSP_REG_ADSPCS_CPA(core_mask)) &&
                   (val & ADSP_REG_ADSPCS_SPA(core_mask)) &&
                   !(val & ADSP_REG_ADSPCS_CSTALL(core_mask)) &&
                   !(val & ADSP_REG_ADSPCS_CRST(core_mask));
    return enabled;
}

zx_status_t IntelDsp::ResetCore(uint8_t core_mask) {
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

zx_status_t IntelDsp::UnResetCore(uint8_t core_mask) {
    REG_CLR_BITS(&regs()->adspcs, ADSP_REG_ADSPCS_CRST(core_mask));
    return WaitCondition(INTEL_ADSP_TIMEOUT_NSEC,
                         INTEL_ADSP_POLL_NSEC,
                         [this, &core_mask]() -> bool {
                             return (REG_RD(&regs()->adspcs) &
                                     ADSP_REG_ADSPCS_CRST(core_mask)) == 0;
                         });
}

zx_status_t IntelDsp::PowerDownCore(uint8_t core_mask) {
    REG_CLR_BITS(&regs()->adspcs, ADSP_REG_ADSPCS_SPA(core_mask));
    return WaitCondition(INTEL_ADSP_TIMEOUT_NSEC,
                         INTEL_ADSP_POLL_NSEC,
                         [this, &core_mask]() -> bool {
                             return (REG_RD(&regs()->adspcs) &
                                     ADSP_REG_ADSPCS_CPA(core_mask)) == 0;
                         });
}

zx_status_t IntelDsp::PowerUpCore(uint8_t core_mask) {
    REG_SET_BITS(&regs()->adspcs, ADSP_REG_ADSPCS_SPA(core_mask));
    return WaitCondition(INTEL_ADSP_TIMEOUT_NSEC,
                         INTEL_ADSP_POLL_NSEC,
                         [this, &core_mask]() -> bool {
                             return (REG_RD(&regs()->adspcs) & ADSP_REG_ADSPCS_CPA(core_mask)) != 0;
                         });
}

void IntelDsp::RunCore(uint8_t core_mask) {
    REG_CLR_BITS(&regs()->adspcs, ADSP_REG_ADSPCS_CSTALL(core_mask));
}

void IntelDsp::EnableInterrupts() {
    REG_SET_BITS(&regs()->adspic, ADSP_REG_ADSPIC_CLDMA | ADSP_REG_ADSPIC_IPC);
    REG_SET_BITS(&regs()->hipcctl, ADSP_REG_HIPCCTL_IPCTDIE | ADSP_REG_HIPCCTL_IPCTBIE);
}

void IntelDsp::ProcessIrq() {
    uint32_t ppsts = REG_RD(&pp_regs_->ppsts);
    if (!(ppsts & HDA_PPSTS_PIS)) {
        return;
    }
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

zx_status_t IntelDsp::GetMmio(zx_handle_t* out_vmo, size_t* out_size) {
    // Fetch the BAR which the Audio DSP registers (BAR 4), then sanity check the type
    // and size.
    zx_pci_bar_t bar_info;
    zx_status_t res = pci_get_bar(controller_->pci(), 4u, &bar_info);
    if (res != ZX_OK) {
        LOG(ERROR, "Error attempting to fetch registers from PCI (res %d)\n", res);
        return res;
    }

    if (bar_info.type != ZX_PCI_BAR_TYPE_MMIO) {
        LOG(ERROR, "Bad register window type (expected %u got %u)\n",
            ZX_PCI_BAR_TYPE_MMIO, bar_info.type);
        return ZX_ERR_INTERNAL;
    }

    *out_vmo = bar_info.handle;
    *out_size = bar_info.size;
    return ZX_OK;
}

zx_status_t IntelDsp::GetBti(zx_handle_t* out_handle) {
    ZX_DEBUG_ASSERT(pci_bti_ != nullptr);
    zx::bti bti;
    zx_status_t res = pci_bti_->initiator().duplicate(ZX_RIGHT_SAME_RIGHTS, &bti);
    if (res != ZX_OK) {
        LOG(ERROR, "Error duplicating BTI for DSP (res %d)\n", res);
        return res;
    }
    *out_handle = bti.release();
    return ZX_OK;
}

void IntelDsp::Enable() {
    // Note: The GPROCEN bit does not really enable or disable the Audio DSP
    // operation, but mainly to work around some legacy Intel HD Audio driver
    // software such that if GPROCEN = 0, ADSPxBA (BAR2) is mapped to the Intel
    // HD Audio memory mapped configuration registers, for compliancy with some
    // legacy SW implementation. If GPROCEN = 1, only then ADSPxBA (BAR2) is
    // mapped to the actual Audio DSP memory mapped configuration registers.
    REG_SET_BITS<uint32_t>(&pp_regs_->ppctl, HDA_PPCTL_GPROCEN);
}

void IntelDsp::Disable() {
    REG_WR(&pp_regs_->ppctl, 0u);
}

void IntelDsp::IrqEnable() {
    REG_SET_BITS<uint32_t>(&pp_regs_->ppctl, HDA_PPCTL_PIE);
}

void IntelDsp::IrqDisable() {
    REG_CLR_BITS<uint32_t>(&pp_regs_->ppctl, HDA_PPCTL_PIE);
}

}  // namespace intel_hda
}  // namespace audio
