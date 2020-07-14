// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_BIN_DRIVER_HOST_ZX_DEVICE_H_
#define SRC_DEVICES_BIN_DRIVER_HOST_ZX_DEVICE_H_

#include <fuchsia/device/llcpp/fidl.h>
#include <fuchsia/device/manager/c/fidl.h>
#include <fuchsia/device/manager/llcpp/fidl.h>
#include <lib/fit/function.h>
#include <lib/fit/result.h>
#include <lib/trace/event.h>
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
#include <fbl/string_buffer.h>
#include <fbl/vector.h>

#include "devfs_vnode.h"
#include "inspect.h"

class CompositeDevice;
class DeviceControllerConnection;
class DriverHostContext;
class DeviceInspect;
struct ProxyIostate;

// RAII object around async trace entries
class AsyncTrace {
 public:
  AsyncTrace(const char* category, const char* name)
      : category_(category), async_id_(TRACE_NONCE()) {
    label_.Append(name);
    TRACE_ASYNC_BEGIN(category, label_.data(), async_id_);
  }
  ~AsyncTrace() { finish(); }

  AsyncTrace(const AsyncTrace&) = delete;
  AsyncTrace& operator=(const AsyncTrace&) = delete;

  AsyncTrace(AsyncTrace&& other) { *this = std::move(other); }
  AsyncTrace& operator=(AsyncTrace&& other) {
    if (this != &other) {
      category_ = other.category_;
      label_ = std::move(other.label_);
      async_id_ = other.async_id_;
      other.label_.Clear();
    }
    return *this;
  }

  trace_async_id_t async_id() const { return async_id_; }

  // End the async trace immediately
  void finish() {
    if (!label_.empty()) {
      TRACE_ASYNC_END(category_, label_.data(), async_id_);
      label_.Clear();
    }
  }

 private:
  const char* category_;
  fbl::StringBuffer<32> label_;
  trace_async_id_t async_id_;
};

// 'MDEV'
#define DEV_MAGIC 0x4D444556

// Maximum number of dead devices to hold on the dead device list
// before we start free'ing the oldest when adding a new one.
constexpr size_t DEAD_DEVICE_MAX = 7;

// Tags used to manage the different containers a zx_device may exist in
namespace internal {
struct ZxDeviceChildrenListTag {};
struct ZxDeviceDeferListTag {};
struct ZxDeviceLocalIdMapTag {};
}  // namespace internal

// This needs to be a struct, not a class, to match the public definition
struct zx_device
    : public fbl::RefCountedUpgradeable<zx_device>,
      public fbl::Recyclable<zx_device>,
      public fbl::ContainableBaseClasses<
          fbl::TaggedDoublyLinkedListable<zx_device*, internal::ZxDeviceChildrenListTag>,
          fbl::TaggedDoublyLinkedListable<zx_device*, internal::ZxDeviceDeferListTag>,
          fbl::TaggedWAVLTreeContainable<fbl::RefPtr<zx_device>, internal::ZxDeviceLocalIdMapTag>> {
 private:
  using TraceLabelBuffer = fbl::StringBuffer<32>;

 public:
  using ChildrenListTag = internal::ZxDeviceChildrenListTag;
  using DeferListTag = internal::ZxDeviceDeferListTag;
  using LocalIdMapTag = internal::ZxDeviceLocalIdMapTag;

  ~zx_device() = default;

  zx_device(const zx_device&) = delete;
  zx_device& operator=(const zx_device&) = delete;

  // |ctx| must outlive |*out_dev|.  This is managed in the full binary by creating
  // the DriverHostContext in main() (having essentially a static lifetime).
  static zx_status_t Create(DriverHostContext* ctx, std::string name, zx_driver_t* driver,
                            fbl::RefPtr<zx_device>* out_dev);

  void CloseAllConnections();

  void InitOp() {
    TraceLabelBuffer trace_label;
    TRACE_DURATION("driver_host:driver-hooks", get_trace_label("init", &trace_label));
    Dispatch(ops_->init);
  }

  zx_status_t OpenOp(zx_device_t** dev_out, uint32_t flags) {
    TraceLabelBuffer trace_label;
    TRACE_DURATION("driver_host:driver-hooks", get_trace_label("open", &trace_label));
    return Dispatch(ops_->open, ZX_OK, dev_out, flags);
  }

  zx_status_t CloseOp(uint32_t flags) {
    TraceLabelBuffer trace_label;
    TRACE_DURATION("driver_host:driver-hooks", get_trace_label("close", &trace_label));
    return Dispatch(ops_->close, ZX_OK, flags);
  }

  void UnbindOp() {
    TraceLabelBuffer trace_label;
    TRACE_DURATION("driver_host:driver-hooks", get_trace_label("unbind", &trace_label));
    Dispatch(ops_->unbind);
  }

  void ReleaseOp() {
    TraceLabelBuffer trace_label;
    TRACE_DURATION("driver_host:driver-hooks", get_trace_label("release", &trace_label));
    Dispatch(ops_->release);
  }

  void SuspendNewOp(uint8_t requested_state, bool enable_wake, uint8_t suspend_reason) {
    TraceLabelBuffer trace_label;
    TRACE_DURATION("driver_host:driver-hooks", get_trace_label("suspend", &trace_label));
    Dispatch(ops_->suspend, requested_state, enable_wake, suspend_reason);
  }

  zx_status_t SetPerformanceStateOp(uint32_t requested_state, uint32_t* out_state) {
    TraceLabelBuffer trace_label;
    TRACE_DURATION("driver_host:driver-hooks",
                   get_trace_label("set_performance_state", &trace_label));
    return Dispatch(ops_->set_performance_state, ZX_ERR_NOT_SUPPORTED, requested_state, out_state);
  }

  zx_status_t ConfigureAutoSuspendOp(bool enable, uint8_t requested_state) {
    TraceLabelBuffer trace_label;
    TRACE_DURATION("driver_host:driver-hooks", get_trace_label("conf_auto_suspend", &trace_label));
    return Dispatch(ops_->configure_auto_suspend, ZX_ERR_NOT_SUPPORTED, enable, requested_state);
  }

  void ResumeNewOp(uint32_t requested_state) {
    TraceLabelBuffer trace_label;
    TRACE_DURATION("driver_host:driver-hooks", get_trace_label("resume", &trace_label));
    Dispatch(ops_->resume, requested_state);
  }

  zx_status_t ReadOp(void* buf, size_t count, zx_off_t off, size_t* actual) {
    TraceLabelBuffer trace_label;
    TRACE_DURATION("driver_host:driver-hooks", get_trace_label("read", &trace_label));
    inspect_->ReadOpStats().Update();
    return Dispatch(ops_->read, ZX_ERR_NOT_SUPPORTED, buf, count, off, actual);
  }

  zx_status_t WriteOp(const void* buf, size_t count, zx_off_t off, size_t* actual) {
    TraceLabelBuffer trace_label;
    TRACE_DURATION("driver_host:driver-hooks", get_trace_label("write", &trace_label));
    inspect_->WriteOpStats().Update();
    return Dispatch(ops_->write, ZX_ERR_NOT_SUPPORTED, buf, count, off, actual);
  }

  zx_off_t GetSizeOp() {
    TraceLabelBuffer trace_label;
    TRACE_DURATION("driver_host:driver-hooks", get_trace_label("get_size", &trace_label));
    return Dispatch(ops_->get_size, 0lu);
  }

  zx_status_t MessageOp(fidl_msg_t* msg, fidl_txn_t* txn) {
    TraceLabelBuffer trace_label;
    TRACE_DURATION("driver_host:driver-hooks", get_trace_label("message", &trace_label));
    inspect_->MessageOpStats().Update();
    return Dispatch(ops_->message, ZX_ERR_NOT_SUPPORTED, msg, txn);
  }

  void ChildPreReleaseOp(void* child_ctx) {
    TraceLabelBuffer trace_label;
    TRACE_DURATION("driver_host:driver-hooks", get_trace_label("child_pre_release", &trace_label));
    Dispatch(ops_->child_pre_release, child_ctx);
  }

  void set_bind_conn(fit::callback<void(zx_status_t)>);
  fit::callback<void(zx_status_t)> take_bind_conn();

  void set_rebind_conn(fit::callback<void(zx_status_t)>);
  fit::callback<void(zx_status_t)> take_rebind_conn();
  void set_rebind_drv_name(const char* drv_name);
  std::optional<std::string> get_rebind_drv_name() { return rebind_drv_name_; }

  void set_unbind_children_conn(fit::callback<void(zx_status_t)>);
  fit::callback<void(zx_status_t)> take_unbind_children_conn();

  void PushTestCompatibilityConn(fit::callback<void(zx_status_t)>);
  fit::callback<void(zx_status_t)> PopTestCompatibilityConn();

  // Check if this driver_host has a device with the given ID, and if so returns a
  // reference to it.
  static fbl::RefPtr<zx_device> GetDeviceFromLocalId(uint64_t local_id);

  uint64_t local_id() const { return local_id_; }
  void set_local_id(uint64_t id);

  uintptr_t magic = DEV_MAGIC;

  // reserved for driver use; will not be touched by devmgr
  void* ctx = nullptr;

  const zx_protocol_device_t* ops() const { return ops_; }
  void set_ops(const zx_protocol_device_t* ops) {
    ops_ = ops;
    inspect_->set_ops(ops);
  }

  uint32_t flags() const { return flags_; }
  void set_flag(uint32_t flag) {
    flags_ |= flag;
    inspect_->set_flags(flags_);
  }
  void unset_flag(uint32_t flag) {
    flags_ &= ~flag;
    inspect_->set_flags(flags_);
  }

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
  fit::callback<void(zx_status_t, uint8_t, uint32_t)> resume_cb;

  // most devices implement a single
  // protocol beyond the base device protocol
  uint32_t protocol_id() const { return protocol_id_; }

  void set_protocol_id(uint32_t protocol_id) {
    protocol_id_ = protocol_id;
    inspect_->set_protocol_id(protocol_id);
  }
  void* protocol_ops = nullptr;

  // driver that has published this device
  zx_driver_t* driver = nullptr;

  const fbl::RefPtr<zx_device_t>& parent() const { return parent_; }
  void set_parent(fbl::RefPtr<zx_device_t> parent) {
    parent_ = parent;
    inspect_->set_parent(parent);
  }

  void add_child(zx_device* child);
  void remove_child(zx_device& child);
  const fbl::TaggedDoublyLinkedList<zx_device*, ChildrenListTag>& children() { return children_; }

  // This is an atomic so that the connection's async loop can inspect this
  // value to determine if an expected shutdown is happening.  See comments in
  // DriverManagerRemove().
  std::atomic<DeviceControllerConnection*> conn = nullptr;
  // Actual type is DevfsVnode.  Needs to be fs::Vnode to break header cycle
  fbl::RefPtr<fs::Vnode> vnode;

  fbl::Mutex proxy_ios_lock;
  ProxyIostate* proxy_ios TA_GUARDED(proxy_ios_lock) = nullptr;

  const char* name() const { return name_; }

  // Trait structure for the local ID map
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
  void set_composite(fbl::RefPtr<CompositeDevice> composite);
  fbl::RefPtr<CompositeDevice> take_composite();

  const DevicePowerStates& GetPowerStates() const;
  const PerformanceStates& GetPerformanceStates() const;

  zx_status_t SetPowerStates(const device_power_state_info_t* power_states, uint8_t count);

  bool IsPowerStateSupported(::llcpp::fuchsia::device::DevicePowerState requested_state) {
    // requested_state is bounded by the enum.
    return power_states_[static_cast<uint8_t>(requested_state)].is_supported;
  }

  bool IsPerformanceStateSupported(uint32_t requested_state);
  bool auto_suspend_configured() { return auto_suspend_configured_; }
  void set_auto_suspend_configured(bool value) {
    auto_suspend_configured_ = value;
    inspect_->set_auto_suspend(value);
  }

  zx_status_t SetSystemPowerStateMapping(const SystemPowerStateMapping& mapping);

  zx_status_t SetPerformanceStates(const device_performance_state_info_t* performance_states,
                                   uint8_t count);

  const SystemPowerStateMapping& GetSystemPowerStateMapping() const;

  uint32_t current_performance_state() { return current_performance_state_; }

  void set_current_performance_state(uint32_t state) {
    current_performance_state_ = state;
    inspect_->set_current_performance_state(state);
  }

  // Begin an async tracing entry for this device.  It will have the given category, and the name
  // "<device_name>:<tag>".
  AsyncTrace BeginAsyncTrace(const char* category, const char* tag) {
    TraceLabelBuffer name;
    get_trace_label(tag, &name);
    return AsyncTrace(category, name.data());
  }

  bool Unbound();

  DriverHostContext* driver_host_context() const { return driver_host_context_; };
  bool complete_bind_rebind_after_init() const { return complete_bind_rebind_after_init_; }
  void set_complete_bind_rebind_after_init(bool value) { complete_bind_rebind_after_init_ = value; }

  DeviceInspect& inspect() { return *inspect_; }
  void FreeInspect() { inspect_.reset(); }

 private:
  explicit zx_device(DriverHostContext* ctx, std::string name, zx_driver_t* driver);

  char name_[ZX_DEVICE_NAME_MAX + 1] = {};

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

  // Utility for getting a label for tracing that identifies this device
  template <size_t N>
  const char* get_trace_label(const char* label, fbl::StringBuffer<N>* out) const {
    out->Clear();
    out->AppendPrintf("%s:%s", this->name(), label);
    return out->data();
  }

  // If this device is a fragment of a composite, this points to the
  // composite control structure.
  fbl::RefPtr<CompositeDevice> composite_;

  // Identifier assigned by devmgr that can be used to assemble composite
  // devices.
  uint64_t local_id_ = 0;

  uint32_t flags_ = 0;

  const zx_protocol_device_t* ops_ = nullptr;

  uint32_t protocol_id_ = 0;

  // parent in the device tree
  fbl::RefPtr<zx_device_t> parent_;

  // list of this device's children in the device tree
  fbl::TaggedDoublyLinkedList<zx_device*, ChildrenListTag> children_;

  fbl::Mutex bind_conn_lock_;

  fit::callback<void(zx_status_t)> bind_conn_ TA_GUARDED(bind_conn_lock_);

  fbl::Mutex rebind_conn_lock_;

  fit::callback<void(zx_status_t)> rebind_conn_ TA_GUARDED(rebind_conn_lock_);
  bool complete_bind_rebind_after_init_ = false;

  fbl::Mutex unbind_children_conn_lock_;

  fit::callback<void(zx_status_t)> unbind_children_conn_ TA_GUARDED(unbind_children_conn_lock_);

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

  DriverHostContext* const driver_host_context_;
  std::optional<DeviceInspect> inspect_;
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
#define DEV_FLAG_UNBINDABLE            0x00000004  // nobody may autobind to this device
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
zx_status_t device_schedule_unbind_children(const fbl::RefPtr<zx_device_t>& dev);
zx_status_t device_schedule_remove(const fbl::RefPtr<zx_device_t>& dev, bool unbind_self);
zx_status_t device_run_compatibility_tests(const fbl::RefPtr<zx_device_t>& dev,
                                           int64_t hook_wait_time);
zx_status_t device_open(const fbl::RefPtr<zx_device_t>& dev, fbl::RefPtr<zx_device_t>* out,
                        uint32_t flags);
// Note that device_close() is intended to consume a reference (logically, the
// one created by device_open).
zx_status_t device_close(fbl::RefPtr<zx_device_t> dev, uint32_t flags);

#endif  // SRC_DEVICES_BIN_DRIVER_HOST_ZX_DEVICE_H_
