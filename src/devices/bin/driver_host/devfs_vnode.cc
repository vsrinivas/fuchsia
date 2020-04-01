// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "devfs_vnode.h"

#include <ddk/device.h>
#include <fs/vfs_types.h>

#include "driver_host.h"

zx_status_t DevfsVnode::Open(fs::Vnode::ValidatedOptions options,
                             fbl::RefPtr<Vnode>* out_redirect) {
  fbl::RefPtr<zx_device_t> new_dev;
  zx_status_t status = device_open(dev_, &new_dev, options->ToIoV1Flags());
  if (status != ZX_OK) {
    return status;
  }
  if (new_dev != dev_) {
    *out_redirect = new_dev->vnode;
  }
  return ZX_OK;
}

zx_status_t DevfsVnode::Close() {
  zx_status_t status = device_close(dev_, 0);
  // If this vnode is for an instance device, drop its reference on close to break
  // the reference cycle.  This is handled for non-instance devices during the device
  // remove path.
  if (dev_->flags & DEV_FLAG_INSTANCE) {
    dev_->vnode.reset();
  }
  return status;
}

zx_status_t DevfsVnode::GetAttributes(fs::VnodeAttributes* a) {
  a->mode = V_TYPE_CDEV | V_IRUSR | V_IWUSR;
  a->content_size = dev_->GetSizeOp();
  a->link_count = 1;
  return ZX_OK;
}

fs::VnodeProtocolSet DevfsVnode::GetProtocols() const { return fs::VnodeProtocol::kDevice; }

zx_status_t DevfsVnode::GetNodeInfoForProtocol(fs::VnodeProtocol protocol, fs::Rights rights,
                                               fs::VnodeRepresentation* info) {
  if (protocol == fs::VnodeProtocol::kDevice) {
    zx::eventpair event;
    if (dev_->event.is_valid()) {
      zx_status_t status = dev_->event.duplicate(ZX_RIGHTS_BASIC, &event);
      if (status != ZX_OK) {
        return status;
      }
    }
    *info = fs::VnodeRepresentation::Device{std::move(event)};
    return ZX_OK;
  }
  return ZX_ERR_NOT_SUPPORTED;
}

void DevfsVnode::HandleFsSpecificMessage(fidl_msg_t* msg, fidl::Transaction* txn) {
  bool dispatched = llcpp::fuchsia::device::Controller::TryDispatch(this, msg, txn);
  if (dispatched) {
    return;
  }

  auto ddk_txn = MakeDdkInternalTransaction(txn);
  zx_status_t status = dev_->MessageOp(msg, ddk_txn.Txn());
  if (status != ZX_OK && status != ZX_ERR_ASYNC) {
    // Close the connection on any error
    txn->Close(status);
  }
}

zx_status_t DevfsVnode::Read(void* data, size_t len, size_t off, size_t* out_actual) {
  return dev_->ReadOp(data, len, off, out_actual);
}
zx_status_t DevfsVnode::Write(const void* data, size_t len, size_t off, size_t* out_actual) {
  return dev_->WriteOp(data, len, off, out_actual);
}

void DevfsVnode::Bind(::fidl::StringView driver, BindCompleter::Sync completer) {
  char drv_libname[fuchsia_device_MAX_DRIVER_PATH_LEN + 1];
  memcpy(drv_libname, driver.data(), driver.size());
  drv_libname[driver.size()] = 0;

  zx_status_t status = device_bind(dev_, drv_libname);
  if (status != ZX_OK) {
    completer.ReplyError(status);
  } else {
    dev_->set_bind_conn([completer = completer.ToAsync()](zx_status_t status) mutable {
      if (status != ZX_OK) {
        completer.ReplyError(status);
      } else {
        completer.ReplySuccess();
      }
    });
  }
};

void DevfsVnode::GetDevicePerformanceStates(GetDevicePerformanceStatesCompleter::Sync completer) {
  auto& perf_states = dev_->GetPerformanceStates();
  ZX_DEBUG_ASSERT(perf_states.size() == fuchsia_device_MAX_DEVICE_PERFORMANCE_STATES);

  ::fidl::Array<::llcpp::fuchsia::device::DevicePerformanceStateInfo, 20> states{};
  for (size_t i = 0; i < fuchsia_device_MAX_DEVICE_PERFORMANCE_STATES; i++) {
    states[i] = perf_states[i];
  }
  completer.Reply(states, ZX_OK);
}

void DevfsVnode::GetCurrentPerformanceState(GetCurrentPerformanceStateCompleter::Sync completer) {
  completer.Reply(dev_->current_performance_state());
}

void DevfsVnode::Rebind(::fidl::StringView driver, RebindCompleter::Sync completer) {
  char drv_libname[fuchsia_device_MAX_DRIVER_PATH_LEN + 1];
  memcpy(drv_libname, driver.data(), driver.size());
  drv_libname[driver.size()] = 0;

  dev_->set_rebind_drv_name(drv_libname);
  zx_status_t status = device_rebind(dev_.get());

  if (status != ZX_OK) {
    completer.ReplyError(status);
  } else {
    // These will be set, until device is unbound and then bound again.
    dev_->set_rebind_conn([completer = completer.ToAsync()](zx_status_t status) mutable {
      if (status != ZX_OK) {
        completer.ReplyError(status);
      } else {
        completer.ReplySuccess();
      }
    });
  }
}

void DevfsVnode::UnbindChildren(UnbindChildrenCompleter::Sync completer) {
  zx_status_t status = device_schedule_unbind_children(dev_);

  if (status != ZX_OK) {
    completer.ReplyError(status);
  } else {
    // The unbind conn will be set until all the children of this device are unbound.
    dev_->set_unbind_children_conn([completer = completer.ToAsync()](zx_status_t status) mutable {
      if (status != ZX_OK) {
        completer.ReplyError(status);
      } else {
        completer.ReplySuccess();
      }
    });
  }
}

void DevfsVnode::ScheduleUnbind(ScheduleUnbindCompleter::Sync completer) {
  zx_status_t status = device_schedule_remove(dev_, true /* unbind_self */);
  if (status != ZX_OK) {
    completer.ReplyError(status);
  } else {
    completer.ReplySuccess();
  }
}

void DevfsVnode::GetDriverName(GetDriverNameCompleter::Sync completer) {
  if (!dev_->driver) {
    return completer.Reply(ZX_ERR_NOT_SUPPORTED, {});
  }
  const char* name = dev_->driver->name();
  if (name == nullptr) {
    name = "unknown";
  }
  completer.Reply(ZX_OK, fidl::unowned_str(name, strlen(name)));
}

void DevfsVnode::GetDeviceName(GetDeviceNameCompleter::Sync completer) {
  completer.Reply({dev_->name, strlen(dev_->name)});
}

void DevfsVnode::GetTopologicalPath(GetTopologicalPathCompleter::Sync completer) {
  char buf[fuchsia_device_MAX_DEVICE_PATH_LEN + 1];
  size_t actual;
  zx_status_t status = dev_->driver_host_context()->GetTopoPath(dev_, buf, sizeof(buf), &actual);
  if (status != ZX_OK) {
    return completer.ReplyError(status);
  }
  if (actual > 0) {
    // Remove the accounting for the null byte
    actual--;
  }
  auto path = ::fidl::StringView(buf, actual);
  completer.ReplySuccess(std::move(path));
}

void DevfsVnode::GetEventHandle(GetEventHandleCompleter::Sync completer) {
  zx::eventpair event;
  zx_status_t status = dev_->event.duplicate(ZX_RIGHTS_BASIC, &event);
  static_assert(fuchsia_device_DEVICE_SIGNAL_READABLE == DEV_STATE_READABLE);
  static_assert(fuchsia_device_DEVICE_SIGNAL_WRITABLE == DEV_STATE_WRITABLE);
  static_assert(fuchsia_device_DEVICE_SIGNAL_ERROR == DEV_STATE_ERROR);
  static_assert(fuchsia_device_DEVICE_SIGNAL_HANGUP == DEV_STATE_HANGUP);
  static_assert(fuchsia_device_DEVICE_SIGNAL_OOB == DEV_STATE_OOB);
  // TODO(teisenbe): The FIDL definition erroneously describes this as an event rather than
  // eventpair.
  completer.Reply(status, zx::event(event.release()));
}

void DevfsVnode::GetDriverLogFlags(GetDriverLogFlagsCompleter::Sync completer) {
  if (!dev_->driver) {
    return completer.Reply(ZX_ERR_UNAVAILABLE, 0);
  }
  uint32_t flags = dev_->driver->driver_rec()->log_flags;
  completer.Reply(ZX_OK, flags);
}

void DevfsVnode::SetDriverLogFlags(uint32_t clear_flags, uint32_t set_flags,
                                   SetDriverLogFlagsCompleter::Sync completer) {
  if (!dev_->driver) {
    return completer.Reply(ZX_ERR_UNAVAILABLE);
  }
  uint32_t flags = dev_->driver->driver_rec()->log_flags;
  flags &= ~clear_flags;
  flags |= set_flags;
  dev_->driver->driver_rec()->log_flags = flags;
  completer.Reply(ZX_OK);
}

void DevfsVnode::RunCompatibilityTests(int64_t hook_wait_time,
                                       RunCompatibilityTestsCompleter::Sync completer) {
  auto status = device_run_compatibility_tests(dev_, hook_wait_time);
  if (status) {
    dev_->PushTestCompatibilityConn([completer = completer.ToAsync()](zx_status_t status) mutable {
      completer.Reply(std::move(status));
    });
  }
};

void DevfsVnode::GetDevicePowerCaps(GetDevicePowerCapsCompleter::Sync completer) {
  // For now, the result is always a successful response because the device itself is not added
  // without power states validation. In future, we may add more checks for validation, and the
  // error result will be put to use.
  ::llcpp::fuchsia::device::Controller_GetDevicePowerCaps_Response response{};
  auto& states = dev_->GetPowerStates();

  ZX_DEBUG_ASSERT(states.size() == fuchsia_device_MAX_DEVICE_POWER_STATES);
  for (size_t i = 0; i < fuchsia_device_MAX_DEVICE_POWER_STATES; i++) {
    response.dpstates[i] = states[i];
  }
  completer.Reply(::llcpp::fuchsia::device::Controller_GetDevicePowerCaps_Result::WithResponse(
      fidl::unowned_ptr(&response)));
};

void DevfsVnode::SetPerformanceState(uint32_t requested_state,
                                     SetPerformanceStateCompleter::Sync completer) {
  uint32_t out_state;
  zx_status_t status =
      dev_->driver_host_context()->DeviceSetPerformanceState(dev_, requested_state, &out_state);
  return completer.Reply(status, out_state);
}

void DevfsVnode::ConfigureAutoSuspend(bool enable,
                                      ::llcpp::fuchsia::device::DevicePowerState requested_state,
                                      ConfigureAutoSuspendCompleter::Sync completer) {
  zx_status_t status =
      dev_->driver_host_context()->DeviceConfigureAutoSuspend(dev_, enable, requested_state);
  return completer.Reply(status);
}

void DevfsVnode::UpdatePowerStateMapping(
    ::fidl::Array<::llcpp::fuchsia::device::SystemPowerStateInfo,
                  ::llcpp::fuchsia::device::manager::MAX_SYSTEM_POWER_STATES>
        mapping,
    UpdatePowerStateMappingCompleter::Sync completer) {
  std::array<::llcpp::fuchsia::device::SystemPowerStateInfo,
             ::llcpp::fuchsia::device::manager::MAX_SYSTEM_POWER_STATES>
      states_mapping;

  for (size_t i = 0; i < fuchsia_device_manager_MAX_SYSTEM_POWER_STATES; i++) {
    states_mapping[i] = mapping[i];
  }
  zx_status_t status = dev_->SetSystemPowerStateMapping(states_mapping);
  if (status != ZX_OK) {
    return completer.ReplyError(status);
  }

  fidl::aligned<::llcpp::fuchsia::device::Controller_UpdatePowerStateMapping_Response> response;
  completer.Reply(::llcpp::fuchsia::device::Controller_UpdatePowerStateMapping_Result::WithResponse(
      fidl::unowned_ptr(&response)));
}

void DevfsVnode::GetPowerStateMapping(GetPowerStateMappingCompleter::Sync completer) {
  ::llcpp::fuchsia::device::Controller_GetPowerStateMapping_Response response;

  auto& mapping = dev_->GetSystemPowerStateMapping();
  ZX_DEBUG_ASSERT(mapping.size() == fuchsia_device_manager_MAX_SYSTEM_POWER_STATES);

  for (size_t i = 0; i < fuchsia_device_manager_MAX_SYSTEM_POWER_STATES; i++) {
    response.mapping[i] = mapping[i];
  }
  completer.Reply(::llcpp::fuchsia::device::Controller_GetPowerStateMapping_Result::WithResponse(
      fidl::unowned_ptr(&response)));
};

void DevfsVnode::Suspend(::llcpp::fuchsia::device::DevicePowerState requested_state,
                         SuspendCompleter::Sync completer) {
  dev_->suspend_cb = [completer = completer.ToAsync()](zx_status_t status,
                                                       uint8_t out_state) mutable {
    completer.Reply(status, static_cast<::llcpp::fuchsia::device::DevicePowerState>(out_state));
  };
  dev_->driver_host_context()->DeviceSuspendNew(dev_, requested_state);
}

void DevfsVnode::Resume(ResumeCompleter::Sync completer) {
  dev_->resume_cb = [completer = completer.ToAsync()](zx_status_t status, uint8_t out_power_state,
                                                      uint32_t out_perf_state) mutable {
    completer.Reply(status,
                    static_cast<::llcpp::fuchsia::device::DevicePowerState>(out_power_state),
                    out_perf_state);
  };
  dev_->driver_host_context()->DeviceResumeNew(dev_);
}

namespace {

// Reply originating from driver.
zx_status_t DdkReply(fidl_txn_t* txn, const fidl_msg_t* msg) {
  fidl::Message fidl_msg(
      fidl::BytePart(reinterpret_cast<uint8_t*>(msg->bytes), msg->num_bytes, msg->num_bytes),
      fidl::HandlePart(msg->handles, msg->num_handles, msg->num_handles));

  // If FromDdkInternalTransaction returns a unique_ptr variant, it will be destroyed when exiting
  // this scope.
  auto fidl_txn = FromDdkInternalTransaction(ddk::internal::Transaction::FromTxn(txn));
  std::visit([&](auto&& arg) { arg->Reply(std::move(fidl_msg)); }, fidl_txn);
  return ZX_OK;
}

// Bitmask for checking if a pointer stashed in a ddk::internal::Transaction is from the heap or
// not. This is safe to use on our pointers, because fidl::Transactions are always aligned to more
// than one byte.
static_assert(alignof(fidl::Transaction) > 1);
constexpr uintptr_t kTransactionIsBoxed = 0x1;

}  // namespace

ddk::internal::Transaction MakeDdkInternalTransaction(fidl::Transaction* txn) {
  device_fidl_txn_t fidl_txn = {};
  fidl_txn.txn = {
      .reply = DdkReply,
  };
  fidl_txn.driver_host_context = reinterpret_cast<uintptr_t>(txn);
  return ddk::internal::Transaction(fidl_txn);
}

ddk::internal::Transaction MakeDdkInternalTransaction(std::unique_ptr<fidl::Transaction> txn) {
  device_fidl_txn_t fidl_txn = {};
  fidl_txn.txn = {
      .reply = DdkReply,
  };
  fidl_txn.driver_host_context = reinterpret_cast<uintptr_t>(txn.release()) | kTransactionIsBoxed;
  return ddk::internal::Transaction(fidl_txn);
}

std::variant<fidl::Transaction*, std::unique_ptr<fidl::Transaction>> FromDdkInternalTransaction(
    ddk::internal::Transaction* txn) {
  uintptr_t raw = txn->DriverHostCtx();
  ZX_ASSERT_MSG(raw != 0, "Reused a fidl_txn_t!\n");

  // Invalidate the source transaction
  txn->DeviceFidlTxn()->driver_host_context = 0;

  auto ptr = reinterpret_cast<fidl::Transaction*>(raw & ~kTransactionIsBoxed);
  if (raw & kTransactionIsBoxed) {
    return std::unique_ptr<fidl::Transaction>(ptr);
  }
  return ptr;
}
