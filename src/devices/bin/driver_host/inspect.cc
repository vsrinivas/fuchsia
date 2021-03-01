// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "inspect.h"

#include "driver_host.h"
#include "log.h"
#include "src/lib/storage/vfs/cpp/synchronous_vfs.h"
#include "src/lib/storage/vfs/cpp/vmo_file.h"
#include "zx_device.h"

DriverHostInspect::DriverHostInspect() {
  inspect_vmo_ = inspect_.DuplicateVmo();
  uint64_t vmo_size;
  ZX_ASSERT(inspect_vmo_.get_size(&vmo_size) == ZX_OK);
  auto vmo_file = fbl::MakeRefCounted<fs::VmoFile>(inspect_vmo_, 0, vmo_size);

  diagnostics_dir_ = fbl::MakeRefCounted<fs::PseudoDir>();
  diagnostics_dir_->AddEntry("root.inspect", vmo_file);

  drivers_.nodes = root_node().CreateChild("drivers");
  drivers_.count = root_node().CreateUint("driver_count", 0);

  // Device defaults
  auto default_node = root_node().CreateChild("device_defaults");
  SetDeviceDefaultPowerStates(default_node);
  SetDeviceDefaultPerfStates(default_node);
  SetDeviceDefaultStateMapping(default_node);
  static_values_.emplace(std::move(default_node));
}

zx_status_t DriverHostInspect::Serve(zx::channel remote, async_dispatcher_t* dispatcher) {
  diagnostics_vfs_ = std::make_unique<fs::SynchronousVfs>(dispatcher);
  return diagnostics_vfs_->ServeDirectory(diagnostics_dir_, std::move(remote));
}

void DriverHostInspect::SetDeviceDefaultPowerStates(inspect::Node& parent) {
  auto power_states = parent.CreateChild("default_power_states");
  for (uint8_t i = 0; i < countof(internal::kDeviceDefaultPowerStates); i++) {
    const auto& info = internal::kDeviceDefaultPowerStates[i];
    auto& state = power_states_[i];
    state.emplace(power_states, info.state_id);
    state->restore_latency.Set(info.restore_latency);
    state->wakeup_capable.Set(info.wakeup_capable);
    state->system_wake_state.Set(info.system_wake_state);
    static_values_.emplace(std::move(state->power_state));
  }
  static_values_.emplace(std::move(power_states));
}

void DriverHostInspect::SetDeviceDefaultPerfStates(inspect::Node& parent) {
  auto perf_states = parent.CreateChild("default_performance_states");
  for (uint8_t i = 0; i < countof(internal::kDeviceDefaultPerfStates); i++) {
    const auto& info = internal::kDeviceDefaultPerfStates[i];
    auto& state = performance_states_[i];
    state.emplace(perf_states, info.state_id);
    state->restore_latency.Set(info.restore_latency);
    static_values_.emplace(std::move(state->performance_state));
  }
  static_values_.emplace(std::move(perf_states));
}

void DriverHostInspect::SetDeviceDefaultStateMapping(inspect::Node& parent) {
  auto state_mapping = parent.CreateChild("default_system_power_state_mapping");
  for (uint8_t i = 0; i < internal::kDeviceDefaultStateMapping.size(); i++) {
    auto info = &internal::kDeviceDefaultStateMapping[i];
    auto& state = state_mappings_[i];
    state.emplace(state_mapping, i);
    state->power_state.Set(static_cast<uint8_t>(info->dev_state));
    state->performance_state.Set(info->performance_state);
    state->wakeup_enable.Set(info->wakeup_enable);
    state->suspend_flag.Set(info->suspend_flag);
    static_values_.emplace(std::move(state->system_power_state));
  }
  static_values_.emplace(std::move(state_mapping));
}

inspect::Node& DriverHostInspect::GetCallStatsNode() {
  if (!call_stats_) {
    call_stats_ = root_node().CreateChild("call_stats");
  }
  return call_stats_;
}

InspectCallStats& DriverHostInspect::DeviceCreateStats() {
  if (!device_create_stats_) {
    device_create_stats_.emplace(GetCallStatsNode(), "device_create");
  }
  return *device_create_stats_;
}

InspectCallStats& DriverHostInspect::DeviceDestroyStats() {
  if (!device_destroy_stats_) {
    device_destroy_stats_.emplace(GetCallStatsNode(), "device_destroy");
  }
  return *device_destroy_stats_;
}

InspectCallStats& DriverHostInspect::DeviceInitStats() {
  if (!device_init_stats_) {
    device_init_stats_.emplace(GetCallStatsNode(), "device_init");
  }
  return *device_init_stats_;
}

InspectCallStats& DriverHostInspect::DeviceAddStats() {
  if (!device_add_stats_) {
    device_add_stats_.emplace(GetCallStatsNode(), "device_add");
  }
  return *device_add_stats_;
}

InspectCallStats& DriverHostInspect::DeviceRemoveStats() {
  if (!device_remove_stats_) {
    device_remove_stats_.emplace(GetCallStatsNode(), "device_remove");
  }
  return *device_remove_stats_;
}

InspectCallStats& DriverHostInspect::DeviceOpenStats() {
  if (!device_open_stats_) {
    device_open_stats_.emplace(GetCallStatsNode(), "device_open");
  }
  return *device_open_stats_;
}

InspectCallStats& DriverHostInspect::DeviceCloseStats() {
  if (!device_close_stats_) {
    device_close_stats_.emplace(GetCallStatsNode(), "device_close");
  }
  return *device_close_stats_;
}

InspectCallStats& DriverHostInspect::DeviceSuspendStats() {
  if (!device_suspend_stats_) {
    device_suspend_stats_.emplace(GetCallStatsNode(), "device_suspend");
  }
  return *device_suspend_stats_;
}

InspectCallStats& DriverHostInspect::DeviceResumeStats() {
  if (!device_resume_stats_) {
    device_resume_stats_.emplace(GetCallStatsNode(), "device_resume");
  }
  return *device_resume_stats_;
}

InspectCallStats& DriverHostInspect::DeviceUnbindStats() {
  if (!device_unbind_stats_) {
    device_unbind_stats_.emplace(GetCallStatsNode(), "device_unbind");
  }
  return *device_unbind_stats_;
}

DriverInspect::DriverInspect(InspectNodeCollection& drivers, std::string name) : drivers_(drivers) {
  driver_node_ = drivers_.nodes.CreateChild(name);
  // Increment driver count.
  drivers_.count.Add(1);

  devices_.nodes = driver_node_.CreateChild("devices");
  devices_.count = driver_node_.CreateUint("device_count", 0);

  // create properties with default values
}

DriverInspect::~DriverInspect() {
  // Decrement driver count.
  drivers_.count.Subtract(1);
}

void DriverInspect::set_ops(const zx_driver_ops_t* ops) {
  fbl::StringBuffer<128> ops_list;

  if (ops->bind) {
    ops_list.Append("bind ");
  }
  if (ops->create) {
    ops_list.Append("create ");
  }
  if (ops->init) {
    ops_list.Append("init ");
  }
  if (ops->release) {
    ops_list.Append("release ");
  }
  if (ops->run_unit_tests) {
    ops_list.Append("run_unit_tests ");
  }
  if (ops->version) {
    ops_list.Append("version ");
  }
  driver_node_.CreateString("ops", {ops_list.data(), ops_list.length()}, &static_values_);
}
void DriverInspect::set_status(zx_status_t status) {
  if (!status) {
    status_ = driver_node_.CreateInt("status", 0);
  }
  status_.Set(status);
}

DeviceInspect::DeviceInspect(InspectNodeCollection& devices, std::string name) : devices_(devices) {
  device_node_ = devices_.nodes.CreateChild(name);
  // Increment device count.
  devices_.count.Add(1);
}

DeviceInspect::~DeviceInspect() {
  // Decrement device count.
  devices_.count.Subtract(1);
}

void DeviceInspect::set_local_id(uint64_t local_id) {
  if (!local_id_) {
    local_id_ = device_node_.CreateUint("local_id", 0);
  }
  local_id_.Set(local_id);
}

void DeviceInspect::set_flags(uint32_t flags) {
  if (!flags_) {
    flags_ = device_node_.CreateString("flags", "");
  }
  fbl::StringBuffer<128> flags_str;

  if (flags & DEV_FLAG_DEAD) {
    flags_str.Append("dead ");
  }
  if (flags & DEV_FLAG_INITIALIZING) {
    flags_str.Append("initializing ");
  }
  if (flags & DEV_FLAG_UNBINDABLE) {
    flags_str.Append("unbindable ");
  }
  if (flags & DEV_FLAG_BUSY) {
    flags_str.Append("busy ");
  }
  if (flags & DEV_FLAG_INSTANCE) {
    flags_str.Append("instance ");
  }
  if (flags & DEV_FLAG_MULTI_BIND) {
    flags_str.Append("multi-bind ");
  }
  if (flags & DEV_FLAG_ADDED) {
    flags_str.Append("added ");
  }
  if (flags & DEV_FLAG_INVISIBLE) {
    flags_str.Append("invisible ");
  }
  if (flags & DEV_FLAG_UNBOUND) {
    flags_str.Append("unbound ");
  }
  if (flags & DEV_FLAG_WANTS_REBIND) {
    flags_str.Append("rebind ");
  }
  if (flags & DEV_FLAG_ALLOW_MULTI_COMPOSITE) {
    flags_str.Append("multi-composite ");
  }
  flags_.Set({flags_str.data(), flags_str.length()});
}

void DeviceInspect::set_ops(const zx_protocol_device_t* ops) {
  if (!ops_) {
    ops_ = device_node_.CreateString("ops", "");
  }
  if (!ops) {
    return;
  }
  fbl::StringBuffer<256> ops_str;
  if (ops->get_protocol) {
    ops_str.Append("get_protocol ");
  }
  if (ops->init) {
    ops_str.Append("init ");
  }
  if (ops->open) {
    ops_str.Append("open ");
  }
  if (ops->close) {
    ops_str.Append("close ");
  }
  if (ops->unbind) {
    ops_str.Append("unbind ");
  }
  if (ops->release) {
    ops_str.Append("release ");
  }
  if (ops->read) {
    ops_str.Append("read ");
  }
  if (ops->write) {
    ops_str.Append("write ");
  }
  if (ops->get_size) {
    ops_str.Append("get_size ");
  }
  if (ops->suspend) {
    ops_str.Append("suspend ");
  }
  if (ops->resume) {
    ops_str.Append("resume ");
  }
  if (ops->set_performance_state) {
    ops_str.Append("set_performance_state ");
  }
  if (ops->configure_auto_suspend) {
    ops_str.Append("configure_auto_suspend ");
  }
  if (ops->rxrpc) {
    ops_str.Append("rxrpc ");
  }
  if (ops->message) {
    ops_str.Append("message ");
  }
  if (ops->child_pre_release) {
    ops_str.Append("child_pre_release");
  }
  if (ops->open_protocol_session_multibindable) {
    ops_str.Append("open_protocol_session_multibindable");
  }
  if (ops->close_protocol_session_multibindable) {
    ops_str.Append("close_protocol_session_multibindable");
  }
  ops_.Set({ops_str.data(), ops_str.length()});
}

void DeviceInspect::set_protocol_id(uint32_t protocol_id) {
  std::string protocol_name;
  // Protocol Identifiers
#define DDK_PROTOCOL_DEF(tag, val, name, flags) \
  case val:                                     \
    protocol_name = name;                       \
    break;

  switch (protocol_id) {
#include <ddk/protodefs.h>
    default:
      protocol_name = std::string("unknown-").append(std::to_string(protocol_id));
  }
  device_node_.CreateString("protocol", protocol_name, &static_values_);
}

void DeviceInspect::increment_child_count() {
  if (!child_count_) {
    child_count_ = device_node_.CreateUint("child_count", 0);
  }
  child_count_.Add(1);
}

void DeviceInspect::decrement_child_count() {
  ZX_ASSERT(child_count_);
  child_count_.Subtract(1);
}

void DeviceInspect::increment_instance_count() {
  if (!instance_count_) {
    instance_count_ = device_node_.CreateUint("instance_count", 0);
  }
  instance_count_.Add(1);
}

void DeviceInspect::decrement_instance_count() {
  ZX_ASSERT(instance_count_);
  instance_count_.Subtract(1);
}

void DeviceInspect::increment_open_count() {
  if (!open_count_) {
    open_count_ = device_node_.CreateUint("opened_connections", 0);
  }
  open_count_.Add(1);
}

void DeviceInspect::increment_close_count() {
  if (!close_count_) {
    close_count_ = device_node_.CreateUint("closed_connections", 0);
  }
  close_count_.Add(1);
}

void DeviceInspect::set_parent(fbl::RefPtr<zx_device> parent) {
  if (!parent_) {
    parent_ = device_node_.CreateString("parent", "");
  }
  std::string parent_id;
  if (parent) {
    parent_id.append(parent->name());
    parent_id.append(" (local-id:").append(std::to_string(parent->local_id())).append(")");
  }
  parent_.Set(parent_id);
}

inspect::Node& DeviceInspect::GetCallStatsNode() {
  if (!call_stats_) {
    call_stats_ = device_node_.CreateChild("call_stats");
  }
  return call_stats_;
}

InspectCallStats& DeviceInspect::ReadOpStats() {
  if (!read_stats_) {
    read_stats_.emplace(GetCallStatsNode(), "read_op");
  }
  return *read_stats_;
}

InspectCallStats& DeviceInspect::WriteOpStats() {
  if (!write_stats_) {
    write_stats_.emplace(GetCallStatsNode(), "write_op");
  }
  return *write_stats_;
}

InspectCallStats& DeviceInspect::MessageOpStats() {
  if (!message_stats_) {
    message_stats_.emplace(GetCallStatsNode(), "message_op");
  }
  return *message_stats_;
}

void DeviceInspect::set_current_performance_state(uint32_t state) {
  if (!current_performance_state_) {
    current_performance_state_ = device_node_.CreateUint("current_performance_state", 0);
  }
  current_performance_state_.Set(state);
}

void DeviceInspect::set_auto_suspend(bool value) {
  if (!auto_suspend_) {
    auto_suspend_ = device_node_.CreateBool("auto_suspend", false);
  }
  auto_suspend_.Set(value);
}

void DeviceInspect::set_power_states(const device_power_state_info_t* power_states, uint8_t count) {
  if (power_states == internal::kDeviceDefaultPowerStates) {
    // To increase readability of inspect data and save space, default power state is only included
    // in driver host, and not per device.
    return;
  }
  if (!power_states_node_) {
    power_states_node_ = device_node_.CreateChild("power_states");
  }
  for (uint8_t i = 0; i < count; i++) {
    const auto& info = power_states[i];
    auto& state = power_states_[info.state_id];
    if (!state) {
      state.emplace(power_states_node_, info.state_id);
    }
    state->restore_latency.Set(info.restore_latency);
    state->wakeup_capable.Set(info.wakeup_capable);
    state->system_wake_state.Set(info.system_wake_state);
  }
}

void DeviceInspect::set_performance_states(
    const device_performance_state_info_t* performance_states, uint8_t count) {
  if (performance_states == internal::kDeviceDefaultPerfStates) {
    // To increase readability of inspect data and save space, default performance state is only
    // included in driver host, and not per device.
    return;
  }
  if (!performance_states_node_) {
    performance_states_node_ = device_node_.CreateChild("performance_states");
  }
  for (uint8_t i = 0; i < count; i++) {
    const auto& info = performance_states[i];
    auto& state = performance_states_[info.state_id];
    if (!state) {
      state.emplace(performance_states_node_, info.state_id);
    }
    state->restore_latency.Set(info.restore_latency);
  }
}

void DeviceInspect::set_system_power_state_mapping(
    const zx_device::SystemPowerStateMapping& mapping) {
  if (&mapping == &internal::kDeviceDefaultStateMapping) {
    // To increase readability of inspect data and save space, default state mapping is only
    // included in driver host, and not per device.
    return;
  }
  if (!system_power_states_node_) {
    system_power_states_node_ = device_node_.CreateChild("system_power_states_mapping");
  }
  for (uint8_t i = 0; i < mapping.size(); i++) {
    const auto& info = mapping[i];
    auto& state = system_power_states_mapping_[i];
    if (!state) {
      state.emplace(system_power_states_node_, i);
    }
    state->power_state.Set(static_cast<uint8_t>(info.dev_state));
    state->performance_state.Set(info.performance_state);
    state->wakeup_enable.Set(info.wakeup_enable);
    state->suspend_flag.Set(info.suspend_flag);
  }
}
