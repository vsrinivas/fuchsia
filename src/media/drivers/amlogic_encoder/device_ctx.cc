// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/drivers/amlogic_encoder/device_ctx.h"

#include <fuchsia/mediacodec/cpp/fidl.h>
#include <lib/async/cpp/task.h>
#include <zircon/assert.h>
#include <zircon/types.h>

#include <iterator>
#include <limits>
#include <memory>
#include <optional>

#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/mmio-buffer.h>
#include <ddk/platform-defs.h>
#include <ddk/protocol/composite.h>
#include <ddk/protocol/platform/device.h>
#include <ddktl/protocol/composite.h>
#include <fbl/algorithm.h>

#include "ddktl/protocol/amlogiccanvas.h"
#include "ddktl/protocol/sysmem.h"
#include "lib/mmio/mmio.h"
#include "lib/zx/interrupt.h"
#include "src/media/drivers/amlogic_encoder/local_codec_factory.h"
#include "src/media/drivers/amlogic_encoder/macros.h"
#include "src/media/drivers/amlogic_encoder/memory_barriers.h"
#include "src/media/drivers/amlogic_encoder/registers.h"
#include "src/media/drivers/amlogic_encoder/scoped_canvas.h"

enum MmioRegion {
  kCbus,
  kDosbus,
  kAobus,
  kHiubus,
};

enum Interrupt {
  kDosMbox2Irq,
};

enum {
  kFragmentPdev = 0,
  kFragmentSysmem = 1,
  kFragmentCanvas = 2,
  kFragmentCount = 3,
};

// clock_level = 5 or 6 will cause encoder reset, reference bug 120995073
constexpr uint32_t kClockLevel = 4;

static uint32_t kV3MvSad[] = {
    // step 0
    0x00000010, 0x00010020, 0x00020030, 0x00030040, 0x00040050, 0x00050060, 0x00060070, 0x00070080,
    0x00080090, 0x000900a0, 0x000a00b0, 0x000b00c0, 0x000c00d0, 0x000d00e0, 0x000e00f0, 0x000f0100,
    // step1
    0x00100008, 0x00110010, 0x00120018, 0x00130020, 0x00140028, 0x00150030, 0x00160038, 0x00170040,
    0x00180048, 0x00190050, 0x001a0058, 0x001b0060, 0x001c0068, 0x001d0070, 0x001e0078, 0x001f0080,
    // step2
    0x00200008, 0x00210018, 0x00220018, 0x00230030, 0x00240030, 0x00250030, 0x00260030, 0x00270048,
    0x00280048, 0x00290048, 0x002a0048, 0x002b0048, 0x002c0048, 0x002d0048, 0x002e0048, 0x002f0060,
    // for step2 4x4-8x8
    0x00300001, 0x00310002, 0x00320003, 0x00330004, 0x00340005, 0x00350006, 0x00360007, 0x00370008,
    0x00380009, 0x0039000a, 0x003a000b, 0x003b000c, 0x003c000d, 0x003d000e, 0x003e000f, 0x003f0010};

constexpr uint32_t kPPicQpCDefault = 39;
constexpr uint32_t kIPicQpCDefault = 39;
static uint8_t kPicQpC[] = {0,  1,  2,  3,  4,  5,  6,  7,  8,  9,  10, 11, 12, 13, 14, 15, 16,
                            17, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 29, 30, 31, 32, 33,
                            34, 34, 35, 35, 36, 36, 37, 37, 37, 38, 38, 38, 39, 39, 39};

const fuchsia_hardware_mediacodec_Device_ops_t kFidlOps = {
    .GetCodecFactory =
        [](void* ctx, zx_handle_t handle) {
          zx::channel request(handle);
          reinterpret_cast<DeviceCtx*>(ctx)->GetCodecFactory(std::move(request));
          return ZX_OK;
        },
};

std::unique_ptr<DeviceCtx> DeviceCtx::Create(zx_device_t* parent) {
  ddk::CompositeProtocolClient composite(parent);
  if (!composite.is_valid()) {
    ENCODE_ERROR("Could not get composite protocol");
    return nullptr;
  }

  zx_device_t* fragments[kFragmentCount];
  size_t fragment_count = 0;
  composite.GetFragments(fragments, std::size(fragments), &fragment_count);

  if (fragment_count != kFragmentCount) {
    ENCODE_ERROR("Could not get fragments");
    return nullptr;
  }

  ddk::PDev pdev(fragments[kFragmentPdev]);
  if (!pdev.is_valid()) {
    ENCODE_ERROR("Failed to get pdev protocol");
    return nullptr;
  }

  ddk::SysmemProtocolClient sysmem(fragments[kFragmentSysmem]);
  if (!sysmem.is_valid()) {
    ENCODE_ERROR("Could not get sysmem protocol");
    return nullptr;
  }

  ddk::AmlogicCanvasProtocolClient canvas(fragments[kFragmentCanvas]);
  if (!canvas.is_valid()) {
    ENCODE_ERROR("Could not get canvas protocol");
    return nullptr;
  }

  std::optional<ddk::MmioBuffer> mmio;
  auto status = pdev.MapMmio(kCbus, &mmio);
  if (status != ZX_OK || !mmio) {
    ENCODE_ERROR("Failed map cbus %d", status);
    return nullptr;
  }
  CbusRegisterIo cbus(std::move(*mmio));

  mmio = std::nullopt;
  status = pdev.MapMmio(kDosbus, &mmio);
  if (status != ZX_OK || !mmio) {
    ENCODE_ERROR("Failed map dosbus %d", status);
    return nullptr;
  }
  DosRegisterIo dosbus(std::move(*mmio));

  mmio = std::nullopt;
  status = pdev.MapMmio(kAobus, &mmio);
  if (status != ZX_OK || !mmio) {
    ENCODE_ERROR("Failed map aobus %d", status);
    return nullptr;
  }
  AoRegisterIo aobus(std::move(*mmio));

  mmio = std::nullopt;
  status = pdev.MapMmio(kHiubus, &mmio);
  if (status != ZX_OK) {
    ENCODE_ERROR("Failed map hiubus %d", status);
    return nullptr;
  }
  HiuRegisterIo hiubus(std::move(*mmio));

  zx::interrupt interrupt;
  status = pdev.GetInterrupt(kDosMbox2Irq, 0, &interrupt);
  if (status != ZX_OK) {
    ENCODE_ERROR("Failed get enc interrupt");
    return nullptr;
  }

  zx::bti bti;
  status = pdev.GetBti(0, &bti);
  if (status != ZX_OK) {
    ENCODE_ERROR("Failed get bti");
    return nullptr;
  }

  return std::make_unique<DeviceCtx>(parent, pdev, canvas, sysmem, std::move(cbus),
                                     std::move(dosbus), std::move(aobus), std::move(hiubus),
                                     std::move(interrupt), std::move(bti));
}

void DeviceCtx::DdkUnbindNew(ddk::UnbindTxn txn) {
  ShutDown();
  txn.Reply();
}

void DeviceCtx::DdkRelease() { delete this; }

zx_status_t DeviceCtx::DdkMessage(fidl_msg_t* msg, fidl_txn_t* txn) {
  return fuchsia_hardware_mediacodec_Device_dispatch(this, txn, msg, &kFidlOps);
}

void DeviceCtx::ShutDown() {
  if (interrupt_handle_) {
    zx_interrupt_destroy(interrupt_handle_.get());
    if (interrupt_thread_.joinable()) {
      interrupt_thread_.join();
    }
  }
  loop_.Shutdown();
}

zx_status_t DeviceCtx::Bind() {
  pdev_device_info_t info;
  auto status = pdev_.GetDeviceInfo(&info);
  if (status != ZX_OK) {
    ENCODE_ERROR("pdev_get_device_info failed");
    return status;
  }

  switch (info.pid) {
    case PDEV_PID_AMLOGIC_S905D2:
      soc_type_ = SocType::kG12A;
      break;
    case PDEV_PID_AMLOGIC_T931:
      soc_type_ = SocType::kG12B;
      break;
    default:
      ENCODE_ERROR("Unknown soc pid: %d", info.pid);
      return ZX_ERR_INVALID_ARGS;
  }

  status = load_firmware(parent(), "h264_enc.bin", firmware_package_vmo_.reset_and_get_address(),
                         &firmware_package_size_);
  if (status != ZX_OK) {
    ENCODE_ERROR("Couldn't load firmware");
    return status;
  }

  zx::vmar::root_self()->map(0, firmware_package_vmo_, 0, firmware_package_size_, ZX_VM_PERM_READ,
                             &firmware_package_ptr_);

  sysmem_sync_ptr_.Bind(ConnectToSysmem());
  if (!sysmem_sync_ptr_) {
    ENCODE_ERROR("ConnectToSysmem() failed");
    status = ZX_ERR_INTERNAL;
    return status;
  }

  InterruptInit();

  status = loop_.StartThread("device-loop", &loop_thread_);
  if (status != ZX_OK) {
    ENCODE_ERROR("could not start loop thread");
    return status;
  }

  status = DdkAdd("amlogic_video_enc");

  return status;
}

zx_status_t DeviceCtx::ParseFirmwarePackage() {
  constexpr std::string_view kEncFirmwareName = "ga_h264_enc_cabac.bin";
  constexpr uint32_t kMaxFirmwareSize = 8 * 4096;

  if (!firmware_package_ptr_) {
    return ZX_ERR_INVALID_ARGS;
  }

  // TODO(50890) unify this code with similar parsing code over in decoder

  constexpr uint32_t kFirmwareHeaderSize = 512;
  struct FirmwareHeader {
    uint32_t magic;
    uint32_t checksum;
    char name[32];
    char cpu[16];
    char format[32];
    char version[32];
    char author[32];
    char date[32];
    char commit[16];
    uint32_t data_size;
    uint32_t time;
  };

  constexpr uint32_t kPackageMagic = ('P' << 24 | 'A' << 16 | 'C' << 8 | 'K');
  constexpr uint32_t kPackageHeaderSize = 256;
  struct PackageHeader {
    uint32_t magic;
    uint32_t size;
    uint32_t checksum;
  };

  constexpr uint32_t kPackageEntryHeaderSize = 256;
  struct PackageEntryHeader {
    char name[32];
    char format[32];
    char cpu[32];
    uint32_t length;
  };

  if (firmware_package_size_ < kPackageHeaderSize) {
    return ZX_ERR_NO_MEMORY;
  }

  uint32_t offset = 0;
  auto header = reinterpret_cast<PackageHeader*>(firmware_package_ptr_ + offset);

  if (header->magic != kPackageMagic) {
    return ZX_ERR_INVALID_ARGS;
  }

  offset += kPackageHeaderSize;

  while (offset < firmware_package_size_) {
    if (offset + kPackageEntryHeaderSize > firmware_package_size_) {
      return ZX_ERR_NO_MEMORY;
    }

    auto entry = reinterpret_cast<PackageEntryHeader*>(firmware_package_ptr_ + offset);

    offset += kPackageEntryHeaderSize;

    if (offset + entry->length > firmware_package_size_) {
      return ZX_ERR_NO_MEMORY;
    }

    if (kFirmwareHeaderSize > entry->length) {
      return ZX_ERR_NO_MEMORY;
    }

    auto firmware_header = reinterpret_cast<FirmwareHeader*>(firmware_package_ptr_ + offset);
    if (firmware_header->data_size + kFirmwareHeaderSize > entry->length) {
      return ZX_ERR_NO_MEMORY;
    }

    if (firmware_header->data_size > kMaxFirmwareSize) {
      return ZX_ERR_NO_MEMORY;
    }

    // TODO(afoxley) check crc

    if (kEncFirmwareName.compare(0, kEncFirmwareName.size(), firmware_header->name,
                                 strnlen(firmware_header->name, sizeof(firmware_header->name))) ==
        0) {
      firmware_ptr_ = firmware_package_ptr_ + offset + kFirmwareHeaderSize;
      firmware_size_ = firmware_header->data_size;
      return ZX_OK;
    }

    offset += entry->length;
  }

  return ZX_ERR_NOT_FOUND;
}

zx_status_t DeviceCtx::LoadFirmware() {
  io_buffer_t firmware_buffer;
  const uint32_t kDmaCount = 0x1000;
  // Request page alignment.
  const uint32_t kBufferAlignShift = 0;
  const char* kBufferName = "EncFirmware";

  HcodecAssistMmcCtrl1::Get().FromValue(HcodecAssistMmcCtrl1::kCtrl).WriteTo(&dosbus_);

  if (!firmware_ptr_) {
    zx_status_t status = ParseFirmwarePackage();
    if (status != ZX_OK) {
      ENCODE_ERROR("Couldn't parse firmware package: %d", status);
      return status;
    }
  }

  ZX_DEBUG_ASSERT(firmware_ptr_ && firmware_size_);

  zx_status_t status = io_buffer_init_aligned(&firmware_buffer, bti_.get(), firmware_size_,
                                              kBufferAlignShift, IO_BUFFER_RW | IO_BUFFER_CONTIG);
  if (status != ZX_OK) {
    ENCODE_ERROR("Failed to make firmware buffer");
    return status;
  }

  zx_object_set_property(firmware_buffer.vmo_handle, ZX_PROP_NAME, kBufferName,
                         strlen(kBufferName));

  memcpy(io_buffer_virt(&firmware_buffer), reinterpret_cast<void*>(firmware_ptr_), firmware_size_);
  io_buffer_cache_flush(&firmware_buffer, 0, firmware_size_);

  BarrierAfterFlush();

  HcodecMpsr::Get().FromValue(0).WriteTo(&dosbus_);
  HcodecCpsr::Get().FromValue(0).WriteTo(&dosbus_);

  // delay
  for (int i = 0; i < 3; i++) {
    HcodecMpsr::Get().ReadFrom(&dosbus_);
  }

  HcodecImemDmaAdr::Get()
      .FromValue(truncate_to_32(io_buffer_phys(&firmware_buffer)))
      .WriteTo(&dosbus_);
  HcodecImemDmaCount::Get().FromValue(kDmaCount).WriteTo(&dosbus_);
  HcodecImemDmaCtrl::Get()
      .FromValue(0)
      .set_ready(1)
      .set_ctrl(HcodecImemDmaCtrl::kCtrl)
      .WriteTo(&dosbus_);

  if (!WaitForRegister(std::chrono::seconds(1), [this] {
        return HcodecImemDmaCtrl::Get().ReadFrom(&dosbus_).ready() == 0;
      })) {
    ENCODE_ERROR("Failed to load microcode.");

    BarrierBeforeRelease();
    io_buffer_release(&firmware_buffer);
    return ZX_ERR_TIMED_OUT;
  }

  BarrierBeforeRelease();
  io_buffer_release(&firmware_buffer);
  return ZX_OK;
}

void DeviceCtx::InterruptInit() {
  interrupt_thread_ = std::thread([this]() {
    while (true) {
      zx_time_t time;
      zx_status_t status = zx_interrupt_wait(interrupt_handle_.get(), &time);
      if (status != ZX_OK) {
        ENCODE_ERROR("zx_interrupt_wait() failed - status: %d", status);
        return;
      }

      HcodecIrqMboxClear::Get().FromValue(1).WriteTo(&dosbus_);
      hw_status_ =
          static_cast<EncoderStatus>(HcodecEncoderStatus::Get().ReadFrom(&dosbus_).reg_value());
      sync_completion_signal(&interrupt_sync_);
    }
  });
}

zx_status_t DeviceCtx::BufferAlloc() {
  constexpr uint32_t kDec0Size = 0x300000;
  constexpr uint32_t kDec1Size = 0x300000;
  constexpr uint32_t kAssitSize = 0xc0000;
  constexpr uint32_t kScaleBufSize = 0x300000;
  constexpr uint32_t kDumpInfoSize = 0xa0000;
  constexpr uint32_t kCbrInfoSize = 0x2000;
  constexpr uint32_t kSPSPPSSize = 0x1000;

  auto result = InternalBuffer::Create("H264EncoderDec0", &sysmem_sync_ptr_, bti(), kDec0Size,
                                       /*is_writable=*/true,
                                       /*is_mapping_needed*/
                                       false);
  if (!result.is_ok()) {
    ENCODE_ERROR("Failed to make buffer - status: %d", result.error());
    return result.error();
  }
  dec0_.emplace(result.take_value());

  result = InternalBuffer::Create("H264EncoderDec1", &sysmem_sync_ptr_, bti(), kDec1Size,

                                  /*is_writable=*/true,
                                  /*is_mapping_needed*/
                                  false);
  if (!result.is_ok()) {
    ENCODE_ERROR("Failed to make buffer - status: %d", result.error());
    return result.error();
  }
  dec1_.emplace(result.take_value());

  result = InternalBuffer::Create("H264EncoderAssit", &sysmem_sync_ptr_, bti(), kAssitSize,

                                  /*is_writable=*/true,
                                  /*is_mapping_needed*/
                                  false);
  if (!result.is_ok()) {
    ENCODE_ERROR("Failed to make buffer - status: %d", result.error());
    return result.error();
  }
  assit_.emplace(result.take_value());

  result = InternalBuffer::Create("H264EncoderScaleBuf", &sysmem_sync_ptr_, bti(), kScaleBufSize,
                                  /*is_writable=*/true,
                                  /*is_mapping_needed*/
                                  false);
  if (!result.is_ok()) {
    ENCODE_ERROR("Failed to make buffer - status: %d", result.error());
    return result.error();
  }
  scale_buff_.emplace(result.take_value());

  result = InternalBuffer::Create("H264EncoderDumpInfo", &sysmem_sync_ptr_, bti(), kDumpInfoSize,
                                  /*is_writable=*/true,
                                  /*is_mapping_needed*/
                                  false);
  if (!result.is_ok()) {
    ENCODE_ERROR("Failed to make buffer - status: %d", result.error());
    return result.error();
  }
  dump_info_.emplace(result.take_value());

  result = InternalBuffer::Create("H264EncoderCbrInfo", &sysmem_sync_ptr_, bti(), kCbrInfoSize,
                                  /*is_writable=*/true,
                                  /*is_mapping_needed*/
                                  false);
  if (!result.is_ok()) {
    ENCODE_ERROR("Failed to make buffer - status: %d", result.error());
    return result.error();
  }
  cbr_info_.emplace(result.take_value());

  result = InternalBuffer::Create("H264EncoderSPSPPS", &sysmem_sync_ptr_, bti(), kSPSPPSSize,
                                  /*is_writable=*/true,
                                  /*is_mapping_needed*/
                                  true);
  if (!result.is_ok()) {
    ENCODE_ERROR("Failed to make buffer - status: %d", result.error());
    return result.error();
  }
  sps_pps_data_.emplace(result.take_value());

  return ZX_OK;
}

zx_status_t DeviceCtx::SetInputBuffer(const CodecBuffer* buffer) {
  input_y_canvas_.Reset();
  input_uv_canvas_.Reset();

  uint32_t canvas_h = fbl::round_up(encoder_height_, kCanvasMinHeightAlignment);
  uint32_t canvas_w = fbl::round_up(encoder_width_, kCanvasMinWidthAlignment);

  zx::vmo dup_vmo;
  auto status = buffer->vmo().duplicate(ZX_RIGHT_SAME_RIGHTS, &dup_vmo);
  if (status != ZX_OK) {
    return status;
  }

  // TODO(afoxley) We could pre-setup these canvas handles in BufferCollection setup. Need to decide
  // on a reasonable limit for encoder canvas usage.
  status = CanvasConfig(dup_vmo.get(), canvas_w, canvas_h, /*offset=*/0, &input_y_canvas_,
                        CANVAS_FLAGS_READ);
  if (status != ZX_OK) {
    return status;
  }

  status = buffer->vmo().duplicate(ZX_RIGHT_SAME_RIGHTS, &dup_vmo);
  if (status != ZX_OK) {
    return status;
  }

  status = CanvasConfig(dup_vmo.get(), canvas_w, canvas_h / 2, /*offset=*/canvas_w * canvas_h,
                        &input_uv_canvas_, CANVAS_FLAGS_READ);
  if (status != ZX_OK) {
    return status;
  }

  input_buffer_ = buffer;
  input_canvas_ids_ = ((input_uv_canvas_.id()) << 8) | input_y_canvas_.id();
  input_format_ = InputFormat::kNv12;
  noise_reduction_ = NoiseReductionMode::kSNROnly;

  return ZX_OK;
}

void DeviceCtx::SetOutputBuffer(const CodecBuffer* buffer) {
  output_buffer_ = buffer;
  zx_status_t status = zx_cache_flush(buffer->base(), buffer->size(),
                                      ZX_CACHE_FLUSH_DATA | ZX_CACHE_FLUSH_INVALIDATE);
  if (status != ZX_OK) {
    ENCODE_ERROR("failed to flush output buffer");
  }
}

zx_status_t DeviceCtx::PowerOn() {
  // ungate dos clk
  // TODO(45193) shared with vdec
  HhiGclkMpeg0::Get().ReadFrom(&hiubus_).set_dos(true).WriteTo(&hiubus_);

  // power up HCODEC
  AoRtiGenPwrSleep0::Get()
      .ReadFrom(&aobus_)
      .set_dos_hcodec_d1_pwr_off(0)
      .set_dos_hcodec_pwr_off(0)
      .WriteTo(&aobus_);
  zx_nanosleep(zx_deadline_after(ZX_USEC(10)));

  // reset all of HCODEC
  DosSwReset1::Get().FromValue(DosSwReset1::kAll).WriteTo(&dosbus_);
  DosSwReset1::Get().FromValue(DosSwReset1::kNone).WriteTo(&dosbus_);

  // ungate clock
  switch (kClockLevel) {
    case 4:  // 400mhz
    {
      // TODO(45193) shared with vdec, should be synchronized
      HhiVdecClkCntl::Get()
          .ReadFrom(&hiubus_)
          .set_hcodec_clk_sel(/*fclk_div4*/ 2)
          .set_hcodec_clk_en(1)
          .set_hcodec_clk_div(/*fclk_div2p5*/ 0)
          .WriteTo(&hiubus_);
      break;
    }
    default:
      return ZX_ERR_NOT_SUPPORTED;
  }

  // TODO(45193) shared with vdec
  DosGclkEn0::Get().ReadFrom(&dosbus_).set_hcodec_en(0x7fff).WriteTo(&dosbus_);

  // powerup hcodec memories
  DosMemPdHcodec::Get().FromValue(0).WriteTo(&dosbus_);

  // remove hcodec iso
  AoRtiGenPwrIso0::Get()
      .ReadFrom(&aobus_)
      .set_dos_hcodec_iso_in_en(0)
      .set_dos_hcodec_iso_out_en(0)
      .WriteTo(&aobus_);
  zx_nanosleep(zx_deadline_after(ZX_USEC(10)));

  // TODO(45193) shared with vdec
  // disable auto clock gate
  DosGenCtrl0::Get().ReadFrom(&dosbus_).set_hcodec_auto_clock_gate(1).WriteTo(&dosbus_);
  DosGenCtrl0::Get().ReadFrom(&dosbus_).set_hcodec_auto_clock_gate(0).WriteTo(&dosbus_);

  zx_nanosleep(zx_deadline_after(ZX_USEC(10)));

  return ZX_OK;
}

zx_status_t DeviceCtx::CanvasConfig(zx_handle_t vmo, uint32_t bytes_per_row, uint32_t height,
                                    uint32_t offset, ScopedCanvasId* canvas_id_out,
                                    uint32_t alloc_flag) {
  canvas_info_t info = {};
  info.height = height;
  info.stride_bytes = bytes_per_row;
  info.flags = alloc_flag;
  uint8_t id;
  zx_handle_t vmo_dup;
  zx_status_t status = zx_handle_duplicate(vmo, ZX_RIGHT_SAME_RIGHTS, &vmo_dup);
  if (status != ZX_OK) {
    return status;
  }
  status = canvas_.Config(zx::vmo(vmo_dup), offset, &info, &id);
  if (status != ZX_OK) {
    return status;
  }
  *canvas_id_out = ScopedCanvasId(&canvas_, id);
  return ZX_OK;
}

zx_status_t DeviceCtx::CanvasInitReference(InternalBuffer* buf, ScopedCanvasId* y_canvas,
                                           ScopedCanvasId* uv_canvas, uint32_t* packed_canvas_ids) {
  uint32_t canvas_width = fbl::round_up(encoder_width_, kCanvasMinWidthAlignment);
  uint32_t canvas_height = fbl::round_up(encoder_height_, kCanvasMinHeightAlignment);

  zx::vmo dup_vmo;
  auto status = buf->vmo_duplicate(&dup_vmo);
  if (status != ZX_OK) {
    return status;
  }
  status = CanvasConfig(dup_vmo.get(), canvas_width, canvas_height, /*offset=*/0, y_canvas,
                        CANVAS_FLAGS_READ);
  if (status != ZX_OK) {
    return status;
  }

  status = buf->vmo_duplicate(&dup_vmo);
  if (status != ZX_OK) {
    return status;
  }
  status = CanvasConfig(dup_vmo.get(), canvas_width, canvas_height / 2,
                        /*offset=*/canvas_width * canvas_height, uv_canvas, CANVAS_FLAGS_READ);
  if (status != ZX_OK) {
    return status;
  }

  // use same canvas for both u and v as they share memory
  *packed_canvas_ids = (uv_canvas->id() << 16) | (uv_canvas->id() << 8) | (y_canvas->id());
  return ZX_OK;
}

zx_status_t DeviceCtx::CanvasInit() {
  auto status = CanvasInitReference(&*dec0_, &dec0_y_canvas_, &dec0_uv_canvas_, &dblk_buf_canvas_);
  if (status != ZX_OK) {
    return status;
  }

  status = CanvasInitReference(&*dec1_, &dec1_y_canvas_, &dec1_uv_canvas_, &ref_buf_canvas_);
  if (status != ZX_OK) {
    return status;
  }

  return ZX_OK;
}

void DeviceCtx::SetInputFormat() {
  uint32_t picsize_x = fbl::round_up(encoder_width_, kPictureMinAlignment);
  uint32_t picsize_y = fbl::round_up(encoder_height_, kPictureMinAlignment);
  uint8_t r2y_en = 0;
  uint32_t ifmt_extra = 0;
  uint8_t downsample_enable = 0;
  uint8_t interpolation_enable = 0;
  uint8_t y_size = 0;
  uint8_t rgb2yuv_en = 0;
  uint8_t r2y_mode = (rgb2yuv_en == 1) ? 1 : 0;
  uint8_t canv_idx0_bppx = 1;
  uint8_t canv_idx1_bppx = 1;
  uint8_t canv_idx0_bppy = 1;
  uint8_t canv_idx1_bppy = 0;
  uint8_t linear_bytes4p = 0;
  uint8_t linear_bytesperline = picsize_x * linear_bytes4p;
  bool linear_enable = false;
  uint32_t oformat = 0;

  uint8_t nr_enable = (noise_reduction_ != NoiseReductionMode::kDisabled) ? 1 : 0;
  uint8_t cfg_y_snr_en = ((noise_reduction_ == NoiseReductionMode::kSNROnly) ||
                          (noise_reduction_ == NoiseReductionMode::k3DNR))
                             ? 1
                             : 0;
  uint8_t cfg_y_tnr_en = ((noise_reduction_ == NoiseReductionMode::kTNROnly) ||
                          (noise_reduction_ == NoiseReductionMode::k3DNR))
                             ? 1
                             : 0;
  uint8_t cfg_c_snr_en = cfg_y_snr_en;
  uint8_t cfg_c_tnr_en = 0;

  // Noise Reduction For Y
  HcodecMfdInReg0D::Get()
      .FromValue(0)
      .set_cfg_y_snr_en(cfg_y_snr_en)
      .set_y_snr_err_norm(1)
      .set_y_snr_gau_bld_core(4)
      .set_y_snr_gau_bld_ofst(-4 & 0x3c)
      .set_y_snr_gau_bld_rate(30)
      .set_y_snr_gau_alp0_min(0)
      .set_y_snr_gau_alp0_max(63)
      .WriteTo(&dosbus_);

  HcodecMfdInReg0E::Get()
      .FromValue(0)
      .set_cfg_y_tnr_en(cfg_y_tnr_en)
      .set_y_tnr_mc_en(1)
      .set_y_tnr_txt_mode(0)
      .set_y_tnr_mot_sad_margin(1)
      .set_y_tnr_alpha_min(8)
      .set_y_tnr_alpha_max(63)
      .set_y_tnr_deghost_os(3)
      .WriteTo(&dosbus_);

  HcodecMfdInReg0F::Get()
      .FromValue(0)
      .set_y_tnr_mot_cortxt_rate(4)
      .set_y_tnr_mot_distxt_ofst(5)
      .set_y_tnr_mot_distxt_rate(4)
      .set_y_tnr_mot_dismot_ofst(4)
      .set_y_tnr_mot_frcsad_lock(8)
      .WriteTo(&dosbus_);

  HcodecMfdInReg10::Get()
      .FromValue(0)
      .set_y_tnr_mot2alp_frc_gain(10)
      .set_y_tnr_mot2alp_nrm_gain(216)
      .set_y_tnr_mot2alp_dis_gain(144)
      .set_y_tnr_mot2alp_dis_ofst(32)
      .WriteTo(&dosbus_);

  HcodecMfdInReg11::Get()
      .FromValue(0)
      .set_y_bld_beta2alp_rate(24)
      .set_y_bld_beta_min(0)
      .set_y_bld_beta_max(63)
      .WriteTo(&dosbus_);

  // Noise Reduction For C
  HcodecMfdInReg12::Get()
      .FromValue(0)
      .set_cfg_c_snr_en(cfg_c_snr_en)
      .set_c_snr_err_norm(0)
      .set_c_snr_gau_bld_core(4)
      .set_c_snr_gau_bld_ofst(-4 & 0x3c)
      .set_c_snr_gau_bld_rate(30)
      .set_c_snr_gau_alp0_min(0)
      .set_c_snr_gau_alp0_max(63)
      .WriteTo(&dosbus_);

  HcodecMfdInReg13::Get()
      .FromValue(0)
      .set_cfg_c_tnr_en(cfg_c_tnr_en)
      .set_c_tnr_mc_en(1)
      .set_c_tnr_txt_mode(0)
      .set_c_tnr_mot_sad_margin(1)
      .set_c_tnr_alpha_min(8)
      .set_c_tnr_alpha_max(63)
      .set_c_tnr_deghost_os(3)
      .WriteTo(&dosbus_);

  HcodecMfdInReg14::Get()
      .FromValue(0)
      .set_c_tnr_mot_cortxt_rate(4)
      .set_c_tnr_mot_distxt_ofst(5)
      .set_c_tnr_mot_distxt_rate(4)
      .set_c_tnr_mot_dismot_ofst(4)
      .set_c_tnr_mot_frcsad_lock(8)
      .WriteTo(&dosbus_);

  HcodecMfdInReg15::Get()
      .FromValue(0)
      .set_c_tnr_mot2alp_frc_gain(10)
      .set_c_tnr_mot2alp_nrm_gain(216)
      .set_c_tnr_mot2alp_dis_gain(144)
      .set_c_tnr_mot2alp_dis_ofst(32)
      .WriteTo(&dosbus_);

  HcodecMfdInReg16::Get()
      .FromValue(0)
      .set_c_bld_beta2alp_rate(24)
      .set_c_bld_beta_min(0)
      .set_c_bld_beta_max(63)
      .WriteTo(&dosbus_);

  HcodecMfdInReg1Ctrl::Get()
      .FromValue(0)
      .set_iformat(input_format_)
      .set_oformat(oformat)
      .set_dsample_en(downsample_enable)
      .set_y_size(y_size)
      .set_interp_en(interpolation_enable)
      .set_r2y_en(r2y_en)
      .set_r2y_mode(r2y_mode)
      .set_ifmt_extra(ifmt_extra)
      .set_nr_enable(nr_enable)
      .WriteTo(&dosbus_);

  HcodecMfdInReg8Dmbl::Get().FromValue(0).set_picsize_x(picsize_x).set_picsize_y(picsize_y).WriteTo(
      &dosbus_);

  if (!linear_enable) {
    HcodecMfdInReg3Canv::Get()
        .FromValue(0)
        .set_input(input_canvas_ids_)
        .set_canv_idx1_bppy(canv_idx1_bppy)
        .set_canv_idx0_bppy(canv_idx0_bppy)
        .set_canv_idx1_bppx(canv_idx1_bppx)
        .set_canv_idx0_bppx(canv_idx0_bppx)
        .WriteTo(&dosbus_);
    HcodecMfdInReg4Lnr0::Get()
        .FromValue(0)
        .set_linear_bytes4p(0)
        .set_linear_bytesperline(0)
        .WriteTo(&dosbus_);
    HcodecMfdInReg5Lnr1::Get().FromValue(0).WriteTo(&dosbus_);
  } else {
    HcodecMfdInReg3Canv::Get()
        .FromValue(0)
        .set_canv_idx1_bppy(canv_idx1_bppy)
        .set_canv_idx0_bppy(canv_idx0_bppy)
        .set_canv_idx1_bppx(canv_idx1_bppx)
        .set_canv_idx0_bppx(canv_idx0_bppx)
        .WriteTo(&dosbus_);
    HcodecMfdInReg4Lnr0::Get()
        .FromValue(0)
        .set_linear_bytes4p(linear_bytes4p)
        .set_linear_bytesperline(linear_bytesperline)
        .WriteTo(&dosbus_);
    HcodecMfdInReg5Lnr1::Get().FromValue(input_canvas_ids_).WriteTo(&dosbus_);
  }

  HcodecMfdInReg9Endn::Get()
      .FromValue(0)
      .set_field0(7)
      .set_field3(6)
      .set_field6(5)
      .set_field9(4)
      .set_field12(3)
      .set_field15(2)
      .set_field18(1)
      .set_field21(0)
      .WriteTo(&dosbus_);
}

void DeviceCtx::InputBufferConfig(zx_paddr_t phys, size_t size) {
  HcodecQdctMbStartPtr::Get().FromValue(phys).WriteTo(&dosbus_);
  HcodecQdctMbEndPtr::Get().FromValue(phys + size - 1).WriteTo(&dosbus_);
  HcodecQdctMbWrPtr::Get().FromValue(phys).WriteTo(&dosbus_);
  HcodecQdctMbRdPtr::Get().FromValue(phys).WriteTo(&dosbus_);
  HcodecQdctMbBuff::Get().FromValue(0).WriteTo(&dosbus_);
}

void DeviceCtx::OutputBufferConfig(zx_paddr_t phys, size_t size) {
  HcodecVlcVbMemCtl::Get()
      .FromValue(0)
      .set_bit_31(1)
      .set_bits_30_24(0x3f)
      .set_bits_23_16(0x20)
      .set_bits_1_0(0x2)
      .WriteTo(&dosbus_);

  HcodecVlcVbStartPtr::Get().FromValue(phys).WriteTo(&dosbus_);
  HcodecVlcVbWrPtr::Get().FromValue(phys).WriteTo(&dosbus_);
  HcodecVlcVbSwRdPtr::Get().FromValue(phys).WriteTo(&dosbus_);
  HcodecVlcVbEndPtr::Get().FromValue(phys + size - 1).WriteTo(&dosbus_);

  // TODO(afoxley) Ask amlogic what these bits do.
  HcodecVlcVbControl::Get().FromValue(0).set_bit_0(1).WriteTo(&dosbus_);
  HcodecVlcVbControl::Get()
      .FromValue(0)
      .set_bit_14(0)
      .set_bits_5_3(0x7)
      .set_bit_1(1)
      .set_bit_0(0)
      .WriteTo(&dosbus_);
}

void DeviceCtx::ReferenceBuffersConfig() {
  HcodecRecCanvasAddr::Get().FromValue(dblk_buf_canvas_).WriteTo(&dosbus_);
  HcodecDbkRCanvasAddr::Get().FromValue(dblk_buf_canvas_).WriteTo(&dosbus_);
  HcodecDbkWCanvasAddr::Get().FromValue(dblk_buf_canvas_).WriteTo(&dosbus_);

  HcodecAnc0CanvasAddr::Get().FromValue(ref_buf_canvas_).WriteTo(&dosbus_);
  HcodecVlcHcmdConfig::Get().FromValue(0).WriteTo(&dosbus_);

  HcodecMemOffsetReg::Get().FromValue(assit_->phys_base()).WriteTo(&dosbus_);
}

void DeviceCtx::IeMeParameterInit(HcodecIeMeMbType::MbType mb_type) {
  // currently disable half and sub pixel
  HcodecIeMeMode::Get().FromValue(0).set_ie_pippeline_block(12).WriteTo(&dosbus_);
  HcodecIeRefSel::Get().FromValue(0).WriteTo(&dosbus_);

  HcodecIeMeMbType::Get().FromValue(mb_type).WriteTo(&dosbus_);

  if (rows_per_slice_ != picture_to_mb(encoder_height_)) {
    uint32_t mb_per_slice = picture_to_mb(encoder_height_) * rows_per_slice_;
    HcodecFixedSliceCfg::Get().FromValue(mb_per_slice).WriteTo(&dosbus_);
  } else {
    HcodecFixedSliceCfg::Get().FromValue(0).WriteTo(&dosbus_);
  }
}

void DeviceCtx::QuantTableInit() {
  HcodecQQuantControl::Get()
      .FromValue(0)
      .set_quant_table_addr(0)
      .set_quant_table_addr_update(1)
      .WriteTo(&dosbus_);

  for (unsigned int q : quant_table_i4_) {
    HcodecQuantTableData::Get().FromValue(q).WriteTo(&dosbus_);
  }

  HcodecQQuantControl::Get()
      .FromValue(0)
      .set_quant_table_addr(0x8)
      .set_quant_table_addr_update(1)
      .WriteTo(&dosbus_);

  for (unsigned int q : quant_table_i16_) {
    HcodecQuantTableData::Get().FromValue(q).WriteTo(&dosbus_);
  }

  HcodecQQuantControl::Get()
      .FromValue(0)
      .set_quant_table_addr(0x10)
      .set_quant_table_addr_update(1)
      .WriteTo(&dosbus_);

  for (unsigned int q : quant_table_me_) {
    HcodecQuantTableData::Get().FromValue(q).WriteTo(&dosbus_);
  }
}

void DeviceCtx::Reset() {
  for (int i = 0; i < 3; i++) {
    // delay
    DosSwReset1::Get().ReadFrom(&dosbus_);
  }

  DosSwReset1::Get()
      .FromValue(0)
      .set_hcodec_assist(1)
      .set_hcodec_iqidct(1)
      .set_hcodec_mc(1)
      .set_hcodec_dblk(1)
      .set_hcodec_afifo(1)
      .set_hcodec_vlc(1)
      .set_hcodec_qdct(1)
      .WriteTo(&dosbus_);

  DosSwReset1::Get().FromValue(DosSwReset1::kNone).WriteTo(&dosbus_);

  for (int i = 0; i < 3; i++) {
    // delay
    DosSwReset1::Get().ReadFrom(&dosbus_);
  }
}

void DeviceCtx::Config(bool idr) {
  constexpr uint32_t kLog2MaxFrameNum = 4;
  constexpr uint32_t kLog2MaxPicOrderCntLsb = 4;
  constexpr uint32_t kAnc0BufferId = 0;
  constexpr uint32_t kCbrTableSize = 0x800;

  uint32_t pic_width_in_mb = picture_to_mb(encoder_width_);

  HcodecVlcTotalBytes::Get().FromValue(0).WriteTo(&dosbus_);
  HcodecVlcConfig::Get().FromValue(0x7).WriteTo(&dosbus_);
  HcodecVlcIntControl::Get().FromValue(0).WriteTo(&dosbus_);

  HcodecAssistAmr1Int0::Get().FromValue(0x15).WriteTo(&dosbus_);
  HcodecAssistAmr1Int1::Get().FromValue(0x8).WriteTo(&dosbus_);
  HcodecAssistAmr1Int3::Get().FromValue(0x14).WriteTo(&dosbus_);

  HcodecIdrPicId::Get().FromValue(idr_pic_id_).WriteTo(&dosbus_);
  HcodecFrameNumber::Get().FromValue(frame_number_).WriteTo(&dosbus_);
  HcodecPicOrderCntLsb::Get().FromValue(pic_order_cnt_lsb_).WriteTo(&dosbus_);

  HcodecLog2MaxPicOrderCntLsb::Get().FromValue(kLog2MaxPicOrderCntLsb).WriteTo(&dosbus_);
  HcodecLog2MaxFrameNum::Get().FromValue(kLog2MaxFrameNum).WriteTo(&dosbus_);
  HcodecAnc0BufferId::Get().FromValue(kAnc0BufferId).WriteTo(&dosbus_);
  HcodecQpPicture::Get().FromValue(kInitialQuant).WriteTo(&dosbus_);

  HcodecHdecMcOmemAuto::Get()
      .FromValue(0)
      .set_use_omem_mb_xy(1)
      .set_omem_max_mb_x(pic_width_in_mb - 1)
      .WriteTo(&dosbus_);

  HcodecVlcAdvConfig::Get()
      .FromValue(0)
      .set_early_mix_mc_hcmd(0)
      .set_update_top_left_mix(1)
      .set_p_top_left_mix(1)
      .set_mv_cal_mixed_type(0)
      .set_mc_hcmd_mixed_type(0)
      .set_use_separate_int_control(1)
      .set_hcmd_intra_use_q_info(1)
      .set_hcmd_left_use_prev_info(1)
      .set_hcmd_use_q_info(1)
      .set_use_q_delta_quant(1)
      .set_detect_I16_from_I4(0)
      .WriteTo(&dosbus_);

  HcodecQdctAdvConfig::Get()
      .FromValue(0)
      .set_mb_info_latch_no_I16_pred_mode(1)
      .set_ie_dma_mbxy_use_i_pred(1)
      .set_ie_dma_read_write_use_ip_idx(1)
      .set_ie_start_use_top_dma_count(1)
      .set_i_pred_top_dma_rd_mbbot(1)
      .set_i_pred_top_dma_wr_disable(1)
      .set_i_pred_mix(0)  // will enable in P Picture
      .set_me_ab_rd_when_intra_in_p(1)
      .set_force_mb_skip_run_when_intra(1)
      .set_mc_out_mixed_type(0)  // will enable in P Picture
      .set_ie_start_when_quant_not_full(1)
      .set_mb_info_state_mix(1)
      .set_mb_type_use_mix_result(0)  // will enable in P Picture
      .set_me_cb_ie_read_enable(0)    // will enable in P Picture
      .set_ie_cur_data_from_me(0)     // will enable in P Picture
      .set_rem_per_use_table(1)
      .set_q_latch_int_enable(0)
      .set_q_use_table(1)
      .set_q_start_wait(0)
      .set_LUMA_16_LEFT_use_cur(1)
      .set_DC_16_LEFT_SUM_use_cur(1)
      .set_c_ref_ie_sel_cur(1)
      .set_c_ipred_perfect_mode(0)
      .set_ref_ie_ul_sel(1)
      .set_mb_type_use_ie_result(1)
      .set_detect_I16_from_I4(1)
      .set_ie_not_wait_ref_busy(1)
      .set_ie_I16_enable(1)
      .set_ie_done_sel(0x3)
      .WriteTo(&dosbus_);

  HcodecIeWeight::Get()
      .FromValue(0)
      .set_i4_weight(i4_weight_)
      .set_i16_weight(i16_weight_)
      .WriteTo(&dosbus_);
  HcodecMeWeight::Get().FromValue(me_weight_).WriteTo(&dosbus_);
  HcodecSadControl0::Get()
      .FromValue(0)
      .set_ie_sad_offset_I16(i16_weight_)
      .set_ie_sad_offset_I4(i4_weight_)
      .WriteTo(&dosbus_);

  HcodecSadControl1::Get()
      .FromValue(0)
      .set_ie_sad_shift_I16(3)
      .set_ie_sad_shift_I4(3)
      .set_me_sad_shift_INTER(2)
      .set_me_sad_offset_INTER(me_weight_)
      .WriteTo(&dosbus_);

  HcodecAdvMvCtl0::Get()
      .FromValue(0)
      .set_adv_mv_large_16x8(1)
      .set_adv_mv_large_8x16(1)
      .set_adv_mv_8x8_weight(0x300)
      .set_adv_mv_4x4x4_weight(0x400)
      .WriteTo(&dosbus_);

  HcodecAdvMvCtl1::Get()
      .FromValue(0)
      .set_adv_mv_16x16_weight(0x080)
      .set_adv_mv_large_16x16(1)
      .set_adv_mv_16x8_weight(0x100)
      .WriteTo(&dosbus_);

  QuantTableInit();

  uint32_t i_pic_qp = (quant_table_i4_[0] & 0xff) + (quant_table_i16_[0] & 0xff);
  uint32_t p_pic_qp = 0;

  if (idr) {
    i_pic_qp /= 2;
  } else {
    i_pic_qp += quant_table_me_[0] & 0xff;
    i_pic_qp /= 3;
  }
  p_pic_qp = i_pic_qp;

  HcodecEncCbrTableAddr::Get().FromValue(cbr_info_->phys_base()).WriteTo(&dosbus_);
  HcodecEncCbrMbSizeAddr::Get().FromValue(cbr_info_->phys_base() + kCbrTableSize).WriteTo(&dosbus_);

  HcodecEncCbrCtl::Get()
      .FromValue(0)
      .set_init_qp_table_idx(HcodecEncCbrCtl::kStartTableId)
      .set_short_term_adjust_shift(HcodecEncCbrCtl::kShortShift)
      .set_long_term_mb_number(HcodecEncCbrCtl::kLongMbNum)
      .set_long_term_adjust_threshold(HcodecEncCbrCtl::kLongThreshold)
      .WriteTo(&dosbus_);

  HcodecEncCbrRegionSize::Get()
      .FromValue(0)
      .set_block_w(HcodecEncCbrRegionSize::kBlockWidth)
      .set_block_h(HcodecEncCbrRegionSize::kBlockHeight)
      .WriteTo(&dosbus_);

  HcodecQdctVlcQuantCtl0::Get()
      .FromValue(0)
      .set_vlc_delta_quant_1(0)
      .set_vlc_quant_1(i_pic_qp)
      .set_vlc_delta_quant_0(0)
      .set_vlc_quant_0(i_pic_qp)
      .WriteTo(&dosbus_);

  HcodecQdctVlcQuantCtl1::Get()
      .FromValue(0)
      .set_vlc_max_delta_q_neg(14)
      .set_vlc_max_delta_q_pos(13)
      .WriteTo(&dosbus_);

  HcodecVlcPicSize::Get()
      .FromValue(0)
      .set_pic_height(encoder_height_)
      .set_pic_width(encoder_width_)
      .WriteTo(&dosbus_);

  HcodecVlcPicPosition::Get().FromValue(0).set_pic_mb_nr(0).set_pic_mby(0).set_pic_mbx(0).WriteTo(
      &dosbus_);

  uint32_t i_pic_qp_c = kIPicQpCDefault;
  uint32_t p_pic_qp_c = kPPicQpCDefault;

  if (i_pic_qp < sizeof(kPicQpC)) {
    i_pic_qp_c = kPicQpC[i_pic_qp];
  }

  if (p_pic_qp < sizeof(kPicQpC)) {
    p_pic_qp_c = kPicQpC[p_pic_qp];
  }

  HcodecQdctQQuantI::Get()
      .FromValue(0)
      .set_i_pic_qp_c(i_pic_qp_c)
      .set_i_pic_qp_c_mod6(i_pic_qp_c % 6)
      .set_i_pic_qp_c_div6(i_pic_qp_c / 6)
      .set_i_pic_qp(i_pic_qp)
      .set_i_pic_qp_mod6(i_pic_qp % 6)
      .set_i_pic_qp_div6(i_pic_qp / 6)
      .WriteTo(&dosbus_);

  HcodecQdctQQuantP::Get()
      .FromValue(0)
      .set_p_pic_qp_c(p_pic_qp_c)
      .set_p_pic_qp_c_mod6(p_pic_qp_c % 6)
      .set_p_pic_qp_c_div6(p_pic_qp_c / 6)
      .set_p_pic_qp(p_pic_qp)
      .set_p_pic_qp_mod6(p_pic_qp % 6)
      .set_p_pic_qp_div6(p_pic_qp / 6)
      .WriteTo(&dosbus_);

  HcodecIgnoreConfig::Get()
      .FromValue(0)
      .set_ignore_lac_coeff_en(1)
      .set_ignore_lac_coeff_2(1)
      .set_ignore_lac_coeff_1(2)
      .set_ignore_cac_coeff_en(1)
      .set_ignore_cac_coeff_else(1)
      .set_ignore_cac_coeff_2(1)
      .set_ignore_cac_coeff_1(2)
      .WriteTo(&dosbus_);

  HcodecIgnoreConfig2::Get()
      .FromValue(0)
      .set_ignore_t_lac_coeff_en(1)
      .set_ignore_t_lac_coeff_else(1)
      .set_ignore_t_lac_coeff_2(2)
      .set_ignore_t_lac_coeff_1(6)
      .set_ignore_cdc_coeff_en(1)
      .set_ignore_t_lac_coeff_else_le_3(0)
      .set_ignore_t_lac_coeff_else_le_4(1)
      .set_ignore_cdc_only_when_empty_cac_inter(1)
      .set_ignore_cdc_only_when_one_empty_inter(1)
      .set_ignore_cdc_range_max_inter(2)
      .set_ignore_cdc_abs_max_inter(0)
      .set_ignore_cdc_only_when_empty_cac_intra(1)
      .set_ignore_cdc_only_when_one_empty_intra(1)
      .set_ignore_cdc_range_max_intra(1)
      .set_ignore_cdc_abs_max_intra(0)
      .WriteTo(&dosbus_);

  HcodecQdctMbControl::Get().FromValue(0).set_mb_info_soft_reset(1).set_soft_reset(1).WriteTo(
      &dosbus_);

  HcodecQdctMbControl::Get()
      .FromValue(0)
      .set_ignore_t_p8x8(0)
      .set_zero_mc_out_null_non_skipped_mb(0)
      .set_no_mc_out_null_non_skipped_mb(0)
      .set_mc_out_even_skipped_mb(0)
      .set_mc_out_wait_cbp_ready(0)
      .set_mc_out_wait_mb_type_ready(0)
      .set_ie_start_int_enable(1)
      .set_i_pred_enable(1)
      .set_ie_sub_enable(1)
      .set_iq_enable(1)
      .set_idct_enable(1)
      .set_mb_pause_enable(1)
      .set_q_enable(1)
      .set_dct_enable(1)
      .set_mb_info_en(1)
      .set_endian(0)
      .set_mb_read_en(0)
      .set_soft_reset(0)
      .WriteTo(&dosbus_);

  HcodecSadControl::Get()
      .FromValue(0)
      .set_ie_result_buff_enable(0)
      .set_ie_result_buff_soft_reset(1)
      .set_sad_enable(0)
      .set_sad_soft_reset(1)
      .WriteTo(&dosbus_);

  HcodecIeResultBuffer::Get().FromValue(0).WriteTo(&dosbus_);

  HcodecSadControl::Get()
      .FromValue(0)
      .set_ie_result_buff_enable(1)
      .set_ie_result_buff_soft_reset(0)
      .set_sad_enable(1)
      .set_sad_soft_reset(0)
      .WriteTo(&dosbus_);

  HcodecIeControl::Get()
      .FromValue(0)
      .set_active_ul_block(1)
      .set_ie_enable(0)
      .set_ie_soft_reset(1)
      .WriteTo(&dosbus_);

  HcodecIeControl::Get()
      .FromValue(0)
      .set_active_ul_block(1)
      .set_ie_enable(0)
      .set_ie_soft_reset(0)
      .WriteTo(&dosbus_);

  HcodecMeSkipLine::Get()
      .FromValue(0)
      .set_step_3_skip_line(8)
      .set_step_2_skip_line(8)
      .set_step_1_skip_line(2)
      .set_step_0_skip_line(0)
      .WriteTo(&dosbus_);

  HcodecMeStep0CloseMv::Get()
      .FromValue(0)
      .set_me_step0_big_sad(0x100)
      .set_me_step0_close_mv_y(2)
      .set_me_step0_close_mv_x(2)
      .WriteTo(&dosbus_);

  HcodecMeStep0CloseMv::Get()
      .FromValue(0)
      .set_me_step0_big_sad(0x100)
      .set_me_step0_close_mv_y(2)
      .set_me_step0_close_mv_x(2)
      .WriteTo(&dosbus_);

  HcodecMeSadEnough01::Get().FromValue(0).set_me_sad_enough_1(0x8).set_me_sad_enough_0(0x8).WriteTo(
      &dosbus_);

  HcodecMeSadEnough23::Get()
      .FromValue(0)
      .set_adv_mv_8x8_enough(0x81)
      .set_me_sad_enough_2(0x11)
      .WriteTo(&dosbus_);

  HcodecMeFSkipSad::Get()
      .FromValue(0)
      .set_force_skip_sad_3(0)
      .set_force_skip_sad_2(0)
      .set_force_skip_sad_1(0)
      .set_force_skip_sad_0(0)
      .WriteTo(&dosbus_);

  HcodecMeFSkipWeight::Get()
      .FromValue(0)
      .set_force_skip_weight_3(0)
      .set_force_skip_weight_2(0)
      .set_force_skip_weight_1(0)
      .set_force_skip_weight_0(0)
      .WriteTo(&dosbus_);

  HcodecMeMvWeight01::Get()
      .FromValue(0)
      .set_me_mv_step_weight_1(0)
      .set_me_mv_pre_weight_1(0)
      .set_me_mv_step_weight_0(0)
      .set_me_mv_pre_weight_0(0)
      .WriteTo(&dosbus_);

  HcodecMeMvWeight23::Get()
      .FromValue(0)
      .set_me_mv_step_weight_3(0)
      .set_me_mv_pre_weight_3(0)
      .set_me_mv_step_weight_2(0)
      .set_me_mv_pre_weight_2(0)
      .WriteTo(&dosbus_);

  HcodecMeSadRangeInc::Get()
      .FromValue(0)
      .set_me_sad_range_3(0)
      .set_me_sad_range_2(0)
      .set_me_sad_range_1(0)
      .set_me_sad_range_0(0)
      .WriteTo(&dosbus_);

  HcodecV4ForceSkipCfg::Get()
      .FromValue(0)
      .set_v4_force_q_r_intra(40)
      .set_v4_force_q_r_inter(40)
      .set_v4_force_q_y_enable(0)
      .set_v4_force_qr_y(5)
      .set_v4_force_qp_y(6)
      .set_v4_force_skip_sad(0)
      .WriteTo(&dosbus_);

  HcodecV3SkipControl::Get()
      .FromValue(0)
      .set_v3_skip_enable(1)
      .set_v3_step_1_weight_enable(1)
      .set_v3_mv_sad_weight_enable(1)
      .set_v3_ipred_type_enable(1)
      .set_v3_force_skip_sad_1(0x60)
      .set_v3_force_skip_sad_0(0x10)
      .WriteTo(&dosbus_);

  HcodecV3SkipWeight::Get()
      .FromValue(0)
      .set_v3_skip_weight_1(0x100)
      .set_v3_skip_weight_0(0x20)
      .WriteTo(&dosbus_);

  HcodecV3L1SkipMaxSad::Get()
      .FromValue(0)
      .set_v3_level_1_f_skip_max_sad(0x20)
      .set_v3_level_1_skip_max_sad(0x60)
      .WriteTo(&dosbus_);

  HcodecV3L2SkipWeight::Get()
      .FromValue(0)
      .set_v3_force_skip_sad_2(0)
      .set_v3_skip_weight_2(0x40)
      .WriteTo(&dosbus_);

  HcodecV3FZeroCtl0::Get()
      .FromValue(0)
      .set_v3_ie_f_zero_sad_I16(0x3c0)
      .set_v3_ie_f_zero_sad_I4(0x7d5)
      .WriteTo(&dosbus_);

  HcodecV3FZeroCtl1::Get()
      .FromValue(0)
      .set_v3_no_ver_when_top_zero_en(0)
      .set_v3_no_hor_when_left_zero_en(1)
      .set_type_hor_break(3)
      .set_v3_me_f_zero_sad(0x360)
      .WriteTo(&dosbus_);

  // MV SAD Table
  for (unsigned int i : kV3MvSad) {
    HcodecV3MvSadTable::Get().FromValue(i).WriteTo(&dosbus_);
  }

  // IE PRED SAD Table
  HcodecV3IpredTypeWeight0::Get()
      .FromValue(0)
      .set_C_ipred_weight_H(0x8)
      .set_C_ipred_weight_V(0x4)
      .set_I4_ipred_weight_else(0x28)
      .set_I4_ipred_weight_most(0x18)
      .WriteTo(&dosbus_);
  HcodecV3IpredTypeWeight1::Get()
      .FromValue(0)
      .set_I16_ipred_weight_DC(0xc)
      .set_I16_ipred_weight_H(0x8)
      .set_I16_ipred_weight_V(0x4)
      .set_C_ipred_weight_DC(0xc)
      .WriteTo(&dosbus_);

  HcodecV3LeftSmallMaxSad::Get()
      .FromValue(0)
      .set_v3_left_small_max_me_sad(0x40)
      .set_v3_left_small_max_ie_sad(0x00)
      .WriteTo(&dosbus_);

  HcodecIeDataFeedBuffInfo::Get().FromValue(0).WriteTo(&dosbus_);

  HcodecCurrCanvasCtrl::Get().FromValue(0).WriteTo(&dosbus_);

  HcodecVlcConfig::Get().ReadFrom(&dosbus_).set_pop_coeff_even_all_zero(1).WriteTo(&dosbus_);

  HcodecInfoDumpStartAddr::Get().FromValue(dump_info_->phys_base()).WriteTo(&dosbus_);

  HcodecIrqMboxClear::Get().FromValue(1).WriteTo(&dosbus_);
  HcodecIrqMboxMask::Get().FromValue(1).WriteTo(&dosbus_);
}

// encoder control
zx_status_t DeviceCtx::EncoderInit(const fuchsia::media::FormatDetails& format_details) {
  memset(quant_table_i4_, kInitialQuant, sizeof(quant_table_i4_));
  memset(quant_table_i16_, kInitialQuant, sizeof(quant_table_i16_));
  memset(quant_table_me_, kInitialQuant, sizeof(quant_table_me_));

  auto status = UpdateEncoderSettings(format_details);
  if (status != ZX_OK) {
    return status;
  }

  status = BufferAlloc();
  if (status != ZX_OK) {
    return status;
  }

  status = PowerOn();
  if (status != ZX_OK) {
    return status;
  }

  status = CanvasInit();
  if (status != ZX_OK) {
    return status;
  }

  firmware_loaded_ = false;
  return ZX_OK;
}

zx_status_t DeviceCtx::UpdateEncoderSettings(const fuchsia::media::FormatDetails& format_details) {
  if (format_details.has_domain() && format_details.domain().is_video() &&
      format_details.domain().video().is_uncompressed()) {
    uint32_t width = format_details.domain().video().uncompressed().image_format.display_width;
    uint32_t height = format_details.domain().video().uncompressed().image_format.display_height;

    if ((encoder_width_ && width != encoder_width_) ||
        (encoder_height_ && height != encoder_height_)) {
      ENCODE_ERROR("frame size change not allowed");
      return ZX_ERR_INVALID_ARGS;
    }

    encoder_width_ = width;
    encoder_height_ = height;
    rows_per_slice_ = picture_to_mb(encoder_height_);
  }

  if (encoder_width_ == 0 || encoder_height_ == 0) {
    ENCODE_ERROR("frame size must be specified");
    return ZX_ERR_INVALID_ARGS;
  }

  if (encoder_width_ % 16 != 0 || encoder_height_ % 2 != 0) {
    ENCODE_ERROR("frame size must be multiple of 16");
    return ZX_ERR_INVALID_ARGS;
  }

  if (format_details.has_encoder_settings() && format_details.encoder_settings().is_h264()) {
    auto& settings = format_details.encoder_settings().h264();
    // TODO(afoxley) reset any stream level state when these settings change
    if (settings.has_bit_rate()) {
      bit_rate_ = settings.bit_rate();
      sps_pps_size_ = 0;
    }
    if (settings.has_frame_rate()) {
      frame_rate_ = settings.frame_rate();
      sps_pps_size_ = 0;
    }
    if (settings.has_gop_size()) {
      gop_size_ = settings.gop_size();
      if (gop_size_ > std::numeric_limits<uint16_t>::max()) {
        gop_size_ = std::numeric_limits<uint16_t>::max();
      }

      // reset any in progress group
      frame_number_ = 0;
      pic_order_cnt_lsb_ = 0;
      sps_pps_size_ = 0;
    }
  }

  return ZX_OK;
}

void DeviceCtx::Start() {
  for (int i = 0; i < 3; i++) {
    // delay
    DosSwReset1::Get().ReadFrom(&dosbus_);
  }

  DosSwReset1::Get().FromValue(0).set_hcodec_ccpu(1).set_hcodec_mcpu(1).WriteTo(&dosbus_);
  DosSwReset1::Get().FromValue(0).WriteTo(&dosbus_);

  for (int i = 0; i < 3; i++) {
    // delay
    DosSwReset1::Get().ReadFrom(&dosbus_);
  }

  HcodecMpsr::Get().FromValue(0x0001).WriteTo(&dosbus_);
}

zx_status_t DeviceCtx::EnsureFwLoaded() {
  if (firmware_loaded_) {
    return ZX_OK;
  }

  auto status = LoadFirmware();
  if (status != ZX_OK) {
    return status;
  }

  firmware_loaded_ = true;

  Reset();
  HcodecEncoderStatus::Get().FromValue(EncoderStatus::kIdle).WriteTo(&dosbus_);
  Start();

  return ZX_OK;
}

void DeviceCtx::FrameReset(bool idr) {
  ZX_DEBUG_ASSERT(input_buffer_);
  ZX_DEBUG_ASSERT(output_buffer_);
  HcodecIeMeMbType::MbType mb_type = HcodecIeMeMbType::MbType::kAuto;

  Reset();
  Config(idr);
  ReferenceBuffersConfig();
  InputBufferConfig(input_buffer_->physical_base(), input_buffer_->size());
  OutputBufferConfig(output_buffer_->physical_base(), output_buffer_->size());

  if (idr) {
    mb_type = HcodecIeMeMbType::MbType::kI4MB;
  }
  IeMeParameterInit(mb_type);
  SetInputFormat();
}

zx_status_t DeviceCtx::StopEncoder() {
  HcodecMpsr::Get().FromValue(0).WriteTo(&dosbus_);
  HcodecCpsr::Get().FromValue(0).WriteTo(&dosbus_);

  if (!WaitForRegister(std::chrono::seconds(1), [this] {
        return HcodecImemDmaCtrl::Get().ReadFrom(&dosbus_).ready() == 0;
      })) {
    ENCODE_ERROR("Failed to stop dma.");
    return ZX_ERR_TIMED_OUT;
  }

  for (int i = 0; i < 3; i++) {
    // delay
    DosSwReset1::Get().ReadFrom(&dosbus_);
  }

  DosSwReset1::Get()
      .FromValue(0)
      .set_hcodec_ccpu(1)
      .set_hcodec_mcpu(1)
      .set_hcodec_assist(1)
      .set_hcodec_iqidct(1)
      .set_hcodec_mc(1)
      .set_hcodec_dblk(1)
      .set_hcodec_afifo(1)
      .set_hcodec_vlc(1)
      .set_hcodec_qdct(1)
      .WriteTo(&dosbus_);

  DosSwReset1::Get().FromValue(DosSwReset1::kNone).WriteTo(&dosbus_);

  for (int i = 0; i < 3; i++) {
    // delay
    DosSwReset1::Get().ReadFrom(&dosbus_);
  }

  return ZX_OK;
}

zx_status_t DeviceCtx::EncodeCmd(EncoderStatus cmd, uint32_t* output_len) {
  constexpr zx::duration kCmdTimeout = zx::msec(1000);

  ZX_DEBUG_ASSERT(output_len);

  sync_completion_reset(&interrupt_sync_);

  hw_status_ = cmd;
  HcodecEncoderStatus::Get().FromValue(hw_status_).WriteTo(&dosbus_);

  zx_status_t status = sync_completion_wait(&interrupt_sync_, kCmdTimeout.get());

  hw_status_ =
      static_cast<EncoderStatus>(HcodecEncoderStatus::Get().ReadFrom(&dosbus_).reg_value());
  if (status == ZX_ERR_TIMED_OUT) {
    ENCODE_ERROR("no interrupt, status %d", hw_status_);
    ENCODE_INFO("mb info: 0x%x, encode status: 0x%x, dct status: 0x%x",
                HcodecVlcMbInfo::Get().ReadFrom(&dosbus_).reg_value(),
                HcodecEncoderStatus::Get().ReadFrom(&dosbus_).reg_value(),
                HcodecQdctStatusCtrl::Get().ReadFrom(&dosbus_).reg_value());
    ENCODE_INFO("vlc status: 0x%x, me status: 0x%x, risc pc:0x%x, debug:0x%x",
                HcodecVlcStatusCtrl::Get().ReadFrom(&dosbus_).reg_value(),
                HcodecMeStatus::Get().ReadFrom(&dosbus_).reg_value(),
                HcodecMpcE::Get().ReadFrom(&dosbus_).reg_value(),
                HcodecDebugReg::Get().ReadFrom(&dosbus_).reg_value());
    return status;
  }

  if (hw_status_ == EncoderStatus::kIdrDone || hw_status_ == EncoderStatus::kNonIdrDone ||
      hw_status_ == EncoderStatus::kPictureDone || hw_status_ == EncoderStatus::kSequenceDone) {
    *output_len = HcodecVlcTotalBytes::Get().ReadFrom(&dosbus_).reg_value();
  } else {
    ENCODE_ERROR("status %d", hw_status_);
    return ZX_ERR_TIMED_OUT;
  }

  return ZX_OK;
}

zx_status_t DeviceCtx::EncodeFrame(uint32_t* output_len) {
  ZX_DEBUG_ASSERT(output_len);
  ZX_DEBUG_ASSERT(output_buffer_);
  ZX_DEBUG_ASSERT(input_buffer_);

  auto status = EnsureFwLoaded();
  if (status != ZX_OK) {
    return status;
  }

  EncoderStatus cmd = EncoderStatus::kIdr;
  if (frame_number_ > 0) {
    cmd = EncoderStatus::kNonIdr;
  }
  bool idr = cmd == EncoderStatus::kIdr;

  if (idr && !sps_pps_size_) {
    status = EncodeSPSPPS();
    if (status != ZX_OK) {
      return status;
    }
  }

  FrameReset(idr);

  status = EncodeCmd(cmd, output_len);
  if (status != ZX_OK) {
    return status;
  }

  // setup for next frame
  if (idr) {
    idr_pic_id_++;
  }

  pic_order_cnt_lsb_ += 2;
  frame_number_ += 1;

  if (frame_number_ > gop_size_) {
    frame_number_ = 0;
    pic_order_cnt_lsb_ = 0;
  }

  // commit and swap to next canvas buffer
  std::swap(ref_buf_canvas_, dblk_buf_canvas_);

  // TODO(afoxley) If output is in RAM domain the invalidate would not be required
  status = zx_cache_flush(output_buffer_->base(), *output_len,
                          ZX_CACHE_FLUSH_DATA | ZX_CACHE_FLUSH_INVALIDATE);

  if (idr) {
    // The encoder hardware aparently requires the output pointer to be 16 byte aligned, so always
    // encode to the start and then shuffle the SPS/PPS data in when we insert it.
    // TODO(afoxley), if frequent SPS/PPS NAL's are required then make a separate API for them.
    memmove(output_buffer_->base() + sps_pps_size_, output_buffer_->base(), *output_len);
    memcpy(output_buffer_->base(), sps_pps_data_->virt_base(), sps_pps_size_);
    *output_len += sps_pps_size_;
  }

  if (status != ZX_OK) {
    return status;
  }

  return ZX_OK;
}

zx_status_t DeviceCtx::EncodeSPSPPS() {
  sps_pps_size_ = 0;
  FrameReset(/*idr*/ false);
  OutputBufferConfig(sps_pps_data_->phys_base(), sps_pps_data_->size());

  IeMeParameterInit(HcodecIeMeMbType::kDefault);
  auto status = EncodeCmd(EncoderStatus::kSequence, &sps_pps_size_);
  if (status != ZX_OK) {
    return status;
  }

  IeMeParameterInit(HcodecIeMeMbType::kDefault);
  status = EncodeCmd(EncoderStatus::kPicture, &sps_pps_size_);
  if (status != ZX_OK) {
    return status;
  }

  sps_pps_data_->CacheFlushInvalidate(0, sps_pps_size_);
  return ZX_OK;
}

fidl::InterfaceHandle<fuchsia::sysmem::Allocator> DeviceCtx::ConnectToSysmem() {
  fidl::InterfaceHandle<fuchsia::sysmem::Allocator> client_end;
  fidl::InterfaceRequest<fuchsia::sysmem::Allocator> server_end = client_end.NewRequest();
  zx_status_t connect_status = sysmem_.Connect(server_end.TakeChannel());
  if (connect_status != ZX_OK) {
    // failure
    return fidl::InterfaceHandle<fuchsia::sysmem::Allocator>();
  }
  return client_end;
}

void DeviceCtx::GetCodecFactory(zx::channel request) {
  // post to fidl thread to avoid racing on creation/error
  async::PostTask(loop_.dispatcher(), [this, request = std::move(request)]() mutable {
    fidl::InterfaceRequest<fuchsia::mediacodec::CodecFactory> factory_request(std::move(request));

    std::unique_ptr<LocalCodecFactory> codec_factory = std::make_unique<LocalCodecFactory>(
        loop_.dispatcher(), this, std::move(factory_request),
        /*factory_done_callback=*/
        [this](LocalCodecFactory* codec_factory,
               std::unique_ptr<CodecImpl> created_codec_instance) {
          // own codec impl and bind it
          codec_instance_ = std::move(created_codec_instance);
          codec_instance_->BindAsync(/*error_handler=*/[this] {
            // Drop codec impl and close channel on error
            codec_instance_ = nullptr;
          });
          // drop factory and close factory channel
          codec_factories_.erase(codec_factory);
        },
        codec_admission_control_.get(),
        /* error_handler */
        [this](LocalCodecFactory* codec_factory, zx_status_t error) {
          // Drop and close factory channel on error.
          codec_factories_.erase(codec_factory);
        });

    codec_factories_.emplace(codec_factory.get(), std::move(codec_factory));
  });
}
