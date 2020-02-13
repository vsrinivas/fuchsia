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

  status = device_ctx->Start();

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

zx_status_t DeviceCtx::Start() {
  auto status = loop_.StartThread("device-loop", &loop_thread_);
  if (status != ZX_OK) {
    ENCODE_ERROR("could not start loop thread");
    return status;
  }

  // add device, but not visible till after fw is loaded.
  status = DdkAdd("amlogic_video_enc", DEVICE_ADD_INVISIBLE);

  async::PostTask(loop_.dispatcher(), [this]() {
    LoadFirmware();
    DdkMakeVisible();
  });

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

  return ZX_OK;
}

zx_status_t DeviceCtx::LoadFirmware() { return ZX_ERR_NOT_SUPPORTED; }

// encoder control
zx_status_t DeviceCtx::StartEncoder() { return ZX_ERR_NOT_SUPPORTED; }
zx_status_t DeviceCtx::StopEncoder() { return ZX_ERR_NOT_SUPPORTED; }
zx_status_t DeviceCtx::WaitForIdle() { return ZX_ERR_NOT_SUPPORTED; }
zx_status_t DeviceCtx::EncodeFrame(const CodecBuffer* buffer, uint8_t* data, uint32_t len) {
  return ZX_ERR_NOT_SUPPORTED;
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
