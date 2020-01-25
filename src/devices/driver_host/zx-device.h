// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_DRIVER_HOST_ZX_DEVICE_H_
#define SRC_DEVICES_DRIVER_HOST_ZX_DEVICE_H_

#include <fuchsia/device/llcpp/fidl.h>
#include <fuchsia/device/manager/c/fidl.h>
#include <fuchsia/device/manager/llcpp/fidl.h>
#include <lib/fit/function.h>
#include <lib/fit/result.h>
#include <lib/zircon-internal/thread_annotations.h>
#include <lib/zx/channel.h>
#include <lib/zx/eventpair.h>
#include <zircon/compiler.h>

#include <array>
#include <atomic>
#include <optional>
#include <string>

#include <ddk/debug.h>
#include <ddk/device-power-states.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <fbl/intrusive_double_list.h>
#include <fbl/intrusive_wavl_tree.h>
#include <fbl/mutex.h>
#include <fbl/recycler.h>
#include <fbl/ref_counted_upgradeable.h>
#include <fbl/ref_ptr.h>
#include <fbl/vector.h>

#include "devfs-connection.h"

namespace devmgr {

class CompositeDevice;
class DeviceControllerConnection;
struct ProxyIostate;

}  // namespace devmgr

// 'MDEV'
#define DEV_MAGIC 0x4D444556

// This needs to be a struct, not a class, to match the public definition
struct zx_device : fbl::RefCountedUpgradeable<zx_device>, fbl::Recyclable<zx_device> {
  ~zx_device() = default;

  zx_device(const zx_device&) = delete;
  zx_device& operator=(const zx_device&) = delete;

  static zx_status_t Create(fbl::RefPtr<zx_device>* out_dev);

  void InitOp() { Dispatch(ops->init); }

  zx_status_t OpenOp(zx_device_t** dev_out, uint32_t flags) {
    return Dispatch(ops->open, ZX_OK, dev_out, flags);
  }

  zx_status_t CloseOp(uint32_t flags) { return Dispatch(ops->close, ZX_OK, flags); }

  void UnbindOp() { Dispatch(ops->unbind); }

  void ReleaseOp() { Dispatch(ops->release); }

  void SuspendNewOp(uint8_t requested_state, bool enable_wake, uint8_t suspend_reason) {
    Dispatch(ops->suspend_new, requested_state, enable_wake, suspend_reason);
  }

  zx_status_t ResumeOp(uint32_t flags) {
    return Dispatch(ops->resume, ZX_ERR_NOT_SUPPORTED, flags);
  }

  zx_status_t SetPerformanceStateOp(uint32_t requested_state, uint32_t* out_state) {
    return Dispatch(ops->set_performance_state, ZX_ERR_NOT_SUPPORTED, requested_state, out_state);
  }

  zx_status_t ConfigureAutoSuspendOp(bool enable, uint8_t requested_state) {
    return Dispatch(ops->configure_auto_suspend, ZX_ERR_NOT_SUPPORTED, enable, requested_state);
  }

  zx_status_t ResumeNewOp(uint8_t requested_state, uint8_t* out_state) {
    return Dispatch(ops->resume_new, ZX_ERR_NOT_SUPPORTED, requested_state, out_state);
  }

  zx_status_t ReadOp(void* buf, size_t count, zx_off_t off, size_t* actual) {
    return Dispatch(ops->read, ZX_ERR_NOT_SUPPORTED, buf, count, off, actual);
  }

  zx_status_t WriteOp(const void* buf, size_t count, zx_off_t off, size_t* actual) {
    return Dispatch(ops->write, ZX_ERR_NOT_SUPPORTED, buf, count, off, actual);
  }

  zx_off_t GetSizeOp() { return Dispatch(ops->get_size, 0lu); }

  zx_status_t MessageOp(fidl_msg_t* msg, fidl_txn_t* txn) {
    return Dispatch(ops->message, ZX_ERR_NOT_SUPPORTED, msg, txn);
  }

  void ChildPreReleaseOp(void* child_ctx) { Dispatch(ops->child_pre_release, child_ctx); }

  void set_bind_conn(fit::callback<void(zx_status_t)>);
  fit::callback<void(zx_status_t)> take_bind_conn();

  void set_rebind_conn(fit::callback<void(zx_status_t)>);
  fit::callback<void(zx_status_t)> take_rebind_conn();
  void set_rebind_drv_name(const char* drv_name);
  std::optional<std::string> get_rebind_drv_name() { return rebind_drv_name_; }

  void PushTestCompatibilityConn(fit::callback<void(zx_status_t)>);
  fit::callback<void(zx_status_t)> PopTestCompatibilityConn();

  // Check if this devhost has a device with the given ID, and if so returns a
  // reference to it.
  static fbl::RefPtr<zx_device> GetDeviceFromLocalId(uint64_t local_id);

  uint64_t local_id() const { return local_id_; }
  void set_local_id(uint64_t id);

  uintptr_t magic = DEV_MAGIC;

  const zx_protocol_device_t* ops = nullptr;

  // reserved for driver use; will not be touched by devmgr
  void* ctx = nullptr;

  uint32_t flags = 0;

  // Reference count of all outstanding transactions belonging to this device. These include read,
  // write, and fidl message transactions.
  std::atomic<uint32_t> outstanding_transactions = 0;

  zx::eventpair event;
  zx::eventpair local_event;

  // The RPC channel is owned by |conn|
  // fuchsia.device.manager.DeviceController
  zx::unowned_channel rpc;

  // The RPC channel is owned by |conn|
  // fuchsia.device.manager.Coordinator
  zx::unowned_channel coordinator_rpc;

  fit::callback<void(zx_status_t)> init_cb;

  fit::callback<void(zx_status_t)> removal_cb;

  fit::callback<void(zx_status_t)> unbind_cb;

  fit::callback<void(zx_status_t, uint8_t)> suspend_cb;

  // most devices implement a single
  // protocol beyond the base device protocol
  uint32_t protocol_id = 0;
  void* protocol_ops = nullptr;

  // driver that has published this device
  zx_driver_t* driver = nullptr;

  // parent in the device tree
  fbl::RefPtr<zx_device_t> parent;

  // for the parent's device_list
  fbl::DoublyLinkedListNodeState<zx_device*> node;
  struct Node {
    static fbl::DoublyLinkedListNodeState<zx_device*>& node_state(zx_device& obj) {
      return obj.node;
    }
  };

  // list of this device's children in the device tree
  fbl::DoublyLinkedList<zx_device*, Node> children;

  // list node for the defer_device_list
  fbl::DoublyLinkedListNodeState<zx_device*> defer;
  struct DeferNode {
    static fbl::DoublyLinkedListNodeState<zx_device*>& node_state(zx_device& obj) {
      return obj.defer;
    }
  };

  // This is an atomic so that the connection's async loop can inspect this
  // value to determine if an expected shutdown is happening.  See comments in
  // devhost_remove().
  std::atomic<devmgr::DeviceControllerConnection*> conn = nullptr;

  fbl::Mutex proxy_ios_lock;
  devmgr::ProxyIostate* proxy_ios TA_GUARDED(proxy_ios_lock) = nullptr;

  char name[ZX_DEVICE_NAME_MAX + 1] = {};

  // Trait structures for the local ID map
  struct LocalIdNode {
    static fbl::WAVLTreeNodeState<fbl::RefPtr<zx_device>>& node_state(zx_device& obj) {
      return obj.local_id_node_;
    }
  };
  struct LocalIdKeyTraits {
    static uint64_t GetKey(const zx_device& obj) { return obj.local_id_; }
    static bool LessThan(const uint64_t& key1, const uint64_t& key2) { return key1 < key2; }
    static bool EqualTo(const uint64_t& key1, const uint64_t& key2) { return key1 == key2; }
  };

  using DevicePowerStates = std::array<::llcpp::fuchsia::device::DevicePowerStateInfo,
                                       ::llcpp::fuchsia::device::MAX_DEVICE_POWER_STATES>;
  using SystemPowerStateMapping =
      std::array<::llcpp::fuchsia::device::SystemPowerStateInfo,
                 ::llcpp::fuchsia::device::manager::MAX_SYSTEM_POWER_STATES>;
  using PerformanceStates = std::array<::llcpp::fuchsia::device::DevicePerformanceStateInfo,
                                       ::llcpp::fuchsia::device::MAX_DEVICE_PERFORMANCE_STATES>;

  bool has_composite();
  void set_composite(fbl::RefPtr<devmgr::CompositeDevice> composite);
  fbl::RefPtr<devmgr::CompositeDevice> take_composite();

  const DevicePowerStates& GetPowerStates() const;
  const PerformanceStates& GetPerformanceStates() const;

  zx_status_t SetPowerStates(const device_power_state_info_t* power_states, uint8_t count);

  bool IsPowerStateSupported(::llcpp::fuchsia::device::DevicePowerState requested_state) {
    // requested_state is bounded by the enum.
    return power_states_[static_cast<uint8_t>(requested_state)].is_supported;
  }

  bool IsPerformanceStateSupported(uint32_t requested_state);
  bool auto_suspend_configured() { return auto_suspend_configured_; }
  void set_auto_suspend_configured(bool value) { auto_suspend_configured_ = value; }

  zx_status_t SetSystemPowerStateMapping(const SystemPowerStateMapping& mapping);

  zx_status_t SetPerformanceStates(const device_performance_state_info_t* performance_states,
                                   uint8_t count);

  const SystemPowerStateMapping& GetSystemPowerStateMapping() const;

  uint32_t current_performance_state() { return current_performance_state_; }

  void set_current_performance_state(uint32_t state) { current_performance_state_ = state; }

 private:
  zx_device() = default;

  // The fuchsia.Device.Manager.Coordinator protocol
  zx::channel coordinator_rpc_;

  friend class fbl::Recyclable<zx_device_t>;
  void fbl_recycle();

  // Templates that dispatch the protocol operations if they were set.
  // If they were not set, the second paramater is returned to the caller
  // (usually ZX_ERR_NOT_SUPPORTED)
  template <typename RetType, typename... ArgTypes>
  RetType Dispatch(RetType (*op)(void* ctx, ArgTypes...), RetType fallback, ArgTypes... args) {
    return op ? (*op)(ctx, args...) : fallback;
  }

  template <typename... ArgTypes>
  void Dispatch(void (*op)(void* ctx, ArgTypes...), ArgTypes... args) {
    if (op) {
      (*op)(ctx, args...);
    }
  }

  // If this device is a component of a composite, this points to the
  // composite control structure.
  fbl::RefPtr<devmgr::CompositeDevice> composite_;

  fbl::WAVLTreeNodeState<fbl::RefPtr<zx_device>> local_id_node_;

  // Identifier assigned by devmgr that can be used to assemble composite
  // devices.
  uint64_t local_id_ = 0;

  fbl::Mutex bind_conn_lock_;

  fit::callback<void(zx_status_t)> bind_conn_ TA_GUARDED(bind_conn_lock_);

  fbl::Mutex rebind_conn_lock_;

  fit::callback<void(zx_status_t)> rebind_conn_ TA_GUARDED(rebind_conn_lock_);

  std::optional<std::string> rebind_drv_name_ = std::nullopt;

  // The connection associated with fuchsia.device.Controller/RunCompatibilityTests
  fbl::Mutex test_compatibility_conn_lock_;

  fbl::Vector<fit::callback<void(zx_status_t)>> test_compatibility_conn_
      TA_GUARDED(test_compatibility_conn_lock_);

  PerformanceStates performance_states_;
  DevicePowerStates power_states_;
  SystemPowerStateMapping system_power_states_mapping_;
  uint32_t current_performance_state_ = llcpp::fuchsia::device::DEVICE_PERFORMANCE_STATE_P0;
  bool auto_suspend_configured_ = false;
};

// zx_device_t objects must be created or initialized by the driver manager's
// device_create() function.  Drivers MAY NOT touch any
// fields in the zx_device_t, except for the protocol_id and protocol_ops
// fields which it may fill out after init and before device_add() is called,
// and the ctx field which may be used to store driver-specific data.

// clang-format off

#define DEV_FLAG_DEAD                  0x00000001  // this device has been removed and
                                                   // is safe for ref0 and release()
#define DEV_FLAG_INITIALIZING          0x00000002  // device is being initialized
#define DEV_FLAG_UNBINDABLE            0x00000004  // nobody may bind to this device
#define DEV_FLAG_BUSY                  0x00000010  // device being created
#define DEV_FLAG_INSTANCE              0x00000020  // this device was created-on-open
#define DEV_FLAG_MULTI_BIND            0x00000080  // this device accepts many children
#define DEV_FLAG_ADDED                 0x00000100  // device_add() has been called for this device
#define DEV_FLAG_INVISIBLE             0x00000200  // device not visible via devfs
#define DEV_FLAG_UNBOUND               0x00000400  // informed that it should self-delete asap
#define DEV_FLAG_WANTS_REBIND          0x00000800  // when last child goes, rebind this device
#define DEV_FLAG_ALLOW_MULTI_COMPOSITE 0x00001000 // can be part of multiple composite devices
// clang-format on

// Request to bind a driver with drv_libname to device. If device is already bound to a driver,
// ZX_ERR_ALREADY_BOUND is returned
zx_status_t device_bind(const fbl::RefPtr<zx_device_t>& dev, const char* drv_libname);
zx_status_t device_unbind(const fbl::RefPtr<zx_device_t>& dev);
zx_status_t device_schedule_remove(const fbl::RefPtr<zx_device_t>& dev, bool unbind_self);
zx_status_t device_run_compatibility_tests(const fbl::RefPtr<zx_device_t>& dev,
                                           int64_t hook_wait_time);
zx_status_t device_open(const fbl::RefPtr<zx_device_t>& dev, fbl::RefPtr<zx_device_t>* out,
                        uint32_t flags);
// Note that device_close() is intended to consume a reference (logically, the
// one created by device_open).
zx_status_t device_close(fbl::RefPtr<zx_device_t> dev, uint32_t flags);

#endif  // SRC_DEVICES_DRIVER_HOST_ZX_DEVICE_H_
