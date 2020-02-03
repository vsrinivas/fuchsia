// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "device-controller-connection.h"

#include <fuchsia/device/c/fidl.h>
#include <fuchsia/io/llcpp/fidl.h>
#include <lib/zx/vmo.h>
#include <zircon/status.h>

#include <fbl/auto_lock.h>

#include "connection-destroyer.h"
#include "devhost.h"
#include "env.h"
#include "fidl_txn.h"
#include "fuchsia/device/llcpp/fidl.h"
#include "log.h"
#include "proxy-iostate.h"
#include "zx-device.h"

namespace {

namespace fuchsia = ::llcpp::fuchsia;

// Handles outstanding calls to fuchsia.device.manager.DeviceController/BindDriver
// and fuchsia.device.Controller/Bind.
void BindReply(const fbl::RefPtr<zx_device_t>& dev,
               DeviceControllerConnection::BindDriverCompleter::Sync completer, zx_status_t status,
               zx::channel test_output = zx::channel()) {
  completer.Reply(status, std::move(test_output));

  if (auto bind_conn = dev->take_bind_conn(); bind_conn) {
    bind_conn(status);
  }

  if (auto rebind_conn = dev->take_rebind_conn(); rebind_conn) {
    rebind_conn(status);
  }
}

}  // namespace

void DeviceControllerConnection::CompleteCompatibilityTests(
    llcpp::fuchsia::device::manager::CompatibilityTestStatus status,
    CompleteCompatibilityTestsCompleter::Sync _completer) {
  if (auto compat_conn = dev()->PopTestCompatibilityConn(); compat_conn) {
    compat_conn(static_cast<zx_status_t>(status));
  }
}

void DeviceControllerConnection::Init(InitCompleter::Sync completer) {
  ZX_ASSERT(this->dev()->init_cb == nullptr);
  this->dev()->init_cb = [completer = completer.ToAsync()](zx_status_t status) mutable {
    completer.Reply(status);
  };
  ApiAutoLock lock;
  devhost_device_init(this->dev());
}

void DeviceControllerConnection::Suspend(uint32_t flags, SuspendCompleter::Sync completer) {
  ZX_ASSERT(this->dev()->suspend_cb == nullptr);
  this->dev()->suspend_cb = [completer = completer.ToAsync()](zx_status_t status,
                                                              uint8_t out_state) mutable {
    if (status == ZX_ERR_NOT_SUPPORTED) {
      status = ZX_OK;
    }
    completer.Reply(status);
  };
  ApiAutoLock lock;
  devhost_device_system_suspend(this->dev(), flags);
}

void DeviceControllerConnection::Resume(uint32_t target_system_state,
                                        ResumeCompleter::Sync completer) {
  ZX_ASSERT(this->dev()->resume_cb == nullptr);
  this->dev()->resume_cb = [completer = completer.ToAsync()](zx_status_t status,
                                                             uint8_t out_power_state,
                                                             uint32_t out_perf_state) mutable {
    if (status == ZX_ERR_NOT_SUPPORTED) {
      status = ZX_OK;
    }
    if (status != ZX_OK &&
        (out_power_state ==
         static_cast<uint8_t>(llcpp::fuchsia::device::DevicePowerState::DEVICE_POWER_STATE_D0))) {
      // Do not fail system resume, when the device is unable to go into a particular performance
      // state, but resumed to a working state.
      status = ZX_OK;
    }
    completer.Reply(status);
  };
  ApiAutoLock lock;
  devhost_device_system_resume(this->dev(), target_system_state);
}

void DeviceControllerConnection::ConnectProxy(::zx::channel shadow,
                                              ConnectProxyCompleter::Sync _completer) {
  log(RPC_SDW, "devhost connect proxy rpc\n");
  this->dev()->ops->rxrpc(this->dev()->ctx, ZX_HANDLE_INVALID);
  // Ignore any errors in the creation for now?
  // TODO(teisenbe): Investigate if this is the right thing
  ProxyIostate::Create(this->dev(), std::move(shadow), DevhostAsyncLoop()->dispatcher());
}

void DeviceControllerConnection::BindDriver(::fidl::StringView driver_path_view, zx::vmo driver,
                                            BindDriverCompleter::Sync completer) {
  const auto& dev = this->dev();
  fbl::StringPiece driver_path(driver_path_view.data(), driver_path_view.size());

  // get path
  char buffer[512];
  const char* path = mkdevpath(dev, buffer, sizeof(buffer));

  // TODO: api lock integration
  log(ERROR, "devhost[%s] bind driver '%.*s'\n", path, static_cast<int>(driver_path.size()),
      driver_path.data());
  fbl::RefPtr<zx_driver_t> drv;
  if (dev->flags & DEV_FLAG_DEAD) {
    log(ERROR, "devhost[%s] bind to removed device disallowed\n", path);
    BindReply(dev, std::move(completer), ZX_ERR_IO_NOT_PRESENT);
    return;
  }

  zx_status_t r;
  if ((r = dh_find_driver(driver_path, std::move(driver), &drv)) < 0) {
    log(ERROR, "devhost[%s] driver load failed: %d\n", path, r);
    BindReply(dev, std::move(completer), r);
    return;
  }

  // Check for driver test flags.
  bool tests_default = getenv_bool("driver.tests.enable", false);
  char tmp[128];
  snprintf(tmp, sizeof(tmp), "driver.%s.tests.enable", drv->name());
  zx::channel test_output;
  if (getenv_bool(tmp, tests_default) && drv->has_run_unit_tests_op()) {
    zx::channel test_input;
    zx::channel::create(0, &test_input, &test_output);
    bool tests_passed = drv->RunUnitTestsOp(dev, std::move(test_input));
    if (!tests_passed) {
      log(ERROR, "driver_host: driver '%s' unit tests failed\n", drv->name());
      drv->set_status(ZX_ERR_BAD_STATE);
      BindReply(dev, std::move(completer), ZX_ERR_BAD_STATE, std::move(test_output));
      return;
    }
    log(INFO, "driver_host: driver '%s' unit tests passed\n", drv->name());
  }

  if (drv->has_bind_op()) {
    BindContext bind_ctx = {
        .parent = dev,
        .child = nullptr,
    };
    r = drv->BindOp(&bind_ctx, dev);

    if ((r == ZX_OK) && (bind_ctx.child == nullptr)) {
      printf("driver_host: WARNING: driver '%.*s' did not add device in bind()\n",
             static_cast<int>(driver_path.size()), driver_path.data());
    }
    if (r != ZX_OK) {
      log(ERROR, "devhost[%s] bind driver '%.*s' failed: %d\n", path,
          static_cast<int>(driver_path.size()), driver_path.data(), r);
    }
    BindReply(dev, std::move(completer), r, std::move(test_output));
    return;
  }

  if (!drv->has_create_op()) {
    log(ERROR, "devhost[%s] neither create nor bind are implemented: '%.*s'\n", path,
        static_cast<int>(driver_path.size()), driver_path.data());
  }
  BindReply(dev, std::move(completer), ZX_ERR_NOT_SUPPORTED, std::move(test_output));
}

void DeviceControllerConnection::Unbind(UnbindCompleter::Sync completer) {
  ZX_ASSERT(this->dev()->unbind_cb == nullptr);
  this->dev()->unbind_cb = [dev = this->dev(),
                            completer = completer.ToAsync()](zx_status_t status) mutable {
    llcpp::fuchsia::device::manager::DeviceController_Unbind_Result result;
    llcpp::fuchsia::device::manager::DeviceController_Unbind_Response response;
    if (status != ZX_OK && dev->parent) {
      // If unbind returns an error, and if client is waiting for unbind to complete,
      // inform the client.
      if (auto unbind_children_conn = dev->parent->take_unbind_children_conn();
          unbind_children_conn) {
        unbind_children_conn(status);
      }
    }
    result.set_response(&response);
    completer.Reply(std::move(result));
  };
  ApiAutoLock lock;
  devhost_device_unbind(this->dev());
}

void DeviceControllerConnection::CompleteRemoval(CompleteRemovalCompleter::Sync completer) {
  ZX_ASSERT(this->dev()->removal_cb == nullptr);
  this->dev()->removal_cb = [completer = completer.ToAsync()](zx_status_t status) mutable {
    llcpp::fuchsia::device::manager::DeviceController_CompleteRemoval_Result result;
    llcpp::fuchsia::device::manager::DeviceController_CompleteRemoval_Response response;
    result.set_response(&response);
    completer.Reply(std::move(result));
  };
  ApiAutoLock lock;
  devhost_device_complete_removal(this->dev());
}

DeviceControllerConnection::DeviceControllerConnection(fbl::RefPtr<zx_device> dev, zx::channel rpc,
                                                       zx::channel coordinator_rpc)
    : dev_(std::move(dev)) {
  dev_->rpc = zx::unowned_channel(rpc);
  dev_->coordinator_rpc = zx::unowned_channel(coordinator_rpc);
  dev_->conn.store(this);
  set_channel(std::move(rpc));
  set_coordinator_channel(std::move(coordinator_rpc));
}

DeviceControllerConnection::~DeviceControllerConnection() {
  // Ensure that the device has no dangling references to the resources we're
  // destroying.  This is safe because a device only ever has one associated
  // DeviceControllerConnection.
  dev_->conn.store(nullptr);
  dev_->rpc = zx::unowned_channel();
}

zx_status_t DeviceControllerConnection::Create(fbl::RefPtr<zx_device> dev,
                                               zx::channel controller_rpc,
                                               zx::channel coordinator_rpc,
                                               std::unique_ptr<DeviceControllerConnection>* conn) {
  *conn = std::make_unique<DeviceControllerConnection>(std::move(dev), std::move(controller_rpc),
                                                       std::move(coordinator_rpc));
  if (*conn == nullptr) {
    return ZX_ERR_NO_MEMORY;
  }
  return ZX_OK;
}

// Handler for when a io.fidl open() is called on a device
void DeviceControllerConnection::Open(uint32_t flags, uint32_t mode, ::fidl::StringView path,
                                      ::zx::channel object, OpenCompleter::Sync completer) {
  if (path.size() != 1 && path.data()[0] != '.') {
    log(ERROR, "driver_host: Tried to open path '%.*s'\n", static_cast<int>(path.size()),
        path.data());
  }
  devhost_device_connect(this->dev(), flags, std::move(object));
}

void DeviceControllerConnection::HandleRpc(std::unique_ptr<DeviceControllerConnection> conn,
                                           async_dispatcher_t* dispatcher, async::WaitBase* wait,
                                           zx_status_t status, const zx_packet_signal_t* signal) {
  if (status != ZX_OK) {
    log(ERROR, "driver_host: devcoord conn wait error: %d\n", status);
    return;
  }
  if (signal->observed & ZX_CHANNEL_READABLE) {
    zx_status_t r = conn->HandleRead();
    if (r != ZX_OK) {
      if (conn->dev_->conn.load() == nullptr && (r == ZX_ERR_INTERNAL || r == ZX_ERR_PEER_CLOSED)) {
        // Treat this as a PEER_CLOSED below.  It can happen if the
        // devcoordinator sent us a request while we asked the
        // devcoordinator to remove us.  The coordinator then closes the
        // channel before we can reply, and the FIDL bindings convert
        // the PEER_CLOSED on zx_channel_write() to a ZX_ERR_INTERNAL.  See ZX-4114.
        __UNUSED auto r = conn.release();
        return;
      }
      log(ERROR, "driver_host: devmgr rpc unhandleable ios=%p r=%d. fatal.\n", conn.get(), r);
      abort();
    }
    BeginWait(std::move(conn), dispatcher);
    return;
  }
  if (signal->observed & ZX_CHANNEL_PEER_CLOSED) {
    // Check if we were expecting this peer close.  If not, this could be a
    // serious bug.
    if (conn->dev_->conn.load() == nullptr) {
      // We're in the middle of shutting down, so just stop processing
      // signals and wait for the queued shutdown packet.  It has a
      // reference to the connection, which it will use to recover
      // ownership of it.
      __UNUSED auto r = conn.release();
      return;
    }

    log(ERROR, "driver_host: devmgr disconnected! fatal. (conn=%p)\n", conn.get());
    abort();
  }
  log(ERROR, "driver_host: no work? %08x\n", signal->observed);
  BeginWait(std::move(conn), dispatcher);
}

zx_status_t DeviceControllerConnection::HandleRead() {
  zx::unowned_channel conn = channel();
  uint8_t msg[8192];
  zx_handle_t hin[ZX_CHANNEL_MAX_MSG_HANDLES];
  uint32_t msize = sizeof(msg);
  uint32_t hcount = fbl::count_of(hin);
  zx_status_t status = conn->read(0, msg, hin, msize, hcount, &msize, &hcount);
  if (status != ZX_OK) {
    return status;
  }

  fidl_msg_t fidl_msg = {
      .bytes = msg,
      .handles = hin,
      .num_bytes = msize,
      .num_handles = hcount,
  };

  if (fidl_msg.num_bytes < sizeof(fidl_message_header_t)) {
    zx_handle_close_many(fidl_msg.handles, fidl_msg.num_handles);
    return ZX_ERR_IO;
  }

  char buffer[512];
  const char* path = mkdevpath(dev_, buffer, sizeof(buffer));

  auto hdr = static_cast<fidl_message_header_t*>(fidl_msg.bytes);
  // Depending on the state of the migration, GenOrdinal and Ordinal may be the
  // same value.  See FIDL-524.
  uint64_t ordinal = hdr->ordinal;
  if (ordinal == fuchsia_io_DirectoryOpenOrdinal || ordinal == fuchsia_io_DirectoryOpenGenOrdinal) {
    log(RPC_RIO, "devhost[%s] FIDL OPEN\n", path);
    zx::unowned_channel conn = channel();
    DevmgrFidlTxn txn(std::move(conn), hdr->txid);
    fuchsia::io::Directory::Dispatch(this, &fidl_msg, &txn);
    if (status != ZX_OK) {
      return txn.Status();
    }
    return txn.Status();
  }

  DevmgrFidlTxn txn(std::move(conn), hdr->txid);
  fuchsia::device::manager::DeviceController::Dispatch(this, &fidl_msg, &txn);
  return txn.Status();
}
