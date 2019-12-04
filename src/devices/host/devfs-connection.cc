// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "devfs-connection.h"

#include <fuchsia/io/c/fidl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zircon/assert.h>
#include <zircon/syscalls.h>

#include <src/storage/deprecated-fs-fidl-handler/fidl-handler.h>

#include "devhost.h"
#include "log.h"
#include "zircon/system/ulib/fbl/include/fbl/ref_ptr.h"

namespace devmgr {

namespace {

zx_status_t Reply(fidl_txn_t* txn, const fidl_msg_t* msg) {
  auto* connection = Connection::FromTxn(txn);

  if (connection->devfs_connection()->last_txid == connection->Txid()) {
    connection->devfs_connection()->reply_called = true;
  }

  auto header = reinterpret_cast<fidl_message_header_t*>(msg->bytes);
  header->txid = connection->Txid();

  return connection->channel()->write(0, msg->bytes, msg->num_bytes, msg->handles,
                                      msg->num_handles);
}

// Reply originating from driver.
zx_status_t DdkReply(fidl_txn_t* txn, const fidl_msg_t* msg) {
  auto connection = Connection(ddk::Connection::FromTxn(txn));
  return Reply(connection.Txn(), msg);
}

// Don't actually send anything on a channel when completing this operation.
// This is useful for testing "close" requests.
zx_status_t NullReply(fidl_txn_t* reply, const fidl_msg_t* msg) { return ZX_OK; }

}  // namespace

Connection::Connection(fidl_txn_t txn, zx_txid_t txid, fbl::RefPtr<DevfsConnection> conn)
    : txn_(std::move(txn)), txid_(std::move(txid)), conn_(conn) {
  conn_->dev->outstanding_transactions++;
}

Connection::Connection(const Connection& other)
    : txn_(other.txn_), txid_(other.txid_), conn_(other.conn_) {
  conn_->dev->outstanding_transactions++;
}

Connection& Connection::operator=(const Connection& other) {
  txn_ = other.txn_;
  txid_ = other.txid_;
  conn_ = other.conn_;

  conn_->dev->outstanding_transactions++;
  return *this;
}

Connection::Connection(const ddk::Connection* conn)
    : txn_(*conn->Txn()),
      txid_(conn->Txid()),
      conn_(fbl::ImportFromRawPtr(reinterpret_cast<DevfsConnection*>(conn->DevhostContext()))) {}

Connection::~Connection() {
  if (conn_.get() != nullptr) {
    conn_->dev->outstanding_transactions--;
  }
}

ddk::Connection Connection::ToDdkConnection() {
  fidl_txn_t txn = {
      .reply = DdkReply,
  };

  ZX_ASSERT(conn_.get() != nullptr);
  auto internal = reinterpret_cast<uintptr_t>(fbl::ExportToRawPtr(&conn_));

  return ddk::Connection(txn, txid_, internal);
}

void DevfsConnection::Bind(::fidl::StringView driver, BindCompleter::Sync completer) {
  char drv_libname[fuchsia_device_MAX_DRIVER_PATH_LEN + 1];
  memcpy(drv_libname, driver.data(), driver.size());
  drv_libname[driver.size()] = 0;

  zx_status_t status = device_bind(dev, drv_libname);
  if (status != ZX_OK) {
    completer.ReplyError(status);
  } else {
    dev->set_bind_conn([completer = completer.ToAsync()](zx_status_t status) mutable {
      if (status != ZX_OK) {
        completer.ReplyError(status);
      } else {
        completer.ReplySuccess();
      }
    });
  }
};

void DevfsConnection::GetDevicePerformanceStates(
    GetDevicePerformanceStatesCompleter::Sync completer) {
  auto& perf_states = dev->GetPerformanceStates();
  ZX_DEBUG_ASSERT(perf_states.size() == fuchsia_device_MAX_DEVICE_PERFORMANCE_STATES);

  ::fidl::Array<::llcpp::fuchsia::device::DevicePerformanceStateInfo, 20> states{};
  for (size_t i = 0; i < fuchsia_device_MAX_DEVICE_PERFORMANCE_STATES; i++) {
    states[i] = perf_states[i];
  }
  completer.Reply(states, ZX_OK);
}

void DevfsConnection::Rebind(::fidl::StringView driver, RebindCompleter::Sync completer) {
  char drv_libname[fuchsia_device_MAX_DRIVER_PATH_LEN + 1];
  memcpy(drv_libname, driver.data(), driver.size());
  drv_libname[driver.size()] = 0;

  dev->set_rebind_drv_name(drv_libname);
  zx_status_t status = device_rebind(dev.get());

  if (status != ZX_OK) {
    completer.ReplyError(status);
  } else {
    // These will be set, until device is unbound and then bound again.
    dev->set_rebind_conn([completer = completer.ToAsync()](zx_status_t status) mutable {
      if (status != ZX_OK) {
        completer.ReplyError(status);
      } else {
        completer.ReplySuccess();
      }
    });
  }
}

void DevfsConnection::ScheduleUnbind(ScheduleUnbindCompleter::Sync completer) {
  zx_status_t status = device_schedule_remove(this->dev, true /* unbind_self */);
  if (status != ZX_OK) {
    completer.ReplyError(status);
  } else {
    completer.ReplySuccess();
  }
}

void DevfsConnection::GetDriverName(GetDriverNameCompleter::Sync completer) {
  if (!this->dev->driver) {
    return completer.Reply(ZX_ERR_NOT_SUPPORTED, {});
  }
  const char* name = this->dev->driver->name();
  if (name == nullptr) {
    name = "unknown";
  }
  completer.Reply(ZX_OK, {name, strlen(name)});
}

void DevfsConnection::GetDeviceName(GetDeviceNameCompleter::Sync completer) {
  completer.Reply({this->dev->name, strlen(this->dev->name)});
}

void DevfsConnection::GetTopologicalPath(GetTopologicalPathCompleter::Sync completer) {
  char buf[fuchsia_device_MAX_DEVICE_PATH_LEN + 1];
  size_t actual;
  zx_status_t status = devhost_get_topo_path(this->dev, buf, sizeof(buf), &actual);
  if (status != ZX_OK) {
    return completer.ReplyError(status);
  }
  if (actual > 0) {
    // Remove the accounting for the null byte
    actual--;
  }
  auto path = ::fidl::StringView(buf, actual);
  completer.ReplySuccess(path);
}

void DevfsConnection::GetEventHandle(GetEventHandleCompleter::Sync completer) {
  zx::eventpair event;
  zx_status_t status = this->dev->event.duplicate(ZX_RIGHTS_BASIC, &event);
  static_assert(fuchsia_device_DEVICE_SIGNAL_READABLE == DEV_STATE_READABLE);
  static_assert(fuchsia_device_DEVICE_SIGNAL_WRITABLE == DEV_STATE_WRITABLE);
  static_assert(fuchsia_device_DEVICE_SIGNAL_ERROR == DEV_STATE_ERROR);
  static_assert(fuchsia_device_DEVICE_SIGNAL_HANGUP == DEV_STATE_HANGUP);
  static_assert(fuchsia_device_DEVICE_SIGNAL_OOB == DEV_STATE_OOB);
  // TODO(teisenbe): The FIDL definition erroneously describes this as an event rather than
  // eventpair.
  completer.Reply(status, zx::event(event.release()));
}

void DevfsConnection::GetDriverLogFlags(GetDriverLogFlagsCompleter::Sync completer) {
  if (!this->dev->driver) {
    return completer.Reply(ZX_ERR_UNAVAILABLE, 0);
  }
  uint32_t flags = this->dev->driver->driver_rec()->log_flags;
  completer.Reply(ZX_OK, flags);
}

void DevfsConnection::SetDriverLogFlags(uint32_t clear_flags, uint32_t set_flags,
                                        SetDriverLogFlagsCompleter::Sync completer) {
  if (!this->dev->driver) {
    return completer.Reply(ZX_ERR_UNAVAILABLE);
  }
  uint32_t flags = this->dev->driver->driver_rec()->log_flags;
  flags &= ~clear_flags;
  flags |= set_flags;
  this->dev->driver->driver_rec()->log_flags = flags;
  completer.Reply(ZX_OK);
}

void DevfsConnection::DebugSuspend(DebugSuspendCompleter::Sync completer) {
  completer.Reply(this->dev->SuspendOp(0));
}

void DevfsConnection::DebugResume(DebugResumeCompleter::Sync completer) {
  completer.Reply(this->dev->ResumeOp(0));
}

void DevfsConnection::RunCompatibilityTests(int64_t hook_wait_time,
                                            RunCompatibilityTestsCompleter::Sync completer) {
  auto status = device_run_compatibility_tests(dev, hook_wait_time);
  if (status) {
    dev->PushTestCompatibilityConn([completer = completer.ToAsync()](zx_status_t status) mutable {
      completer.Reply(std::move(status));
    });
  }
};

void DevfsConnection::GetDevicePowerCaps(GetDevicePowerCapsCompleter::Sync completer) {
  // For now, the result is always a successful response because the device itself is not added
  // without power states validation. In future, we may add more checks for validation, and the
  // error result will be put to use.
  ::llcpp::fuchsia::device::Controller_GetDevicePowerCaps_Response response{};
  auto& states = dev->GetPowerStates();

  ZX_DEBUG_ASSERT(states.size() == fuchsia_device_MAX_DEVICE_POWER_STATES);
  for (size_t i = 0; i < fuchsia_device_MAX_DEVICE_POWER_STATES; i++) {
    response.dpstates[i] = states[i];
  }
  completer.Reply(::llcpp::fuchsia::device::Controller_GetDevicePowerCaps_Result::WithResponse(
      &response));
};

void DevfsConnection::SetPerformanceState(uint32_t requested_state,
                                          SetPerformanceStateCompleter::Sync completer) {
  uint32_t out_state;
  zx_status_t status = devhost_device_set_performance_state(dev, requested_state, &out_state);
  return completer.Reply(status, out_state);
}

void DevfsConnection::ConfigureAutoSuspend(
    bool enable, ::llcpp::fuchsia::device::DevicePowerState requested_state,
    ConfigureAutoSuspendCompleter::Sync completer) {
  zx_status_t status = devhost_device_configure_auto_suspend(dev, enable, requested_state);
  return completer.Reply(status);
}

void DevfsConnection::UpdatePowerStateMapping(
    ::fidl::Array<::llcpp::fuchsia::device::SystemPowerStateInfo,
                  ::llcpp::fuchsia::device::manager::MAX_SYSTEM_POWER_STATES>
        mapping,
    UpdatePowerStateMappingCompleter::Sync completer) {
  ::llcpp::fuchsia::device::Controller_UpdatePowerStateMapping_Response response;

  std::array<::llcpp::fuchsia::device::SystemPowerStateInfo,
             ::llcpp::fuchsia::device::manager::MAX_SYSTEM_POWER_STATES>
      states_mapping;

  for (size_t i = 0; i < fuchsia_device_manager_MAX_SYSTEM_POWER_STATES; i++) {
    states_mapping[i] = mapping[i];
  }
  zx_status_t status = dev->SetSystemPowerStateMapping(states_mapping);
  if (status != ZX_OK) {
    return completer.Reply(
        ::llcpp::fuchsia::device::Controller_UpdatePowerStateMapping_Result::WithErr(
            &status));
  }
  completer.Reply(::llcpp::fuchsia::device::Controller_UpdatePowerStateMapping_Result::WithResponse(
      &response));
}

void DevfsConnection::GetPowerStateMapping(GetPowerStateMappingCompleter::Sync completer) {
  ::llcpp::fuchsia::device::Controller_GetPowerStateMapping_Response response;

  auto& mapping = dev->GetSystemPowerStateMapping();
  ZX_DEBUG_ASSERT(mapping.size() == fuchsia_device_manager_MAX_SYSTEM_POWER_STATES);

  for (size_t i = 0; i < fuchsia_device_manager_MAX_SYSTEM_POWER_STATES; i++) {
    response.mapping[i] = mapping[i];
  }
  completer.Reply(::llcpp::fuchsia::device::Controller_GetPowerStateMapping_Result::WithResponse(
      &response));
};

void DevfsConnection::Suspend(::llcpp::fuchsia::device::DevicePowerState requested_state,
                              SuspendCompleter::Sync completer) {
  ::llcpp::fuchsia::device::DevicePowerState out_state;
  zx_status_t status = devhost_device_suspend_new(dev, requested_state, &out_state);
  completer.Reply(status, out_state);
}

void DevfsConnection::Resume(::llcpp::fuchsia::device::DevicePowerState requested_state,
                             ResumeCompleter::Sync completer) {
  ::llcpp::fuchsia::device::DevicePowerState out_state;
  zx_status_t status = devhost_device_resume_new(this->dev, requested_state, &out_state);
  if (status != ZX_OK) {
    return completer.Reply(
        ::llcpp::fuchsia::device::Controller_Resume_Result::WithErr(&status));
  }

  ::llcpp::fuchsia::device::Controller_Resume_Response response;
  response.out_state = out_state;
  completer.Reply(
      ::llcpp::fuchsia::device::Controller_Resume_Result::WithResponse(&response));
}

void DevfsConnection::HandleRpc(fbl::RefPtr<DevfsConnection>&& conn,
                                async_dispatcher_t* dispatcher, async::WaitBase* wait,
                                zx_status_t status, const zx_packet_signal_t* signal) {
  if (status != ZX_OK) {
    log(ERROR, "devhost: devfs conn wait error: %d\n", status);
    return;
  }

  if (signal->observed & ZX_CHANNEL_READABLE) {
    status = conn->ReadMessage([&conn](fidl_msg_t* msg, Connection* txn) {
      return devhost_fidl_handler(msg, txn->Txn(), conn.get());
    });
    if (status == ZX_OK) {
      // Stop accepting new requests once we are unbound.
      if (!(conn->dev->flags & DEV_FLAG_UNBOUND)) {
        BeginWait(std::move(conn), dispatcher);
      }
      return;
    }
  } else if (signal->observed & ZX_CHANNEL_PEER_CLOSED) {
    conn->CloseMessage([&conn](fidl_msg_t* msg, Connection* txn) {
      return devhost_fidl_handler(msg, txn->Txn(), conn.get());
    });
  } else {
    printf("dh_handle_fidl_rpc: invalid signals %x\n", signal->observed);
    abort();
  }

  // We arrive here if devhost_fidl_handler was a clean close (ERR_DISPATCHER_DONE),
  // or close-due-to-error (non-ZX_OK), or if the channel was closed
  // out from under us.  In all cases, we are done with this connection, so we
  // will destroy it by letting it leave scope.
  log(TRACE, "devhost: destroying devfs conn %p\n", conn.get());
}

zx_status_t DevfsConnection::ReadMessage(FidlDispatchFunction dispatch) {
  ZX_ASSERT(channel()->get_info(ZX_INFO_HANDLE_VALID, nullptr, 0, nullptr, nullptr) == ZX_OK);
  uint8_t bytes[ZXFIDL_MAX_MSG_BYTES];
  zx_handle_t handles[ZXFIDL_MAX_MSG_HANDLES];
  fidl_msg_t msg = {
      .bytes = bytes,
      .handles = handles,
      .num_bytes = 0,
      .num_handles = 0,
  };

  zx_status_t r = channel()->read(0, bytes, handles, countof(bytes), countof(handles),
                                  &msg.num_bytes, &msg.num_handles);
  if (r != ZX_OK) {
    return r;
  }

  if (msg.num_bytes < sizeof(fidl_message_header_t)) {
    zx_handle_close_many(msg.handles, msg.num_handles);
    return ZX_ERR_IO;
  }

  auto header = reinterpret_cast<fidl_message_header_t*>(msg.bytes);
  fidl_txn_t txn = {
      .reply = Reply,
  };
  Connection connection(txn, header->txid, fbl::RefPtr(this));

  this->last_txid = header->txid;
  this->reply_called = false;

  // Callback is responsible for decoding the message, and closing
  // any associated handles.
  r = dispatch(&msg, &connection);

  if (r != ZX_OK && r != ZX_ERR_ASYNC && !this->reply_called) {
    // The transaction wasn't handed back to us, so we must manually remove reference count to
    // prevent leak.
    log(TRACE, "devhost: Reply not called! Manually decrementing refcount.\n");
    ZX_ASSERT(Release() == false);
    dev->outstanding_transactions--;
  }

  return (r == ZX_ERR_ASYNC) ? ZX_OK : r;
}

zx_status_t DevfsConnection::CloseMessage(FidlDispatchFunction dispatch) {
  fuchsia_io_NodeCloseRequest request;
  memset(&request, 0, sizeof(request));
  fidl_init_txn_header(&request.hdr, 0, fuchsia_io_NodeCloseGenOrdinal);
  fidl_msg_t msg = {
      .bytes = &request,
      .handles = NULL,
      .num_bytes = sizeof(request),
      .num_handles = 0u,
  };

  fidl_txn_t txn = {
      .reply = NullReply,
  };
  Connection connection(txn, 0, fbl::RefPtr(this));

  // Remote side was closed.
  dispatch(&msg, &connection);
  return ERR_DISPATCHER_DONE;
}

}  // namespace devmgr
