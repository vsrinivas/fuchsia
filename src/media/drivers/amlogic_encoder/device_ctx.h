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

#include <ddk/driver.h>
#include <ddk/platform-defs.h>
#include <ddk/protocol/amlogiccanvas.h>
#include <ddk/protocol/platform/device.h>
#include <ddk/protocol/sysmem.h>
#include <ddktl/device.h>
#include <ddktl/protocol/empty-protocol.h>

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
      : DeviceType(parent), loop_(&kAsyncLoopConfigNoAttachToCurrentThread) {}

  DeviceCtx(const DeviceCtx&) = delete;
  DeviceCtx& operator=(const DeviceCtx&) = delete;

  ~DeviceCtx() { ShutDown(); }

  void GetCodecFactory(zx::channel request);

  // Methods required by the ddk.
  void DdkRelease();
  void DdkUnbindNew(ddk::UnbindTxn txn);
  zx_status_t DdkMessage(fidl_msg_t* msg, fidl_txn_t* txn);

 private:
  zx_status_t StartThread();
  void ShutDown();
  zx_status_t Init();
  zx_status_t AddDevice();

  pdev_protocol_t pdev_;
  amlogic_canvas_protocol_t canvas_;
  sysmem_protocol_t sysmem_;

  SocType soc_type_ = SocType::kUnknown;
  std::unique_ptr<CbusRegisterIo> cbus_;
  std::unique_ptr<DosRegisterIo> dosbus_;
  zx::bti bti_;

  zx::interrupt enc_interrupt_handle_;
  std::thread enc_interrupt_thread_;

  async::Loop loop_;
  thrd_t loop_thread_;
};

#endif  // SRC_MEDIA_DRIVERS_AMLOGIC_ENCODER_DEVICE_CTX_H_
