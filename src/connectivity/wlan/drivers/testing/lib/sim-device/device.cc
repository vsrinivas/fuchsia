// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "device.h"

#include <stdio.h>
#include <zircon/errors.h>

#include <list>

#include <ddk/debug.h>

namespace wlan {
namespace simulation {

FakeDevMgr::FakeDevMgr() {}

// In the destructor, empty the device list
FakeDevMgr::~FakeDevMgr() {
  wlan_sim_dev_info_t* dev;

  DBG_PRT("DevMgr destructor called. Num entries in list: %lu\n", device_list_.size());
  std::list<wlan_sim_dev_info_t*>::iterator dev_list_itr;

  while (device_list_.size()) {
    dev_list_itr = device_list_.begin();
    dev = *dev_list_itr;
    device_list_.remove(dev);
    free(dev);
    DBG_PRT("%s: Removed SIM device %p. # devices: %lu\n", __func__, dev, device_list_.size());
  }
}
// WLAN SIM device add. Memory is allocated for device args and copied before adding it
// into the list. The allocated memory pointer is returned in out for use as device
// context
zx_status_t FakeDevMgr::wlan_sim_device_add(zx_device_t* parent, device_add_args_t* args,
                                            zx_device_t** out) {
  wlan_sim_dev_info_t* dev_info;

  // Allocated memory and copy the args
  dev_info = (wlan_sim_dev_info_t*)std::calloc(1, sizeof(wlan_sim_dev_info_t));
  if (!dev_info) {
    DBG_PRT("%s No memory for device add\n", __func__);
    return ZX_ERR_NO_RESOURCES;
  }
  if (args) {
    dev_info->dev_args = *args;
  }
  dev_info->parent = parent;

  // Add it to the end of the list
  device_list_.emplace_back(dev_info);

  if (out) {
    *out = (zx_device_t*)dev_info;
  }

  DBG_PRT("%s: Added SIM device. # devices: %lu Handle: %p\n", __func__, device_list_.size(),
          dev_info);
  return ZX_OK;
}

// WLAN SIM device remove. If the device is found it is removed from the list
// and allocated memory is freed.
zx_status_t FakeDevMgr::wlan_sim_device_remove(zx_device_t* device) {
  wlan_sim_dev_info_t* dev_info;
  // Use a local iterator to avoid messing the global one
  std::list<wlan_sim_dev_info_t*>::iterator dev_list_itr;

  for (dev_list_itr = device_list_.begin(); dev_list_itr != device_list_.end(); dev_list_itr++) {
    dev_info = *dev_list_itr;
    if (device == (zx_device_t*)dev_info) {
      // Found device
      device_list_.remove((wlan_sim_dev_info_t*)device);
      free(device);
      DBG_PRT("%s: Removed SIM device %p. # devices: %lu\n", __func__, device, device_list_.size());
      return ZX_OK;
    }
  }
  DBG_PRT("%s device %p does not exist\n", __func__, device);
  return ZX_ERR_INVALID_ARGS;
}

// WLAN_SIM get the first device on the list
zx_device_t* FakeDevMgr::wlan_sim_device_get_first(zx_device_t** parent, device_add_args_t** args) {
  wlan_sim_dev_info_t* dev_info;

  // If there is at least one entry in the list, start the iteration
  // and return the first entry in the list. Note that the iterator gets
  // initialized here. If the list is empty, return nullptr.
  dev_list_itr_ = device_list_.begin();
  if (dev_list_itr_ != device_list_.end()) {
    dev_info = *dev_list_itr_;
    if (parent) {
      *parent = dev_info->parent;
    }
    if (args) {
      *args = &dev_info->dev_args;
    }
    return ((zx_device_t*)dev_info);
  } else {
    return nullptr;
  }
}

// WLAN_SIM get the next device on the list. Note that the function
// wlan_sim_device_get_first() has to be called once before calling
// this function. Otherwise results are unpredictable.
zx_device_t* FakeDevMgr::wlan_sim_device_get_next(zx_device_t** parent, device_add_args_t** args) {
  wlan_sim_dev_info_t* dev_info;

  // If there is at least one entry in the list (and assuming
  // wlan_sim_device_get_first() was called earlier, advance
  // the iterator. If the iterator has not reached the end
  // of the list, return the next entry else return nullptr.

  dev_list_itr_++;
  if (dev_list_itr_ != device_list_.end()) {
    dev_info = *dev_list_itr_;
    if (parent) {
      *parent = dev_info->parent;
    }
    if (args) {
      *args = &dev_info->dev_args;
    }
    return ((zx_device_t*)dev_info);
  } else {
    return nullptr;
  }
}

// WLAN_SIM returns the first device which uses the same protocol as specified via `proto_id`.
std::optional<wlan_sim_dev_info_t> FakeDevMgr::find_device_by_proto_id(uint32_t proto_id) {
  auto iface =
      std::find_if(begin(), end(), [proto_id](auto e) { return e->dev_args.proto_id == proto_id; });
  if (iface == end()) {
    return {};
  }
  return {*(*iface)};
}

// WLAN_SIM get the num of devices in the list
size_t FakeDevMgr::wlan_sim_device_get_num_devices() { return device_list_.size(); }
}  // namespace simulation
}  // namespace wlan
