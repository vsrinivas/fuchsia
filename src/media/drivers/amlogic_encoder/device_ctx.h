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
#include <zircon/fidl.h>

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

#include "fuchsia/media/cpp/fidl.h"
#include "src/media/drivers/amlogic_encoder/local_codec_factory.h"
#include "src/media/drivers/amlogic_encoder/registers.h"

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
  static std::pair<zx_status_t, std::unique_ptr<DeviceCtx>> Bind(zx_device_t* parent);

  explicit DeviceCtx(zx_device_t* parent)
      : DeviceType(parent),
        loop_(&kAsyncLoopConfigNoAttachToCurrentThread),
        codec_admission_control_(std::make_unique<CodecAdmissionControl>(loop_.dispatcher())) {}

  DeviceCtx(const DeviceCtx&) = delete;
  DeviceCtx& operator=(const DeviceCtx&) = delete;

  ~DeviceCtx() { ShutDown(); }

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
  zx_status_t InitEncoder(const fuchsia::media::FormatDetails& initial_format_details);
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
  zx_status_t Init();
  void ShutDown();
  zx_status_t AddDevice();

  zx_status_t EnsureHwInited();
  zx_status_t PowerOn();
  zx_status_t BufferInit();
  zx_status_t CanvasInit();
  zx_status_t LoadFirmware();
  void Reset();
  // configures hcodec block with frame and pic count info.
  void Config();

  pdev_protocol_t pdev_;
  amlogic_canvas_protocol_t canvas_;
  sysmem_protocol_t sysmem_;

  SocType soc_type_ = SocType::kUnknown;
  std::unique_ptr<CbusRegisterIo> cbus_;
  std::unique_ptr<DosRegisterIo> dosbus_;
  std::unique_ptr<AoRegisterIo> aobus_;
  std::unique_ptr<HiuRegisterIo> hiubus_;
  zx::bti bti_;
  zx::vmo firmware_vmo_;
  uintptr_t firmware_ptr_ = 0;
  uint64_t firmware_size_ = 0;

  zx::interrupt enc_interrupt_handle_;
  std::thread enc_interrupt_thread_;

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
};

#endif  // SRC_MEDIA_DRIVERS_AMLOGIC_ENCODER_DEVICE_CTX_H_
