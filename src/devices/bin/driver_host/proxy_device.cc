// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/devices/bin/driver_host/proxy_device.h"

#include <fbl/auto_lock.h>
#include <fbl/mutex.h>

#include "lib/fdio/directory.h"
#include "lib/zx/status.h"
#include "src/devices/bin/driver_host/driver_host.h"

namespace {

class ProxyDeviceInstance {
 public:
  ProxyDeviceInstance(zx_device_t* zxdev, fidl::ClientEnd<fuchsia_io::Directory> incoming_dir)
      : zxdev_(zxdev), incoming_dir_(std::move(incoming_dir)) {}

  static std::unique_ptr<ProxyDeviceInstance> Create(
      fbl::RefPtr<zx_device> zxdev, fidl::ClientEnd<fuchsia_io::Directory> incoming_dir) {
    // Leak a reference to the zxdev here.  It will be cleaned up by the
    // device_unbind_reply() in Unbind().
    return std::make_unique<ProxyDeviceInstance>(fbl::ExportToRawPtr(&zxdev),
                                                 std::move(incoming_dir));
  }

  zx::status<> ConnectToProtocol(const char* protocol, zx::channel request) {
    fbl::StringBuffer<fuchsia_io::wire::kMaxPath> path;
    path.Append("svc/");
    path.Append(protocol);
    return zx::make_status(
        fdio_service_connect_at(incoming_dir_.channel().get(), path.c_str(), request.release()));
  }

  void Release() { delete this; }

  void Unbind() { device_unbind_reply(zxdev_); }

 private:
  zx_device_t* zxdev_;
  fidl::ClientEnd<fuchsia_io::Directory> incoming_dir_;
};

}  // namespace

void InitializeProxyDevice(const fbl::RefPtr<zx_device>& dev,
                           fidl::ClientEnd<fuchsia_io::Directory> incoming_dir) {
  static const zx_protocol_device_t proxy_device_ops = []() {
    zx_protocol_device_t ops = {};
    ops.unbind = [](void* ctx) { static_cast<ProxyDeviceInstance*>(ctx)->Unbind(); };
    ops.release = [](void* ctx) { static_cast<ProxyDeviceInstance*>(ctx)->Release(); };
    return ops;
  }();

  auto proxy = fbl::MakeRefCounted<ProxyDevice>(dev);

  auto new_device = ProxyDeviceInstance::Create(dev, std::move(incoming_dir));

  dev->set_proxy(proxy);
  dev->set_ops(&proxy_device_ops);
  dev->ctx = new_device.release();
  // Flag that when this is cleaned up, we should run its release hook.
  dev->set_flag(DEV_FLAG_ADDED);
}

fbl::RefPtr<zx_driver> GetProxyDriver(DriverHostContext* ctx) {
  static fbl::Mutex lock;
  static fbl::RefPtr<zx_driver> proxy TA_GUARDED(lock);

  fbl::AutoLock guard(&lock);
  if (proxy == nullptr) {
    auto status = zx_driver::Create("<internal:proxy>", ctx->inspect().drivers(), &proxy);
    if (status != ZX_OK) {
      return nullptr;
    }
    proxy->set_name("internal:proxy");
  }
  return proxy;
}

zx::status<> ProxyDevice::ConnectToProtocol(const char* protocol, zx::channel request) {
  return static_cast<ProxyDeviceInstance*>(device_->ctx)
      ->ConnectToProtocol(protocol, std::move(request));
}
