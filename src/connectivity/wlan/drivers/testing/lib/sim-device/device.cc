// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "device.h"

#include <lib/ddk/debug.h>
#include <stdio.h>
#include <zircon/assert.h>
#include <zircon/errors.h>

#include <vector>

namespace wlan::simulation {

enum class FakeDevMgr::DdkCallState {
  kInvalid = 0,
  kIdle = 1,
  kCallPending = 2,
  kReplyPending = 3,
  kComplete = 4,
};

FakeDevMgr::FakeDevMgr()
    : fake_root_dev_id_(1),
      init_state_(DdkCallState::kIdle),
      unbind_state_(DdkCallState::kIdle),
      init_result_(ZX_OK) {
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
    DBG_PRT("%s Fake root device is missing.\n", __func__);
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
    DBG_PRT(
        "%s The number of devices is %zu, only fake root device should be left at this point.\n",
        __func__, devices_.size());
  }
}

void FakeDevMgr::DeviceInitReply(zx_device_t* device, zx_status_t status,
                                 const device_init_reply_args_t* args) {
  // FakeDevMgr relies on synchronous completion of the init() reply.
  ZX_DEBUG_ASSERT(std::this_thread::get_id() == init_thread_id_);
  ZX_DEBUG_ASSERT(init_state_ == DdkCallState::kReplyPending);
  init_state_ = DdkCallState::kComplete;
  init_result_ = status;
}

void FakeDevMgr::DeviceUnbindReply(zx_device_t* device) {
  // FakeDevMgr relies on synchronous completion of the unbind() reply.
  ZX_DEBUG_ASSERT(std::this_thread::get_id() == unbind_thread_id_);
  ZX_DEBUG_ASSERT(unbind_state_ == DdkCallState::kReplyPending);
  unbind_state_ = DdkCallState::kComplete;
}

zx_status_t FakeDevMgr::DeviceAdd(zx_device_t* parent, device_add_args_t* args, zx_device_t** out) {
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

  // Set the device in *out, since the synchronous call to init() may rely on it
  // being set.
  zx_device_t* const old_out = *out;
  *out = id.as_device();

  DBG_PRT("%s: Added SIM device. proto %d # devices: %lu Handle: %p\n", __func__,
          args ? args->proto_id : 0, devices_.size(), out ? *out : nullptr);

  // TODO(fxbug.dev/76420) - Add async support for Init()
  if (args && args->ops && args->ops->init) {
    init_thread_id_ = std::this_thread::get_id();
    ZX_DEBUG_ASSERT(init_state_ == DdkCallState::kIdle);
    init_state_ = DdkCallState::kReplyPending;
    args->ops->init(args->ctx);

    // The init reply should have arrived synchronously.
    ZX_ASSERT(init_state_ == DdkCallState::kComplete);

    // If the init failed, or an unbind was requested during the init call, do it now.
    if (init_result_ != ZX_OK || unbind_state_ == DdkCallState::kCallPending) {
      DeviceUnbind(id.as_device());
    }
  }
  init_state_ = DdkCallState::kIdle;

  // If the init reply was a failure, we restore the old value of the argument.
  if (init_result_ != ZX_OK && out) {
    *out = old_out;
  }

  // In the actual device manager, the call to the init hook is scheduled for after the device_add()
  // call is complete.  So the status we return is the status of the device_add() operation, without
  // any consideration as to what the init reply returned.
  return ZX_OK;
}

void FakeDevMgr::DeviceUnbind(zx_device_t* device) {
  auto iter = devices_.find(DeviceId::FromDevice(device));
  if (iter == devices_.end()) {
    DBG_PRT("%s device %p does not exist\n", __func__, device);
    return;
  }

  auto args = iter->second.dev_args;
  if (args.ops && args.ops->unbind) {
    unbind_thread_id_ = std::this_thread::get_id();
    unbind_state_ = DdkCallState::kReplyPending;
    (*args.ops->unbind)(args.ctx);

    // The unbind reply should have arrived synchronously.
    ZX_ASSERT(unbind_state_ == DdkCallState::kComplete);
  }
  unbind_state_ = DdkCallState::kIdle;

  // Invoke unbind on a list of child devices.
  {
    std::vector<zx_device_t*> children;
    for (const auto& [key, value] : devices_) {
      if (value.parent == device) {
        children.emplace_back(key.as_device());
      }
    }
    for (auto child : children) {
      DeviceUnbind(child);
    }
  }

  // Now that all children have been unbound, we can unreference this device.
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

void FakeDevMgr::DeviceAsyncRemove(zx_device_t* device) {
  auto iter = devices_.find(DeviceId::FromDevice(device));
  if (iter == devices_.end()) {
    DBG_PRT("%s device %p does not exist\n", __func__, device);
    return;
  }

  // If we're in an init call, wait until it is complete to start unbinding.
  if (init_state_ == DdkCallState::kReplyPending) {
    unbind_state_ = DdkCallState::kCallPending;
    return;
  }

  // TODO(fxbug.dev/76420) - Add async support for DeviceUnbind()
  DeviceUnbind(device);
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
