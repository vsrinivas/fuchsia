// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "device_controller_connection.h"

#include <fuchsia/device/c/fidl.h>
#include <fuchsia/io/llcpp/fidl.h>
#include <lib/zx/vmo.h>
#include <zircon/status.h>

#include <fbl/auto_lock.h>
#include <fbl/string_printf.h>

#include "connection_destroyer.h"
#include "driver_host.h"
#include "env.h"
#include "fidl_txn.h"
#include "fuchsia/device/llcpp/fidl.h"
#include "log.h"
#include "proxy_iostate.h"
#include "zx_device.h"
#include "zx_driver.h"

namespace {

// Handles outstanding calls to fuchsia.device.manager.DeviceController/BindDriver
// and fuchsia.device.Controller/Bind.
void BindReply(const fbl::RefPtr<zx_device_t>& dev,
               DeviceControllerConnection::BindDriverCompleter::Sync& completer, zx_status_t status,
               zx::channel test_output = zx::channel()) {
  completer.Reply(status, std::move(test_output));

  bool complete_bind = true;
  for (auto& child : dev->children()) {
    if ((child.flags() & DEV_FLAG_INVISIBLE) || child.ops()->init) {
      // Driver has initialization to do.
      complete_bind = false;
    }
  }
  if (!complete_bind) {
    dev->set_complete_bind_rebind_after_init(true);
    return;
  }
  if (auto bind_conn = dev->take_bind_conn(); bind_conn) {
    bind_conn(status);
  }

  if (auto rebind_conn = dev->take_rebind_conn(); rebind_conn) {
    rebind_conn(status);
  }
}

}  // namespace

void DeviceControllerConnection::CompleteCompatibilityTests(
    fuchsia_device_manager::wire::CompatibilityTestStatus status,
    CompleteCompatibilityTestsCompleter::Sync& _completer) {
  if (auto compat_conn = dev()->PopTestCompatibilityConn(); compat_conn) {
    compat_conn(static_cast<zx_status_t>(status));
  }
}

void DeviceControllerConnection::Init(InitCompleter::Sync& completer) {
  ZX_ASSERT(this->dev()->init_cb == nullptr);

  auto trace = this->dev()->BeginAsyncTrace("driver_host:lifecycle", "init");
  this->dev()->init_cb = [completer = completer.ToAsync(), trace = std::move(trace)](
                             zx_status_t status) mutable { completer.Reply(status); };
  fbl::AutoLock lock(&driver_host_context_->api_lock());
  driver_host_context_->DeviceInit(this->dev());
}

void DeviceControllerConnection::Suspend(uint32_t flags, SuspendCompleter::Sync& completer) {
  ZX_ASSERT(this->dev()->suspend_cb == nullptr);

  auto trace = this->dev()->BeginAsyncTrace("driver_host:lifecycle", "suspend");
  this->dev()->suspend_cb = [completer = completer.ToAsync(), trace = std::move(trace)](
                                zx_status_t status, uint8_t out_state) mutable {
    if (status == ZX_ERR_NOT_SUPPORTED) {
      status = ZX_OK;
    }
    completer.Reply(status);
  };
  fbl::AutoLock lock(&driver_host_context_->api_lock());
  driver_host_context_->DeviceSystemSuspend(this->dev(), flags);
}

void DeviceControllerConnection::Resume(uint32_t target_system_state,
                                        ResumeCompleter::Sync& completer) {
  ZX_ASSERT(this->dev()->resume_cb == nullptr);

  auto trace = this->dev()->BeginAsyncTrace("driver_host:lifecycle", "resume");
  this->dev()->resume_cb = [completer = completer.ToAsync(), trace = std::move(trace)](
                               zx_status_t status, uint8_t out_power_state,
                               uint32_t out_perf_state) mutable {
    if (status == ZX_ERR_NOT_SUPPORTED) {
      status = ZX_OK;
    }
    if (status != ZX_OK &&
        (out_power_state ==
         static_cast<uint8_t>(fuchsia_device::wire::DevicePowerState::DEVICE_POWER_STATE_D0))) {
      // Do not fail system resume, when the device is unable to go into a particular performance
      // state, but resumed to a working state.
      status = ZX_OK;
    }
    completer.Reply(status);
  };
  fbl::AutoLock lock(&driver_host_context_->api_lock());
  driver_host_context_->DeviceSystemResume(this->dev(), target_system_state);
}

void DeviceControllerConnection::ConnectProxy(::zx::channel shadow,
                                              ConnectProxyCompleter::Sync& _completer) {
  VLOGD(1, *dev(), "Connected to proxy for device %p", dev().get());
  dev()->ops()->rxrpc(dev()->ctx, ZX_HANDLE_INVALID);
  // Ignore any errors in the creation for now?
  // TODO(teisenbe): Investigate if this is the right thing
  ProxyIostate::Create(dev(), std::move(shadow), driver_host_context_->loop().dispatcher());
}

void DeviceControllerConnection::BindDriver(::fidl::StringView driver_path_view, zx::vmo driver,
                                            BindDriverCompleter::Sync& completer) {
  const auto& dev = this->dev();
  std::string_view driver_path(driver_path_view.data(), driver_path_view.size());

  // TODO: api lock integration
  LOGD(INFO, *dev, "Binding driver '%.*s'", static_cast<int>(driver_path.size()),
       driver_path.data());
  fbl::RefPtr<zx_driver_t> drv;
  if (dev->flags() & DEV_FLAG_DEAD) {
    LOGD(ERROR, *dev, "Cannot bind to removed device");
    BindReply(dev, completer, ZX_ERR_IO_NOT_PRESENT);
    return;
  }

  zx_status_t r = driver_host_context_->FindDriver(driver_path, std::move(driver), &drv);
  if (r != ZX_OK) {
    LOGD(ERROR, *dev, "Failed to load driver '%.*s': %s", static_cast<int>(driver_path.size()),
         driver_path.data(), zx_status_get_string(r));
    BindReply(dev, completer, r);
    return;
  }

  // Check for driver test flags.
  bool tests_default = getenv_bool("driver.tests.enable", false);
  auto option = fbl::StringPrintf("driver.%s.tests.enable", drv->name());
  zx::channel test_output;
  if (getenv_bool(option.data(), tests_default) && drv->has_run_unit_tests_op()) {
    zx::channel test_input;
    zx::channel::create(0, &test_input, &test_output);
    bool tests_passed = drv->RunUnitTestsOp(dev, std::move(test_input));
    if (!tests_passed) {
      FX_LOGF(ERROR, "unit-tests", "[  FAILED  ] %s", drv->name());
      drv->set_status(ZX_ERR_BAD_STATE);
      BindReply(dev, completer, ZX_ERR_BAD_STATE, std::move(test_output));
      return;
    }
    FX_LOGF(INFO, "unit-tests", "[  PASSED  ] %s", drv->name());
  }

  if (drv->has_bind_op()) {
    internal::BindContext bind_ctx = {
        .parent = dev,
        .child = nullptr,
    };
    r = drv->BindOp(&bind_ctx, dev);

    if (r != ZX_OK) {
      LOGD(ERROR, *dev, "Failed to bind driver '%.*s': %s", static_cast<int>(driver_path.size()),
           driver_path.data(), zx_status_get_string(r));
    } else if (bind_ctx.child == nullptr) {
      LOGD(WARNING, *dev, "Driver '%.*s' did not add a child device in bind()",
           static_cast<int>(driver_path.size()), driver_path.data());
    }
    BindReply(dev, completer, r, std::move(test_output));
    return;
  }

  if (!drv->has_create_op()) {
    LOGD(ERROR, *dev, "Neither create() nor bind() are implemented for driver '%.*s'",
         static_cast<int>(driver_path.size()), driver_path.data());
  }
  BindReply(dev, completer, ZX_ERR_NOT_SUPPORTED, std::move(test_output));
}

void DeviceControllerConnection::Unbind(UnbindCompleter::Sync& completer) {
  ZX_ASSERT(this->dev()->unbind_cb == nullptr);

  auto trace = this->dev()->BeginAsyncTrace("driver_host:lifecycle", "unbind");

  this->dev()->unbind_cb = [dev = this->dev(), completer = completer.ToAsync(),
                            trace = std::move(trace)](zx_status_t status) mutable {
    fuchsia_device_manager::wire::DeviceController_Unbind_Result result;
    fuchsia_device_manager::wire::DeviceController_Unbind_Response response;
    if (status != ZX_OK && dev->parent()) {
      // If unbind returns an error, and if client is waiting for unbind to complete,
      // inform the client.
      if (auto unbind_children_conn = dev->parent()->take_unbind_children_conn();
          unbind_children_conn) {
        unbind_children_conn(status);
      }
    }
    result.set_response(
        fidl::ObjectView<fuchsia_device_manager::wire::DeviceController_Unbind_Response>::
            FromExternal(&response));
    completer.Reply(std::move(result));
  };
  fbl::AutoLock lock(&driver_host_context_->api_lock());
  driver_host_context_->DeviceUnbind(this->dev());
}

void DeviceControllerConnection::CompleteRemoval(CompleteRemovalCompleter::Sync& completer) {
  ZX_ASSERT(this->dev()->removal_cb == nullptr);
  this->dev()->removal_cb = [completer = completer.ToAsync()](zx_status_t status) mutable {
    fuchsia_device_manager::wire::DeviceController_CompleteRemoval_Result result;
    fuchsia_device_manager::wire::DeviceController_CompleteRemoval_Response response;
    result.set_response(
        fidl::ObjectView<fuchsia_device_manager::wire::DeviceController_CompleteRemoval_Response>::
            FromExternal(&response));
    completer.Reply(std::move(result));
  };
  fbl::AutoLock lock(&driver_host_context_->api_lock());
  driver_host_context_->DeviceCompleteRemoval(this->dev());
}

DeviceControllerConnection::DeviceControllerConnection(
    DriverHostContext* ctx, fbl::RefPtr<zx_device> dev, zx::channel rpc,
    fidl::Client<fuchsia_device_manager::Coordinator> coordinator_client)
    : driver_host_context_(ctx),
      dev_(std::move(dev)),
      coordinator_client_(std::move(coordinator_client)) {
  dev_->rpc = zx::unowned_channel(rpc);
  dev_->coordinator_client = coordinator_client_.get();
  dev_->conn.store(this);
  set_channel(std::move(rpc));
}

DeviceControllerConnection::~DeviceControllerConnection() {
  // Ensure that the device has no dangling references to the resources we're
  // destroying.  This is safe because a device only ever has one associated
  // DeviceControllerConnection.
  dev_->conn.store(nullptr);
  dev_->rpc = zx::unowned_channel();
}

zx_status_t DeviceControllerConnection::Create(
    DriverHostContext* ctx, fbl::RefPtr<zx_device> dev, zx::channel controller_rpc,
    fidl::Client<fuchsia_device_manager::Coordinator> coordinator_client,
    std::unique_ptr<DeviceControllerConnection>* conn) {
  *conn = std::make_unique<DeviceControllerConnection>(
      ctx, std::move(dev), std::move(controller_rpc), std::move(coordinator_client));
  if (*conn == nullptr) {
    return ZX_ERR_NO_MEMORY;
  }
  return ZX_OK;
}

// Handler for when a io.fidl open() is called on a device
void DeviceControllerConnection::Open(uint32_t flags, uint32_t mode, ::fidl::StringView path,
                                      ::zx::channel object, OpenCompleter::Sync& completer) {
  if (path.size() != 1 && path.data()[0] != '.') {
    LOGD(ERROR, *dev(), "Attempt to open path '%.*s'", static_cast<int>(path.size()), path.data());
  }
  driver_host_context_->DeviceConnect(this->dev(), flags, std::move(object));
}

void DeviceControllerConnection::HandleRpc(std::unique_ptr<DeviceControllerConnection> conn,
                                           async_dispatcher_t* dispatcher, async::WaitBase* wait,
                                           zx_status_t status, const zx_packet_signal_t* signal) {
  const auto& dev = conn->dev_;
  if (status != ZX_OK) {
    LOGD(ERROR, *dev, "Failed to wait for device controller connection: %s",
         zx_status_get_string(status));
    return;
  }
  if (signal->observed & ZX_CHANNEL_READABLE) {
    zx_status_t r = conn->HandleRead();
    if (r != ZX_OK) {
      if (dev->conn.load() == nullptr && (r == ZX_ERR_INTERNAL || r == ZX_ERR_PEER_CLOSED)) {
        // Treat this as a PEER_CLOSED below.  It can happen if the
        // devcoordinator sent us a request while we asked the
        // devcoordinator to remove us.  The coordinator then closes the
        // channel before we can reply, and the FIDL bindings convert
        // the PEER_CLOSED on zx_channel_write() to a ZX_ERR_INTERNAL.  See fxbug.dev/33897.
        __UNUSED auto r = conn.release();
        return;
      }
      LOGD(FATAL, *dev, "Failed to handle RPC for device %p: %s", dev.get(),
           zx_status_get_string(r));
    }
    BeginWait(std::move(conn), dispatcher);
    return;
  }
  if (signal->observed & ZX_CHANNEL_PEER_CLOSED) {
    // Check if we were expecting this peer close.  If not, this could be a
    // serious bug.
    if (dev->conn.load() == nullptr) {
      // We're in the middle of shutting down, so just stop processing
      // signals and wait for the queued shutdown packet.  It has a
      // reference to the connection, which it will use to recover
      // ownership of it.
      __UNUSED auto r = conn.release();
      return;
    }

    // This is expected in test environments where driver_manager has terminated.
    // TODO(fxbug.dev/52627): Support graceful termination.
    LOGD(WARNING, *dev, "driver_manager disconnected from device %p", dev.get());
    exit(1);
  }
  LOGD(WARNING, *dev, "Unexpected signal state %#08x for device %p", signal->observed, dev.get());
  BeginWait(std::move(conn), dispatcher);
}

zx_status_t DeviceControllerConnection::HandleRead() {
  zx::unowned_channel conn = channel();
  uint8_t msg[8192];
  zx_handle_info_t hin[ZX_CHANNEL_MAX_MSG_HANDLES];
  uint32_t msize = sizeof(msg);
  uint32_t hcount = std::size(hin);
  zx_status_t status = conn->read_etc(0, msg, hin, msize, hcount, &msize, &hcount);
  if (status != ZX_OK) {
    return status;
  }

  fidl_incoming_msg_t fidl_msg = {
      .bytes = msg,
      .handles = hin,
      .num_bytes = msize,
      .num_handles = hcount,
  };

  if (fidl_msg.num_bytes < sizeof(fidl_message_header_t)) {
    FidlHandleInfoCloseMany(fidl_msg.handles, fidl_msg.num_handles);
    return ZX_ERR_IO;
  }

  auto hdr = static_cast<fidl_message_header_t*>(fidl_msg.bytes);
  uint64_t ordinal = hdr->ordinal;
  // llcpp (intentionally!) does not expose the fidl ordinal, so this is a hacky way to get it
  // anyway for backwards compatibility for porting code from the old C bindings.
  static fuchsia_io::Directory::OpenRequest for_ordinal(zx_txid_t(0));
  if (ordinal == for_ordinal._hdr.ordinal) {
    VLOGD(1, *dev(), "Opening device %p", dev().get());
    zx::unowned_channel conn = channel();
    DevmgrFidlTxn txn(std::move(conn), hdr->txid);
    fuchsia_io::Directory::Dispatch(this, &fidl_msg, &txn);
    if (status != ZX_OK) {
      return txn.Status();
    }
    return txn.Status();
  }

  DevmgrFidlTxn txn(std::move(conn), hdr->txid);
  fuchsia_device_manager::DeviceController::Dispatch(this, &fidl_msg, &txn);
  return txn.Status();
}
