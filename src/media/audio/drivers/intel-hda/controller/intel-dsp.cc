// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "intel-dsp.h"

#include <fidl/fuchsia.hardware.acpi/cpp/wire.h>
#include <lib/ddk/metadata.h>
#include <lib/device-protocol/pci.h>
#include <lib/fit/defer.h>
#include <lib/fzl/vmo-mapper.h>
#include <string.h>
#include <zircon/errors.h>

#include <cstring>
#include <map>
#include <utility>

#include <fbl/alloc_checker.h>
#include <fbl/array.h>
#include <fbl/auto_lock.h>
#include <fbl/string_printf.h>
#include <intel-hda/utils/nhlt.h>

#include "intel-dsp-code-loader.h"
#include "intel-dsp-modules.h"
#include "intel-hda-controller.h"
#include "src/devices/lib/acpi/util.h"

namespace audio {
namespace intel_hda {

namespace {

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

IntelDsp::IntelDsp(IntelHDAController* controller, MMIO_PTR hda_pp_registers_t* pp_regs)
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

zx::result<> IntelDsp::ParseNhlt() {
  std::array<fuchsia_hardware_acpi::wire::Object, 3> args;
  // Reference:
  // Intel Smart Sound Technology NHLT Specification
  // Architecture Guide/Overview
  // Revision 1.0
  // June 2018
  //
  // 595976-intel-sst-nhlt-archguide-rev1p0.pdf
  acpi::Uuid nhlt_query_uuid =
      acpi::Uuid::Create(0xa69f886e, 0x6ceb, 0x4594, 0xa41f, 0x7b5dce24c553);
  uint64_t nhlt_query_revid = 1;
  uint64_t nhlt_query_func_index = 1;

  auto uuid_buf = fidl::VectorView<uint8_t>::FromExternal(nhlt_query_uuid.bytes, acpi::kUuidBytes);
  args[0] = fuchsia_hardware_acpi::wire::Object::WithBufferVal(
      fidl::ObjectView<fidl::VectorView<uint8_t>>::FromExternal(&uuid_buf));
  args[1] = fuchsia_hardware_acpi::wire::Object::WithIntegerVal(
      fidl::ObjectView<uint64_t>::FromExternal(&nhlt_query_revid));
  args[2] = fuchsia_hardware_acpi::wire::Object::WithIntegerVal(
      fidl::ObjectView<uint64_t>::FromExternal(&nhlt_query_func_index));
  auto& acpi = controller_->acpi().borrow();
  auto result = acpi->EvaluateObject(
      "_DSM", fuchsia_hardware_acpi::wire::EvaluateObjectMode::kParseResources,
      fidl::VectorView<fuchsia_hardware_acpi::wire::Object>::FromExternal(args));
  if (!result.ok()) {
    return zx::error(result.status());
  }
  if (result->is_error()) {
    LOG(ERROR, "NHLT query failed: %d", fidl::ToUnderlying(result->error_value()));
    return zx::error(ZX_ERR_INTERNAL);
  }

  fidl::WireOptional<fuchsia_hardware_acpi::wire::EncodedObject>& maybe_encoded =
      result->value()->result;
  if (!maybe_encoded.has_value() || !maybe_encoded->is_resources() ||
      maybe_encoded->resources().empty() || !maybe_encoded->resources()[0].is_mmio()) {
    LOG(ERROR, "ACPI did not return NHLT resource");
    return zx::error(ZX_ERR_INTERNAL);
  }
  auto& resource = maybe_encoded->resources()[0].mmio();
  size_t size = resource.size;
  // Allocate buffer.
  fbl::AllocChecker ac;
  auto* buff = new (&ac) uint8_t[size];
  if (!ac.check()) {
    return zx::error(ZX_ERR_NO_MEMORY);
  }
  fbl::Array<uint8_t> buffer = fbl::Array<uint8_t>(buff, size);

  // We have to map in a physical VMO to read from it.
  fzl::VmoMapper mapper;
  zx_status_t res = mapper.Map(resource.vmo, 0, 0, ZX_VM_PERM_READ);
  if (res != ZX_OK) {
    return zx::error(res);
  }
  // Fetch actual NHLT data.
  memcpy(buffer.begin(), static_cast<uint8_t*>(mapper.start()) + resource.offset, buffer.size());
  // Parse NHLT.
  zx::result<std::unique_ptr<Nhlt>> nhlt =
      Nhlt::FromBuffer(cpp20::span<const uint8_t>(buffer.begin(), buffer.end()));
  if (!nhlt.is_ok()) {
    return zx::error(nhlt.status_value());
  }
  nhlt_ = std::move(nhlt.value());

  if (zxlog_level_enabled(DEBUG)) {
    nhlt_->Dump();
  }

  return zx::ok();
}

zx::result<> IntelDsp::Init(zx_device_t* dsp_dev) {
  zx::result result = Bind(dsp_dev, "intel-sst-dsp");
  if (!result.is_ok()) {
    LOG(ERROR, "Error binding DSP device");
    return zx::error(result.status_value());
  }

  result = SetupDspDevice();
  if (!result.is_ok()) {
    LOG(ERROR, "Error setting up DSP");
    return zx::error(result.status_value());
  }

  result = ParseNhlt();
  if (!result.is_ok()) {
    LOG(ERROR, "Error parsing NHLT");
    return zx::error(result.status_value());
  }
  LOG(DEBUG, "parse success, found %zu formats", nhlt_->configs().size());

  result = InitializeDsp();
  if (!result.is_ok()) {
    LOG(ERROR, "Error initializing DSP");
    return zx::error(result.status_value());
  }

  result = CreateAndStartStreams();
  if (!result.is_ok()) {
    DeviceShutdown();
    LOG(ERROR, "Error creating and publishing streams");
    return zx::error(result.status_value());
  }

  return zx::ok();
}

MMIO_PTR adsp_registers_t* IntelDsp::regs() const {
  return reinterpret_cast<MMIO_PTR adsp_registers_t*>(mapped_regs_->get());
}

MMIO_PTR adsp_fw_registers_t* IntelDsp::fw_regs() const {
  return reinterpret_cast<MMIO_PTR adsp_fw_registers_t*>(
      static_cast<MMIO_PTR uint8_t*>(mapped_regs_->get()) + SKL_ADSP_SRAM0_OFFSET);
}

zx_status_t IntelDsp::CodecGetDispatcherChannel(zx_handle_t* remote_endpoint_out) {
  if (!remote_endpoint_out)
    return ZX_ERR_INVALID_ARGS;

  zx::channel channel_local;
  zx::channel channel_remote;
  zx_status_t res = zx::channel::create(0, &channel_local, &channel_remote);
  if (res != ZX_OK) {
    return res;
  }

  fbl::AutoLock lock(&codec_driver_channel_lock_);
  fbl::RefPtr<Channel> channel = Channel::Create(std::move(channel_local));
  if (channel == nullptr) {
    return ZX_ERR_NO_MEMORY;
  }

  codec_driver_channel_ = std::move(channel);
  codec_driver_channel_->SetHandler(
      [dsp = fbl::RefPtr(this)](async_dispatcher_t* dispatcher, async::WaitBase* wait,
                                zx_status_t status, const zx_packet_signal_t* signal) {
        dsp->ChannelSignalled(dispatcher, wait, status, signal);
      });
  res = codec_driver_channel_->BeginWait(controller_->dispatcher());
  if (res != ZX_OK) {
    codec_driver_channel_.reset();
    return res;
  }

  // If things went well, release the reference to the remote endpoint
  // from the zx::channel instance into the unmanaged world of DDK
  // protocols.
  *remote_endpoint_out = channel_remote.release();
  return ZX_OK;
}

void IntelDsp::ChannelSignalled(async_dispatcher_t* dispatcher, async::WaitBase* wait,
                                zx_status_t status, const zx_packet_signal_t* signal) {
  if (status != ZX_OK) {
    if (status != ZX_ERR_CANCELED) {  // Cancel is expected.
      return;
    }
  }
  bool readable_asserted = signal->observed & ZX_CHANNEL_READABLE;
  bool peer_closed_asserted = signal->observed & ZX_CHANNEL_PEER_CLOSED;
  if (readable_asserted) {
    zx_status_t status = ProcessClientRequest(true);
    if (status != ZX_OK) {
      peer_closed_asserted = true;
    }
  }
  if (peer_closed_asserted) {
    ProcessClientDeactivate();
  } else if (readable_asserted) {
    wait->Begin(dispatcher);
  }
}

#define PROCESS_CMD_INNER(_req_ack, _req_driver_chan, _ioctl, _payload, _handler)                 \
  case _ioctl:                                                                                    \
    if (req_size != sizeof(req._payload)) {                                                       \
      LOG(DEBUG, "Bad " #_payload " request length (%u != %zu)", req_size, sizeof(req._payload)); \
      return ZX_ERR_INVALID_ARGS;                                                                 \
    }                                                                                             \
    if ((_req_ack) && (req.hdr.cmd & IHDA_NOACK_FLAG)) {                                          \
      LOG(DEBUG, "Cmd " #_payload                                                                 \
                 " requires acknowledgement, but the "                                            \
                 "NOACK flag was set!");                                                          \
      return ZX_ERR_INVALID_ARGS;                                                                 \
    }                                                                                             \
    if ((_req_driver_chan) && !is_driver_channel) {                                               \
      LOG(DEBUG, "Cmd " #_payload " requires a privileged driver channel.");                      \
      return ZX_ERR_ACCESS_DENIED;                                                                \
    }

#define PROCESS_CMD(_req_ack, _req_driver_chan, _ioctl, _payload, _handler) \
  PROCESS_CMD_INNER(_req_ack, _req_driver_chan, _ioctl, _payload, _handler) \
  return _handler(channel, req._payload)

#define PROCESS_CMD_WITH_HANDLE(_req_ack, _req_driver_chan, _ioctl, _payload, _handler) \
  PROCESS_CMD_INNER(_req_ack, _req_driver_chan, _ioctl, _payload, _handler)             \
  return _handler(channel, req._payload, std::move(rxed_handle))

zx_status_t IntelDsp::ProcessClientRequest(bool is_driver_channel) {
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
  fbl::AutoLock lock(&codec_driver_channel_lock_);
  Channel* channel = codec_driver_channel_.get();
  ZX_DEBUG_ASSERT(channel != nullptr);
  zx::handle rxed_handle;
  res = codec_driver_channel_->Read(&req, sizeof(req), &req_size, rxed_handle);
  if (res != ZX_OK) {
    LOG(DEBUG, "Failed to read client request (res %d)", res);
    return res;
  }

  // Sanity checks.
  if (req_size < sizeof(req.hdr)) {
    LOG(DEBUG, "Client request too small to contain header (%u < %zu)", req_size, sizeof(req.hdr));
    return ZX_ERR_INVALID_ARGS;
  }

  auto cmd_id = static_cast<ihda_cmd_t>(req.hdr.cmd & ~IHDA_NOACK_FLAG);
  if (req.hdr.transaction_id == IHDA_INVALID_TRANSACTION_ID) {
    LOG(DEBUG, "Invalid transaction ID in client request 0x%04x", cmd_id);
    return ZX_ERR_INVALID_ARGS;
  }

  // Dispatch
  LOG(TRACE, "Client Request (cmd 0x%04x tid %u) len %u", req.hdr.cmd, req.hdr.transaction_id,
      req_size);

  switch (cmd_id) {
    PROCESS_CMD(true, true, IHDA_CODEC_REQUEST_STREAM, request_stream, ProcessRequestStream);
    PROCESS_CMD(false, true, IHDA_CODEC_RELEASE_STREAM, release_stream, ProcessReleaseStream);
    PROCESS_CMD_WITH_HANDLE(false, true, IHDA_CODEC_SET_STREAM_FORMAT, set_stream_fmt,
                            ProcessSetStreamFmt);
    default:
      LOG(DEBUG, "Unrecognized command ID 0x%04x", req.hdr.cmd);
      return ZX_ERR_INVALID_ARGS;
  }
}
#undef PROCESS_CMD
#undef PROCESS_CMD_INNER
#undef PROCESS_CMD_WITH_HANDLE

void IntelDsp::ProcessClientDeactivate() {
  // This should be the driver channel (client channels created with IOCTL do
  // not register a deactivate handler).  Start by releasing the internal
  // channel reference from within the codec_driver_channel_lock.
  {
    fbl::AutoLock lock(&codec_driver_channel_lock_);
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

zx_status_t IntelDsp::ProcessRequestStream(Channel* channel,
                                           const ihda_proto::RequestStreamReq& req) {
  ZX_DEBUG_ASSERT(channel != nullptr);

  ihda_proto::RequestStreamResp resp;
  resp.hdr = req.hdr;

  // Attempt to get a stream of the proper type.
  auto type = req.input ? IntelHDAStream::Type::INPUT : IntelHDAStream::Type::OUTPUT;
  auto stream = controller_->AllocateStream(type);

  if (stream != nullptr) {
    LOG(DEBUG, "Decouple stream #%u", stream->id());
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

zx_status_t IntelDsp::ProcessReleaseStream(Channel* channel,
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

  LOG(DEBUG, "Couple stream #%u", stream->id());

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

zx_status_t IntelDsp::ProcessSetStreamFmt(Channel* channel, const ihda_proto::SetStreamFmtReq& req,
                                          zx::handle rxed_handle) {
  ZX_DEBUG_ASSERT(channel != nullptr);

  zx::channel server_channel;
  zx_status_t res = ConvertHandle(&rxed_handle, &server_channel);
  if (res != ZX_OK) {
    LOG(DEBUG, "Failed to convert handle to channel (res %d)", res);
    return res;
  }

  // Sanity check the requested format.
  if (!StreamFormat(req.format).SanityCheck()) {
    LOG(DEBUG, "Invalid encoded stream format 0x%04hx!", req.format);
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
  res = stream->SetStreamFormat(controller_->dispatcher(), req.format, std::move(server_channel));
  if (res != ZX_OK) {
    LOG(DEBUG, "Failed to set stream format 0x%04hx for stream %hu (res %d)", req.format,
        req.stream_id, res);
    return res;
  }

  // Reply to the codec driver.
  ihda_proto::SetStreamFmtResp resp;
  resp.hdr = req.hdr;
  res = channel->Write(&resp, sizeof(resp));

  if (res != ZX_OK)
    LOG(DEBUG, "Failed to send stream channel back to codec driver (res %d)", res);

  return res;
}

zx::result<> IntelDsp::SetupDspDevice() {
  const fuchsia_hardware_pci::wire::DeviceInfo& hda_dev_info = controller_->dev_info();
  snprintf(log_prefix_, sizeof(log_prefix_), "IHDA DSP %02x:%02x.%01x", hda_dev_info.bus_id,
           hda_dev_info.dev_id, hda_dev_info.func_id);
  // Fetch the BAR which holds the Audio DSP registers (BAR 4).
  std::optional<fdf::MmioBuffer> mmio;
  zx_status_t res = controller_->pci().MapMmio(4u, ZX_CACHE_POLICY_UNCACHED_DEVICE, &mmio);
  if (res != ZX_OK) {
    LOG(ERROR, "Failed to fetch and map DSP register (err %u)", res);
    return zx::error(res);
  }

  if (mmio->get_size() < sizeof(adsp_registers_t)) {
    LOG(ERROR, "Bad register window size (expected 0x%zx got 0x%zx)", sizeof(adsp_registers_t),
        mmio->get_size());
    return zx::error(res);
  }
  mapped_regs_ = std::move(mmio);

  // Initialize IPC.
  ipc_ = CreateHardwareDspChannel(log_prefix_, regs(),
                                  [this](NotificationType type) { DspNotificationReceived(type); });

  // Initialize DSP module controller.
  module_controller_ = std::make_unique<DspModuleController>(ipc_.get());

  // Enable HDA interrupt. Interrupts are still masked at the DSP level.
  IrqEnable();

  return zx::ok();
}

void IntelDsp::DeviceShutdown() {
  ProcessClientDeactivate();
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

zx::result<> IntelDsp::InitializeDsp() {
  auto cleanup = fit::defer([this]() { DeviceShutdown(); });

  // Enable Audio DSP
  Enable();

  // The HW loads the DSP base firmware from ROM during the initialization,
  // when the Tensilica Core is out of reset, but halted.
  zx_status_t st = Boot();
  if (st != ZX_OK) {
    LOG(ERROR, "Error in DSP boot (err %d)", st);
    return zx::error(st);
  }

  // Wait for ROM initialization done
  st = WaitCondition(INTEL_ADSP_ROM_INIT_TIMEOUT_NSEC, INTEL_ADSP_POLL_FW_NSEC, [this]() -> bool {
    return ((REG_RD(&fw_regs()->fw_status) & ADSP_FW_STATUS_STATE_MASK) ==
            ADSP_FW_STATUS_STATE_INITIALIZATION_DONE);
  });
  if (st != ZX_OK) {
    LOG(ERROR, "Error waiting for DSP ROM init (err %d)", st);
    return zx::error(st);
  }

  state_ = State::OPERATING;
  EnableInterrupts();

  // Load DSP Firmware
  st = LoadFirmware();
  if (st != ZX_OK) {
    LOG(ERROR, "Error loading firmware (err %d)", st);
    return zx::error(st);
  }

  // DSP Firmware is now ready.
  LOG(INFO, "DSP firmware ready");
  cleanup.cancel();
  return zx::ok();
}

zx_status_t IntelDsp::Boot() {
  zx_status_t st = ZX_OK;

  // Put core into reset
  if ((st = ResetCore(ADSP_REG_ADSPCS_CORE0_MASK)) != ZX_OK) {
    LOG(ERROR, "Error attempting to enter reset on core 0 (err %d)", st);
    return st;
  }

  // Power down core
  if ((st = PowerDownCore(ADSP_REG_ADSPCS_CORE0_MASK)) != ZX_OK) {
    LOG(ERROR, "Error attempting to power down core 0 (err %d)", st);
    return st;
  }

  // Power up core
  if ((st = PowerUpCore(ADSP_REG_ADSPCS_CORE0_MASK)) != ZX_OK) {
    LOG(ERROR, "Error attempting to power up core 0 (err %d)", st);
    return st;
  }

  // Take core out of reset
  if ((st = UnResetCore(ADSP_REG_ADSPCS_CORE0_MASK)) != ZX_OK) {
    LOG(ERROR, "Error attempting to take core 0 out of reset (err %d)", st);
    return st;
  }

  // Run core
  RunCore(ADSP_REG_ADSPCS_CORE0_MASK);
  if (!IsCoreEnabled(ADSP_REG_ADSPCS_CORE0_MASK)) {
    LOG(ERROR, "Failed to start core 0");
    ResetCore(ADSP_REG_ADSPCS_CORE0_MASK);
    return st;
  }

  LOG(DEBUG, "DSP core 0 booted!");
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
    LOG(ERROR, "Error initializing firmware code loader (err %d)", st);
    return st;
  }

  // Get the VMO containing the firmware.
  zx::vmo fw_vmo;
  size_t fw_size;
  st = load_firmware(codec_device(), ADSP_FIRMWARE_PATH, fw_vmo.reset_and_get_address(), &fw_size);
  if (st != ZX_OK) {
    LOG(ERROR, "Error fetching firmware (err %d)", st);
    return st;
  }

  // The max length of the firmware is 256 pages, assuming a fully distinguous VMO.
  const size_t max_fw_bytes = zx_system_get_page_size() * IntelDspCodeLoader::MAX_BDL_LENGTH;
  if (fw_size > max_fw_bytes) {
    LOG(ERROR, "DSP firmware is too big (0x%zx bytes > 0x%zx bytes)", fw_size, max_fw_bytes);
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
    LOG(ERROR, "Error creating DSP firmware VMO (err %d)", st);
    return st;
  }

  size_t stripped_size = fw_size;
  st = StripFirmware(fw_vmo, stripped_fw.start(), &stripped_size);
  if (st != ZX_OK) {
    LOG(ERROR, "Error stripping DSP firmware (err %d)", st);
    return st;
  }

  // Pin this VMO and grant the controller access to it.  The controller
  // should only need read access to the firmware.
  constexpr uint32_t DSP_MAP_FLAGS = ZX_BTI_PERM_READ;
  fzl::PinnedVmo pinned_fw;
  st = pinned_fw.Pin(stripped_vmo, controller_->pci_bti()->initiator(), DSP_MAP_FLAGS);
  if (st != ZX_OK) {
    LOG(ERROR, "Failed to pin pages for DSP firmware (res %d)", st);
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
    LOG(ERROR, "Error waiting for DSP base firmware entry (err %d, fw_status = 0x%08x)", st,
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
    LOG(ERROR, "Error waiting for FW Ready IPC (err %d, fw_status = 0x%08x)", st,
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
      LOG(ERROR, "DSP reported exception.");
      break;

    default:
      LOG(DEBUG, "Received unknown notification type %d from DSP.", static_cast<int>(type));
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
    LOG(DEBUG, "Got CLDMA irq");
    uint32_t w = REG_RD(&regs()->cldma.stream.ctl_sts.w);
    REG_WR(&regs()->cldma.stream.ctl_sts.w, w);
  }

  // Allow the IPC module to check for incoming messages.
  if (ipc_ != nullptr) {
    ipc_->ProcessIrq();
  }
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
