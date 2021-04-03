// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "zx_device.h"

#include <stdio.h>

#include <fbl/auto_lock.h>
#include <fbl/mutex.h>

#include "composite_device.h"
#include "driver_host.h"
#include "log.h"

zx_device::zx_device(DriverHostContext* ctx, std::string name, zx_driver_t* drv)
    : driver(drv), driver_host_context_(ctx) {
  size_t len = name.length();
  // TODO(teisenbe): I think this is overly aggressive, and could be changed
  // to |len > ZX_DEVICE_NAME_MAX| and |len = ZX_DEVICE_NAME_MAX|.
  if (len >= ZX_DEVICE_NAME_MAX) {
    LOGF(WARNING, "Name too large for device %p: %s", this, name.c_str());
    len = ZX_DEVICE_NAME_MAX - 1;
    magic = 0;
  }

  memcpy(name_, name.data(), len);
  name_[len] = '\0';

  inspect_.emplace(driver->inspect().devices(), name_);
}

zx_status_t zx_device::Create(DriverHostContext* ctx, std::string name, zx_driver_t* driver,
                              fbl::RefPtr<zx_device>* out_dev) {
  *out_dev = fbl::AdoptRef(new zx_device(ctx, name, driver));
  (*out_dev)->vnode = fbl::MakeRefCounted<DevfsVnode>(*out_dev);
  return ZX_OK;
}

void zx_device::set_bind_conn(fit::callback<void(zx_status_t)> conn) {
  fbl::AutoLock<fbl::Mutex> lock(&bind_conn_lock_);
  bind_conn_ = std::move(conn);
}

fit::callback<void(zx_status_t)> zx_device::take_bind_conn() {
  fbl::AutoLock<fbl::Mutex> lock(&bind_conn_lock_);
  auto conn = std::move(bind_conn_);
  bind_conn_ = nullptr;
  return conn;
}

void zx_device::set_rebind_conn(fit::callback<void(zx_status_t)> conn) {
  fbl::AutoLock<fbl::Mutex> lock(&rebind_conn_lock_);
  rebind_conn_ = std::move(conn);
}

fit::callback<void(zx_status_t)> zx_device::take_rebind_conn() {
  fbl::AutoLock<fbl::Mutex> lock(&rebind_conn_lock_);
  auto conn = std::move(rebind_conn_);
  rebind_conn_ = nullptr;
  return conn;
}

void zx_device::set_unbind_children_conn(fit::callback<void(zx_status_t)> conn) {
  fbl::AutoLock<fbl::Mutex> lock(&unbind_children_conn_lock_);
  unbind_children_conn_ = std::move(conn);
}

fit::callback<void(zx_status_t)> zx_device::take_unbind_children_conn() {
  fbl::AutoLock<fbl::Mutex> lock(&unbind_children_conn_lock_);
  auto conn = std::move(unbind_children_conn_);
  unbind_children_conn_ = nullptr;
  return conn;
}

void zx_device::PushTestCompatibilityConn(fit::callback<void(zx_status_t)> conn) {
  fbl::AutoLock<fbl::Mutex> lock(&test_compatibility_conn_lock_);
  test_compatibility_conn_.push_back(std::move(conn));
}

fit::callback<void(zx_status_t)> zx_device::PopTestCompatibilityConn() {
  fbl::AutoLock<fbl::Mutex> lock(&test_compatibility_conn_lock_);
  auto conn = std::move(test_compatibility_conn_[0]);
  test_compatibility_conn_.erase(0);
  return conn;
}

void zx_device::set_rebind_drv_name(const char* drv_name) {
  rebind_drv_name_ = std::string(drv_name);
}

const zx_device::DevicePowerStates& zx_device::GetPowerStates() const { return power_states_; }

const zx_device::PerformanceStates& zx_device::GetPerformanceStates() const {
  return performance_states_;
}

const zx_device::SystemPowerStateMapping& zx_device::GetSystemPowerStateMapping() const {
  return system_power_states_mapping_;
}

zx_status_t zx_device::SetPowerStates(const device_power_state_info_t* power_states,
                                      uint8_t count) {
  if (count < fuchsia_device::wire::MIN_DEVICE_POWER_STATES ||
      count > fuchsia_device::wire::MAX_DEVICE_POWER_STATES) {
    return ZX_ERR_INVALID_ARGS;
  }
  bool visited[fuchsia_device::wire::MAX_DEVICE_POWER_STATES] = {false};
  for (uint8_t i = 0; i < count; i++) {
    const auto& info = power_states[i];
    if (info.state_id >= std::size(visited)) {
      return ZX_ERR_INVALID_ARGS;
    }
    if (visited[info.state_id]) {
      return ZX_ERR_INVALID_ARGS;
    }
    auto state = &power_states_[info.state_id];
    state->state_id = static_cast<fuchsia_device::wire::DevicePowerState>(info.state_id);
    state->is_supported = true;
    state->restore_latency = info.restore_latency;
    state->wakeup_capable = info.wakeup_capable;
    state->system_wake_state = info.system_wake_state;
    visited[info.state_id] = true;
  }
  if (!(power_states_[static_cast<uint8_t>(
                          fuchsia_device::wire::DevicePowerState::DEVICE_POWER_STATE_D0)]
            .is_supported) ||
      !(power_states_[static_cast<uint8_t>(
                          fuchsia_device::wire::DevicePowerState::DEVICE_POWER_STATE_D3COLD)]
            .is_supported)) {
    return ZX_ERR_INVALID_ARGS;
  }
  inspect_->set_power_states(power_states, count);
  return ZX_OK;
}

zx_status_t zx_device::SetPerformanceStates(
    const device_performance_state_info_t* performance_states, uint8_t count) {
  if (count < fuchsia_device_MIN_DEVICE_PERFORMANCE_STATES ||
      count > fuchsia_device_MAX_DEVICE_PERFORMANCE_STATES) {
    return ZX_ERR_INVALID_ARGS;
  }
  bool visited[fuchsia_device_MAX_DEVICE_PERFORMANCE_STATES] = {false};
  for (uint8_t i = 0; i < count; i++) {
    const auto& info = performance_states[i];
    if (info.state_id >= std::size(visited)) {
      return ZX_ERR_INVALID_ARGS;
    }
    if (visited[info.state_id]) {
      return ZX_ERR_INVALID_ARGS;
    }
    fuchsia_device::wire::DevicePerformanceStateInfo* state = &(performance_states_[info.state_id]);
    state->state_id = info.state_id;
    state->is_supported = true;
    state->restore_latency = info.restore_latency;
    visited[info.state_id] = true;
  }
  if (!(performance_states_[fuchsia_device_DEVICE_PERFORMANCE_STATE_P0].is_supported)) {
    return ZX_ERR_INVALID_ARGS;
  }
  inspect_->set_performance_states(performance_states, count);
  return ZX_OK;
}

void zx_device::CloseAllConnections() {
  for (auto& child : children_) {
    if (child.flags_ & DEV_FLAG_INSTANCE) {
      child.CloseAllConnections();
    }
  }
  // Posted to the main event loop to synchronize with any other calls that may manipulate
  // the state of this Vnode (such as dev->vnode being reset by DevfsVnode::Close or
  // DriverHostContext::DriverManagerRemove)
  async::PostTask(internal::ContextForApi()->loop().dispatcher(),
                  [dev = fbl::RefPtr<zx_device>(this)] {
                    if (dev->vnode) {
                      dev->driver_host_context_->vfs()->CloseAllConnectionsForVnode(
                          *dev->vnode, /*callback=*/nullptr);
                    }
                  });
}

zx_status_t zx_device::SetSystemPowerStateMapping(const SystemPowerStateMapping& mapping) {
  for (size_t i = 0; i < mapping.size(); i++) {
    auto info = &mapping[i];
    if (!power_states_[static_cast<uint8_t>(info->dev_state)].is_supported) {
      return ZX_ERR_INVALID_ARGS;
    }
    if (info->wakeup_enable &&
        !power_states_[static_cast<uint8_t>(info->dev_state)].wakeup_capable) {
      return ZX_ERR_INVALID_ARGS;
    }
    // TODO(ravoorir): Validate whether the system can wake up from that state,
    // when power states make more sense. Currently we cannot compare the
    // system sleep power states.
    system_power_states_mapping_[i] = mapping[i];
  }
  inspect_->set_system_power_state_mapping(mapping);
  return ZX_OK;
}

// We must disable thread-safety analysis due to not being able to statically
// guarantee the lock holding invariant.  Instead, we acquire the lock if
// it's not already being held by the current thread.
void zx_device::fbl_recycle() TA_NO_THREAD_SAFETY_ANALYSIS {
  bool acq_lock = !driver_host_context_->api_lock().IsHeldByCurrentThread();
  if (acq_lock) {
    driver_host_context_->api_lock().Acquire();
  }
  auto unlock = fit::defer([this, acq_lock]() TA_NO_THREAD_SAFETY_ANALYSIS {
    if (acq_lock) {
      driver_host_context_->api_lock().Release();
    }
  });

  if (this->flags_ & DEV_FLAG_INSTANCE) {
    // these don't get removed, so mark dead state here
    this->set_flag(DEV_FLAG_DEAD);
  }
  if (this->flags() & DEV_FLAG_BUSY) {
    // this can happen if creation fails
    // the caller to device_add() will free it
    LOGD(WARNING, *this, "Not releasing device %p, it is busy", this);
    return;
  }
  VLOGD(1, *this, "Releasing device %p", this);

  if (!(this->flags() & DEV_FLAG_DEAD)) {
    LOGD(WARNING, *this, "Releasing device %p which is not yet dead", this);
  }
  if (!this->children().is_empty()) {
    LOGD(WARNING, *this, "Releasing device %p which still has children", this);
  }

  composite_.reset();
  this->event.reset();
  this->local_event.reset();

  driver_host_context_->QueueDeviceForFinalization(this);
}

static fbl::Mutex local_id_map_lock_;
static fbl::TaggedWAVLTree<uint64_t, fbl::RefPtr<zx_device>, zx_device::LocalIdMapTag,
                           zx_device::LocalIdKeyTraits>
    local_id_map_ TA_GUARDED(local_id_map_lock_);

void zx_device::set_local_id(uint64_t id) {
  // If this is the last reference, we want it to go away outside of the lock
  fbl::RefPtr<zx_device> old_entry;

  fbl::AutoLock guard(&local_id_map_lock_);
  if (local_id_ != 0) {
    old_entry = local_id_map_.erase(*this);
    ZX_ASSERT(old_entry.get() == this);
  }

  local_id_ = id;
  if (id != 0) {
    local_id_map_.insert(fbl::RefPtr(this));
  }
  inspect_->set_local_id(id);

  // Update parent local id all inspect data of children.
  // This is needed because sometimes parent local id is set after the children are created.
  for (auto& child : children_) {
    child.inspect().set_parent(fbl::RefPtr(this));
  }
}

fbl::RefPtr<zx_device> zx_device::GetDeviceFromLocalId(uint64_t local_id) {
  fbl::AutoLock guard(&local_id_map_lock_);
  auto itr = local_id_map_.find(local_id);
  if (itr == local_id_map_.end()) {
    return nullptr;
  }
  return fbl::RefPtr(&*itr);
}

bool zx_device::Unbound() {
  if (flags_ & DEV_FLAG_INSTANCE) {
    return parent_->Unbound();
  }
  return flags_ & DEV_FLAG_UNBOUND;
}

bool zx_device::has_composite() const { return !!composite_; }

fbl::RefPtr<CompositeDevice> zx_device::take_composite() { return std::move(composite_); }

void zx_device::set_composite(fbl::RefPtr<CompositeDevice> composite, bool fragment) {
  composite_ = std::move(composite);
  is_composite_ = !fragment;
  if (fragment) {
    inspect_->set_fragment();
  } else {
    inspect_->set_composite();
  }
}

bool zx_device::is_composite() const { return is_composite_ && !!composite_; }

fbl::RefPtr<CompositeDevice> zx_device::composite() { return composite_; }

bool zx_device::IsPerformanceStateSupported(uint32_t requested_state) {
  if (requested_state >= fuchsia_device_MAX_DEVICE_PERFORMANCE_STATES) {
    return false;
  }
  return performance_states_[requested_state].is_supported;
}

void zx_device::add_child(zx_device* child) {
  children_.push_back(child);
  if (child->flags_ & DEV_FLAG_INSTANCE) {
    inspect_->increment_instance_count();
  } else {
    inspect_->increment_child_count();
  }
}
void zx_device::remove_child(zx_device& child) {
  children_.erase(child);
  if (child.flags_ & DEV_FLAG_INSTANCE) {
    inspect_->decrement_instance_count();
  } else {
    inspect_->decrement_child_count();
  }
}
