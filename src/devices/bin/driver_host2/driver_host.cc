// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/devices/bin/driver_host2/driver_host.h"

#include <fidl/fuchsia.io/cpp/fidl.h>
#include <lib/async/cpp/task.h>
#include <lib/driver2/start_args.h>
#include <lib/fdf/cpp/dispatcher.h>
#include <lib/fdf/cpp/env.h>
#include <lib/fit/defer.h>
#include <lib/fit/function.h>
#include <lib/sys/component/cpp/outgoing_directory.h>
#include <lib/syslog/cpp/macros.h>

#include "src/lib/storage/vfs/cpp/service.h"

// The driver runtime libraries use the fdf namespace, but we would also like to use fdf
// as an alias for the fdf FIDL library.
namespace fdf {
using namespace fuchsia_driver_framework;
}  // namespace fdf

namespace fdh = fuchsia_driver_host;

namespace dfv2 {

DriverHost::DriverHost(inspect::Inspector& inspector, async::Loop& loop) : loop_(loop) {
  inspector.GetRoot().CreateLazyNode(
      "drivers", [this] { return Inspect(); }, &inspector);
}

fpromise::promise<inspect::Inspector> DriverHost::Inspect() {
  inspect::Inspector inspector;
  auto& root = inspector.GetRoot();
  size_t i = 0;

  std::lock_guard<std::mutex> lock(mutex_);
  for (auto& driver : drivers_) {
    auto child = root.CreateChild("driver-" + std::to_string(++i));
    child.CreateString("url", driver.url(), &inspector);
    inspector.emplace(std::move(child));
  }

  return fpromise::make_ok_promise(std::move(inspector));
}

zx::status<> DriverHost::PublishDriverHost(component::OutgoingDirectory& outgoing_directory) {
  const auto service = [this](fidl::ServerEnd<fdh::DriverHost> request) {
    fidl::BindServer(loop_.dispatcher(), std::move(request), this);
  };
  auto status = outgoing_directory.AddProtocol<fdh::DriverHost>(std::move(service));
  if (status.is_error()) {
    FX_SLOG(ERROR, "Failed to add directory entry",
            KV("name", fidl::DiscoverableProtocolName<fdh::DriverHost>),
            KV("status_str", status.status_string()));
  }

  return status;
}

void DriverHost::Start(StartRequest& request, StartCompleter::Sync& completer) {
  auto callback = [this, request = std::move(request.driver()),
                   completer = completer.ToAsync()](zx::status<LoadedDriver> loaded) mutable {
    if (loaded.is_error()) {
      completer.Close(loaded.error_value());
      return;
    }
    async_dispatcher_t* driver_async_dispatcher = loaded->dispatcher.async_dispatcher();

    // Task to start the driver. Post this to the driver dispatcher thread.
    auto start_task = [this, request = std::move(request), completer = std::move(completer),
                       loaded = std::move(*loaded)]() mutable {
      auto status = StartDriver(std::move(loaded.driver), std::move(loaded.start_args),
                                std::move(loaded.dispatcher), std::move(request));
      if (status.is_error()) {
        completer.Close(status.error_value());
      }
    };
    async::PostTask(driver_async_dispatcher, std::move(start_task));
  };
  LoadDriver(std::move(request.start_args()), loop_.dispatcher(), std::move(callback));
}

void DriverHost::GetProcessKoid(GetProcessKoidRequest& request,
                                GetProcessKoidCompleter::Sync& completer) {
  zx_info_handle_basic_t info;
  zx_status_t status =
      zx::process::self()->get_info(ZX_INFO_HANDLE_BASIC, &info, sizeof(info), nullptr, nullptr);
  if (status != ZX_OK) {
    FX_SLOG(ERROR, "Failed to get info about process handle",
            KV("status_str", zx_status_get_string(status)));
    completer.Reply(zx::error(status));
  }
  completer.Reply(zx::ok(info.koid));
}

zx::status<> DriverHost::StartDriver(fbl::RefPtr<Driver> driver,
                                     fuchsia_driver_framework::DriverStartArgs start_args,
                                     fdf::Dispatcher dispatcher,
                                     fidl::ServerEnd<fuchsia_driver_host::Driver> request) {
  // We have to add the driver to this list before calling Start in order to have an accurate
  // count of how many drivers exist in this driver host.
  {
    std::lock_guard<std::mutex> lock(mutex_);
    drivers_.push_back(driver);
  }
  auto remove_driver = fit::defer([this, driver = driver.get()]() {
    std::lock_guard<std::mutex> lock(mutex_);
    drivers_.erase(*driver);
  });

  // Save a ptr to the dispatcher so we can shut it down if starting the driver fails.
  fdf::UnownedDispatcher unowned_dispatcher = dispatcher.borrow();
  auto start = driver->Start(std::move(start_args), std::move(dispatcher));
  if (start.is_error()) {
    FX_SLOG(ERROR, "Failed to start driver", KV("url", driver->url().data()),
            KV("status_str", start.status_string()));
    // If we fail to start the driver, we need to initiate shutting down the dispatcher.
    unowned_dispatcher->ShutdownAsync();
    // The dispatcher will be destroyed in the shutdown callback, when the last driver reference
    // is released.
    return start.take_error();
  }
  FX_SLOG(INFO, "Started driver", KV("url", driver->url().data()));

  auto unbind_callback = [this](Driver* driver, fidl::UnbindInfo info,
                                fidl::ServerEnd<fdh::Driver> server) {
    if (!info.is_user_initiated()) {
      FX_SLOG(WARNING, "Unexpected stop of driver", KV("url", driver->url().data()),
              KV("status_str", info.FormatDescription()).data());
    }
    ShutdownDriver(driver, std::move(server));
  };
  auto bind = fidl::BindServer(loop_.dispatcher(), std::move(request), driver.get(),
                               std::move(unbind_callback));
  driver->set_binding(std::move(bind));
  remove_driver.cancel();
  return zx::ok();
}

void DriverHost::ShutdownDriver(Driver* driver, fidl::ServerEnd<fdh::Driver> server) {
  // Request the driver runtime shutdown all dispatchers owned by the driver.
  // Once we get the callback, we will stop the driver.
  auto driver_shutdown = std::make_unique<fdf_env::DriverShutdown>();
  auto driver_shutdown_ptr = driver_shutdown.get();
  auto shutdown_callback = [this, driver_shutdown = std::move(driver_shutdown), driver,
                            server = std::move(server)](const void* shutdown_driver) mutable {
    ZX_ASSERT(driver == shutdown_driver);

    std::lock_guard<std::mutex> lock(mutex_);
    // This removes the driver's unique_ptr from the list, which will
    // run the destructor and call the driver's Stop hook.
    drivers_.erase(*driver);

    // Send the epitaph to the driver runner letting it know we stopped
    // the driver correctly.
    server.Close(ZX_OK);

    // If this is the last driver, shutdown the driver host.
    if (drivers_.is_empty()) {
      loop_.Quit();
    }
  };
  // We always expect this call to succeed, as we should be the only entity
  // that attempts to forcibly shutdown drivers.
  ZX_ASSERT(ZX_OK == driver_shutdown_ptr->Begin(driver, std::move(shutdown_callback)));
}

}  // namespace dfv2
