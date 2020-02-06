// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/drivers/amlogic_encoder/device_ctx.h"

#include <zircon/assert.h>

#include <memory>

#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/mmio-buffer.h>
#include <ddk/platform-defs.h>
#include <ddk/protocol/composite.h>
#include <ddk/protocol/platform/device.h>

#include "src/media/drivers/amlogic_encoder/macros.h"

enum MmioRegion {
  kCbus,
  kDosbus,
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

  status = device_ctx->DdkAdd("amlogic_video_enc");

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

zx_status_t DeviceCtx::StartThread() { return loop_.StartThread("device-loop", &loop_thread_); }

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

  mmio_buffer_t mmio;
  status = pdev_map_mmio_buffer(&pdev_, kDosbus, ZX_CACHE_POLICY_UNCACHED_DEVICE, &mmio);
  if (status != ZX_OK) {
    ENCODE_ERROR("Failed map dosbus");
    return ZX_ERR_NO_MEMORY;
  }
  dosbus_ = std::make_unique<DosRegisterIo>(mmio);

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

  // TODO load firmware
  // TODO connect to sysmem

  status = StartThread();
  if (status != ZX_OK) {
    ENCODE_ERROR("could not start loop thread");
    return status;
  }

  return ZX_OK;
}

void DeviceCtx::GetCodecFactory(zx::channel request) {
  // drop
}
