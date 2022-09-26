// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/devices/bin/driver_host2/driver.h"

#include <lib/driver2/start_args.h>
#include <lib/fdf/cpp/dispatcher.h>
#include <lib/fdf/cpp/env.h>
#include <lib/fdio/directory.h>
#include <lib/fit/defer.h>
#include <zircon/dlfcn.h>

#include <fbl/string_printf.h>

#include "src/devices/lib/log/log.h"

namespace fdh = fuchsia_driver_host;
namespace fio = fuchsia_io;
namespace frunner = fuchsia_component_runner;

namespace fdf {
using namespace fuchsia_driver_framework;
}  // namespace fdf

namespace dfv2 {

namespace {

std::string_view GetManifest(std::string_view url) {
  auto index = url.rfind('/');
  return index == std::string_view::npos ? url : url.substr(index + 1);
}

class FileEventHandler : public fidl::AsyncEventHandler<fio::File> {
 public:
  explicit FileEventHandler(std::string url) : url_(std::move(url)) {}

  void on_fidl_error(fidl::UnbindInfo info) override {
    LOGF(ERROR, "Failed to start driver '%s'; could not open library: %s", url_.c_str(),
         info.FormatDescription().c_str());
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
      return zx::ok(fidl::UnownedClientEnd<fuchsia_io::Directory>(*entry.directory()));
    }
  }
  return zx::error(ZX_ERR_NOT_FOUND);
}

zx::status<fidl::ClientEnd<fio::File>> OpenDriverFile(
    const fdf::DriverStartArgs& start_args, const fuchsia_data::wire::Dictionary& program) {
  const auto& ns = start_args.ns();
  auto pkg = ns ? NsValue(*ns, "/pkg") : zx::error(ZX_ERR_INVALID_ARGS);
  if (pkg.is_error()) {
    LOGF(ERROR, "Failed to start driver, missing '/pkg' directory: %s", pkg.status_string());
    return pkg.take_error();
  }

  zx::status<std::string> binary = driver::ProgramValue(program, "binary");
  if (binary.is_error()) {
    LOGF(ERROR, "Failed to start driver, missing 'binary' argument: %s", binary.status_string());
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
    LOGF(ERROR, "Failed to start driver; could not open library: %s", zx_status_get_string(status));
    return zx::error(status);
  }
  return zx::ok(std::move(endpoints->client));
}

}  // namespace

zx::status<fbl::RefPtr<Driver>> Driver::Load(std::string url, zx::vmo vmo) {
  // Give the driver's VMO a name. We can't fit the entire URL in the name, so
  // use the name of the manifest from the URL.
  auto manifest = GetManifest(url);
  zx_status_t status = vmo.set_property(ZX_PROP_NAME, manifest.data(), manifest.size());
  if (status != ZX_OK) {
    LOGF(ERROR, "Failed to start driver '%s',, could not name library VMO: %s", url.c_str(),
         zx_status_get_string(status));
    return zx::error(status);
  }

  void* library = dlopen_vmo(vmo.get(), RTLD_NOW);
  if (library == nullptr) {
    LOGF(ERROR, "Failed to start driver '%s', could not load library: %s", url.data(), dlerror());
    return zx::error(ZX_ERR_INTERNAL);
  }
  auto record = static_cast<const DriverRecordV1*>(dlsym(library, "__fuchsia_driver_record__"));
  if (record == nullptr) {
    LOGF(ERROR, "Failed to start driver '%s', driver record not found", url.data());
    return zx::error(ZX_ERR_NOT_FOUND);
  }
  if (record->version != 1) {
    LOGF(ERROR, "Failed to start driver '%s', unknown driver record version: %lu", url.data(),
         record->version);
    return zx::error(ZX_ERR_WRONG_TYPE);
  }
  return zx::ok(fbl::MakeRefCounted<Driver>(std::move(url), library, record));
}

Driver::Driver(std::string url, void* library, const DriverRecordV1* record)
    : url_(std::move(url)), library_(library), record_(record) {}

Driver::~Driver() {
  if (opaque_.has_value()) {
    zx_status_t status = record_->stop(*opaque_);
    if (status != ZX_OK) {
      LOGF(ERROR, "Failed to stop driver '%s': %s", url_.data(), zx_status_get_string(status));
    }
  }
  dlclose(library_);
}

void Driver::set_binding(fidl::ServerBindingRef<fdh::Driver> binding) {
  binding_.emplace(std::move(binding));
}

void Driver::Stop(StopRequest& request, StopCompleter::Sync& completer) { binding_->Unbind(); }

zx::status<> Driver::Start(fuchsia_driver_framework::DriverStartArgs start_args,
                           ::fdf::Dispatcher dispatcher) {
  initial_dispatcher_ = std::move(dispatcher);

  fidl::OwnedEncodeResult encoded = fidl::Encode(std::move(start_args));
  if (!encoded.message().ok()) {
    LOGF(ERROR, "Failed to start driver, could not encode start args: %s",
         encoded.message().FormatDescription().data());
    return zx::error(encoded.message().status());
  }
  fidl_opaque_wire_format_metadata_t wire_format_metadata =
      encoded.wire_format_metadata().ToOpaque();

  // We convert the outgoing message into an incoming message to provide to the
  // driver on start.
  fidl::OutgoingToIncomingMessage converted_message{encoded.message()};
  if (!converted_message.ok()) {
    LOGF(ERROR, "Failed to start driver, could not convert start args: %s",
         converted_message.FormatDescription().data());
    return zx::error(converted_message.status());
  }

  // After calling |record_->start|, we assume it has taken ownership of
  // the handles from |start_args|, and can therefore relinquish ownership.
  fidl_incoming_msg_t c_msg =
      std::move(converted_message.incoming_message()).ReleaseToEncodedCMessage();
  void* opaque = nullptr;
  zx_status_t status =
      record_->start({&c_msg, wire_format_metadata}, initial_dispatcher_.get(), &opaque);
  if (status != ZX_OK) {
    return zx::error(status);
  }
  opaque_.emplace(opaque);
  return zx::ok();
}

uint32_t ExtractDefaultDispatcherOpts(const fuchsia_data::wire::Dictionary& program) {
  auto default_dispatcher_opts = driver::ProgramValueAsVector(program, "default_dispatcher_opts");

  uint32_t opts = 0;
  if (default_dispatcher_opts.is_ok()) {
    for (auto opt : *default_dispatcher_opts) {
      if (opt == "allow_sync_calls") {
        opts |= FDF_DISPATCHER_OPTION_ALLOW_SYNC_CALLS;
      } else {
        LOGF(WARNING, "Ignoring unknown default_dispatcher_opt: %s", opt.c_str());
      }
    }
  }
  return opts;
}

zx::status<fdf::Dispatcher> CreateDispatcher(fbl::RefPtr<Driver> driver, uint32_t dispatcher_opts) {
  auto name = GetManifest(driver->url());
  // The dispatcher must be shutdown before the dispatcher is destroyed.
  // Usually we will wait for the callback from |fdf_env::DriverShutdown| before destroying
  // the driver object (and hence the dispatcher).
  // In the case where we fail to start the driver, the driver object would be destructed
  // immediately, so here we hold an extra reference to the driver object to ensure the
  // dispatcher will not be destructed until shutdown completes.
  //
  // We do not destroy the dispatcher in the shutdown callback, to prevent crashes that
  // would happen if the driver attempts to access the dispatcher in its Stop hook.
  return fdf_env::DispatcherBuilder::CreateWithOwner(
      driver.get(), dispatcher_opts,
      fbl::StringPrintf("%.*s-default-%p", (int)name.size(), name.data(), driver.get()),
      [driver_ref = driver](fdf_dispatcher_t* dispatcher) {});
}

void LoadDriver(fuchsia_driver_framework::DriverStartArgs start_args,
                async_dispatcher_t* dispatcher,
                fit::callback<void(zx::status<LoadedDriver>)> callback) {
  if (!start_args.url()) {
    LOGF(ERROR, "Failed to start driver, missing 'url' argument");
    callback(zx::error(ZX_ERR_INVALID_ARGS));
    return;
  }
  if (!start_args.program().has_value()) {
    LOGF(ERROR, "Failed to start driver, missing 'program' argument");
    callback(zx::error(ZX_ERR_INVALID_ARGS));
    return;
  }
  const std::string& url = *start_args.url();
  fidl::Arena arena;
  fuchsia_data::wire::Dictionary wire_program = fidl::ToWire(arena, *start_args.program());

  auto driver_file = OpenDriverFile(start_args, wire_program);
  if (driver_file.is_error()) {
    LOGF(ERROR, "Failed to open driver '%s' file: %s", url.c_str(), driver_file.status_string());
    callback(driver_file.take_error());
    return;
  }

  uint32_t default_dispatcher_opts = dfv2::ExtractDefaultDispatcherOpts(wire_program);

  // Once we receive the VMO from the call to GetBackingMemory, we can load the driver into this
  // driver host. We move the storage and encoded for start_args into this callback to extend its
  // lifetime.
  fidl::SharedClient file(std::move(*driver_file), dispatcher,
                          std::make_unique<FileEventHandler>(url));
  auto vmo_callback =
      [start_args = std::move(start_args), default_dispatcher_opts, callback = std::move(callback),
       _ = file.Clone()](fidl::Result<fio::File::GetBackingMemory>& result) mutable {
        const std::string& url = *start_args.url();
        if (!result.is_ok()) {
          LOGF(ERROR, "Failed to start driver '%s', could not get library VMO: %s", url.c_str(),
               result.error_value().FormatDescription().c_str());
          zx_status_t status = result.error_value().is_application_error()
                                   ? result.error_value().application_error()
                                   : result.error_value().transport_error().status();
          callback(zx::error(status));
          return;
        }
        auto driver = Driver::Load(url, std::move(result->vmo()));
        if (driver.is_error()) {
          callback(driver.take_error());
          return;
        }

        zx::status<fdf::Dispatcher> driver_dispatcher =
            CreateDispatcher(*driver, default_dispatcher_opts);
        if (driver_dispatcher.is_error()) {
          callback(driver_dispatcher.take_error());
          return;
        }

        callback(zx::ok(LoadedDriver{
            .driver = std::move(*driver),
            .start_args = std::move(start_args),
            .dispatcher = std::move(*driver_dispatcher),
        }));
      };
  file->GetBackingMemory(fio::VmoFlags::kRead | fio::VmoFlags::kExecute |
                         fio::VmoFlags::kPrivateClone)
      .ThenExactlyOnce(std::move(vmo_callback));
}

}  // namespace dfv2
