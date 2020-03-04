// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_DRIVERS_AMLOGIC_ENCODER_DEVICE_CTX_H_
#define SRC_MEDIA_DRIVERS_AMLOGIC_ENCODER_DEVICE_CTX_H_

#include <fuchsia/hardware/mediacodec/c/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/device-protocol/pdev.h>
#include <lib/device-protocol/platform-device.h>
#include <lib/fidl-utils/bind.h>
#include <lib/sync/completion.h>
#include <zircon/fidl.h>
#include <zircon/types.h>

#include <memory>
#include <thread>
#include <unordered_map>

#include <ddk/driver.h>
#include <ddk/platform-defs.h>
#include <ddk/protocol/amlogiccanvas.h>
#include <ddk/protocol/platform/device.h>
#include <ddk/protocol/sysmem.h>
#include <ddktl/device.h>
#include <ddktl/protocol/empty-protocol.h>

#include "ddktl/protocol/amlogiccanvas.h"
#include "ddktl/protocol/platform/device.h"
#include "ddktl/protocol/sysmem.h"
#include "fuchsia/media/cpp/fidl.h"
#include "lib/mmio/mmio.h"
#include "lib/zx/interrupt.h"
#include "src/media/drivers/amlogic_encoder/internal_buffer.h"
#include "src/media/drivers/amlogic_encoder/local_codec_factory.h"
#include "src/media/drivers/amlogic_encoder/registers.h"
#include "src/media/drivers/amlogic_encoder/scoped_canvas.h"

constexpr uint32_t kCanvasMinWidthAlignment = 32;
constexpr uint32_t kCanvasMinHeightAlignment = 16;

enum class SocType {
  kUnknown,
  // These should be ordered from oldest to newest.
  kG12A = 2,  // S905D2
  kG12B = 3,  // T931
};

// Returns true if |a| is newer than or the same as |b|.
inline bool IsDeviceAtLeast(SocType a, SocType b) {
  return static_cast<int>(a) >= static_cast<int>(b);
}

class DeviceCtx;
using DeviceType = ddk::Device<DeviceCtx, ddk::UnbindableNew, ddk::Messageable>;

// Manages context for the device's lifetime.
class DeviceCtx : public DeviceType, public ddk::EmptyProtocol<ZX_PROTOCOL_MEDIA_CODEC> {
 public:
  // Creates and binds an instance of `DeviceCtx`. If successful, returns ZX_OK
  // and a bound instance of `DeviceCtx`, otherwise an error status and
  // `nullptr`.
  static std::unique_ptr<DeviceCtx> Create(zx_device_t* parent);

  explicit DeviceCtx(zx_device_t* parent, ddk::PDevProtocolClient pdev,
                     ddk::AmlogicCanvasProtocolClient canvas, ddk::SysmemProtocolClient sysmem,
                     CbusRegisterIo cbus, DosRegisterIo dosbus, AoRegisterIo aobus,
                     HiuRegisterIo hiubus, zx::interrupt interrupt, zx::bti bti)
      : DeviceType(parent),
        pdev_(pdev),
        canvas_(canvas),
        sysmem_(sysmem),
        cbus_(std::move(cbus)),
        dosbus_(std::move(dosbus)),
        aobus_(std::move(aobus)),
        hiubus_(std::move(hiubus)),
        bti_(std::move(bti)),
        interrupt_handle_(std::move(interrupt)),
        loop_(&kAsyncLoopConfigNoAttachToCurrentThread),
        codec_admission_control_(std::make_unique<CodecAdmissionControl>(loop_.dispatcher())) {}

  DeviceCtx(const DeviceCtx&) = delete;
  DeviceCtx& operator=(const DeviceCtx&) = delete;

  ~DeviceCtx() { ShutDown(); }

  // init and add device
  zx_status_t Bind();

  // media codec FIDL implementation
  void GetCodecFactory(zx::channel request);

  // Methods required by the ddk.
  void DdkRelease();
  void DdkUnbindNew(ddk::UnbindTxn txn);
  zx_status_t DdkMessage(fidl_msg_t* msg, fidl_txn_t* txn);

  zx::unowned_bti bti() { return zx::unowned_bti(bti_); }

  // This gets started connecting to sysmem, but returns an InterfaceHandle
  // instead of InterfacePtr so that the caller can bind to the dispatcher.
  fidl::InterfaceHandle<fuchsia::sysmem::Allocator> ConnectToSysmem();

  // encoder control, runs on input processing thread
  zx_status_t EncoderInit(const fuchsia::media::FormatDetails& format_details);
  zx_status_t StartEncoder();
  zx_status_t StopEncoder();
  zx_status_t WaitForIdle();
  zx_status_t EncodeFrame(const CodecBuffer* buffer, uint8_t* data, uint32_t len);
  void ReturnBuffer(const CodecBuffer* buffer);
  void SetOutputBuffers(std::vector<const CodecBuffer*> buffers);

  // TODO(afoxley), this should likely take some encoder specific info struct. The intention is to
  // support quality adjustments on the fly.
  void SetEncodeParams(fuchsia::media::FormatDetails format_details);

 private:
  void ShutDown();

  zx_status_t EnsureHwInited();
  zx_status_t PowerOn();
  void InterruptInit();
  zx_status_t BufferAlloc();
  zx_status_t CanvasInit();
  zx_status_t LoadFirmware();
  void Reset();
  // configures hcodec block with frame and pic count info.
  void Config();
  zx_status_t CanvasConfig(zx_handle_t vmo, uint32_t height, uint32_t bytes_per_row,
                           uint32_t offset, ScopedCanvasId* canvas_id_out, uint32_t alloc_flag);

zx_status_t CanvasInitReference(InternalBuffer* buf, ScopedCanvasId* y_canvas, ScopedCanvasId* uv_canvas, uint32_t* packed_canvas_ids);

  ddk::PDevProtocolClient pdev_;
  ddk::AmlogicCanvasProtocolClient canvas_;
  ddk::SysmemProtocolClient sysmem_;

  SocType soc_type_ = SocType::kUnknown;
  CbusRegisterIo cbus_;
  DosRegisterIo dosbus_;
  AoRegisterIo aobus_;
  HiuRegisterIo hiubus_;
  zx::bti bti_;
  zx::vmo firmware_vmo_;
  uintptr_t firmware_ptr_ = 0;
  uint64_t firmware_size_ = 0;

  EncoderStatus hw_status_;

  zx::interrupt interrupt_handle_;
  std::thread interrupt_thread_;
  sync_completion_t interrupt_sync_;
  fuchsia::sysmem::AllocatorSyncPtr sysmem_sync_ptr_;

  // working buffers for encoder
  std::optional<InternalBuffer> dec0_;
  std::optional<InternalBuffer> dec1_;
  std::optional<InternalBuffer> assit_;
  std::optional<InternalBuffer> scale_buff_;
  std::optional<InternalBuffer> dump_info_;
  std::optional<InternalBuffer> cbr_info_;

  async::Loop loop_;
  thrd_t loop_thread_;

  // FIDL interfaces
  std::unordered_map<LocalCodecFactory*, std::unique_ptr<LocalCodecFactory>> codec_factories_;
  std::unique_ptr<CodecAdmissionControl> codec_admission_control_;
  std::unique_ptr<CodecImpl> codec_instance_;

  // Picture info
  uint32_t encoder_width_;
  uint32_t encoder_height_;
  uint32_t rows_per_slice_;
  // reset to 0 for IDR
  uint32_t idr_pic_id_ = 0;
  // increment each frame
  uint32_t frame_number_ = 0;
  // reset to 0 for IDR and imcrement by 2 for NON-IDR
  uint32_t pic_order_cnt_lsb_ = 0;

  ScopedCanvasId input_y_canvas_;
  ScopedCanvasId input_uv_canvas_;
  ScopedCanvasId dec0_y_canvas_;
  ScopedCanvasId dec0_uv_canvas_;
  ScopedCanvasId dec1_y_canvas_;
  ScopedCanvasId dec1_uv_canvas_;
  uint32_t dblk_buf_canvas_;
  uint32_t ref_buf_canvas_;
};

#endif  // SRC_MEDIA_DRIVERS_AMLOGIC_ENCODER_DEVICE_CTX_H_
