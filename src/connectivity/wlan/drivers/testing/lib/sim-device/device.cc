// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <list>
#include <stdio.h>
#include <ddk/debug.h>
#include <zircon/errors.h>
#include "device.h"

typedef struct wlan_sim_dev_info {
  zx_device* parent;
  device_add_args_t dev_args;
} wlan_sim_dev_info_t;

std::list<wlan_sim_dev_info_t*> device_list;
std::list<wlan_sim_dev_info_t*>::iterator dev_list_itr;

// WLAN SIM device add. Memory is allocated for device args and copied before adding it
// into the list. The allocated memory pointer is returned in out for use as device
// context
zx_status_t wlan_sim_device_add(zx_device_t* parent, device_add_args_t* args, zx_device_t** out) {
  wlan_sim_dev_info_t* dev_info;

  // Allocated memory and copy the args
  dev_info = (wlan_sim_dev_info_t*)std::calloc(1, sizeof(wlan_sim_dev_info_t));
  if (!dev_info) {
    printf("%s No memory for device add\n", __func__);
    return ZX_ERR_NO_RESOURCES;
  }
  if (args) {
    dev_info->dev_args = *args;
  }
  dev_info->parent = parent;

  // Add it to the end of the list
  device_list.emplace_back(dev_info);

  if (out) {
    *out = (zx_device_t*)dev_info;
  }

  printf("%s: Added SIM device. # devices: %lu Handle: %p\n", __func__, device_list.size(),
         dev_info);
  return ZX_OK;
}

// WLAN SIM device remove. If the device is found it is removed from the list
// and allocated memory is freed.
zx_status_t wlan_sim_device_remove(zx_device_t* device) {
  wlan_sim_dev_info_t* dev_info;
  // Use a local iterator to avoid messing the global one
  std::list<wlan_sim_dev_info_t*>::iterator dev_list_itr;

  for (dev_list_itr = device_list.begin(); dev_list_itr != device_list.end(); dev_list_itr++) {
    dev_info = *dev_list_itr;
    if (device == (zx_device_t*)dev_info) {
      // Found device
      device_list.remove((wlan_sim_dev_info_t*)device);
      free(device);
      printf("%s: Removed SIM device %p. # devices: %lu\n", __func__, device, device_list.size());
      return ZX_OK;
    }
  }
  printf("%s device %p does not exist\n", __func__, device);
  return ZX_ERR_INVALID_ARGS;
}

// WLAN_SIM get the first device on the list
zx_device_t* wlan_sim_device_get_first(zx_device_t** parent, device_add_args_t** args) {
  wlan_sim_dev_info_t* dev_info;

  // If there is at least one entry in the list, start the iteration
  // and return the first entry in the list. Note that the iterator gets
  // initialized here. If the list is empty, return nullptr.
  dev_list_itr = device_list.begin();
  if (dev_list_itr != device_list.end()) {
    dev_info = *dev_list_itr;
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
zx_device_t* wlan_sim_device_get_next(zx_device_t** parent, device_add_args_t** args) {
  wlan_sim_dev_info_t* dev_info;

  // If there is at least one entry in the list (and assuming
  // wlan_sim_device_get_first() was called earlier, advance
  // the iterator. If the iterator has not reached the end
  // of the list, return the next entry else return nullptr.

  dev_list_itr++;
  if (dev_list_itr != device_list.end()) {
    dev_info = *dev_list_itr;
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

// WLAN_SIM get the num of devices in the list
size_t wlan_sim_device_get_num_devices() { return device_list.size(); }
