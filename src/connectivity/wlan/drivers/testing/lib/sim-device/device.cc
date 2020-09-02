// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "device.h"

#include <stdio.h>
#include <zircon/errors.h>

#include <ddk/debug.h>

namespace wlan::simulation {

FakeDevMgr::FakeDevMgr() : fake_root_dev_id_(1) {
  // This is the fake root device, which is the parent of all first-level devices added to
  // FakeDevMgr.
  wlan_sim_dev_info_t fake_root = {
      // Parent of fake root device is nullptr(corresponding to device number 0)
      .parent = nullptr,
      .ref_count = 1,
  };

  devices_.insert({fake_root_dev_id_, fake_root});
}

FakeDevMgr::~FakeDevMgr() {
  auto root_iter = devices_.find(fake_root_dev_id_);
  if (root_iter == devices_.end()) {
    DBG_PRT("%s Fake root device is missing.\n" __func__);
    return;
  }

  auto iter = devices_.begin();
  while (iter != devices_.end()) {
    const auto next = std::next(iter);
    const auto& dev_info = iter->second;
    if (dev_info.parent == fake_root_dev_id_.as_device()) {
      if (DeviceUnreference(iter)) {
        root_iter->second.ref_count--;
      }
    }
    iter = next;
  }

  // The fake root device starts with a ref_count of 1; if it does not finish with a ref_count
  // of 1, we are leaking a child device.
  if (root_iter->second.ref_count != 1) {
    DBG_PRT("%s The root device ref count is: %d, it should be 1 when destructing FakeDevMgr.\n",
            __func__, root_iter->second.ref_count);
  }
  // The only remaining device in the device tree should be the fake root.  Any outstanding
  // child devices are leaks.
  if (devices_.size() != 1) {
    DBG_PRT("%s The number of devices is %d, only fake root device should be left at this point.\n",
            __func__, devices_.size());
  }
}

zx_status_t FakeDevMgr::DeviceAdd(zx_device_t* parent, device_add_args_t* args, zx_device_t** out) {
  // We use refcounting to maintain the fake device tree, and do not invoke the unbind hook.
  if (args && args->ops) {
    ZX_ASSERT(args->ops->unbind == nullptr);
  }
  wlan_sim_dev_info_t dev_info = {
      .parent = parent,
      .dev_args = (args == nullptr ? device_add_args_t{} : *args),
      .ref_count = 1,
  };
  DeviceId id(dev_counter_++);
  devices_.insert({id, dev_info});

  auto iter = devices_.find(DeviceId::FromDevice(parent));
  if (iter == devices_.end()) {
    DBG_PRT("%s Parent does not exist, might be root device.\n", __func__);
  } else {
    (iter->second).ref_count++;
  }

  if (out) {
    *out = id.as_device();
  }

  DBG_PRT("%s: Added SIM device. proto %d # devices: %lu Handle: %p\n", __func__,
          args ? args->proto_id : 0, devices_.size(), out ? *out : nullptr);
  return ZX_OK;
}

void FakeDevMgr::DeviceAsyncRemove(zx_device_t* device) {
  auto iter = devices_.find(DeviceId::FromDevice(device));
  if (iter == devices_.end()) {
    DBG_PRT("%s device %p does not exist\n", __func__, device);
    return;
  }

  while (true) {
    const auto parent_device = iter->second.parent;
    if (!DeviceUnreference(iter)) {
      // This device isn't being removed, so we don't need to unreference its parent.
      return;
    }
    iter = devices_.find(DeviceId::FromDevice(parent_device));
    if (iter == devices_.end()) {
      DBG_PRT("%s device %p does not exist, we might reach the root\n", __func__, device);
      return;
    }
  }
}

bool FakeDevMgr::DeviceUnreference(devices_t::iterator iter) {
  if (iter->second.ref_count == 0) {
    DBG_PRT("Error: device %s already has zero refcount\n", iter->second.dev_args.name);
    return false;
  }

  iter->second.ref_count--;
  if (iter->second.ref_count == 0) {
    const auto args = iter->second.dev_args;
    if (args.ops && args.ops->release) {
      (*args.ops->release)(args.ctx);
    }
    devices_.erase(iter);
    // Device released
    return true;
  }
  // Just decrease ref_count, not actually released
  return false;
}

std::optional<wlan_sim_dev_info_t> FakeDevMgr::FindFirst(const Predicate& pred) {
  auto iter = std::find_if(begin(), end(), [pred](auto& entry) -> bool {
    auto& [dev, dev_info] = entry;
    return pred(dev, dev_info);
  });
  if (iter == end()) {
    return {};
  }
  return {iter->second};
}

std::optional<wlan_sim_dev_info_t> FakeDevMgr::FindFirstByProtocolId(uint32_t proto_id) {
  return FindFirst(
      [proto_id](auto _dev, auto& dev_info) { return proto_id == dev_info.dev_args.proto_id; });
}

std::optional<DeviceId> FakeDevMgr::FindFirstDev(const Predicate& pred) {
  auto iter = std::find_if(begin(), end(), [pred](auto& entry) -> bool {
    auto& [dev, dev_info] = entry;
    return pred(dev, dev_info);
  });
  if (iter == end()) {
    return {};
  }
  return {iter->first};
}

std::optional<DeviceId> FakeDevMgr::FindFirstDevByProtocolId(uint32_t proto_id) {
  return FindFirstDev(
      [proto_id](auto _dev, auto& dev_info) { return proto_id == dev_info.dev_args.proto_id; });
}

std::optional<wlan_sim_dev_info_t> FakeDevMgr::GetDevice(zx_device_t* device) {
  return FindFirst([device](auto dev, auto& dev_info) { return device == dev.as_device(); });
}

// Return the number of devices other than fake root device.
size_t FakeDevMgr::DeviceCount() { return devices_.size() - 1; }

zx_device_t* FakeDevMgr::GetRootDevice() { return fake_root_dev_id_.as_device(); }

}  // namespace wlan::simulation
