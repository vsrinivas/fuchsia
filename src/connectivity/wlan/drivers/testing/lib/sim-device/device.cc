// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "device.h"

#include <stdio.h>
#include <zircon/errors.h>

#include <ddk/debug.h>

namespace wlan::simulation {

zx_status_t FakeDevMgr::DeviceAdd(zx_device_t* parent, device_add_args_t* args, zx_device_t** out) {
  {
    wlan_sim_dev_info_t dev_info = {
        .parent = parent,
        .dev_args = (args == nullptr ? device_add_args_t{} : *args),
    };
    DeviceId id(dev_counter_++);
    devices_.insert({id, dev_info});

    if (out) {
      *out = id.as_device();
    }

    DBG_PRT("%s: Added SIM device. proto %d # devices: %lu Handle: %p\n", __func__,
            args ? args->proto_id : 0, devices_.size(), out ? *out : nullptr);
    return ZX_OK;
  }
}

zx_status_t FakeDevMgr::DeviceRemove(zx_device_t* device) {
  //  if (!device) {
  //    return ZX_ERR_INVALID_ARGS;
  //  }
  auto iter = devices_.find(DeviceId::FromDevice(device));
  if (iter == devices_.end()) {
    DBG_PRT("%s device %p does not exist\n", __func__, device);
    return ZX_ERR_NOT_FOUND;
  }
  devices_.erase(iter);
  DBG_PRT("%s: Removed SIM device %p. # devices: %lu\n", __func__, device, devices_.size());
  return ZX_OK;
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

std::optional<wlan_sim_dev_info_t> FakeDevMgr::GetDevice(zx_device_t* device) {
  return FindFirst([device](auto dev, auto& dev_info) { return device == dev.as_device(); });
}

size_t FakeDevMgr::DevicesCount() { return devices_.size(); }

}  // namespace wlan::simulation
