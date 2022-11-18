// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "device.h"

#include <lib/ddk/debug.h>
#include <stdio.h>
#include <zircon/assert.h>
#include <zircon/errors.h>

#include <algorithm>
#include <iterator>
#include <vector>

namespace wlan::simulation {

DeviceAddArgs::DeviceAddArgs(const DeviceAddArgs& args) { *this = args; }

DeviceAddArgs& DeviceAddArgs::operator=(const DeviceAddArgs& other) {
  CopyRawDeviceAddArgs(other.Args());
  return *this;
}

DeviceAddArgs::DeviceAddArgs(const device_add_args_t& args) { CopyRawDeviceAddArgs(args); }

DeviceAddArgs& DeviceAddArgs::operator=(const device_add_args_t& other) {
  CopyRawDeviceAddArgs(other);
  return *this;
}

void DeviceAddArgs::CopyRawDeviceAddArgs(const device_add_args_t& args) {
  // Copy the known and safe parts of device_add_args_t to our copy of device_add_args_t and clear
  // out the rest.
  args_ = {
      .version = args.version,
      .ctx = args.ctx,
      .proto_id = args.proto_id,
      // Even though this is a plain void* it should be safe to copy. The DDK does the same thing,
      // expecting the lifetime of the ops to outlast the device's lifetime.
      .proto_ops = args.proto_ops,
      .flags = args.flags,
  };

  // Update our local copies.
  name_.clear();
  if (args.name) {
    name_ = args.name;
  }
  ops_.reset();
  if (args.ops) {
    ops_ = *args.ops;
  }
  power_states_.clear();
  if (args.power_states) {
    power_states_.assign(args.power_states, args.power_states + args.power_state_count);
  }
  performance_states_.clear();
  if (args.performance_states) {
    performance_states_.assign(args.performance_states,
                               args.performance_states + args.performance_state_count);
  }
  fidl_protocol_offer_strings_.clear();
  if (args.fidl_protocol_offers) {
    fidl_protocol_offer_strings_.assign(args.fidl_protocol_offers,
                                        args.fidl_protocol_offers + args.fidl_protocol_offer_count);
  }
  proxy_args_.clear();
  if (args.proxy_args) {
    proxy_args_ = args.proxy_args;
  }

  // Update the raw pointers in our device_add_args_t to point to our locally copied data.
  SetRawPointers();
}

void DeviceAddArgs::SetRawPointers() {
  // Some of the information in device_add_args_t must point to our local copies
  args_.name = name_.empty() ? nullptr : name_.c_str();
  args_.ops = ops_.has_value() ? &ops_.value() : nullptr;

  args_.power_states = power_states_.empty() ? nullptr : power_states_.data();
  ZX_ASSERT(power_states_.size() <= std::numeric_limits<decltype(args_.power_state_count)>::max());
  args_.power_state_count = static_cast<uint8_t>(power_states_.size());

  args_.performance_states = performance_states_.empty() ? nullptr : performance_states_.data();
  ZX_ASSERT(performance_states_.size() <=
            std::numeric_limits<decltype(args_.performance_state_count)>::max());
  args_.performance_state_count = static_cast<uint8_t>(performance_states_.size());

  // This needs to be a two step process, we need to store the strings as std::strings to manage
  // the memory and then we need to have an array of char* that fidl_protocol_offers can point to.
  fidl_protocol_offers_.clear();
  std::transform(fidl_protocol_offer_strings_.begin(), fidl_protocol_offer_strings_.end(),
                 std::back_inserter(fidl_protocol_offers_),
                 [](const std::string& str) { return str.c_str(); });
  args_.fidl_protocol_offers =
      fidl_protocol_offers_.empty() ? nullptr : fidl_protocol_offers_.data();
  args_.fidl_protocol_offer_count = static_cast<uint8_t>(fidl_protocol_offers_.size());

  args_.proxy_args = proxy_args_.empty() ? nullptr : proxy_args_.c_str();
}

FakeDevice::FakeDevice(uint64_t id, zx_device_t* parent, device_add_args_t dev_args,
                       wlan::simulation::FakeDevMgr* dev_mgr)
    : id_(id), parent_(parent), dev_args_(dev_args), ref_count_(1), dev_mgr_(dev_mgr) {
  ZX_ASSERT(dev_mgr_ != nullptr);
}

bool FakeDevice::IsRootParent() const { return dev_mgr_->GetRootDevice() == this; }

zx_status_t FakeDevice::AddChild(device_add_args_t* args, zx_device_t** out) {
  return dev_mgr_->DeviceAdd(static_cast<zx_device_t*>(this), args, out);
}

void FakeDevice::AsyncRemove() { dev_mgr_->DeviceAsyncRemove(static_cast<zx_device_t*>(this)); }

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
  devices_.emplace_back(
      std::make_unique<zx_device_t>(fake_root_dev_id_, nullptr, device_add_args_t{}, this));
}

FakeDevMgr::~FakeDevMgr() {
  zx_device_t* root = GetRootDevice();
  if (!root) {
    DBG_PRT("%s Fake root device is missing.\n", __func__);
    return;
  }

  DeviceUnbind(root);
  ZX_ASSERT(devices_.empty());
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
  uint64_t id = dev_counter_++;
  ZX_ASSERT(ContainsDevice(id) == false);

  device_add_args_t dev_args = (args == nullptr ? device_add_args_t{} : *args);
  zx_device_t* device =
      devices_.emplace_back(std::make_unique<zx_device_t>(id, parent, dev_args, this)).get();

  if (ContainsDevice(parent)) {
    parent->AddRef();
  } else {
    DBG_PRT("%s Parent does not exist, might be root device.\n", __func__);
  }

  // Set the device in *out, since the synchronous call to init() may rely on it
  // being set.
  zx_device_t* const old_out = *out;
  *out = device;

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
      DeviceUnbind(device);
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
  if (!ContainsDevice(device)) {
    DBG_PRT("%s device %p does not exist\n", __func__, device);
    return;
  }

  auto args = device->DevArgs();
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
    std::vector<zx_device_t*> children = GetChildren(device);
    for (auto child : children) {
      DeviceUnbind(child);
    }
  }
  ZX_ASSERT(GetChildren(device).empty());

  // Now that all children have been unbound, we can unreference this device.
  while (true) {
    zx_device_t* parent_device = device->Parent();
    // Only unreference the device here. For the initial device there are no children anyway and
    // once we start moving up the parent chain we don't want to unreference other children of the
    // parents.
    if (!DeviceUnreference(device)) {
      // This device isn't being removed, so we don't need to unreference its parent.
      return;
    }
    device = parent_device;
    if (!ContainsDevice(device)) {
      DBG_PRT("%s device %p does not exist, we might reach the root\n", __func__, device);
      return;
    }
  }
}

std::vector<zx_device_t*> FakeDevMgr::GetChildren(zx_device_t* device) {
  std::vector<zx_device_t*> children;
  for (const auto& dev : devices_) {
    if (dev->Parent() == device) {
      children.emplace_back(dev.get());
    }
  }
  return children;
}

void FakeDevMgr::DeviceAsyncRemove(zx_device_t* device) {
  if (!ContainsDevice(device)) {
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

bool FakeDevMgr::DeviceUnreference(zx_device_t* device) {
  if (device->RefCount() == 0) {
    DBG_PRT("Error: device %s already has zero refcount\n", device->DevArgs().name);
    return false;
  }

  device->Release();
  if (device->RefCount() == 0) {
    const auto args = device->DevArgs();
    for (size_t i = 0; i < devices_.size(); i++) {
      if (devices_[i].get() == device) {
        auto dev = std::move(devices_[i]);
        // Remove element from the vector before calling `args.ops->release`. Otherwise tests
        // would sporadically fail because they check whether the device still exists before
        // FakeDevMgr removes them from `devices_`
        devices_.erase(devices_.begin() + i);
        if (args.ops && args.ops->release) {
          (*args.ops->release)(args.ctx);
        }
        // Device released
        return true;
      }
    }
  }
  // Just decrease ref_count, not actually released
  return false;
}

zx_device_t* FakeDevMgr::FindFirst(const Predicate& pred) const {
  auto iter = std::find_if(devices_.begin(), devices_.end(),
                           [pred](auto& device) -> bool { return pred(device.get()); });
  if (iter == devices_.end()) {
    return nullptr;
  }
  return iter->get();
}

zx_device_t* FakeDevMgr::FindFirstByProtocolId(uint32_t proto_id) const {
  return FindFirst([proto_id](zx_device_t* dev) { return proto_id == dev->DevArgs().proto_id; });
}

zx_device_t* FakeDevMgr::FindLatest(const Predicate& pred) {
  auto iter = std::find_if(devices_.rbegin(), devices_.rend(),
                           [pred](auto& device) -> bool { return pred(device.get()); });
  if (iter == devices_.rend()) {
    return nullptr;
  }
  return iter->get();
}

zx_device_t* FakeDevMgr::FindLatestByProtocolId(uint32_t proto_id) {
  return FindLatest([proto_id](zx_device_t* dev) { return proto_id == dev->DevArgs().proto_id; });
}

bool FakeDevMgr::ContainsDevice(zx_device_t* device) const {
  return FindFirst([device](zx_device_t* dev) { return device == dev; }) != nullptr;
}

bool FakeDevMgr::ContainsDevice(uint64_t id) const {
  return FindFirst([id](zx_device_t* dev) { return dev->Id() == id; }) != nullptr;
}

// Return the number of devices other than fake root device.
size_t FakeDevMgr::DeviceCount() { return devices_.size() - 1; }

size_t FakeDevMgr::DeviceCountByProtocolId(uint32_t proto_id) {
  return std::count_if(devices_.begin(), devices_.end(),
                       [proto_id](auto& device) { return device->DevArgs().proto_id == proto_id; });
}

zx_device_t* FakeDevMgr::GetRootDevice() {
  ZX_ASSERT(devices_.front()->Id() == fake_root_dev_id_);
  return devices_.front().get();
}

}  // namespace wlan::simulation

// Provide our own implementation of this function in the global namespace. This will ensure that
// calls to device_add and DdkAdd ends up calling the FakeDevMgr associated with |parent|.
__EXPORT
zx_status_t device_add_from_driver(zx_driver_t* drv, zx_device_t* parent, device_add_args_t* args,
                                   zx_device_t** out) {
  return parent->AddChild(args, out);
}

// Provide our own implementation of this function in the global namespace. This will ensure that
// device_async_remove and DdkAsyncRemove ends up calling the FakeDevMgr associated with |device|.
__EXPORT
void device_async_remove(zx_device_t* device) {
  if (!device) {
    zxlogf(ERROR, "Attempting to remove null device");
    return;
  }
  if (device->IsRootParent()) {
    zxlogf(ERROR, "Attempting to remove root parent");
    return;
  }
  device->AsyncRemove();
}

__EXPORT
__WEAK zx_driver_rec __zircon_driver_rec__ = {
    .ops = {},
    .driver = {},
};
