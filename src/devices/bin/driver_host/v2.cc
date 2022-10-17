// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <dlfcn.h>
#include <fidl/fuchsia.device/cpp/wire.h>
#include <fidl/fuchsia.io/cpp/wire.h>
#include <lib/ddk/debug.h>
#include <lib/driver2/start_args.h>
#include <lib/fdf/cpp/env.h>
#include <lib/fdio/directory.h>
#include <lib/zx/vmo.h>
#include <zircon/dlfcn.h>
#include <zircon/status.h>

#include <optional>

#include "src/devices/bin/driver_host/device_controller_connection.h"
#include "src/devices/bin/driver_host/driver_host.h"
#include "src/devices/bin/driver_host/log.h"

namespace fdf {
using namespace fuchsia_driver_framework;
}

void internal::DriverHostControllerConnection::Start(StartRequestView request,
                                                     StartCompleter::Sync& completer) {
  auto callback = [this, request = std::move(request->driver),
                   completer = completer.ToAsync()](zx::result<dfv2::LoadedDriver> loaded) mutable {
    if (loaded.is_error()) {
      completer.Close(loaded.error_value());
    }
    async_dispatcher_t* driver_async_dispatcher = loaded->dispatcher.async_dispatcher();

    // Task to start the driver. Post this to the driver dispatcher thread.
    auto start_task = [this, request = std::move(request), completer = std::move(completer),
                       loaded = std::move(*loaded)]() mutable {
      // Save a ptr to the dispatcher so we can shut it down if starting the driver fails.
      fdf::UnownedDispatcher unowned_dispatcher = loaded.dispatcher.borrow();

      // Start the driver.
      auto start = loaded.driver->Start(std::move(loaded.start_args), std::move(loaded.dispatcher));
      if (start.is_error()) {
        LOGF(ERROR, "Failed to start driver '%s': %s", loaded.driver->url().data(),
             start.status_string());
        // If we fail to start the driver, we need to initiate shutting down the dispatcher.
        unowned_dispatcher->ShutdownAsync();
        return;
      }
      LOGF(INFO, "Started '%s'", loaded.driver->url().data());

      auto unbind_callback = [this](dfv2::Driver* driver, fidl::UnbindInfo info,
                                    fidl::ServerEnd<fuchsia_driver_host::Driver> server) {
        if (!info.is_user_initiated()) {
          LOGF(WARNING, "Unexpected stop of driver '%s': %s", driver->url().data(),
               info.FormatDescription().data());
        }

        // Request the driver runtime shutdown all dispatchers owned by the driver.
        // Once we get the callback, we will stop the driver.
        auto driver_shutdown = std::make_unique<fdf_env::DriverShutdown>();
        auto driver_shutdown_ptr = driver_shutdown.get();
        auto shutdown_callback = [this, driver_shutdown = std::move(driver_shutdown), driver,
                                  server = std::move(server)](const void* shutdown_driver) mutable {
          ZX_ASSERT(driver == shutdown_driver);

          // This removes the driver's unique_ptr from the list, which will
          // run the destructor and call the driver's Stop hook.
          driver_host_context_->RemoveDriver(*driver);

          // Send the epitaph to the driver runner letting it know we stopped
          // the driver correctly.
          server.Close(ZX_OK);
        };
        // We always expect this call to succeed, as we should be the only entity
        // that attempts to forcibly shutdown drivers.
        ZX_ASSERT(ZX_OK == driver_shutdown_ptr->Begin(driver, std::move(shutdown_callback)));
      };
      auto bind = fidl::BindServer(driver_host_context_->loop().dispatcher(), std::move(request),
                                   loaded.driver.get(), std::move(unbind_callback));
      loaded.driver->set_binding(std::move(bind));
      this->driver_host_context_->AddDriver(std::move(loaded.driver));
    };
    async::PostTask(driver_async_dispatcher, std::move(start_task));
  };
  dfv2::LoadDriver(fidl::ToNatural(request->start_args), driver_host_context_->loop().dispatcher(),
                   std::move(callback));
}
