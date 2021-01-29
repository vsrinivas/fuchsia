// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_BIN_DRIVER_HOST_INSPECT_H_
#define SRC_DEVICES_BIN_DRIVER_HOST_INSPECT_H_

#include <fuchsia/device/llcpp/fidl.h>
#include <fuchsia/device/manager/llcpp/fidl.h>
#include <lib/inspect/cpp/inspect.h>
#include <lib/zx/clock.h>

#include <ddk/driver.h>
#include <fbl/ref_ptr.h>
#include <fs/pseudo_dir.h>
#include <fs/synchronous_vfs.h>

#include "defaults.h"

class InspectCallStats {
 public:
  InspectCallStats(inspect::Node& parent, std::string name) {
    node_ = parent.CreateChild(name);
    count_ = node_.CreateUint("count", 0);
    time_taken_ns_ = node_.CreateExponentialUintHistogram(
        "time_taken(ns)", 0 /* floor */, 1000 /* step size: 1 us */, 10 /* step multiplier */,
        7 /* buckets (upto 1s) */);
  }

  class InspectCallStatsUpdate {
   public:
    InspectCallStatsUpdate(InspectCallStats& stats) : stats_(stats) { stats_.count().Add(1); }
    ~InspectCallStatsUpdate() {
      stats_.time_taken_ns().Insert((zx::clock::get_monotonic() - start_).get());
    }

    // disallow copy and assign
    InspectCallStatsUpdate(const InspectCallStatsUpdate&) = delete;
    InspectCallStatsUpdate& operator=(const InspectCallStatsUpdate&) = delete;

   private:
    zx::time start_ = zx::clock::get_monotonic();
    InspectCallStats& stats_;
  };

  // Call this method to measure time taken for a function. When the Update object goes out of
  // scope, the measurements are recorded.
  // Eg: void ProcessRequest() {
  //       inspect_stat.Update();
  //       // Do something useful.
  //     } // Measurements are recorded here.
  //
  // Note: `InspectCallStatsUpdate` object returned by this method should not outlive
  // `InspectCallStats` i.e. this object
  InspectCallStatsUpdate Update() { return InspectCallStatsUpdate(*this); }

  inspect::ExponentialUintHistogram& time_taken_ns() { return time_taken_ns_; }
  inspect::UintProperty& count() { return count_; }

 private:
  inspect::Node node_;
  inspect::UintProperty count_;
  inspect::ExponentialUintHistogram time_taken_ns_;
};

struct InspectNodeCollection {
  inspect::Node nodes;
  inspect::UintProperty count;
};

// Helper class for device inspect
struct DeviceSystemPowerStateMapping {
  explicit DeviceSystemPowerStateMapping(inspect::Node& parent, uint32_t state_id) {
    system_power_state = parent.CreateChild(std::to_string(state_id));
    power_state = system_power_state.CreateUint("power_state", 0);
    performance_state = system_power_state.CreateUint("performance_state", 0);
    suspend_flag = system_power_state.CreateUint("suspend_flag", 0);
    wakeup_enable = system_power_state.CreateBool("wakeup_enable", false);
  }
  inspect::Node system_power_state;
  inspect::UintProperty power_state;
  inspect::UintProperty performance_state;
  inspect::UintProperty suspend_flag;
  inspect::BoolProperty wakeup_enable;
};

// Helper class for device inspect
struct DevicePowerStates {
  explicit DevicePowerStates(inspect::Node& parent, uint32_t state_id) {
    power_state = parent.CreateChild(std::to_string(state_id));
    restore_latency = power_state.CreateInt("restore_latency", 0);
    wakeup_capable = power_state.CreateBool("wakeup_capable", false);
    system_wake_state = power_state.CreateInt("system_wake_state", 0);
  }
  inspect::Node power_state;
  inspect::IntProperty restore_latency;
  inspect::BoolProperty wakeup_capable;
  inspect::IntProperty system_wake_state;
};

// Helper class for device inspect
struct DevicePerformanceStates {
  explicit DevicePerformanceStates(inspect::Node& parent, uint32_t state_id) {
    performance_state = parent.CreateChild(std::to_string(state_id));
    restore_latency = performance_state.CreateInt("restore_latency", 0);
  }
  inspect::Node performance_state;
  inspect::IntProperty restore_latency;
};

class DriverHostInspect {
 public:
  DriverHostInspect();

  inspect::Node& root_node() { return inspect_.GetRoot(); }
  fs::PseudoDir& diagnostics_dir() { return *diagnostics_dir_; }
  InspectNodeCollection& drivers() { return drivers_; }

  zx_status_t Serve(zx::channel remote, async_dispatcher_t* dispatcher);

  // Public method for test purpose.
  inspect::Inspector& inspector() { return inspect_; }

  InspectCallStats& DeviceCreateStats();
  InspectCallStats& DeviceDestroyStats();
  InspectCallStats& DeviceInitStats();
  InspectCallStats& DeviceOpenStats();
  InspectCallStats& DeviceCloseStats();
  InspectCallStats& DeviceAddStats();
  InspectCallStats& DeviceRemoveStats();
  InspectCallStats& DeviceSuspendStats();
  InspectCallStats& DeviceResumeStats();
  InspectCallStats& DeviceUnbindStats();

 private:
  inspect::Inspector inspect_;
  zx::vmo inspect_vmo_;
  fbl::RefPtr<fs::PseudoDir> diagnostics_dir_;
  std::unique_ptr<fs::SynchronousVfs> diagnostics_vfs_;

  // Data for nodes stored in static_values_.
  std::array<std::optional<DevicePowerStates>, std::size(internal::kDeviceDefaultPowerStates)>
      power_states_;
  std::array<std::optional<DevicePerformanceStates>, std::size(internal::kDeviceDefaultPerfStates)>
      performance_states_;
  std::array<std::optional<DeviceSystemPowerStateMapping>,
             std::size(internal::kDeviceDefaultStateMapping)>
      state_mappings_;

  // Reference to nodes with static properties
  inspect::ValueList static_values_;

  InspectNodeCollection drivers_;

  // Driver host call stats.
  inspect::Node call_stats_;
  std::optional<InspectCallStats> device_create_stats_;
  std::optional<InspectCallStats> device_destroy_stats_;
  std::optional<InspectCallStats> device_init_stats_;
  std::optional<InspectCallStats> device_open_stats_;
  std::optional<InspectCallStats> device_close_stats_;
  std::optional<InspectCallStats> device_add_stats_;
  std::optional<InspectCallStats> device_remove_stats_;
  std::optional<InspectCallStats> device_suspend_stats_;
  std::optional<InspectCallStats> device_resume_stats_;
  std::optional<InspectCallStats> device_unbind_stats_;

  inspect::Node& GetCallStatsNode();

  void SetDeviceDefaultPowerStates(inspect::Node& parent);
  void SetDeviceDefaultPerfStates(inspect::Node& parent);
  void SetDeviceDefaultStateMapping(inspect::Node& parent);
};

class DriverInspect {
 public:
  // |drivers| should outlive DriverInspect class
  DriverInspect(InspectNodeCollection& drivers, std::string name);

  ~DriverInspect();

  inspect::Node& driver_node() { return driver_node_; }

  InspectNodeCollection& devices() { return devices_; }

  void set_name(std::string name) { driver_node_.CreateString("name", name, &static_values_); }

  void set_ops(const zx_driver_ops_t* ops);

  void set_status(zx_status_t status);

  void set_driver_rec(zx_driver_rec_t* driver_rec) {
    driver_node_.CreateUint("log_flags", driver_rec->log_flags, &static_values_);
  }

 private:
  inspect::Node driver_node_;
  InspectNodeCollection& drivers_;
  InspectNodeCollection devices_;

  // Reference to nodes with static properties
  inspect::ValueList static_values_;

  inspect::IntProperty status_;
};

class DeviceInspect {
 public:
  // |devices| should outlive DeviceInspect class
  DeviceInspect(InspectNodeCollection& devices, std::string name);

  ~DeviceInspect();

  inspect::Node& device_node() { return device_node_; }

  void set_local_id(uint64_t local_id);

  void set_performance_states(const device_performance_state_info_t* performance_states,
                              uint8_t count);

  void set_current_performance_state(uint32_t state);

  void set_auto_suspend(bool value);

  void set_power_states(const device_power_state_info_t* power_states, uint8_t count);

  using SystemPowerStateMapping =
      std::array<::llcpp::fuchsia::device::SystemPowerStateInfo,
                 ::llcpp::fuchsia::hardware::power::statecontrol::MAX_SYSTEM_POWER_STATES>;

  void set_system_power_state_mapping(const SystemPowerStateMapping& mapping);

  void set_composite() { device_node_.CreateBool("composite", true, &static_values_); }
  void set_fragment() { device_node_.CreateBool("fragment", true, &static_values_); }

  void set_flags(uint32_t flags);

  void set_ops(const zx_protocol_device_t* ops);

  void set_protocol_id(uint32_t protocol_id);

  void increment_instance_count();
  void decrement_instance_count();

  void increment_child_count();
  void decrement_child_count();

  void increment_open_count();
  void increment_close_count();

  void set_parent(fbl::RefPtr<zx_device> parent);

  InspectCallStats& ReadOpStats();
  InspectCallStats& WriteOpStats();
  InspectCallStats& MessageOpStats();

 private:
  inspect::Node device_node_;
  InspectNodeCollection& devices_;

  // Reference to nodes with static properties
  inspect::ValueList static_values_;

  inspect::UintProperty local_id_;
  inspect::StringProperty flags_;
  inspect::StringProperty ops_;
  inspect::StringProperty parent_;
  inspect::BoolProperty auto_suspend_;

  inspect::UintProperty child_count_;
  inspect::UintProperty instance_count_;
  inspect::UintProperty open_count_;
  inspect::UintProperty close_count_;

  inspect::Node call_stats_;
  std::optional<InspectCallStats> read_stats_;
  std::optional<InspectCallStats> write_stats_;
  std::optional<InspectCallStats> message_stats_;

  inspect::Node& GetCallStatsNode();

  std::array<std::optional<DevicePowerStates>, ::llcpp::fuchsia::device::MAX_DEVICE_POWER_STATES>
      power_states_{};
  inspect::Node power_states_node_;

  std::array<std::optional<DevicePerformanceStates>,
             ::llcpp::fuchsia::device::MAX_DEVICE_PERFORMANCE_STATES>
      performance_states_{};
  inspect::Node performance_states_node_;
  inspect::UintProperty current_performance_state_;

  std::array<std::optional<DeviceSystemPowerStateMapping>,
             ::llcpp::fuchsia::hardware::power::statecontrol::MAX_SYSTEM_POWER_STATES>
      system_power_states_mapping_{};
  inspect::Node system_power_states_node_;
};

#endif  // SRC_DEVICES_BIN_DRIVER_HOST_INSPECT_H_
