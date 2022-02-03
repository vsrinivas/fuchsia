// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/devices/bin/driver_host2/driver_host.h"

#include <fidl/fuchsia.io/cpp/wire.h>
#include <lib/async/cpp/task.h>
#include <lib/fdio/directory.h>
#include <lib/fit/function.h>
#include <zircon/dlfcn.h>

#include "src/devices/lib/driver2/start_args.h"
#include "src/devices/lib/log/log.h"
#include "src/lib/storage/vfs/cpp/service.h"

namespace fdf = fuchsia_driver_framework;
namespace fio = fuchsia_io;
namespace frunner = fuchsia_component_runner;

namespace {

class FileEventHandler : public fidl::WireAsyncEventHandler<fio::File> {
 public:
  explicit FileEventHandler(std::string url) : url_(std::move(url)) {}

  void on_fidl_error(fidl::UnbindInfo info) override {
    LOGF(ERROR, "Failed to start driver '%s', could not open library: %s", url_.data(),
         info.FormatDescription().data());
  }

 private:
  std::string url_;
};

std::string_view GetManifest(std::string_view url) {
  auto i = url.rfind('/');
  return i == std::string_view::npos ? url : url.substr(i + 1);
}

}  // namespace

zx::status<std::unique_ptr<Driver>> Driver::Load(std::string url, zx::vmo vmo) {
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
  return zx::ok(std::make_unique<Driver>(std::move(url), library, record));
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

void Driver::set_binding(fidl::ServerBindingRef<fuchsia_driver_framework::Driver> binding) {
  binding_.emplace(std::move(binding));
}

void Driver::Stop(StopRequestView request, StopCompleter::Sync& completer) { binding_->Unbind(); }

zx::status<> Driver::Start(fidl::IncomingMessage& start_args,
                           async_dispatcher_t* driver_dispatcher) {
  // After calling |record_->start|, we assume it has taken ownership of
  // the handles from |start_args|, and can therefore relinquish ownership.
  fidl_incoming_msg_t c_msg = std::move(start_args).ReleaseToEncodedCMessage();
  void* opaque = nullptr;
  zx_status_t status = record_->start(&c_msg, driver_dispatcher, &opaque);
  if (status != ZX_OK) {
    return zx::error(status);
  }

  opaque_.emplace(opaque);
  return zx::ok();
}

DriverHost::DriverHost(inspect::Inspector& inspector, async::Loop& loop,
                       async_dispatcher_t* driver_dispatcher)
    : loop_(loop), driver_dispatcher_(driver_dispatcher) {
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

zx::status<> DriverHost::PublishDriverHost(const fbl::RefPtr<fs::PseudoDir>& svc_dir) {
  const auto service = [this](fidl::ServerEnd<fdf::DriverHost> request) {
    fidl::BindServer(loop_.dispatcher(), std::move(request), this);
    return ZX_OK;
  };
  zx_status_t status = svc_dir->AddEntry(fidl::DiscoverableProtocolName<fdf::DriverHost>,
                                         fbl::MakeRefCounted<fs::Service>(service));
  if (status != ZX_OK) {
    LOGF(ERROR, "Failed to add directory entry '%s': %s",
         fidl::DiscoverableProtocolName<fdf::DriverHost>, zx_status_get_string(status));
  }
  return zx::make_status(status);
}

void DriverHost::Start(StartRequestView request, StartCompleter::Sync& completer) {
  if (!request->start_args.has_url()) {
    LOGF(ERROR, "Failed to start driver, missing 'url' argument");
    completer.Close(ZX_ERR_INVALID_ARGS);
    return;
  }
  std::string url(request->start_args.url().get());
  auto pkg = request->start_args.has_ns() ? driver::NsValue(request->start_args.ns(), "/pkg")
                                          : zx::error(ZX_ERR_INVALID_ARGS);
  if (pkg.is_error()) {
    LOGF(ERROR, "Failed to start driver, missing '/pkg' directory: %s",
         zx_status_get_string(pkg.error_value()));
    completer.Close(pkg.error_value());
    return;
  }
  zx::status<std::string> binary =
      request->start_args.has_program()
          ? driver::ProgramValue(request->start_args.program(), "binary")
          : zx::error(ZX_ERR_INVALID_ARGS);
  if (binary.is_error()) {
    LOGF(ERROR, "Failed to start driver, missing 'binary' argument: %s",
         zx_status_get_string(binary.error_value()));
    completer.Close(binary.error_value());
    return;
  }
  // Open the driver's binary within the driver's package.
  auto endpoints = fidl::CreateEndpoints<fio::File>();
  if (endpoints.is_error()) {
    completer.Close(endpoints.error_value());
    return;
  }
  zx_status_t status = fdio_open_at(pkg->channel()->get(), binary->data(),
                                    fio::wire::kOpenRightReadable | fio::wire::kOpenRightExecutable,
                                    endpoints->server.TakeChannel().release());
  if (status != ZX_OK) {
    LOGF(ERROR, "Failed to start driver '%s', could not open library: %s", url.data(),
         zx_status_get_string(status));
    completer.Close(status);
    return;
  }
  // We encode start_args outside of callback in order to access stack-allocated
  // data before it is destroyed.
  // TODO(fxbug.dev/45252): Use FIDL at rest.
  fidl::OwnedEncodedMessage<fdf::wire::DriverStartArgs> message(
      fidl::internal::WireFormatVersion::kV1, &request->start_args);
  if (!message.ok()) {
    LOGF(ERROR, "Failed to start driver '%s', could not encode start args: %s", url.data(),
         message.FormatDescription().data());
    completer.Close(message.status());
    return;
  }

  // We convert the outgoing message into an incoming message to provide to the
  // driver on start.
  auto converted_message =
      std::make_unique<fidl::OutgoingToIncomingMessage>(message.GetOutgoingMessage());
  if (!converted_message->ok()) {
    LOGF(ERROR, "Failed to start driver '%s', could not convert start args: %s", url.data(),
         converted_message->FormatDescription().data());
    completer.Close(converted_message->status());
    return;
  }

  // Once we receive the VMO from the call to GetBuffer, we can load the driver
  // into this driver host. We move the storage and encoded for start_args into
  // this callback to extend its lifetime.
  fidl::WireSharedClient file(std::move(endpoints->client), loop_.dispatcher(),
                              std::make_unique<FileEventHandler>(url));
  auto callback = [this, request = std::move(request->driver), completer = completer.ToAsync(),
                   url = std::move(url), converted_message = std::move(converted_message),
                   _ = file.Clone()](
                      fidl::WireUnownedResult<fio::File::GetBuffer>& result) mutable {
    if (!result.ok()) {
      LOGF(ERROR, "Failed to start driver '%s', could not get library VMO: %s", url.data(),
           result.error().FormatDescription().c_str());
      completer.Close(result.status());
      return;
    }
    auto* response = result.Unwrap();
    if (response->s != ZX_OK) {
      LOGF(ERROR, "Failed to start driver '%s', could not get library VMO: %s", url.data(),
           zx_status_get_string(response->s));
      completer.Close(response->s);
      return;
    }
    // Give the driver's VMO a name. We can't fit the entire URL in the name, so
    // use the name of the manifest from the URL.
    auto manifest = GetManifest(url);
    zx_status_t status =
        response->buffer->vmo.set_property(ZX_PROP_NAME, manifest.data(), manifest.size());
    if (status != ZX_OK) {
      LOGF(ERROR, "Failed to start driver '%s', could not name library VMO: %s", url.data(),
           zx_status_get_string(status));
      completer.Close(status);
      return;
    }
    auto driver = Driver::Load(std::move(url), std::move(response->buffer->vmo));
    if (driver.is_error()) {
      completer.Close(driver.error_value());
      return;
    }

    // Task to start the driver. Post this to the driver dispatcher thread.
    auto start_task = [this, request = std::move(request), completer = std::move(completer),
                       converted_message = std::move(converted_message),
                       driver = std::move(*driver)]() mutable {
      auto start = driver->Start(converted_message->incoming_message(), driver_dispatcher_);
      if (start.is_error()) {
        LOGF(ERROR, "Failed to start driver '%s': %s", driver->url().data(), start.status_string());
        completer.Close(start.error_value());
        return;
      }
      LOGF(INFO, "Started '%s'", driver->url().data());

      auto unbind_callback = [this](Driver* driver, fidl::UnbindInfo info,
                                    fidl::ServerEnd<fuchsia_driver_framework::Driver> server) {
        if (!info.is_user_initiated()) {
          LOGF(WARNING, "Unexpected stop of driver '%s': %s", driver->url().data(),
               info.FormatDescription().data());
        }
        // Task to stop the driver. Post this to the driver dispatcher thread.
        auto stop_task = [this, driver, server = std::move(server)]() mutable {
          std::lock_guard<std::mutex> lock(mutex_);
          drivers_.erase(*driver);

          // Send the epitath to the driver runner letting it know we stopped
          // the driver correctly.
          server.Close(ZX_OK);

          // If this is the last driver, shutdown the driver host.
          if (drivers_.is_empty()) {
            loop_.Quit();
          }
        };
        async::PostTask(driver_dispatcher_, std::move(stop_task));
      };
      auto bind = fidl::BindServer(loop_.dispatcher(), std::move(request), driver.get(),
                                   std::move(unbind_callback));
      driver->set_binding(std::move(bind));

      std::lock_guard<std::mutex> lock(mutex_);
      drivers_.push_back(std::move(driver));
    };
    async::PostTask(driver_dispatcher_, std::move(start_task));
  };
  file->GetBuffer(fio::wire::kVmoFlagRead | fio::wire::kVmoFlagExec | fio::wire::kVmoFlagPrivate,
                  std::move(callback));
}
