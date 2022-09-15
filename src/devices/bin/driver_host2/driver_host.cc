// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/devices/bin/driver_host2/driver_host.h"

#include <fidl/fuchsia.io/cpp/fidl.h>
#include <lib/async/cpp/task.h>
#include <lib/driver2/start_args.h>
#include <lib/fdf/cpp/dispatcher.h>
#include <lib/fdf/cpp/internal.h>
#include <lib/fdio/directory.h>
#include <lib/fit/defer.h>
#include <lib/fit/function.h>
#include <lib/sys/component/cpp/outgoing_directory.h>
#include <lib/syslog/cpp/macros.h>
#include <zircon/dlfcn.h>

#include "src/lib/storage/vfs/cpp/service.h"

// The driver runtime libraries use the fdf namespace, but we would also like to use fdf
// as an alias for the fdf FIDL library.
namespace fdf {
using namespace fuchsia_driver_framework;
}  // namespace fdf

namespace fio = fuchsia_io;
namespace frunner = fuchsia_component_runner;
namespace fdh = fuchsia_driver_host;

namespace {

class FileEventHandler : public fidl::AsyncEventHandler<fio::File> {
 public:
  explicit FileEventHandler(std::string url) : url_(std::move(url)) {}

  void on_fidl_error(fidl::UnbindInfo info) override {
    FX_SLOG(ERROR, "Failed to start driver; could not open library", KV("url", url_.data()),
            KV("status_str", info.FormatDescription().data()));
  }

 private:
  std::string url_;
};

// TODO(fxbug.dev/99679): This logic needs to be kept in sync with |driver::NsValue|.
// Once we have the ability to produce a const view from FIDL natural types, we can
// directly use |driver::NsValue| and delete this function.
zx::status<fidl::UnownedClientEnd<fuchsia_io::Directory>> NsValue(
    const std::vector<fuchsia_component_runner::ComponentNamespaceEntry>& entries,
    std::string_view path) {
  for (auto& entry : entries) {
    if (!entry.path() || !entry.directory()) {
      continue;
    }
    if (path == *entry.path()) {
      return zx::ok<fidl::UnownedClientEnd<fuchsia_io::Directory>>(*entry.directory());
    }
  }
  return zx::error(ZX_ERR_NOT_FOUND);
}

zx::status<fidl::ClientEnd<fio::File>> OpenDriverFile(
    const fdf::DriverStartArgs& start_args, const fuchsia_data::wire::Dictionary& program) {
  const auto& ns = start_args.ns();
  auto pkg = ns ? NsValue(*ns, "/pkg") : zx::error(ZX_ERR_INVALID_ARGS);
  if (pkg.is_error()) {
    FX_SLOG(ERROR, "Failed to start driver, missing '/pkg' directory",
            KV("status_str", zx_status_get_string(pkg.error_value())));
    return pkg.take_error();
  }

  zx::status<std::string> binary = driver::ProgramValue(program, "binary");
  if (binary.is_error()) {
    FX_SLOG(ERROR, "Failed to start driver, missing 'binary' argument",
            KV("status_str", zx_status_get_string(binary.error_value())));
    return binary.take_error();
  }
  // Open the driver's binary within the driver's package.
  auto endpoints = fidl::CreateEndpoints<fio::File>();
  if (endpoints.is_error()) {
    return endpoints.take_error();
  }
  zx_status_t status = fdio_open_at(
      pkg->channel()->get(), binary->data(),
      static_cast<uint32_t>(fio::OpenFlags::kRightReadable | fio::OpenFlags::kRightExecutable),
      endpoints->server.TakeChannel().release());
  if (status != ZX_OK) {
    FX_SLOG(ERROR, "Failed to start driver; could not open library",
            KV("status_str", zx_status_get_string(status)));
    return zx::error(status);
  }
  return zx::ok(std::move(endpoints->client));
}

}  // namespace

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
  if (!request.start_args().url()) {
    FX_SLOG(ERROR, "Failed to start driver, missing 'url' argument");
    completer.Close(ZX_ERR_INVALID_ARGS);
    return;
  }
  const std::string& url = *request.start_args().url();

  if (!request.start_args().program().has_value()) {
    FX_SLOG(ERROR, "Failed to start driver, missing 'program' argument");
    completer.Close(ZX_ERR_INVALID_ARGS);
    return;
  }

  fidl::Arena arena;
  fuchsia_data::wire::Dictionary wire_program =
      fidl::ToWire(arena, *request.start_args().program());

  auto driver_file = OpenDriverFile(request.start_args(), wire_program);
  if (driver_file.is_error()) {
    FX_SLOG(ERROR, "Failed to open driver file", KV("url", url.data()),
            KV("status_str", driver_file.status_string()));
    completer.Close(driver_file.error_value());
    return;
  }

  uint32_t default_dispatcher_opts = dfv2::ExtractDefaultDispatcherOpts(wire_program);

  // Once we receive the VMO from the call to GetBackingMemory, we can load the driver into this
  // driver host. We move the storage and encoded for start_args into this callback to extend its
  // lifetime.
  fidl::SharedClient file(std::move(*driver_file), loop_.dispatcher(),
                          std::make_unique<FileEventHandler>(url));
  auto callback = [this, request = std::move(request.driver()), completer = completer.ToAsync(),
                   start_args = std::move(request.start_args()),
                   default_dispatcher_opts = default_dispatcher_opts,
                   _ = file.Clone()](fidl::Result<fio::File::GetBackingMemory>& result) mutable {
    const std::string& url = *start_args.url();
    if (!result.is_ok()) {
      FX_SLOG(ERROR, "Failed to start driver, could not get library VMO", KV("url", url.data()),
              KV("status_str", result.error_value().FormatDescription().data()));
      zx_status_t status = result.error_value().is_application_error()
                               ? result.error_value().application_error()
                               : result.error_value().transport_error().status();
      completer.Close(status);
      return;
    }
    auto driver = Driver::Load(url, std::move(result->vmo()));
    if (driver.is_error()) {
      completer.Close(driver.error_value());
      return;
    }

    zx::status<fdf::Dispatcher> driver_dispatcher =
        CreateDispatcher(*driver, default_dispatcher_opts);
    if (driver_dispatcher.is_error()) {
      completer.Close(driver_dispatcher.status_value());
      return;
    }

    async_dispatcher_t* driver_async_dispatcher = driver_dispatcher->async_dispatcher();

    // Task to start the driver. Post this to the driver dispatcher thread.
    auto start_task = [this, request = std::move(request), completer = std::move(completer),
                       start_args = std::move(start_args), driver = std::move(*driver),
                       driver_dispatcher = std::move(*driver_dispatcher)]() mutable {
      auto status = StartDriver(std::move(driver), std::move(start_args),
                                std::move(driver_dispatcher), std::move(request));
      if (status.is_error()) {
        completer.Close(status.error_value());
      }
    };
    async::PostTask(driver_async_dispatcher, std::move(start_task));
  };
  file->GetBackingMemory(fio::VmoFlags::kRead | fio::VmoFlags::kExecute |
                         fio::VmoFlags::kPrivateClone)
      .ThenExactlyOnce(std::move(callback));
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
  auto driver_shutdown = std::make_unique<fdf_internal::DriverShutdown>();
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
