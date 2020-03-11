// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/drivers/amlogic_encoder/device_ctx.h"

#include <fuchsia/mediacodec/cpp/fidl.h>
#include <lib/async/cpp/task.h>
#include <zircon/assert.h>
#include <zircon/types.h>

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
  composite.GetFragments(fragments, fbl::count_of(fragments), &fragment_count);

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
      ENCODE_ERROR("Unknown soc pid: %d\n", info.pid);
      return ZX_ERR_INVALID_ARGS;
  }

  status = load_firmware(parent(), "h264_enc.bin", firmware_vmo_.reset_and_get_address(),
                         &firmware_size_);
  if (status != ZX_OK) {
    ENCODE_ERROR("Couldn't load firmware\n");
    return status;
  }

  zx::vmar::root_self()->map(0, firmware_vmo_, 0, firmware_size_, ZX_VM_PERM_READ, &firmware_ptr_);

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

zx_status_t DeviceCtx::LoadFirmware() {
  io_buffer_t firmware_buffer;
  const uint64_t kFirmwareSize = 8 * 4096;
  const uint32_t kDmaCount = 0x1000;
  // Most buffers should be 64-kbyte aligned.
  const uint32_t kBufferAlignShift = 16;
  const char* kBufferName = "EncFirmware";

  HcodecAssistMmcCtrl1::Get().FromValue(HcodecAssistMmcCtrl1::kCtrl).WriteTo(&dosbus_);

  zx_status_t status = io_buffer_init_aligned(&firmware_buffer, bti_.get(), kFirmwareSize,
                                              kBufferAlignShift, IO_BUFFER_RW | IO_BUFFER_CONTIG);
  if (status != ZX_OK) {
    ENCODE_ERROR("Failed to make firmware buffer");
    return status;
  }

  zx_object_set_property(firmware_buffer.vmo_handle, ZX_PROP_NAME, kBufferName,
                         strlen(kBufferName));

  memcpy(io_buffer_virt(&firmware_buffer), reinterpret_cast<void*>(firmware_ptr_),
         std::min(firmware_size_, kFirmwareSize));
  io_buffer_cache_flush(&firmware_buffer, 0, kFirmwareSize);

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

  return ZX_OK;
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

zx_status_t DeviceCtx::CanvasInitReference(InternalBuffer* buf, ScopedCanvasId* y_canvas, ScopedCanvasId* uv_canvas, uint32_t* packed_canvas_ids)
{
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
  status =
      CanvasConfig(dup_vmo.get(), canvas_width, canvas_height / 2,
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

void DeviceCtx::Config() {
  HcodecVlcTotalBytes::Get().FromValue(0).WriteTo(&dosbus_);
  HcodecVlcConfig::Get().FromValue(0x7).WriteTo(&dosbus_);
  HcodecVlcIntControl::Get().FromValue(0).WriteTo(&dosbus_);

  HcodecAssistAmr1Int0::Get().FromValue(0x15).WriteTo(&dosbus_);
  HcodecAssistAmr1Int1::Get().FromValue(0x8).WriteTo(&dosbus_);
  HcodecAssistAmr1Int3::Get().FromValue(0x14).WriteTo(&dosbus_);

  HcodecIdrPicId::Get().FromValue(idr_pic_id_).WriteTo(&dosbus_);
  HcodecFrameNumber::Get().FromValue(frame_number_).WriteTo(&dosbus_);
  HcodecPicOrderCntLsb::Get().FromValue(pic_order_cnt_lsb_).WriteTo(&dosbus_);

  const uint32_t kInitQpPicture = 26;
  const uint32_t kLog2MaxFrameNum = 4;
  const uint32_t kLog2MaxPicOrderCntLsb = 4;
  const uint32_t kAnc0BufferId = 0;

  HcodecLog2MaxPicOrderCntLsb::Get().FromValue(kLog2MaxPicOrderCntLsb).WriteTo(&dosbus_);
  HcodecLog2MaxFrameNum::Get().FromValue(kLog2MaxFrameNum).WriteTo(&dosbus_);
  HcodecAnc0BufferId::Get().FromValue(kAnc0BufferId).WriteTo(&dosbus_);
  HcodecQpPicture::Get().FromValue(kInitQpPicture).WriteTo(&dosbus_);
}

// encoder control
zx_status_t DeviceCtx::EncoderInit(const fuchsia::media::FormatDetails& format_details) {
  if (!format_details.has_domain() || !format_details.domain().is_video() ||
      !format_details.domain().video().is_uncompressed()) {
    return ZX_ERR_INVALID_ARGS;
  }

  encoder_width_ = format_details.domain().video().uncompressed().image_format.display_width;
  encoder_height_ = format_details.domain().video().uncompressed().image_format.display_height;
  rows_per_slice_ = picture_to_mb(encoder_height_);

  auto status = BufferAlloc();
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

  status = LoadFirmware();
  if (status != ZX_OK) {
    return status;
  }

  Reset();

  Config();

  return ZX_OK;
}

zx_status_t DeviceCtx::StartEncoder() { return ZX_OK; }

zx_status_t DeviceCtx::StopEncoder() { return ZX_OK; }
zx_status_t DeviceCtx::WaitForIdle() { return ZX_OK; }
zx_status_t DeviceCtx::EncodeFrame(const CodecBuffer* buffer, uint8_t* data, uint32_t len) {
  return ZX_OK;
}
void DeviceCtx::ReturnBuffer(const CodecBuffer* buffer) {}
void DeviceCtx::SetOutputBuffers(std::vector<const CodecBuffer*> buffers) {}

void DeviceCtx::SetEncodeParams(fuchsia::media::FormatDetails format_details) {}

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
