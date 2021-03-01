// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/devices/bin/driver_host2/driver_host.h"

#include <fuchsia/io/llcpp/fidl.h>
#include <lib/async-loop/loop.h>
#include <lib/fdio/directory.h>
#include <lib/fit/function.h>
#include <zircon/dlfcn.h>

#include "src/devices/lib/driver2/start_args.h"
#include "src/devices/lib/log/log.h"
#include "src/lib/storage/vfs/cpp/service.h"

namespace fdf = llcpp::fuchsia::driver::framework;
namespace fio = llcpp::fuchsia::io;
namespace frunner = llcpp::fuchsia::component::runner;

namespace {

class FileEventHandler : public fio::File::AsyncEventHandler {
 public:
  explicit FileEventHandler(const std::string& binary_value) : binary_value_(binary_value) {}

  void Unbound(fidl::UnbindInfo info) override {
    if (info.status != ZX_OK) {
      LOGF(ERROR, "Failed to start driver '/pkg/%s', could not open library: %s",
           binary_value_.c_str(), zx_status_get_string(info.status));
    }
  }

 private:
  const std::string& binary_value_;
};

}  // namespace

zx::status<std::unique_ptr<Driver>> Driver::Load(std::string url, std::string binary, zx::vmo vmo) {
  void* library = dlopen_vmo(vmo.get(), RTLD_NOW);
  if (library == nullptr) {
    LOGF(ERROR, "Failed to start driver, could not load library: %s", dlerror());
    return zx::error(ZX_ERR_INTERNAL);
  }
  auto record = static_cast<DriverRecordV1*>(dlsym(library, "__fuchsia_driver_record__"));
  if (record == nullptr) {
    LOGF(ERROR, "Failed to start driver, driver record not found");
    return zx::error(ZX_ERR_NOT_FOUND);
  }
  if (record->version != 1) {
    LOGF(ERROR, "Failed to start driver, unknown driver record version: %lu", record->version);
    return zx::error(ZX_ERR_WRONG_TYPE);
  }
  return zx::ok(std::make_unique<Driver>(std::move(url), std::move(binary), library, record));
}

Driver::Driver(std::string url, std::string binary, void* library, DriverRecordV1* record)
    : url_(std::move(url)), binary_(std::move(binary)), library_(library), record_(record) {}

Driver::~Driver() {
  zx_status_t status = record_->stop(opaque_);
  if (status != ZX_OK) {
    LOGF(ERROR, "Failed to stop driver: %s", zx_status_get_string(status));
  }
  dlclose(library_);

  if (binding_.has_value()) {
    binding_->Unbind();
  }
}

void Driver::set_binding(
    fidl::ServerBindingRef<llcpp::fuchsia::driver::framework::Driver> binding) {
  binding_.emplace(std::move(binding));
}

zx::status<> Driver::Start(fidl::OutgoingMessage& message, async_dispatcher_t* dispatcher) {
  auto converted = fidl::OutgoingToIncomingMessage(message);
  if (converted.status() != ZX_OK) {
    return zx::make_status(converted.status());
  }
  record_->start(converted.incoming_message(), dispatcher, &opaque_);
  converted.ReleaseHandles();
  return zx::make_status(converted.status());
}

DriverHost::DriverHost(inspect::Inspector* inspector, async::Loop* loop) : loop_(loop) {
  inspector->GetRoot().CreateLazyNode(
      "drivers", [this] { return Inspect(); }, inspector);
}

fit::promise<inspect::Inspector> DriverHost::Inspect() {
  inspect::Inspector inspector;
  auto& root = inspector.GetRoot();
  size_t i = 0;
  for (auto& driver : drivers_) {
    auto child = root.CreateChild("driver-" + std::to_string(++i));
    child.CreateString("url", driver.url(), &inspector);
    child.CreateString("binary", driver.binary(), &inspector);
    inspector.emplace(std::move(child));
  }
  return fit::make_ok_promise(std::move(inspector));
}

zx::status<> DriverHost::PublishDriverHost(const fbl::RefPtr<fs::PseudoDir>& svc_dir) {
  const auto service = [this](zx::channel request) {
    auto result = fidl::BindServer(loop_->dispatcher(), std::move(request), this);
    if (result.is_error()) {
      LOGF(ERROR, "Failed to bind channel to '%s': %s", fdf::DriverHost::Name,
           zx_status_get_string(result.error()));
      return result.error();
    }
    return ZX_OK;
  };
  zx_status_t status =
      svc_dir->AddEntry(fdf::DriverHost::Name, fbl::MakeRefCounted<fs::Service>(service));
  if (status != ZX_OK) {
    LOGF(ERROR, "Failed to add directory entry '%s': %s", fdf::DriverHost::Name,
         zx_status_get_string(status));
  }
  return zx::make_status(status);
}

void DriverHost::Start(fdf::wire::DriverStartArgs start_args,
                       fidl::ServerEnd<llcpp::fuchsia::driver::framework::Driver> request,
                       StartCompleter::Sync& completer) {
  if (!start_args.has_url()) {
    LOGF(ERROR, "Failed to start driver, missing 'url' argument");
    completer.Close(ZX_ERR_INVALID_ARGS);
    return;
  }
  std::string url(start_args.url().get());
  auto pkg = start_args.has_ns() ? start_args::NsValue(start_args.ns(), "/pkg")
                                 : zx::error(ZX_ERR_INVALID_ARGS);
  if (pkg.is_error()) {
    LOGF(ERROR, "Failed to start driver, missing '/pkg' directory: %s",
         zx_status_get_string(pkg.error_value()));
    completer.Close(pkg.error_value());
    return;
  }
  zx::status<std::string> binary = start_args.has_program()
                                       ? start_args::ProgramValue(start_args.program(), "binary")
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
    completer.Close(endpoints.status_value());
    return;
  }
  zx_status_t status = fdio_open_at(pkg->handle(), binary->data(),
                                    fio::OPEN_RIGHT_READABLE | fio::OPEN_RIGHT_EXECUTABLE,
                                    endpoints->server.TakeChannel().release());
  if (status != ZX_OK) {
    LOGF(ERROR, "Failed to start driver '/pkg/%s', could not open library: %s", binary->data(),
         zx_status_get_string(status));
    completer.Close(status);
    return;
  }
  // We encode start_args outside of callback in order to access stack-allocated
  // data before it is destroyed.
  auto message = std::make_unique<fdf::wire::DriverStartArgs::OwnedEncodedMessage>(&start_args);
  if (!message->ok()) {
    LOGF(ERROR, "Failed to start driver '/pkg/%s', could not encode start args: %s", binary->data(),
         message->error());
    completer.Close(message->status());
    return;
  }

  // Once we receive the VMO from the call to GetBuffer, we can load the driver
  // into this driver host. We move the storage and encoded for start_args into
  // this callback to extend its lifetime.
  fidl::Client<fio::File> file(std::move(endpoints->client), loop_->dispatcher(),
                               std::make_shared<FileEventHandler>(binary.value()));
  auto callback = [this, request = std::move(request), completer = completer.ToAsync(),
                   url = std::move(url), binary = std::move(binary.value()),
                   message = std::move(message),
                   _ = file.Clone()](fio::File::GetBufferResponse* response) mutable {
    if (response->s != ZX_OK) {
      LOGF(ERROR, "Failed to start driver '/pkg/%s', could not get library VMO: %s", binary.data(),
           zx_status_get_string(response->s));
      completer.Close(response->s);
      return;
    }
    zx_status_t status =
        response->buffer->vmo.set_property(ZX_PROP_NAME, binary.data(), binary.size());
    if (status != ZX_OK) {
      LOGF(ERROR, "Failed to start driver '/pkg/%s', could not name library VMO: %s", binary.data(),
           zx_status_get_string(status));
      completer.Close(status);
      return;
    }
    auto driver = Driver::Load(std::move(url), std::move(binary), std::move(response->buffer->vmo));
    if (driver.is_error()) {
      completer.Close(driver.error_value());
      return;
    }
    auto driver_ptr = driver.value().get();
    auto bind = fidl::BindServer<Driver>(loop_->dispatcher(), std::move(request), driver_ptr,
                                         [this](Driver* driver, auto, auto) {
                                           drivers_.erase(*driver);
                                           // If this is the last driver, shutdown the driver host.
                                           if (drivers_.is_empty()) {
                                             loop_->Quit();
                                           }
                                         });
    if (bind.is_error()) {
      LOGF(ERROR,
           "Failed to start driver '/pkg/%s', could not bind channel to "
           "'fuchsia.driver.framework.DriverHost': %s",
           binary.data(), zx_status_get_string(bind.error()));
      completer.Close(bind.error());
      return;
    }
    driver->set_binding(bind.take_value());
    drivers_.push_back(std::move(driver.value()));

    auto start = driver_ptr->Start(message->GetOutgoingMessage(), loop_->dispatcher());
    if (start.is_error()) {
      LOGF(ERROR, "Failed to start driver '/pkg/%s': %s", binary.data(), start.status_string());
      completer.Close(start.error_value());
      return;
    }
    // After the driver successfully starts, we assume it has taken ownership of
    // the handles from |start_args|, and can therefore relinquish ownership.
    message->GetOutgoingMessage().ReleaseHandles();
    LOGF(INFO, "Started '%s'", binary.data());
  };
  file->GetBuffer(fio::VMO_FLAG_READ | fio::VMO_FLAG_EXEC | fio::VMO_FLAG_PRIVATE,
                  std::move(callback));
}
