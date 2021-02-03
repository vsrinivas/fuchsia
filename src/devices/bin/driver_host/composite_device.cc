// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/devices/bin/driver_host/composite_device.h"

#include <algorithm>

#include <fbl/auto_lock.h>
#include <fbl/mutex.h>

#include "src/devices/bin/driver_host/driver_host.h"
#include "src/devices/bin/driver_host/zx_device.h"

namespace {

class CompositeDeviceInstance {
 public:
  CompositeDeviceInstance(zx_device_t* zxdev, CompositeFragments&& fragments)
      : zxdev_(zxdev), fragments_(std::move(fragments)) {}

  static zx_status_t Create(fbl::RefPtr<zx_device> zxdev, CompositeFragments&& fragments,
                            std::unique_ptr<CompositeDeviceInstance>* device) {
    // Leak a reference to the zxdev here.  It will be cleaned up by the
    // device_unbind_reply() in Unbind().
    auto dev = std::make_unique<CompositeDeviceInstance>(fbl::ExportToRawPtr(&zxdev),
                                                         std::move(fragments));
    *device = std::move(dev);
    return ZX_OK;
  }

  uint32_t GetFragmentCount() { return static_cast<uint32_t>(fragments_.size()); }

  void GetFragments(composite_device_fragment_t* comp_list, size_t comp_count,
                    size_t* comp_actual) {
    size_t actual = std::min(comp_count, fragments_.size());
    for (size_t i = 0; i < actual; ++i) {
      strncpy(comp_list[i].name, fragments_[i].name.c_str(), 32);
      comp_list[i].device = fragments_[i].device.get();
    }
    *comp_actual = actual;
  }

  bool GetFragment(const char* name, zx_device_t** out) {
    for (auto& fragment : fragments_) {
      if (strncmp(name, fragment.name.c_str(), 32) == 0) {
        *out = fragment.device.get();
        return true;
      }
    }
    return false;
  }

  void Release() { delete this; }

  void Unbind() {
    for (auto& fragment : fragments_) {
      // Drop the reference to the composite device.
      fragment.device->take_composite();
    }
    fragments_.reset();
    device_unbind_reply(zxdev_);
  }

  const CompositeFragments& fragments() { return fragments_; }

 private:
  zx_device_t* zxdev_;
  CompositeFragments fragments_;
};

}  // namespace

// Get the placeholder driver structure for the composite driver
fbl::RefPtr<zx_driver> GetCompositeDriver(DriverHostContext* ctx) {
  static fbl::Mutex lock;
  static fbl::RefPtr<zx_driver> composite TA_GUARDED(lock);

  fbl::AutoLock guard(&lock);
  if (composite == nullptr) {
    zx_status_t status =
        zx_driver::Create("<internal:composite>", ctx->inspect().drivers(), &composite);
    if (status != ZX_OK) {
      return nullptr;
    }
    composite->set_name("internal:composite");
  }
  return composite;
}

zx_status_t InitializeCompositeDevice(const fbl::RefPtr<zx_device>& dev,
                                      CompositeFragments&& fragments) {
  static const zx_protocol_device_t composite_device_ops = []() {
    zx_protocol_device_t ops = {};
    ops.unbind = [](void* ctx) { static_cast<CompositeDeviceInstance*>(ctx)->Unbind(); };
    ops.release = [](void* ctx) { static_cast<CompositeDeviceInstance*>(ctx)->Release(); };
    return ops;
  }();

  auto composite = fbl::MakeRefCounted<CompositeDevice>(dev);

  std::unique_ptr<CompositeDeviceInstance> new_device;
  zx_status_t status = CompositeDeviceInstance::Create(dev, std::move(fragments), &new_device);
  if (status != ZX_OK) {
    return status;
  }

  for (auto& fragment : new_device->fragments()) {
    fragment.device->set_composite(composite);
  }

  dev->set_composite(composite, false);
  dev->set_protocol_id(ZX_PROTOCOL_COMPOSITE);
  dev->set_ops(&composite_device_ops);
  dev->ctx = new_device.release();
  // Flag that when this is cleaned up, we should run its release hook.
  dev->set_flag(DEV_FLAG_ADDED);
  return ZX_OK;
}

CompositeDevice::~CompositeDevice() = default;

uint32_t CompositeDevice::GetFragmentCount() {
  return static_cast<CompositeDeviceInstance*>(device_->ctx)->GetFragmentCount();
}

void CompositeDevice::GetFragments(composite_device_fragment_t* comp_list, size_t comp_count,
                                   size_t* comp_actual) {
  static_cast<CompositeDeviceInstance*>(device_->ctx)
      ->GetFragments(comp_list, comp_count, comp_actual);
}

bool CompositeDevice::GetFragment(const char* name, zx_device_t** out) {
  return static_cast<CompositeDeviceInstance*>(device_->ctx)->GetFragment(name, out);
}
