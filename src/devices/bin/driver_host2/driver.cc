// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/devices/bin/driver_host2/driver.h"

#include <lib/driver2/start_args.h>
#include <lib/fdf/cpp/dispatcher.h>
#include <lib/fdf/cpp/internal.h>
#include <lib/fit/defer.h>
#include <zircon/dlfcn.h>

#include <fbl/string_printf.h>

#include "src/devices/lib/log/log.h"

namespace fdh = fuchsia_driver_host;

namespace dfv2 {

namespace {

std::string_view GetManifest(std::string_view url) {
  auto index = url.rfind('/');
  return index == std::string_view::npos ? url : url.substr(index + 1);
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
  // Let the driver runtime know which driver this dispatcher is for.
  // Since we haven't entered the driver yet, the runtime cannot detect
  // which driver this dispatcher is associated with.
  fdf_internal_push_driver(driver.get());
  auto pop_driver = fit::defer([]() { fdf_internal_pop_driver(); });

  // The dispatcher must be shutdown before the dispatcher is destroyed.
  // Usually we will wait for the callback from |fdf_internal::DriverShutdown| before destroying
  // the driver object (and hence the dispatcher).
  // In the case where we fail to start the driver, the driver object would be destructed
  // immediately, so here we hold an extra reference to the driver object to ensure the
  // dispatcher will not be destructed until shutdown completes.
  //
  // We do not destroy the dispatcher in the shutdown callback, to prevent crashes that
  // would happen if the driver attempts to access the dispatcher in its Stop hook.
  return fdf::Dispatcher::Create(
      dispatcher_opts,
      fbl::StringPrintf("%.*s-default-%p", (int)name.size(), name.data(), driver.get()),
      [driver_ref = driver](fdf_dispatcher_t* dispatcher) {});
}

}  // namespace dfv2
