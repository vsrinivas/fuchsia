// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "composite_device.h"

#include <algorithm>

#include <ddk/protocol/composite.h>
#include <fbl/auto_lock.h>
#include <fbl/mutex.h>

#include "devhost.h"
#include "zx_device.h"

namespace {

class CompositeDeviceInstance {
 public:
  CompositeDeviceInstance(zx_device_t* zxdev, CompositeComponents&& components)
      : zxdev_(zxdev), components_(std::move(components)) {}

  static zx_status_t Create(fbl::RefPtr<zx_device> zxdev, CompositeComponents&& components,
                            std::unique_ptr<CompositeDeviceInstance>* device) {
    // Leak a reference to the zxdev here.  It will be cleaned up by the
    // device_remove() in Unbind().
    auto dev = std::make_unique<CompositeDeviceInstance>(fbl::ExportToRawPtr(&zxdev),
                                                         std::move(components));
    *device = std::move(dev);
    return ZX_OK;
  }

  uint32_t GetComponentCount() { return static_cast<uint32_t>(components_.size()); }

  void GetComponents(zx_device_t** comp_list, size_t comp_count, size_t* comp_actual) {
    size_t actual = std::min(comp_count, components_.size());
    for (size_t i = 0; i < actual; ++i) {
      comp_list[i] = components_[i].get();
    }
    *comp_actual = actual;
  }

  void Release() { delete this; }

  void Unbind() {
    for (auto& component : components_) {
      // Drop the reference to the composite device.
      component->take_composite();
    }
    components_.reset();
    device_remove_deprecated(zxdev_);
  }

  const CompositeComponents& components() { return components_; }

 private:
  zx_device_t* zxdev_;
  CompositeComponents components_;
};

// Get the placeholder driver structure for the composite driver
fbl::RefPtr<zx_driver> GetCompositeDriver() {
  static fbl::Mutex lock;
  static fbl::RefPtr<zx_driver> composite TA_GUARDED(lock);

  fbl::AutoLock guard(&lock);
  if (composite == nullptr) {
    zx_status_t status = zx_driver::Create(&composite);
    if (status != ZX_OK) {
      return nullptr;
    }
    composite->set_name("internal:composite");
    composite->set_libname("<internal:composite>");
  }
  return composite;
}

}  // namespace

zx_status_t InitializeCompositeDevice(const fbl::RefPtr<zx_device>& dev,
                                      CompositeComponents&& components) {
  static const zx_protocol_device_t composite_device_ops = []() {
    zx_protocol_device_t ops = {};
    ops.unbind = [](void* ctx) { static_cast<CompositeDeviceInstance*>(ctx)->Unbind(); };
    ops.release = [](void* ctx) { static_cast<CompositeDeviceInstance*>(ctx)->Release(); };
    return ops;
  }();
  static composite_protocol_ops_t composite_ops = []() {
    composite_protocol_ops_t ops = {};
    ops.get_component_count = [](void* ctx) {
      return static_cast<CompositeDeviceInstance*>(ctx)->GetComponentCount();
    };
    ops.get_components = [](void* ctx, zx_device_t** comp_list, size_t comp_count,
                            size_t* comp_actual) {
      static_cast<CompositeDeviceInstance*>(ctx)->GetComponents(comp_list, comp_count, comp_actual);
    };
    return ops;
  }();

  auto driver = GetCompositeDriver();
  if (driver == nullptr) {
    return ZX_ERR_INTERNAL;
  }

  auto composite = fbl::MakeRefCounted<CompositeDevice>(dev);

  std::unique_ptr<CompositeDeviceInstance> new_device;
  zx_status_t status = CompositeDeviceInstance::Create(dev, std::move(components), &new_device);
  if (status != ZX_OK) {
    return status;
  }

  for (auto& component : new_device->components()) {
    component->set_composite(composite);
  }

  dev->protocol_id = ZX_PROTOCOL_COMPOSITE;
  dev->protocol_ops = &composite_ops;
  dev->driver = driver.get();
  dev->ops = &composite_device_ops;
  dev->ctx = new_device.release();
  // Flag that when this is cleaned up, we should run its release hook.
  dev->flags |= DEV_FLAG_ADDED;
  return ZX_OK;
}

CompositeDevice::~CompositeDevice() = default;
