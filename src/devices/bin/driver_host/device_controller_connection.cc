// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "device_controller_connection.h"

#include <fuchsia/device/llcpp/fidl.h>
#include <lib/zx/channel.h>
#include <lib/zx/vmo.h>
#include <zircon/status.h>

#include <fbl/auto_lock.h>
#include <fbl/string_printf.h>

#include "connection_destroyer.h"
#include "driver_host.h"
#include "env.h"
#include "fidl_txn.h"
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

void DeviceControllerConnection::Init(InitRequestView request, InitCompleter::Sync& completer) {
  ZX_ASSERT(this->dev()->init_cb == nullptr);

  auto trace = this->dev()->BeginAsyncTrace("driver_host:lifecycle", "init");
  this->dev()->init_cb = [completer = completer.ToAsync(), trace = std::move(trace)](
                             zx_status_t status) mutable { completer.Reply(status); };
  fbl::AutoLock lock(&driver_host_context_->api_lock());
  driver_host_context_->DeviceInit(this->dev());
}

void DeviceControllerConnection::Suspend(SuspendRequestView request,
                                         SuspendCompleter::Sync& completer) {
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
  driver_host_context_->DeviceSystemSuspend(this->dev(), request->flags);
}

void DeviceControllerConnection::Resume(ResumeRequestView request,
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
         static_cast<uint8_t>(fuchsia_device::wire::DevicePowerState::kDevicePowerStateD0))) {
      // Do not fail system resume, when the device is unable to go into a particular performance
      // state, but resumed to a working state.
      status = ZX_OK;
    }
    completer.Reply(status);
  };
  fbl::AutoLock lock(&driver_host_context_->api_lock());
  driver_host_context_->DeviceSystemResume(this->dev(), request->target_system_state);
}

void DeviceControllerConnection::ConnectProxy(ConnectProxyRequestView request,
                                              ConnectProxyCompleter::Sync& _completer) {
  VLOGD(1, *dev(), "Connected to proxy for device %p", dev().get());
  dev()->ops()->rxrpc(dev()->ctx, ZX_HANDLE_INVALID);
  // Ignore any errors in the creation for now?
  // TODO(teisenbe): Investigate if this is the right thing
  ProxyIostate::Create(dev(), std::move(request->shadow),
                       driver_host_context_->loop().dispatcher());
}

void DeviceControllerConnection::BindDriver(BindDriverRequestView request,
                                            BindDriverCompleter::Sync& completer) {
  const auto& dev = this->dev();
  std::string_view driver_path(request->driver_path.data(), request->driver_path.size());

  // TODO: api lock integration
  LOGD(INFO, *dev, "Binding driver '%.*s'", static_cast<int>(driver_path.size()),
       driver_path.data());
  fbl::RefPtr<zx_driver_t> drv;
  if (dev->flags() & DEV_FLAG_DEAD) {
    LOGD(ERROR, *dev, "Cannot bind to removed device");
    BindReply(dev, completer, ZX_ERR_IO_NOT_PRESENT);
    return;
  }

  zx_status_t r = driver_host_context_->FindDriver(driver_path, std::move(request->driver), &drv);
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

void DeviceControllerConnection::Unbind(UnbindRequestView request,
                                        UnbindCompleter::Sync& completer) {
  ZX_ASSERT(this->dev()->unbind_cb == nullptr);

  auto trace = this->dev()->BeginAsyncTrace("driver_host:lifecycle", "unbind");

  this->dev()->unbind_cb = [dev = this->dev(), completer = completer.ToAsync(),
                            trace = std::move(trace)](zx_status_t status) mutable {
    fuchsia_device_manager::wire::DeviceControllerUnbindResult result;
    fuchsia_device_manager::wire::DeviceControllerUnbindResponse response;
    if (status != ZX_OK && dev->parent()) {
      // If unbind returns an error, and if client is waiting for unbind to complete,
      // inform the client.
      if (auto unbind_children_conn = dev->parent()->take_unbind_children_conn();
          unbind_children_conn) {
        unbind_children_conn(status);
      }
    }
    result.set_response(
        fidl::ObjectView<
            fuchsia_device_manager::wire::DeviceControllerUnbindResponse>::FromExternal(&response));
    completer.Reply(std::move(result));
  };
  fbl::AutoLock lock(&driver_host_context_->api_lock());
  driver_host_context_->DeviceUnbind(this->dev());
}

void DeviceControllerConnection::CompleteRemoval(CompleteRemovalRequestView request,
                                                 CompleteRemovalCompleter::Sync& completer) {
  ZX_ASSERT(this->dev()->removal_cb == nullptr);
  this->dev()->removal_cb = [completer = completer.ToAsync()](zx_status_t status) mutable {
    fuchsia_device_manager::wire::DeviceControllerCompleteRemovalResult result;
    fuchsia_device_manager::wire::DeviceControllerCompleteRemovalResponse response;
    result.set_response(
        fidl::ObjectView<fuchsia_device_manager::wire::DeviceControllerCompleteRemovalResponse>::
            FromExternal(&response));
    completer.Reply(std::move(result));
  };
  fbl::AutoLock lock(&driver_host_context_->api_lock());
  driver_host_context_->DeviceCompleteRemoval(this->dev());
}

DeviceControllerConnection::DeviceControllerConnection(
    DriverHostContext* ctx, fbl::RefPtr<zx_device> dev,
    fidl::WireSharedClient<fuchsia_device_manager::Coordinator> coordinator_client)
    : driver_host_context_(ctx),
      dev_(std::move(dev)),
      coordinator_client_(std::move(coordinator_client)) {
  dev_->coordinator_client = coordinator_client_.Clone();
}

std::unique_ptr<DeviceControllerConnection> DeviceControllerConnection::Create(
    DriverHostContext* ctx, fbl::RefPtr<zx_device> dev,
    fidl::WireSharedClient<fuchsia_device_manager::Coordinator> coordinator_client) {
  return std::make_unique<DeviceControllerConnection>(ctx, std::move(dev),
                                                      std::move(coordinator_client));
}

void DeviceControllerConnection::Bind(
    std::unique_ptr<DeviceControllerConnection> conn,
    fidl::ServerEnd<fuchsia_device_manager::DeviceController> request,
    async_dispatcher_t* dispatcher) {
  auto dev = conn->dev_;
  fbl::AutoLock al(&dev->controller_lock);
  dev->controller_binding = fidl::BindServer(
      dispatcher, std::move(request), std::move(conn),
      [](DeviceControllerConnection* self, fidl::UnbindInfo info,
         fidl::ServerEnd<fuchsia_device_manager::DeviceController> server_end) {
        auto& dev = self->dev_;
        switch (info.reason()) {
          case fidl::Reason::kUnbind:
          case fidl::Reason::kClose:
            // These are initiated by ourself.
            break;
          case fidl::Reason::kPeerClosed:
            // Check if we were expecting this peer close.  If not, this could be a
            // serious bug.
            {
              fbl::AutoLock al(&dev->controller_lock);
              if (!dev->controller_binding) {
                // We're in the middle of shutting down, so just stop processing
                // signals and wait for the queued shutdown packet.  It has a
                // reference to the connection, which it will use to recover
                // ownership of it.
                return;
              }
            }

            // This is expected in test environments where driver_manager has
            // terminated.
            // TODO(fxbug.dev/52627): Support graceful termination.
            LOGD(WARNING, *dev, "driver_manager disconnected from device %p", dev.get());
            zx_process_exit(1);
            break;
          case fidl::Reason::kDispatcherError:
          case fidl::Reason::kDecodeError:
          case fidl::Reason::kUnexpectedMessage:
            LOGD(FATAL, *dev, "Failed to handle RPC for device %p: %s", dev.get(),
                 info.FormatDescription().c_str());
            break;
          case fidl::Reason::kEncodeError:
            LOGD(FATAL, *dev, "Failed to encode message for device %p: %s", dev.get(),
                 info.FormatDescription().c_str());
            break;
          default:
            LOGD(FATAL, *dev, "Unknown fidl error for device %p: %s", dev.get(),
                 info.FormatDescription().c_str());
        }
      });
}

// Handler for when a io.fidl open() is called on a device
void DeviceControllerConnection::Open(OpenRequestView request, OpenCompleter::Sync& completer) {
  VLOGD(1, *dev(), "Opening device %p", dev().get());
  if (request->path.size() > 1 && request->path.data()[0] != '.') {
    LOGD(ERROR, *dev(), "Attempt to open path '%.*s'", static_cast<int>(request->path.size()),
         request->path.data());
  }
  driver_host_context_->DeviceConnect(this->dev(), request->flags, request->object.TakeChannel());
}
