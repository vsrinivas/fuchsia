// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "intel-dsp.h"

#include <string.h>
#include <zircon/errors.h>

#include <cstring>
#include <map>
#include <utility>

#include <fbl/alloc_checker.h>
#include <fbl/array.h>
#include <fbl/auto_call.h>
#include <fbl/auto_lock.h>
#include <fbl/string_printf.h>
#include <intel-hda/utils/nhlt.h>

#include "intel-dsp-code-loader.h"
#include "intel-dsp-modules.h"
#include "intel-hda-controller.h"

namespace audio {
namespace intel_hda {

namespace {

constexpr const char* ADSP_FIRMWARE_PATH = "dsp_fw_kbl_v3420.bin";

constexpr uint32_t EXT_MANIFEST_HDR_MAGIC = 0x31454124;

constexpr zx_duration_t INTEL_ADSP_TIMEOUT_NSEC = ZX_MSEC(50);             // 50mS Arbitrary
constexpr zx_duration_t INTEL_ADSP_POLL_NSEC = ZX_USEC(500);               // 500uS Arbitrary
constexpr zx_duration_t INTEL_ADSP_ROM_INIT_TIMEOUT_NSEC = ZX_SEC(1);      // 1S Arbitrary
constexpr zx_duration_t INTEL_ADSP_BASE_FW_INIT_TIMEOUT_NSEC = ZX_SEC(3);  // 3S Arbitrary
constexpr zx_duration_t INTEL_ADSP_POLL_FW_NSEC = ZX_MSEC(1);              //.1mS Arbitrary

}  // namespace

struct skl_adspfw_ext_manifest_hdr_t {
  uint32_t id;
  uint32_t len;
  uint32_t version_major;
  uint32_t version_minor;
  uint32_t entries;
} __PACKED;

IntelDsp::IntelDsp(IntelHDAController* controller, hda_pp_registers_t* pp_regs)
    : controller_(controller), pp_regs_(pp_regs) {
  const auto& info = controller_->dev_info();
  snprintf(log_prefix_, sizeof(log_prefix_), "IHDA DSP %02x:%02x.%01x", info.bus_id, info.dev_id,
           info.func_id);
}

IntelDsp::~IntelDsp() {
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

Status IntelDsp::ParseNhlt() {
  // Get NHLT size.
  const uint32_t signature = *reinterpret_cast<const uint32_t*>(ACPI_NHLT_SIGNATURE);
  size_t size;
  zx_status_t res = device_get_metadata_size(codec_device(), signature, &size);
  if (res != ZX_OK) {
    return Status(res, fbl::StringPrintf("Failed to fetch NHLT size."));
  }

  // Allocate buffer.
  fbl::AllocChecker ac;
  auto* buff = new (&ac) uint8_t[size];
  if (!ac.check()) {
    return Status(ZX_ERR_NO_MEMORY);
  }
  fbl::Array<uint8_t> buffer = fbl::Array<uint8_t>(buff, size);

  // Fetch actual NHLT data.
  size_t actual_size;
  res = device_get_metadata(codec_device(), signature, buffer.begin(), buffer.size(), &actual_size);
  if (res != ZX_OK) {
    return Status(res, "Failed to fetch NHLT");
  }
  if (actual_size != buffer.size()) {
    return Status(ZX_ERR_INTERNAL, "NHLT size different than reported.");
  }

  // Parse NHLT.
  StatusOr<std::unique_ptr<Nhlt>> nhlt =
      Nhlt::FromBuffer(fbl::Span<const uint8_t>(buffer.begin(), buffer.end()));
  if (!nhlt.ok()) {
    return nhlt.status();
  }
  nhlt_ = nhlt.ConsumeValueOrDie();

  if (zxlog_level_enabled(TRACE)) {
    nhlt_->Dump();
  }

  return OkStatus();
}

Status IntelDsp::Init(zx_device_t* dsp_dev) {
  Status result = Bind(dsp_dev, "intel-sst-dsp");
  if (!result.ok()) {
    return PrependMessage("Error binding DSP device", result);
  }

  result = SetupDspDevice();
  if (!result.ok()) {
    return PrependMessage("Error setting up DSP", result);
  }

  result = ParseNhlt();
  if (!result.ok()) {
    return PrependMessage("Error parsing NHLT", result);
  }
  LOG(TRACE, "parse success, found %zu formats\n", nhlt_->i2s_configs().size());

  // Perform hardware initialization in a thread.
  state_ = State::INITIALIZING;
  int c11_res = thrd_create(
      &init_thread_, [](void* ctx) { return static_cast<IntelDsp*>(ctx)->InitThread(); }, this);
  if (c11_res < 0) {
    state_ = State::ERROR;
    return Status(ZX_ERR_INTERNAL,
                  fbl::StringPrintf("Failed to create init thread (res = %d)\n", c11_res));
  }

  return OkStatus();
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
      [codec = fbl::RefPtr(this)](dispatcher::Channel* channel) -> zx_status_t {
        OBTAIN_EXECUTION_DOMAIN_TOKEN(t, codec->controller_->default_domain());
        return codec->ProcessClientRequest(channel, true);
      });

  dispatcher::Channel::ChannelClosedHandler chandler(
      [codec = fbl::RefPtr(this)](const dispatcher::Channel* channel) -> void {
        OBTAIN_EXECUTION_DOMAIN_TOKEN(t, codec->controller_->default_domain());
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
  res = CreateAndActivateChannel(controller_->default_domain(), std::move(phandler),
                                 std::move(chandler), &codec_driver_channel_, &client_channel);
  if (res == ZX_OK) {
    // If things went well, release the reference to the remote endpoint
    // from the zx::channel instance into the unmanaged world of DDK
    // protocols.
    *remote_endpoint_out = client_channel.release();
  }

  return res;
}

#define PROCESS_CMD(_req_ack, _req_driver_chan, _ioctl, _payload, _handler)    \
  case _ioctl:                                                                 \
    if (req_size != sizeof(req._payload)) {                                    \
      LOG(TRACE, "Bad " #_payload " request length (%u != %zu)\n", req_size,   \
          sizeof(req._payload));                                               \
      return ZX_ERR_INVALID_ARGS;                                              \
    }                                                                          \
    if ((_req_ack) && (req.hdr.cmd & IHDA_NOACK_FLAG)) {                       \
      LOG(TRACE, "Cmd " #_payload                                              \
                 " requires acknowledgement, but the "                         \
                 "NOACK flag was set!\n");                                     \
      return ZX_ERR_INVALID_ARGS;                                              \
    }                                                                          \
    if ((_req_driver_chan) && !is_driver_channel) {                            \
      LOG(TRACE, "Cmd " #_payload " requires a privileged driver channel.\n"); \
      return ZX_ERR_ACCESS_DENIED;                                             \
    }                                                                          \
    return _handler(channel, req._payload)
zx_status_t IntelDsp::ProcessClientRequest(dispatcher::Channel* channel, bool is_driver_channel) {
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
    LOG(TRACE, "Client request too small to contain header (%u < %zu)\n", req_size,
        sizeof(req.hdr));
    return ZX_ERR_INVALID_ARGS;
  }

  auto cmd_id = static_cast<ihda_cmd_t>(req.hdr.cmd & ~IHDA_NOACK_FLAG);
  if (req.hdr.transaction_id == IHDA_INVALID_TRANSACTION_ID) {
    LOG(TRACE, "Invalid transaction ID in client request 0x%04x\n", cmd_id);
    return ZX_ERR_INVALID_ARGS;
  }

  // Dispatch
  LOG(SPEW, "Client Request (cmd 0x%04x tid %u) len %u\n", req.hdr.cmd, req.hdr.transaction_id,
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
  auto type = req.input ? IntelHDAStream::Type::INPUT : IntelHDAStream::Type::OUTPUT;
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
  zx_status_t res =
      stream->SetStreamFormat(controller_->default_domain(), req.format, &client_channel);
  if (res != ZX_OK) {
    LOG(TRACE, "Failed to set stream format 0x%04hx for stream %hu (res %d)\n", req.format,
        req.stream_id, res);
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

Status IntelDsp::SetupDspDevice() {
  const zx_pcie_device_info_t& hda_dev_info = controller_->dev_info();
  snprintf(log_prefix_, sizeof(log_prefix_), "IHDA DSP %02x:%02x.%01x", hda_dev_info.bus_id,
           hda_dev_info.dev_id, hda_dev_info.func_id);
  // Fetch the bar which holds the Audio DSP registers.
  zx::vmo bar_vmo;
  size_t bar_size;
  zx_status_t res = GetMmio(bar_vmo.reset_and_get_address(), &bar_size);
  if (res != ZX_OK) {
    LOG(ERROR, "Failed to fetch DSP register VMO (err %u)\n", res);
    return Status(res);
  }

  if (bar_size != sizeof(adsp_registers_t)) {
    LOG(ERROR, "Bad register window size (expected 0x%zx got 0x%zx)\n", sizeof(adsp_registers_t),
        bar_size);
    return Status(res);
  }

  // Since this VMO provides access to our registers, make sure to set the
  // cache policy to UNCACHED_DEVICE
  res = bar_vmo.set_cache_policy(ZX_CACHE_POLICY_UNCACHED_DEVICE);
  if (res != ZX_OK) {
    LOG(ERROR, "Error attempting to set cache policy for PCI registers (res %d)\n", res);
    return Status(res);
  }

  // Map the VMO in, make sure to put it in the same VMAR as the rest of our
  // registers.
  constexpr uint32_t CPU_MAP_FLAGS = ZX_VM_PERM_READ | ZX_VM_PERM_WRITE;
  res = mapped_regs_.Map(bar_vmo, 0, bar_size, CPU_MAP_FLAGS);
  if (res != ZX_OK) {
    LOG(ERROR, "Error attempting to map registers (res %d)\n", res);
    return Status(res);
  }

  // Initialize IPC.
  ipc_ = CreateHardwareDspChannel(log_prefix_, regs(),
                                  [this](NotificationType type) { DspNotificationReceived(type); });

  // Initialize DSP module controller.
  module_controller_ = std::make_unique<DspModuleController>(ipc_.get());

  // Enable HDA interrupt. Interrupts are still masked at the DSP level.
  IrqEnable();

  return OkStatus();
}

void IntelDsp::DeviceShutdown() {
  if (state_ == State::INITIALIZING) {
    thrd_join(init_thread_, nullptr);
  }

  // Order is important below.
  // Disable Audio DSP and interrupt
  IrqDisable();
  Disable();

  // Reset and power down the DSP.
  ResetCore(ADSP_REG_ADSPCS_CORE0_MASK);
  PowerDownCore(ADSP_REG_ADSPCS_CORE0_MASK);

  ipc_->Shutdown();

  state_ = State::SHUT_DOWN;
}

zx_status_t IntelDsp::Suspend(uint8_t requested_state, bool enable_wake, uint8_t suspend_reason,
                              uint8_t* out_state) {
  switch (suspend_reason & DEVICE_MASK_SUSPEND_REASON) {
    case DEVICE_SUSPEND_REASON_POWEROFF:
      DeviceShutdown();
      *out_state = requested_state;
      return ZX_OK;
    default:
      *out_state = DEV_POWER_STATE_D0;
      return ZX_ERR_NOT_SUPPORTED;
  }
}

int IntelDsp::InitThread() {
  auto cleanup = fbl::MakeAutoCall([this]() { DeviceShutdown(); });

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
  st = WaitCondition(INTEL_ADSP_ROM_INIT_TIMEOUT_NSEC, INTEL_ADSP_POLL_FW_NSEC, [this]() -> bool {
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
  IntelDspCodeLoader loader(&regs()->cldma, controller_->pci_bti());
  zx_status_t st = loader.Initialize();
  if (st != ZX_OK) {
    LOG(ERROR, "Error initializing firmware code loader (err %d)\n", st);
    return st;
  }

  // Get the VMO containing the firmware.
  zx::vmo fw_vmo;
  size_t fw_size;
  st = load_firmware(codec_device(), ADSP_FIRMWARE_PATH, fw_vmo.reset_and_get_address(), &fw_size);
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
  st = pinned_fw.Pin(stripped_vmo, controller_->pci_bti()->initiator(), DSP_MAP_FLAGS);
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
  st = WaitCondition(INTEL_ADSP_BASE_FW_INIT_TIMEOUT_NSEC, INTEL_ADSP_POLL_FW_NSEC,
                     [this]() -> bool {
                       return ((REG_RD(&fw_regs()->fw_status) & ADSP_FW_STATUS_STATE_MASK) ==
                               ADSP_FW_STATUS_STATE_ENTER_BASE_FW);
                     });
  if (st != ZX_OK) {
    LOG(ERROR, "Error waiting for DSP base firmware entry (err %d, fw_status = 0x%08x)\n", st,
        REG_RD(&fw_regs()->fw_status));
    return st;
  }

  // Stop the DMA
  loader.StopTransfer();

  // Now check whether we received the FW Ready IPC. Receiving this IPC indicates the
  // IPC system is ready. Both FW_STATUS = ADSP_FW_STATUS_STATE_ENTER_BASE_FW and
  // receiving the IPC are required for the DSP to be operational.
  sync_completion_wait(&firmware_ready_, INTEL_ADSP_BASE_FW_INIT_TIMEOUT_NSEC);
  if (st != ZX_OK) {
    LOG(ERROR, "Error waiting for FW Ready IPC (err %d, fw_status = 0x%08x)\n", st,
        REG_RD(&fw_regs()->fw_status));
    return st;
  }

  return ZX_OK;
}

void IntelDsp::DspNotificationReceived(NotificationType type) {
  switch (type) {
    case NotificationType::FW_READY:
      // Indicate that the firmware is ready to go.
      sync_completion_signal(&firmware_ready_);
      break;

    case NotificationType::EXCEPTION_CAUGHT:
      LOG(ERROR, "DSP reported exception.\n");
      break;

    default:
      LOG(TRACE, "Received unknown notification type %d from DSP.\n", static_cast<int>(type));
      break;
  }
}

bool IntelDsp::IsCoreEnabled(uint8_t core_mask) {
  uint32_t val = REG_RD(&regs()->adspcs);
  bool enabled = (val & ADSP_REG_ADSPCS_CPA(core_mask)) && (val & ADSP_REG_ADSPCS_SPA(core_mask)) &&
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
  return WaitCondition(INTEL_ADSP_TIMEOUT_NSEC, INTEL_ADSP_POLL_NSEC, [this, &core_mask]() -> bool {
    return (REG_RD(&regs()->adspcs) & ADSP_REG_ADSPCS_CRST(core_mask)) != 0;
  });
}

zx_status_t IntelDsp::UnResetCore(uint8_t core_mask) {
  REG_CLR_BITS(&regs()->adspcs, ADSP_REG_ADSPCS_CRST(core_mask));
  return WaitCondition(INTEL_ADSP_TIMEOUT_NSEC, INTEL_ADSP_POLL_NSEC, [this, &core_mask]() -> bool {
    return (REG_RD(&regs()->adspcs) & ADSP_REG_ADSPCS_CRST(core_mask)) == 0;
  });
}

zx_status_t IntelDsp::PowerDownCore(uint8_t core_mask) {
  REG_CLR_BITS(&regs()->adspcs, ADSP_REG_ADSPCS_SPA(core_mask));
  return WaitCondition(INTEL_ADSP_TIMEOUT_NSEC, INTEL_ADSP_POLL_NSEC, [this, &core_mask]() -> bool {
    return (REG_RD(&regs()->adspcs) & ADSP_REG_ADSPCS_CPA(core_mask)) == 0;
  });
}

zx_status_t IntelDsp::PowerUpCore(uint8_t core_mask) {
  REG_SET_BITS(&regs()->adspcs, ADSP_REG_ADSPCS_SPA(core_mask));
  return WaitCondition(INTEL_ADSP_TIMEOUT_NSEC, INTEL_ADSP_POLL_NSEC, [this, &core_mask]() -> bool {
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

  // Allow the IPC module to check for incoming messages.
  if (ipc_ != nullptr) {
    ipc_->ProcessIrq();
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
    LOG(ERROR, "Bad register window type (expected %u got %u)\n", ZX_PCI_BAR_TYPE_MMIO,
        bar_info.type);
    return ZX_ERR_INTERNAL;
  }

  *out_vmo = bar_info.handle;
  *out_size = bar_info.size;
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

void IntelDsp::Disable() { REG_WR(&pp_regs_->ppctl, 0u); }

void IntelDsp::IrqEnable() { REG_SET_BITS<uint32_t>(&pp_regs_->ppctl, HDA_PPCTL_PIE); }

void IntelDsp::IrqDisable() { REG_CLR_BITS<uint32_t>(&pp_regs_->ppctl, HDA_PPCTL_PIE); }

}  // namespace intel_hda
}  // namespace audio
