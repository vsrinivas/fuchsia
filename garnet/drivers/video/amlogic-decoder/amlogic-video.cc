// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "amlogic-video.h"

#include <lib/zx/channel.h>
#include <memory.h>
#include <stdint.h>
#include <zircon/errors.h>
#include <zircon/syscalls.h>
#include <zircon/syscalls/smc.h>

#include <chrono>
#include <memory>
#include <thread>

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/platform-defs.h>
#include <ddk/protocol/composite.h>
#include <ddk/protocol/platform/device.h>
#include <hw/reg.h>
#include <hwreg/bitfields.h>
#include <hwreg/mmio.h>

#include "device_ctx.h"
#include "device_fidl.h"
#include "hevcdec.h"
#include "local_codec_factory.h"
#include "macros.h"
#include "memory_barriers.h"
#include "mpeg12_decoder.h"
#include "pts_manager.h"
#include "registers.h"
#include "util.h"
#include "vdec1.h"
#include "video_firmware_session.h"

// TODO(35200):
//
// AllocateIoBuffer() - only used by VP9 - switch to InternalBuffer when we do zero copy on input
// for VP9.
//
// (AllocateStreamBuffer() has been moved to InternalBuffer.)
// (VideoDecoder::Owner::ProtectableHardwareUnit::kParser pays attention to is_secure.)
//
// (Fine as io_buffer_t, at least for now (for both h264 and VP9):
//  search_pattern_ - HW only reads this
//  parser_input_ - not used when secure)

// These match the regions exported when the bus device was added.
enum MmioRegion {
  kCbus,
  kDosbus,
  kHiubus,
  kAobus,
  kDmc,
};

enum Interrupt {
  kDemuxIrq,
  kParserIrq,
  kDosMbox0Irq,
  kDosMbox1Irq,
  kDosMbox2Irq,
};

enum {
  kComponentPdev = 0,
  kComponentSysmem = 1,
  kComponentCanvas = 2,
  // The tee is optional.
  kComponentTee = 3,
  // with tee
  kMaxComponentCount = 4,
  // without tee
  kMinComponentCount = 3,
};

AmlogicVideo::AmlogicVideo() {
  vdec1_core_ = std::make_unique<Vdec1>(this);
  hevc_core_ = std::make_unique<HevcDec>(this);
}

AmlogicVideo::~AmlogicVideo() {
  if (vdec0_interrupt_handle_) {
    zx_interrupt_destroy(vdec0_interrupt_handle_.get());
    if (vdec0_interrupt_thread_.joinable())
      vdec0_interrupt_thread_.join();
  }
  if (vdec1_interrupt_handle_) {
    zx_interrupt_destroy(vdec1_interrupt_handle_.get());
    if (vdec1_interrupt_thread_.joinable())
      vdec1_interrupt_thread_.join();
  }
  swapped_out_instances_.clear();
  if (core_)
    core_->PowerOff();
  current_instance_.reset();
  core_ = nullptr;
  hevc_core_.reset();
  vdec1_core_.reset();
}

// TODO: Remove once we can add single-instance decoders through
// AddNewDecoderInstance.
void AmlogicVideo::SetDefaultInstance(std::unique_ptr<VideoDecoder> decoder, bool hevc) {
  DecoderCore* core = hevc ? hevc_core_.get() : vdec1_core_.get();
  assert(!stream_buffer_);
  assert(!current_instance_);
  current_instance_ = std::make_unique<DecoderInstance>(std::move(decoder), core);
  video_decoder_ = current_instance_->decoder();
  stream_buffer_ = current_instance_->stream_buffer();
  core_ = core;
  core_->PowerOn();
}

void AmlogicVideo::AddNewDecoderInstance(std::unique_ptr<DecoderInstance> instance) {
  swapped_out_instances_.push_back(std::move(instance));
}

void AmlogicVideo::UngateClocks() {
  HhiGclkMpeg0::Get().ReadFrom(hiubus_.get()).set_dos(true).WriteTo(hiubus_.get());
  HhiGclkMpeg1::Get()
      .ReadFrom(hiubus_.get())
      .set_aiu(0xff)
      .set_demux(true)
      .set_audio_in(true)
      .WriteTo(hiubus_.get());
  HhiGclkMpeg2::Get().ReadFrom(hiubus_.get()).set_vpu_interrupt(true).WriteTo(hiubus_.get());
  UngateParserClock();
}

void AmlogicVideo::UngateParserClock() {
  is_parser_gated_ = false;
  HhiGclkMpeg1::Get().ReadFrom(hiubus_.get()).set_u_parser_top(true).WriteTo(hiubus_.get());
}

void AmlogicVideo::GateClocks() {
  // Keep VPU interrupt enabled, as it's used for vsync by the display.
  HhiGclkMpeg1::Get()
      .ReadFrom(hiubus_.get())
      .set_u_parser_top(false)
      .set_aiu(0)
      .set_demux(false)
      .set_audio_in(false)
      .WriteTo(hiubus_.get());
  HhiGclkMpeg0::Get().ReadFrom(hiubus_.get()).set_dos(false).WriteTo(hiubus_.get());
  GateParserClock();
}

void AmlogicVideo::GateParserClock() {
  is_parser_gated_ = true;
  HhiGclkMpeg1::Get().ReadFrom(hiubus_.get()).set_u_parser_top(false).WriteTo(hiubus_.get());
}

void AmlogicVideo::ClearDecoderInstance() {
  std::lock_guard<std::mutex> lock(video_decoder_lock_);
  assert(current_instance_);
  assert(swapped_out_instances_.size() == 0);
  current_instance_.reset();
  core_->PowerOff();
  core_ = nullptr;
  video_decoder_ = nullptr;
  stream_buffer_ = nullptr;
}

void AmlogicVideo::RemoveDecoder(VideoDecoder* decoder) {
  DLOG("Removing decoder: %p\n", decoder);
  std::lock_guard<std::mutex> lock(video_decoder_lock_);
  if (current_instance_ && current_instance_->decoder() == decoder) {
    current_instance_.reset();
    video_decoder_ = nullptr;
    stream_buffer_ = nullptr;
    core_->PowerOff();
    core_ = nullptr;
    TryToReschedule();
    return;
  }
  for (auto it = swapped_out_instances_.begin(); it != swapped_out_instances_.end(); ++it) {
    if ((*it)->decoder() != decoder)
      continue;
    swapped_out_instances_.erase(it);
    return;
  }
}

zx_status_t AmlogicVideo::AllocateStreamBuffer(StreamBuffer* buffer, uint32_t size, bool use_parser,
                                               bool is_secure) {
  // So far, is_secure can only be true if use_parser is also true.
  ZX_DEBUG_ASSERT(!is_secure || use_parser);
  // is_writable is always true because we either need to write into this buffer using the CPU, or
  // using the parser - either way we'll be writing.
  auto create_result =
      InternalBuffer::Create("AMLStreamBuffer", &sysmem_sync_ptr_, zx::unowned_bti(bti_), size,
                             is_secure, /*is_writable=*/true, /*is_mapping_needed=*/!use_parser);
  if (!create_result.is_ok()) {
    DECODE_ERROR("Failed to make video fifo: %d", create_result.error());
    return create_result.error();
  }
  buffer->optional_buffer().emplace(create_result.take_value());

  // Sysmem guarantees that the newly-allocated buffer starts out zeroed and cache clean, to the
  // extent possible based on is_secure.

  return ZX_OK;
}

void AmlogicVideo::InitializeStreamInput(bool use_parser) {
  uint32_t buffer_address = truncate_to_32(stream_buffer_->buffer().phys_base());
  core_->InitializeStreamInput(use_parser, buffer_address, stream_buffer_->buffer().size());
}

zx_status_t AmlogicVideo::InitializeStreamBuffer(bool use_parser, uint32_t size, bool is_secure) {
  zx_status_t status = AllocateStreamBuffer(stream_buffer_, size, use_parser, is_secure);
  if (status != ZX_OK) {
    return status;
  }

  status = SetProtected(VideoDecoder::Owner::ProtectableHardwareUnit::kParser, is_secure);
  if (status != ZX_OK) {
    return status;
  }

  InitializeStreamInput(use_parser);
  return ZX_OK;
}

std::unique_ptr<CanvasEntry> AmlogicVideo::ConfigureCanvas(io_buffer_t* io_buffer, uint32_t offset,
                                                           uint32_t width, uint32_t height,
                                                           uint32_t wrap, uint32_t blockmode) {
  assert(width % 8 == 0);
  assert(offset % 8 == 0);
  canvas_info_t info;
  info.height = height;
  info.stride_bytes = width;
  info.wrap = wrap;
  info.blkmode = blockmode;
  enum {
    kSwapBytes = 1,
    kSwapWords = 2,
    kSwapDoublewords = 4,
    kSwapQuadwords = 8,
  };
  info.endianness =
      kSwapBytes | kSwapWords | kSwapDoublewords;  // 64-bit big-endian to little-endian conversion.
  info.flags = CANVAS_FLAGS_READ | CANVAS_FLAGS_WRITE;

  zx::unowned_vmo vmo(io_buffer->vmo_handle);
  zx::vmo dup_vmo;
  zx_status_t status = vmo->duplicate(ZX_RIGHT_SAME_RIGHTS, &dup_vmo);
  if (status != ZX_OK) {
    DECODE_ERROR("Failed to duplicate handle, status: %d\n", status);
    return nullptr;
  }
  uint8_t idx;
  status = amlogic_canvas_config(&canvas_, dup_vmo.release(), offset, &info, &idx);
  if (status != ZX_OK) {
    DECODE_ERROR("Failed to configure canvas, status: %d\n", status);
    return nullptr;
  }

  return std::make_unique<CanvasEntry>(this, idx);
}

void AmlogicVideo::FreeCanvas(CanvasEntry* canvas) {
  amlogic_canvas_free(&canvas_, canvas->index());
}

zx_status_t AmlogicVideo::AllocateIoBuffer(io_buffer_t* buffer, size_t size,
                                           uint32_t alignment_log2, uint32_t flags,
                                           const char* name) {
  zx_status_t status = io_buffer_init_aligned(buffer, bti_.get(), size, alignment_log2, flags);
  if (status != ZX_OK)
    return status;

  SetIoBufferName(buffer, name);

  return ZX_OK;
}

fuchsia::sysmem::AllocatorSyncPtr& AmlogicVideo::SysmemAllocatorSyncPtr() {
  return sysmem_sync_ptr_;
}

// This parser handles MPEG elementary streams.
zx_status_t AmlogicVideo::InitializeEsParser() {
  std::lock_guard<std::mutex> lock(video_decoder_lock_);
  // Only ever allow one parser, since it takes ownership of the interrupt
  // handle.
  if (!parser_)
    parser_ = std::make_unique<Parser>(this, std::move(parser_interrupt_handle_));
  return parser_->InitializeEsParser(current_instance_.get());
}

zx_status_t AmlogicVideo::ProcessVideoNoParser(const void* data, uint32_t len,
                                               uint32_t* written_out) {
  return ProcessVideoNoParserAtOffset(data, len, core_->GetStreamInputOffset(), written_out);
}

zx_status_t AmlogicVideo::ProcessVideoNoParserAtOffset(const void* data, uint32_t len,
                                                       uint32_t write_offset,
                                                       uint32_t* written_out) {
  uint32_t read_offset = core_->GetReadOffset();
  uint32_t available_space;
  if (read_offset > write_offset) {
    available_space = read_offset - write_offset;
  } else {
    available_space = stream_buffer_->buffer().size() - write_offset + read_offset;
  }
  // Subtract 8 to ensure the read pointer doesn't become equal to the write
  // pointer, as that means the buffer is empty.
  available_space = available_space > 8 ? available_space - 8 : 0;
  if (!written_out) {
    if (len > available_space) {
      DECODE_ERROR("Video too large\n");
      return ZX_ERR_OUT_OF_RANGE;
    }
  } else {
    len = std::min(len, available_space);
    *written_out = len;
  }

  stream_buffer_->set_data_size(stream_buffer_->data_size() + len);
  uint32_t input_offset = 0;
  while (len > 0) {
    uint32_t write_length = len;
    if (write_offset + len > stream_buffer_->buffer().size())
      write_length = stream_buffer_->buffer().size() - write_offset;
    memcpy(stream_buffer_->buffer().virt_base() + write_offset,
           static_cast<const uint8_t*>(data) + input_offset, write_length);
    stream_buffer_->buffer().CacheFlush(write_offset, write_length);
    write_offset += write_length;
    if (write_offset == stream_buffer_->buffer().size())
      write_offset = 0;
    len -= write_length;
    input_offset += write_length;
  }
  BarrierAfterFlush();
  core_->UpdateWritePointer(stream_buffer_->buffer().phys_base() + write_offset);
  return ZX_OK;
}

void AmlogicVideo::SwapOutCurrentInstance() {
  ZX_DEBUG_ASSERT(!!current_instance_);
  // FrameWasOutput() is called during handling of kVp9CommandNalDecodeDone on
  // the interrupt thread, which means the decoder HW is currently paused,
  // which means it's ok to save the state before the stop+wait (without any
  // explicit pause before the save here).  The decoder HW remains paused
  // after the save, and makes no further progress until later after the
  // restore.
  if (!current_instance_->input_context()) {
    current_instance_->InitializeInputContext();
    if (core_->InitializeInputContext(current_instance_->input_context(),
                                      current_instance_->decoder()->is_secure()) != ZX_OK) {
      // TODO: exit cleanly
      exit(-1);
    }
  }
  video_decoder_->SetSwappedOut();
  core_->SaveInputContext(current_instance_->input_context());
  core_->StopDecoding();
  core_->WaitForIdle();
  // TODO: Avoid power off if swapping to another instance on the same core.
  core_->PowerOff();
  core_ = nullptr;
  // Round-robin; place at the back of the line.
  swapped_out_instances_.push_back(std::move(current_instance_));
}

void AmlogicVideo::TryToReschedule() {
  DLOG("AmlogicVideo::TryToReschedule\n");
  if (swapped_out_instances_.size() == 0) {
    DLOG("Nothing swapped out; returning\n");
    return;
  }

  if (current_instance_ && !current_instance_->decoder()->CanBeSwappedOut()) {
    DLOG("Current instance can't be swapped out\n");
    return;
  }

  // Round-robin; first in line that can be swapped in goes first.
  // TODO: Use some priority mechanism to determine which to swap in.
  auto other_instance = swapped_out_instances_.begin();
  for (; other_instance != swapped_out_instances_.end(); ++other_instance) {
    if ((*other_instance)->decoder()->CanBeSwappedIn()) {
      break;
    }
  }
  if (other_instance == swapped_out_instances_.end()) {
    DLOG("nothing to swap to\n");
    return;
  }
  if (current_instance_)
    SwapOutCurrentInstance();
  current_instance_ = std::move(*other_instance);
  swapped_out_instances_.erase(other_instance);

  SwapInCurrentInstance();
}

void AmlogicVideo::SwapInCurrentInstance() {
  ZX_DEBUG_ASSERT(current_instance_);

  core_ = current_instance_->core();
  video_decoder_ = current_instance_->decoder();
  DLOG("Swapping in %p\n", video_decoder_);
  stream_buffer_ = current_instance_->stream_buffer();
  core()->PowerOn();
  zx_status_t status = video_decoder_->InitializeHardware();
  if (status != ZX_OK) {
    // Probably failed to load the right firmware.
    DECODE_ERROR("Failed to initialize hardware: %d\n", status);
    // TODO: exit cleanly
    exit(-1);
  }
  if (!current_instance_->input_context()) {
    InitializeStreamInput(false);
    core_->InitializeDirectInput();
    // If data has added to the stream buffer before the first swap in(only
    // relevant in tests right now) then ensure the write pointer's updated to
    // that spot.
    // Generally data will only be added after this decoder is swapped in, so
    // RestoreInputContext will handle that state.
    core_->UpdateWritePointer(stream_buffer_->buffer().phys_base() + stream_buffer_->data_size() +
                              stream_buffer_->padding_size());
  } else {
    core_->RestoreInputContext(current_instance_->input_context());
  }
  video_decoder_->SwappedIn();
}

fidl::InterfaceHandle<fuchsia::sysmem::Allocator> AmlogicVideo::ConnectToSysmem() {
  fidl::InterfaceHandle<fuchsia::sysmem::Allocator> client_end;
  fidl::InterfaceRequest<fuchsia::sysmem::Allocator> server_end = client_end.NewRequest();
  zx_status_t connect_status = sysmem_connect(&sysmem_, server_end.TakeChannel().release());
  if (connect_status != ZX_OK) {
    // failure
    return fidl::InterfaceHandle<fuchsia::sysmem::Allocator>();
  }
  return client_end;
}

namespace tee_smc {

enum CallType : uint8_t {
  kYieldingCall = 0,
  kFastCall = 1,
};

enum CallConvention : uint8_t {
  kSmc32CallConv = 0,
  kSmc64CallConv = 1,
};

enum Service : uint8_t {
  kArchService = 0x00,
  kCpuService = 0x01,
  kSipService = 0x02,
  kOemService = 0x03,
  kStandardService = 0x04,
  kTrustedOsService = 0x32,
  kTrustedOsServiceEnd = 0x3F,
};

constexpr uint8_t kCallTypeMask = 0x01;
constexpr uint8_t kCallTypeShift = 31;
constexpr uint8_t kCallConvMask = 0x01;
constexpr uint8_t kCallConvShift = 30;
constexpr uint8_t kServiceMask = ARM_SMC_SERVICE_CALL_NUM_MASK;
constexpr uint8_t kServiceShift = ARM_SMC_SERVICE_CALL_NUM_SHIFT;

static constexpr uint32_t CreateFunctionId(CallType call_type, CallConvention call_conv,
                                           Service service, uint16_t function_num) {
  return (((call_type & kCallTypeMask) << kCallTypeShift) |
          ((call_conv & kCallConvMask) << kCallConvShift) |
          ((service & kServiceMask) << kServiceShift) | function_num);
}
}  // namespace tee_smc

zx_status_t AmlogicVideo::SetProtected(ProtectableHardwareUnit unit, bool protect) {
  if (!secure_monitor_)
    return protect ? ZX_ERR_INVALID_ARGS : ZX_OK;

  // Call into the TEE to mark a particular hardware unit as able to access
  // protected memory or not.
  zx_smc_parameters_t params = {};
  zx_smc_result_t result = {};
  constexpr uint32_t kFuncIdConfigDeviceSecure = 14;
  params.func_id = tee_smc::CreateFunctionId(tee_smc::kFastCall, tee_smc::kSmc32CallConv,
                                             tee_smc::kTrustedOsService, kFuncIdConfigDeviceSecure);
  params.arg1 = static_cast<uint32_t>(unit);
  params.arg2 = static_cast<uint32_t>(protect);
  zx_status_t status = zx_smc_call(secure_monitor_.get(), &params, &result);
  if (status != ZX_OK) {
    DECODE_ERROR("Failed to set unit %ld protected status %ld code: %d", params.arg1, params.arg2,
                 status);
    return status;
  }
  if (result.arg0 != 0) {
    DECODE_ERROR("Failed to set unit %ld protected status %ld: %lx", params.arg1, params.arg2,
                 result.arg0);
    return ZX_ERR_INTERNAL;
  }
  return ZX_OK;
}

zx_status_t AmlogicVideo::TeeSmcLoadVideoFirmware(FirmwareBlob::FirmwareType index,
                                                  FirmwareBlob::FirmwareVdecLoadMode vdec) {
  ZX_DEBUG_ASSERT(is_tee_available());
  ZX_DEBUG_ASSERT(secure_monitor_);

  // Call into the TEE to tell the HW to use a particular piece of the previously pre-loaded overall
  // firmware blob.
  zx_smc_parameters_t params = {};
  zx_smc_result_t result = {};
  constexpr uint32_t kFuncIdLoadVideoFirmware = 15;
  params.func_id = tee_smc::CreateFunctionId(tee_smc::kFastCall, tee_smc::kSmc32CallConv,
                                             tee_smc::kTrustedOsService, kFuncIdLoadVideoFirmware);
  params.arg1 = static_cast<uint32_t>(index);
  params.arg2 = static_cast<uint32_t>(vdec);
  zx_status_t status = zx_smc_call(secure_monitor_.get(), &params, &result);
  if (status != ZX_OK) {
    LOG(ERROR, "Failed to kFuncIdLoadVideoFirmware - index: %u vdec: %u status: %d",
        index, vdec, status);
    return status;
  }
  if (result.arg0 != 0) {
    LOG(ERROR, "kFuncIdLoadVideoFirmware result.arg0 != 0 - value: %lu", result.arg0);
    return ZX_ERR_INTERNAL;
  }
  return ZX_OK;
}

zx_status_t AmlogicVideo::InitRegisters(zx_device_t* parent) {
  parent_ = parent;

  composite_protocol_t composite;
  auto status = device_get_protocol(parent, ZX_PROTOCOL_COMPOSITE, &composite);
  if (status != ZX_OK) {
    DECODE_ERROR("Could not get composite protocol\n");
    return status;
  }

  zx_device_t* components[kMaxComponentCount];
  size_t actual;
  composite_get_components(&composite, components, countof(components), &actual);
  if (actual < kMinComponentCount || actual > kMaxComponentCount) {
    DECODE_ERROR("could not get components\n");
    return ZX_ERR_NOT_SUPPORTED;
  }
  // If tee is available as a component, we require that we can get ZX_PROTOCOL_TEE.  It'd be nice
  // if there were a less fragile way to detect this.  Passing in driver metadata for this doesn't
  // seem worthwhile so far.  There's no tee on vim2.
  is_tee_available_ = (actual == kMaxComponentCount);

  status = device_get_protocol(components[kComponentPdev], ZX_PROTOCOL_PDEV, &pdev_);
  if (status != ZX_OK) {
    DECODE_ERROR("Failed to get pdev protocol\n");
    return ZX_ERR_NO_MEMORY;
  }

  status = device_get_protocol(components[kComponentSysmem], ZX_PROTOCOL_SYSMEM, &sysmem_);
  if (status != ZX_OK) {
    DECODE_ERROR("Could not get SYSMEM protocol\n");
    return status;
  }

  status = device_get_protocol(components[kComponentCanvas], ZX_PROTOCOL_AMLOGIC_CANVAS, &canvas_);
  if (status != ZX_OK) {
    DECODE_ERROR("Could not get video CANVAS protocol\n");
    return status;
  }

  if (is_tee_available_) {
    status = device_get_protocol(components[kComponentTee], ZX_PROTOCOL_TEE, &tee_);
    if (status != ZX_OK) {
      DECODE_ERROR("Could not get TEE protocol, despite is_tee_available_");
      return status;
    }
    // TODO(39808): remove log spam once we're loading firmware via video_firmware TA
    LOG(INFO, "Got ZX_PROTOCOL_TEE");
  } else {
    // TODO(39808): remove log spam once we're loading firmware via video_firmware TA
    LOG(INFO, "Skipped ZX_PROTOCOL_TEE");
  }

  pdev_device_info_t info;
  status = pdev_get_device_info(&pdev_, &info);
  if (status != ZX_OK) {
    DECODE_ERROR("pdev_get_device_info failed");
    return status;
  }

  switch (info.pid) {
    case PDEV_PID_AMLOGIC_S912:
      device_type_ = DeviceType::kGXM;
      break;
    case PDEV_PID_AMLOGIC_S905D2:
      device_type_ = DeviceType::kG12A;
      break;
    case PDEV_PID_AMLOGIC_T931:
      device_type_ = DeviceType::kG12B;
      break;
    default:
      DECODE_ERROR("Unknown soc pid: %d\n", info.pid);
      return ZX_ERR_INVALID_ARGS;
  }

  static constexpr uint32_t kTrustedOsSmcIndex = 0;
  status = pdev_get_smc(&pdev_, kTrustedOsSmcIndex, secure_monitor_.reset_and_get_address());
  if (status != ZX_OK) {
    // On systems where there's no protected memory it's fine if we can't get
    // a handle to the secure monitor.
    zxlogf(INFO,
           "amlogic-video: Unable to get secure monitor handle, assuming no "
           "protected memory\n");
  }

  mmio_buffer_t cbus_mmio;
  status = pdev_map_mmio_buffer(&pdev_, kCbus, ZX_CACHE_POLICY_UNCACHED_DEVICE, &cbus_mmio);
  if (status != ZX_OK) {
    DECODE_ERROR("Failed map cbus");
    return ZX_ERR_NO_MEMORY;
  }
  cbus_ = std::make_unique<CbusRegisterIo>(cbus_mmio);
  mmio_buffer_t mmio;
  status = pdev_map_mmio_buffer(&pdev_, kDosbus, ZX_CACHE_POLICY_UNCACHED_DEVICE, &mmio);
  if (status != ZX_OK) {
    DECODE_ERROR("Failed map dosbus");
    return ZX_ERR_NO_MEMORY;
  }
  dosbus_ = std::make_unique<DosRegisterIo>(mmio);
  status = pdev_map_mmio_buffer(&pdev_, kHiubus, ZX_CACHE_POLICY_UNCACHED_DEVICE, &mmio);
  if (status != ZX_OK) {
    DECODE_ERROR("Failed map hiubus");
    return ZX_ERR_NO_MEMORY;
  }
  hiubus_ = std::make_unique<HiuRegisterIo>(mmio);
  status = pdev_map_mmio_buffer(&pdev_, kAobus, ZX_CACHE_POLICY_UNCACHED_DEVICE, &mmio);
  if (status != ZX_OK) {
    DECODE_ERROR("Failed map aobus");
    return ZX_ERR_NO_MEMORY;
  }
  aobus_ = std::make_unique<AoRegisterIo>(mmio);
  status = pdev_map_mmio_buffer(&pdev_, kDmc, ZX_CACHE_POLICY_UNCACHED_DEVICE, &mmio);
  if (status != ZX_OK) {
    DECODE_ERROR("Failed map dmc");
    return ZX_ERR_NO_MEMORY;
  }
  dmc_ = std::make_unique<DmcRegisterIo>(mmio);
  status =
      pdev_get_interrupt(&pdev_, kParserIrq, 0, parser_interrupt_handle_.reset_and_get_address());
  if (status != ZX_OK) {
    DECODE_ERROR("Failed get parser interrupt");
    return ZX_ERR_NO_MEMORY;
  }
  status =
      pdev_get_interrupt(&pdev_, kDosMbox0Irq, 0, vdec0_interrupt_handle_.reset_and_get_address());
  if (status != ZX_OK) {
    DECODE_ERROR("Failed get vdec0 interrupt");
    return ZX_ERR_NO_MEMORY;
  }
  status =
      pdev_get_interrupt(&pdev_, kDosMbox1Irq, 0, vdec1_interrupt_handle_.reset_and_get_address());
  if (status != ZX_OK) {
    DECODE_ERROR("Failed get vdec interrupt");
    return ZX_ERR_NO_MEMORY;
  }
  status = pdev_get_bti(&pdev_, 0, bti_.reset_and_get_address());
  if (status != ZX_OK) {
    DECODE_ERROR("Failed get bti");
    return ZX_ERR_NO_MEMORY;
  }

  int64_t reset_register_offset = 0x1100 * 4;
  int64_t parser_register_offset = 0;
  int64_t demux_register_offset = 0;
  if (IsDeviceAtLeast(device_type_, DeviceType::kG12A)) {
    // Some portions of the cbus moved in newer versions (TXL and later).
    reset_register_offset = 0x0400 * 4;
    parser_register_offset = (0x3800 - 0x2900) * 4;
    demux_register_offset = (0x1800 - 0x1600) * 4;
  }
  reset_ = std::make_unique<ResetRegisterIo>(cbus_mmio, reset_register_offset);
  parser_regs_ = std::make_unique<ParserRegisterIo>(cbus_mmio, parser_register_offset);
  demux_ = std::make_unique<DemuxRegisterIo>(cbus_mmio, demux_register_offset);
  registers_ = std::unique_ptr<MmioRegisters>(
      new MmioRegisters{dosbus_.get(), aobus_.get(), dmc_.get(), hiubus_.get(), reset_.get(),
                        parser_regs_.get(), demux_.get()});

  firmware_ = std::make_unique<FirmwareBlob>();
  status = firmware_->LoadFirmware(parent_);
  if (status != ZX_OK) {
    DECODE_ERROR("Failed load firmware");
    return status;
  }

  sysmem_sync_ptr_.Bind(ConnectToSysmem());
  if (!sysmem_sync_ptr_) {
    DECODE_ERROR("ConnectToSysmem() failed");
    status = ZX_ERR_INTERNAL;
    return status;
  }

  return ZX_OK;
}

zx_status_t AmlogicVideo::PreloadFirmwareViaTee() {
  ZX_DEBUG_ASSERT(is_tee_available_);

  uint8_t* firmware_data;
  uint32_t firmware_size;
  firmware_->GetWholeBlob(&firmware_data, &firmware_size);

  zx::channel tee_client;
  zx::channel tee_server;
  zx_status_t status = zx::channel::create(/*flags=*/0, &tee_client, &tee_server);
  if (status != ZX_OK) {
    LOG(ERROR, "zx::channel::create() failed - status: %d", status);
    return status;
  }

  status = tee_connect(&tee_, tee_server.release(), /*service_provider=*/ZX_HANDLE_INVALID);
  if (status != ZX_OK) {
    LOG(ERROR, "tee_connect() failed - status: %d", status);
    return status;
  }

  TEEC_Context tee_context{};
  // Ownership stays with tee_client so the channel will get closed at the end of this method, or
  // on early return.
  //
  // TODO(dustingreen): Find a way to use TEEC_InitializeContext(), or create a more official way to
  // do this.
  tee_context.imp.tee_channel = tee_client.get();

  VideoFirmwareSession video_firmware_session(&tee_context);
  status = video_firmware_session.Init();
  if (status != ZX_OK) {
    LOG(ERROR, "video_firmware_session.Init() failed - status: %d", status);
    return status;
  }

  status = video_firmware_session.LoadVideoFirmware(firmware_data, firmware_size);
  if (status != ZX_OK) {
    LOG(ERROR, "video_firmware_session.LoadVideoFirmware() failed - status: %d", status);
    return status;
  }

  // ~video_firmware_session
  // ~tee_client
  return ZX_OK;
}

void AmlogicVideo::InitializeInterrupts() {
  vdec0_interrupt_thread_ = std::thread([this]() {
    while (true) {
      zx_time_t time;
      zx_status_t status = zx_interrupt_wait(vdec0_interrupt_handle_.get(), &time);
      if (status != ZX_OK) {
        DECODE_ERROR("vdec0_interrupt_thread_ zx_interrupt_wait() failed - status: %d", status);
        return;
      }
      std::lock_guard<std::mutex> lock(video_decoder_lock_);
      if (video_decoder_) {
        video_decoder_->HandleInterrupt();
      }
    }
  });

  vdec1_interrupt_thread_ = std::thread([this]() {
    while (true) {
      zx_time_t time;
      zx_status_t status = zx_interrupt_wait(vdec1_interrupt_handle_.get(), &time);
      if (status == ZX_ERR_CANCELED) {
        // expected when zx_interrupt_destroy() is called
        return;
      }
      if (status != ZX_OK) {
        // unexpected errors
        DECODE_ERROR(
            "AmlogicVideo::InitializeInterrupts() zx_interrupt_wait() failed "
            "status: %d\n",
            status);
        return;
      }
      std::lock_guard<std::mutex> lock(video_decoder_lock_);
      if (video_decoder_) {
        video_decoder_->HandleInterrupt();
      }
    }
  });
}

zx_status_t AmlogicVideo::InitDecoder() {
  if (is_tee_available_) {
    zx_status_t status = PreloadFirmwareViaTee();
    if (status != ZX_OK) {
      LOG(ERROR, "PreloadFirmwareViaTee() failed - status: %d", status);
      return status;
    }
    // TODO(dustingreen): Remove log spam after secure decode works.
    LOG(INFO, "PreloadFirmwareViaTee() succeeded.");
  } else {
    LOG(INFO, "!is_tee_available_");
  }

  InitializeInterrupts();

  return ZX_OK;
}
