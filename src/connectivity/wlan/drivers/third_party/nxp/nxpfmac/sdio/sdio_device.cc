// Copyright (c) 2022 The Fuchsia Authors
//
// Permission to use, copy, modify, and/or distribute this software for any purpose with or without
// fee is hereby granted, provided that the above copyright notice and this permission notice
// appear in all copies.
//
// THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS
// SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE
// AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
// WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT,
// NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE
// OF THIS SOFTWARE.

#include "sdio_device.h"

#include <lib/async-loop/default.h>
#include <lib/zircon-internal/align.h>

#include <limits>
#include <string>

#include <ddktl/init-txn.h>

namespace wlan {
namespace nxpfmac {

SdioDevice::SdioDevice(zx_device_t* parent) : Device(parent) {}

SdioDevice::~SdioDevice() = default;

void SdioDevice::Shutdown() {
  if (async_loop_) {
    // Explicitly destroy the async loop before further shutdown to prevent asynchronous tasks
    // from using resources as they are being deallocated.
    async_loop_.reset();
  }
}

// static
zx_status_t SdioDevice::Create(zx_device_t* parent_device) {
  zx_status_t status = ZX_OK;

  auto async_loop = std::make_unique<async::Loop>(&kAsyncLoopConfigNoAttachToCurrentThread);
  if ((status = async_loop->StartThread("nxpfmac_sdio-worker", nullptr)) != ZX_OK) {
    return status;
  }

  std::unique_ptr<SdioDevice> device(new SdioDevice(parent_device));
  device->async_loop_ = std::move(async_loop);

  if ((status = device->DdkAdd(ddk::DeviceAddArgs("nxpfmac_sdio-wlanphy"))) != ZX_OK) {
    return status;
  }
  device.release();  // This now has its lifecycle managed by the devhost.

  // Further initialization is performed in the SdioDevice::Init() DDK hook, invoked by the devhost.
  return ZX_OK;
}

async_dispatcher_t* SdioDevice::GetDispatcher() { return async_loop_->dispatcher(); }

zx_status_t SdioDevice::Init() { return ZX_OK; }

zx_status_t SdioDevice::DeviceAdd(device_add_args_t* args, zx_device_t** out_device) {
  return device_add(parent(), args, out_device);
}

void SdioDevice::DeviceAsyncRemove(zx_device_t* dev) { device_async_remove(dev); }

zx_status_t SdioDevice::LoadFirmware(const char* path, zx_handle_t* fw, size_t* size) {
  return ZX_OK;
}

zx_status_t SdioDevice::DeviceGetMetadata(uint32_t type, void* buf, size_t buflen, size_t* actual) {
  return device_get_metadata(zxdev(), type, buf, buflen, actual);
}

}  // namespace nxpfmac
}  // namespace wlan
