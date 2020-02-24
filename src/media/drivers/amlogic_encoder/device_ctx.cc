// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/drivers/amlogic_encoder/device_ctx.h"

#include <fuchsia/mediacodec/cpp/fidl.h>
#include <lib/async/cpp/task.h>
#include <zircon/assert.h>
#include <zircon/types.h>

#include <memory>

#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/mmio-buffer.h>
#include <ddk/platform-defs.h>
#include <ddk/protocol/composite.h>
#include <ddk/protocol/platform/device.h>

#include "src/media/drivers/amlogic_encoder/local_codec_factory.h"
#include "src/media/drivers/amlogic_encoder/macros.h"
#include "src/media/drivers/amlogic_encoder/memory_barriers.h"
#include "src/media/drivers/amlogic_encoder/registers.h"

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
  kComponentPdev = 0,
  kComponentSysmem = 1,
  kComponentCanvas = 2,
  kComponentCount = 3,
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

std::pair<zx_status_t, std::unique_ptr<DeviceCtx>> DeviceCtx::Bind(zx_device_t* parent) {
  auto device_ctx = std::make_unique<DeviceCtx>(parent);
  auto status = device_ctx->Init();

  if (status != ZX_OK) {
    return {status, nullptr};
  }

  status = device_ctx->AddDevice();

  return {status, std::move(device_ctx)};
}

void DeviceCtx::DdkUnbindNew(ddk::UnbindTxn txn) {
  ShutDown();
  txn.Reply();
}

void DeviceCtx::DdkRelease() { delete this; }

zx_status_t DeviceCtx::DdkMessage(fidl_msg_t* msg, fidl_txn_t* txn) {
  return fuchsia_hardware_mediacodec_Device_dispatch(this, txn, msg, &kFidlOps);
}

void DeviceCtx::ShutDown() { loop_.Shutdown(); }

zx_status_t DeviceCtx::AddDevice() {
  auto status = loop_.StartThread("device-loop", &loop_thread_);
  if (status != ZX_OK) {
    ENCODE_ERROR("could not start loop thread");
    return status;
  }

  status = DdkAdd("amlogic_video_enc");

  return status;
}

zx_status_t DeviceCtx::Init() {
  composite_protocol_t composite;
  auto status = device_get_protocol(parent(), ZX_PROTOCOL_COMPOSITE, &composite);
  if (status != ZX_OK) {
    ENCODE_ERROR("Could not get composite protocol\n");
    return status;
  }

  zx_device_t* components[kComponentCount];
  size_t actual;
  composite_get_components(&composite, components, kComponentCount, &actual);
  if (actual != kComponentCount) {
    ENCODE_ERROR("could not get components\n");
    return ZX_ERR_NOT_SUPPORTED;
  }

  status = device_get_protocol(components[kComponentPdev], ZX_PROTOCOL_PDEV, &pdev_);
  if (status != ZX_OK) {
    ENCODE_ERROR("Failed to get pdev protocol\n");
    return ZX_ERR_NO_MEMORY;
  }

  status = device_get_protocol(components[kComponentSysmem], ZX_PROTOCOL_SYSMEM, &sysmem_);
  if (status != ZX_OK) {
    ENCODE_ERROR("Could not get SYSMEM protocol\n");
    return status;
  }

  status = device_get_protocol(components[kComponentCanvas], ZX_PROTOCOL_AMLOGIC_CANVAS, &canvas_);
  if (status != ZX_OK) {
    ENCODE_ERROR("Could not get video CANVAS protocol\n");
    return status;
  }

  pdev_device_info_t info;
  status = pdev_get_device_info(&pdev_, &info);
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

  mmio_buffer_t cbus_mmio;
  status = pdev_map_mmio_buffer(&pdev_, kCbus, ZX_CACHE_POLICY_UNCACHED_DEVICE, &cbus_mmio);
  if (status != ZX_OK) {
    ENCODE_ERROR("Failed map cbus");
    return ZX_ERR_NO_MEMORY;
  }
  cbus_ = std::make_unique<CbusRegisterIo>(cbus_mmio);

  mmio_buffer_t dosbus_mmio;
  status = pdev_map_mmio_buffer(&pdev_, kDosbus, ZX_CACHE_POLICY_UNCACHED_DEVICE, &dosbus_mmio);
  if (status != ZX_OK) {
    ENCODE_ERROR("Failed map dosbus");
    return ZX_ERR_NO_MEMORY;
  }
  dosbus_ = std::make_unique<DosRegisterIo>(dosbus_mmio);

  mmio_buffer_t aobus_mmio;
  status = pdev_map_mmio_buffer(&pdev_, kAobus, ZX_CACHE_POLICY_UNCACHED_DEVICE, &aobus_mmio);
  if (status != ZX_OK) {
    ENCODE_ERROR("Failed map aobus");
    return ZX_ERR_NO_MEMORY;
  }
  aobus_ = std::make_unique<AoRegisterIo>(aobus_mmio);

  mmio_buffer_t hiubus_mmio;
  status = pdev_map_mmio_buffer(&pdev_, kHiubus, ZX_CACHE_POLICY_UNCACHED_DEVICE, &hiubus_mmio);
  if (status != ZX_OK) {
    ENCODE_ERROR("Failed map hiubus");
    return ZX_ERR_NO_MEMORY;
  }
  hiubus_ = std::make_unique<HiuRegisterIo>(hiubus_mmio);

  status =
      pdev_get_interrupt(&pdev_, kDosMbox2Irq, 0, enc_interrupt_handle_.reset_and_get_address());
  if (status != ZX_OK) {
    ENCODE_ERROR("Failed get enc interrupt");
    return ZX_ERR_NO_MEMORY;
  }

  status = pdev_get_bti(&pdev_, 0, bti_.reset_and_get_address());
  if (status != ZX_OK) {
    ENCODE_ERROR("Failed get bti");
    return ZX_ERR_NO_MEMORY;
  }

  status = load_firmware(parent(), "h264_enc.bin", firmware_vmo_.reset_and_get_address(),
                         &firmware_size_);
  if (status != ZX_OK) {
    ENCODE_ERROR("Couldn't load firmware\n");
    return status;
  }

  zx::vmar::root_self()->map(0, firmware_vmo_, 0, firmware_size_, ZX_VM_PERM_READ, &firmware_ptr_);

  return ZX_OK;
}

zx_status_t DeviceCtx::LoadFirmware() {
  io_buffer_t firmware_buffer;
  const uint64_t kFirmwareSize = 8 * 4096;
  const uint32_t kDmaCount = 0x1000;
  // Most buffers should be 64-kbyte aligned.
  const uint32_t kBufferAlignShift = 16;
  const char* kBufferName = "EncFirmware";

  HcodecAssistMmcCtrl1::Get().FromValue(HcodecAssistMmcCtrl1::kCtrl).WriteTo(dosbus_.get());

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

  HcodecMpsr::Get().FromValue(0).WriteTo(dosbus_.get());
  HcodecCpsr::Get().FromValue(0).WriteTo(dosbus_.get());

  // delay
  for (int i = 0; i < 3; i++) {
    HcodecMpsr::Get().ReadFrom(dosbus_.get());
  }

  HcodecImemDmaAdr::Get()
      .FromValue(truncate_to_32(io_buffer_phys(&firmware_buffer)))
      .WriteTo(dosbus_.get());
  HcodecImemDmaCount::Get().FromValue(kDmaCount).WriteTo(dosbus_.get());
  HcodecImemDmaCtrl::Get()
      .FromValue(0)
      .set_ready(1)
      .set_ctrl(HcodecImemDmaCtrl::kCtrl)
      .WriteTo(dosbus_.get());

  if (!WaitForRegister(std::chrono::seconds(1), [this] {
        return HcodecImemDmaCtrl::Get().ReadFrom(dosbus_.get()).ready() == 0;
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

zx_status_t DeviceCtx::BufferInit() { return ZX_OK; }

zx_status_t DeviceCtx::PowerOn() {
  // ungate dos clk
  // TODO(45193) shared with vdec
  HhiGclkMpeg0::Get().ReadFrom(hiubus_.get()).set_dos(true).WriteTo(hiubus_.get());

  // power up HCODEC
  AoRtiGenPwrSleep0::Get()
      .ReadFrom(aobus_.get())
      .set_dos_hcodec_d1_pwr_off(0)
      .set_dos_hcodec_pwr_off(0)
      .WriteTo(aobus_.get());
  zx_nanosleep(zx_deadline_after(ZX_USEC(10)));

  // reset all of HCODEC
  DosSwReset1::Get().FromValue(DosSwReset1::kAll).WriteTo(dosbus_.get());
  DosSwReset1::Get().FromValue(DosSwReset1::kNone).WriteTo(dosbus_.get());

  // ungate clock
  switch (kClockLevel) {
    case 4:  // 400mhz
    {
      // TODO(45193) shared with vdec, should be synchronized
      HhiVdecClkCntl::Get()
          .ReadFrom(hiubus_.get())
          .set_hcodec_clk_sel(/*fclk_div4*/ 2)
          .set_hcodec_clk_en(1)
          .set_hcodec_clk_div(/*fclk_div2p5*/ 0)
          .WriteTo(hiubus_.get());
      break;
    }
    default:
      return ZX_ERR_NOT_SUPPORTED;
  }

  // TODO(45193) shared with vdec
  DosGclkEn0::Get().ReadFrom(dosbus_.get()).set_hcodec_en(0x7fff).WriteTo(dosbus_.get());

  // powerup hcodec memories
  DosMemPdHcodec::Get().FromValue(0).WriteTo(dosbus_.get());

  // remove hcodec iso
  AoRtiGenPwrIso0::Get()
      .ReadFrom(aobus_.get())
      .set_dos_hcodec_iso_in_en(0)
      .set_dos_hcodec_iso_out_en(0)
      .WriteTo(aobus_.get());
  zx_nanosleep(zx_deadline_after(ZX_USEC(10)));

  // TODO(45193) shared with vdec
  // disable auto clock gate
  DosGenCtrl0::Get().ReadFrom(dosbus_.get()).set_hcodec_auto_clock_gate(1).WriteTo(dosbus_.get());
  DosGenCtrl0::Get().ReadFrom(dosbus_.get()).set_hcodec_auto_clock_gate(0).WriteTo(dosbus_.get());

  zx_nanosleep(zx_deadline_after(ZX_USEC(10)));

  return ZX_OK;
}

zx_status_t DeviceCtx::CanvasInit() { return ZX_OK; }

void DeviceCtx::Reset() {
  for (int i = 0; i < 3; i++) {
    // delay
    DosSwReset1::Get().ReadFrom(dosbus_.get());
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
      .WriteTo(dosbus_.get());

  DosSwReset1::Get().FromValue(DosSwReset1::kNone).WriteTo(dosbus_.get());

  for (int i = 0; i < 3; i++) {
    // delay
    DosSwReset1::Get().ReadFrom(dosbus_.get());
  }
}

void DeviceCtx::Config() {
  HcodecVlcTotalBytes::Get().FromValue(0).WriteTo(dosbus_.get());
  HcodecVlcConfig::Get().FromValue(0x7).WriteTo(dosbus_.get());
  HcodecVlcIntControl::Get().FromValue(0).WriteTo(dosbus_.get());

  HcodecAssistAmr1Int0::Get().FromValue(0x15).WriteTo(dosbus_.get());
  HcodecAssistAmr1Int1::Get().FromValue(0x8).WriteTo(dosbus_.get());
  HcodecAssistAmr1Int3::Get().FromValue(0x14).WriteTo(dosbus_.get());

  HcodecIdrPicId::Get().FromValue(idr_pic_id_).WriteTo(dosbus_.get());
  HcodecFrameNumber::Get().FromValue(frame_number_).WriteTo(dosbus_.get());
  HcodecPicOrderCntLsb::Get().FromValue(pic_order_cnt_lsb_).WriteTo(dosbus_.get());

  const uint32_t kInitQpPicture = 26;
  const uint32_t kLog2MaxFrameNum = 4;
  const uint32_t kLog2MaxPicOrderCntLsb = 4;
  const uint32_t kAnc0BufferId = 0;

  HcodecLog2MaxPicOrderCntLsb::Get().FromValue(kLog2MaxPicOrderCntLsb).WriteTo(dosbus_.get());
  HcodecLog2MaxFrameNum::Get().FromValue(kLog2MaxFrameNum).WriteTo(dosbus_.get());
  HcodecAnc0BufferId::Get().FromValue(kAnc0BufferId).WriteTo(dosbus_.get());
  HcodecQpPicture::Get().FromValue(kInitQpPicture).WriteTo(dosbus_.get());
}

// encoder control
zx_status_t DeviceCtx::InitEncoder(const fuchsia::media::FormatDetails& initial_format_details) {
  auto status = BufferInit();
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
  zx_status_t connect_status = sysmem_connect(&sysmem_, server_end.TakeChannel().release());
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
