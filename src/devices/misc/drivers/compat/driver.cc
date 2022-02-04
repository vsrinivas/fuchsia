// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/devices/misc/drivers/compat/driver.h"

#include <lib/async-loop/cpp/loop.h>
#include <lib/ddk/binding_priv.h>
#include <lib/fidl/llcpp/connect_service.h>
#include <lib/fpromise/bridge.h>
#include <lib/service/llcpp/service.h>
#include <zircon/dlfcn.h>

#include "src/devices/lib/compat/symbols.h"
#include "src/devices/lib/driver2/promise.h"
#include "src/devices/lib/driver2/record_cpp.h"
#include "src/devices/lib/driver2/start_args.h"
#include "src/devices/misc/drivers/compat/devfs_vnode.h"
#include "src/devices/misc/drivers/compat/loader.h"

namespace fboot = fuchsia_boot;
namespace fdf = fuchsia_driver_framework;
namespace fio = fuchsia_io;
namespace fldsvc = fuchsia_ldsvc;

using fpromise::bridge;
using fpromise::error;
using fpromise::join_promises;
using fpromise::ok;
using fpromise::promise;
using fpromise::result;

zx::resource kRootResource;

namespace {

constexpr auto kOpenFlags = fio::wire::kOpenRightReadable | fio::wire::kOpenRightExecutable |
                            fio::wire::kOpenFlagNotDirectory;
constexpr auto kVmoFlags = fio::wire::kVmoFlagRead | fio::wire::kVmoFlagExec;
constexpr auto kLibDriverPath = "/pkg/driver/compat.so";

template <typename T>
T GetSymbol(const fidl::VectorView<fdf::wire::NodeSymbol>& symbols, std::string_view name,
            T default_value = nullptr) {
  auto value = driver::SymbolValue<T>(symbols, name);
  return value.is_ok() ? *value : default_value;
}

}  // namespace

namespace compat {

Driver::Driver(async_dispatcher_t* dispatcher, fidl::WireSharedClient<fdf::Node> node,
               driver::Namespace ns, driver::Logger logger, std::string_view url,
               std::string_view name, void* context, const zx_protocol_device_t* ops,
               std::optional<Device*> linked_device)
    : dispatcher_(dispatcher),
      executor_(dispatcher),
      outgoing_(dispatcher),
      ns_(std::move(ns)),
      logger_(std::move(logger)),
      url_(url),
      device_(name, context, ops, std::nullopt, linked_device, inner_logger_, dispatcher) {
  device_.Bind(std::move(node));
}

Driver::~Driver() {
  if (record_ != nullptr && record_->ops->release != nullptr) {
    record_->ops->release(context_);
  }
  dlclose(library_);
}

zx_driver_t* Driver::ZxDriver() { return static_cast<zx_driver_t*>(this); }

zx::status<std::unique_ptr<Driver>> Driver::Start(fdf::wire::DriverStartArgs& start_args,
                                                  async_dispatcher_t* dispatcher,
                                                  fidl::WireSharedClient<fdf::Node> node,
                                                  driver::Namespace ns, driver::Logger logger) {
  fidl::VectorView<fdf::wire::NodeSymbol> symbols;
  if (start_args.has_symbols()) {
    symbols = start_args.symbols();
  }
  auto name = GetSymbol<const char*>(symbols, kName, "compat-device");
  auto context = GetSymbol<void*>(symbols, kContext);
  const zx_protocol_device_t* ops = nullptr;
  std::optional<Device*> parent_opt;
  if (auto parent = driver::SymbolValue<Device*>(symbols, kParent); parent.is_ok()) {
    parent_opt = *parent;
  }
  // Only look for an opts field if we don't have a linked parent.
  if (!parent_opt) {
    ops = GetSymbol<const zx_protocol_device_t*>(symbols, kOps);
  }

  // Open the compat driver's binary within the package.
  auto compat = driver::ProgramValue(start_args.program(), "compat");
  if (compat.is_error()) {
    FDF_LOGL(ERROR, logger, "Field \"compat\" missing from component manifest");
    return compat.take_error();
  }

  auto driver =
      std::make_unique<Driver>(dispatcher, std::move(node), std::move(ns), std::move(logger),
                               start_args.url().get(), name, context, ops, parent_opt);
  auto result = driver->Run(std::move(start_args.outgoing_dir()), "/pkg/" + *compat);
  if (result.is_error()) {
    return result.take_error();
  }
  return zx::ok(std::move(driver));
}

zx::status<> Driver::Run(fidl::ServerEnd<fio::Directory> outgoing_dir,
                         std::string_view driver_path) {
  auto serve = outgoing_.Serve(std::move(outgoing_dir));
  if (serve.is_error()) {
    return serve.take_error();
  }

  auto root_resource =
      driver::Connect<fboot::RootResource>(ns_, dispatcher_)
          .and_then(fit::bind_member<&Driver::GetRootResource>(this))
          .or_else([this](zx_status_t& status) {
            FDF_LOG(WARNING, "Failed to get root resource: %s", zx_status_get_string(status));
            FDF_LOG(WARNING, "Assuming test environment and continuing");
            return error(status);
          });
  auto loader_vmo = driver::Connect<fio::File>(ns_, dispatcher_, kLibDriverPath, kOpenFlags)
                        .and_then(fit::bind_member<&Driver::GetBuffer>(this));
  auto driver_vmo = driver::Connect<fio::File>(ns_, dispatcher_, driver_path, kOpenFlags)
                        .and_then(fit::bind_member<&Driver::GetBuffer>(this));
  auto start_driver =
      join_promises(std::move(root_resource), std::move(loader_vmo), std::move(driver_vmo))
          .then(fit::bind_member<&Driver::Join>(this))
          .and_then(fit::bind_member<&Driver::LoadDriver>(this))
          .and_then(fit::bind_member<&Driver::StartDriver>(this))
          .or_else(fit::bind_member<&Driver::StopDriver>(this))
          .wrap_with(scope_);
  executor_.schedule_task(std::move(start_driver));
  return zx::ok();
}

promise<zx::resource, zx_status_t> Driver::GetRootResource(
    const fidl::WireSharedClient<fboot::RootResource>& root_resource) {
  bridge<zx::resource, zx_status_t> bridge;
  auto callback = [completer = std::move(bridge.completer)](
                      fidl::WireUnownedResult<fboot::RootResource::Get>& result) mutable {
    if (!result.ok()) {
      completer.complete_error(result.status());
      return;
    }
    completer.complete_ok(std::move(result->resource));
  };
  root_resource->Get(std::move(callback));
  return bridge.consumer.promise_or(error(ZX_ERR_UNAVAILABLE));
}

promise<zx::vmo, zx_status_t> Driver::GetBuffer(const fidl::WireSharedClient<fio::File>& file) {
  bridge<zx::vmo, zx_status_t> bridge;
  auto callback = [completer = std::move(bridge.completer)](
                      fidl::WireUnownedResult<fio::File::GetBuffer>& result) mutable {
    if (!result.ok()) {
      completer.complete_error(result.status());
      return;
    }
    if (result->s != ZX_OK) {
      completer.complete_error(result->s);
      return;
    }
    completer.complete_ok(std::move(result->buffer->vmo));
  };
  file->GetBuffer(kVmoFlags, std::move(callback));
  return bridge.consumer.promise_or(error(ZX_ERR_UNAVAILABLE)).or_else([this](zx_status_t& status) {
    FDF_LOG(ERROR, "Failed to get buffer: %s", zx_status_get_string(status));
    return error(status);
  });
}

result<std::tuple<zx::vmo, zx::vmo>, zx_status_t> Driver::Join(
    result<std::tuple<result<zx::resource, zx_status_t>, result<zx::vmo, zx_status_t>,
                      result<zx::vmo, zx_status_t>>>& results) {
  if (results.is_error()) {
    return error(ZX_ERR_INTERNAL);
  }
  auto& [root_resource, loader_vmo, driver_vmo] = results.value();
  if (root_resource.is_ok()) {
    kRootResource = root_resource.take_value();
  }
  if (loader_vmo.is_error()) {
    return loader_vmo.take_error_result();
  }
  if (driver_vmo.is_error()) {
    return driver_vmo.take_error_result();
  }
  return fpromise::ok(std::make_tuple(loader_vmo.take_value(), driver_vmo.take_value()));
}

result<void, zx_status_t> Driver::LoadDriver(std::tuple<zx::vmo, zx::vmo>& vmos) {
  auto& [loader_vmo, driver_vmo] = vmos;

  // Replace loader service to load the DFv1 driver, load the driver,
  // then place the original loader service back.
  {
    auto endpoints = fidl::CreateEndpoints<fldsvc::Loader>();
    if (endpoints.is_error()) {
      return error(endpoints.status_value());
    }
    zx::channel loader_channel(dl_set_loader_service(endpoints->client.channel().release()));
    fidl::ClientEnd<fldsvc::Loader> loader_client(std::move(loader_channel));
    auto clone = fidl::CreateEndpoints<fldsvc::Loader>();
    if (clone.is_error()) {
      return error(clone.status_value());
    }
    auto result = fidl::WireCall(loader_client)->Clone(std::move(clone->server));
    if (!result.ok()) {
      FDF_LOG(ERROR, "Failed to load driver '%s', cloning loader failed with FIDL status: %s",
              url_.data(), result.status_string());
      return error(result.status());
    }
    if (result->rv != ZX_OK) {
      FDF_LOG(ERROR, "Failed to load driver '%s', cloning loader failed with status: %s",
              url_.data(), zx_status_get_string(result->rv));
      return error(result->rv);
    }

    // Start loader.
    async::Loop loader_loop(&kAsyncLoopConfigNeverAttachToThread);
    zx_status_t status = loader_loop.StartThread("loader-loop");
    if (status != ZX_OK) {
      FDF_LOG(ERROR, "Failed to load driver '%s', could not start thread for loader loop: %s",
              url_.data(), zx_status_get_string(status));
      return error(status);
    }
    Loader loader(loader_loop.dispatcher());
    auto bind = loader.Bind(fidl::ClientEnd<fldsvc::Loader>(std::move(loader_client)),
                            std::move(loader_vmo));
    if (bind.is_error()) {
      return error(bind.status_value());
    }
    fidl::BindServer(loader_loop.dispatcher(), std::move(endpoints->server), &loader);

    // Open driver.
    library_ = dlopen_vmo(driver_vmo.get(), RTLD_NOW);
    if (library_ == nullptr) {
      FDF_LOG(ERROR, "Failed to load driver '%s', could not load library: %s", url_.data(),
              dlerror());
      return error(ZX_ERR_INTERNAL);
    }

    // Return original loader service.
    loader_channel.reset(dl_set_loader_service(clone->client.channel().release()));
  }

  // Load and verify symbols.
  auto note = static_cast<const zircon_driver_note_t*>(dlsym(library_, "__zircon_driver_note__"));
  if (note == nullptr) {
    FDF_LOG(ERROR, "Failed to load driver '%s', driver note not found", url_.data());
    return error(ZX_ERR_BAD_STATE);
  }
  FDF_LOG(INFO, "Loaded driver '%s'", note->payload.name);
  record_ = static_cast<zx_driver_rec_t*>(dlsym(library_, "__zircon_driver_rec__"));
  if (record_ == nullptr) {
    FDF_LOG(ERROR, "Failed to load driver '%s', driver record not found", url_.data());
    return error(ZX_ERR_BAD_STATE);
  }
  if (record_->ops == nullptr) {
    FDF_LOG(ERROR, "Failed to load driver '%s', missing driver ops", url_.data());
    return error(ZX_ERR_BAD_STATE);
  }
  if (record_->ops->version != DRIVER_OPS_VERSION) {
    FDF_LOG(ERROR, "Failed to load driver '%s', incorrect driver version", url_.data());
    return error(ZX_ERR_WRONG_TYPE);
  }
  if (record_->ops->bind == nullptr && record_->ops->create == nullptr) {
    FDF_LOG(ERROR, "Failed to load driver '%s', missing '%s'", url_.data(),
            (record_->ops->bind == nullptr ? "bind" : "create"));
    return error(ZX_ERR_BAD_STATE);
  } else if (record_->ops->bind != nullptr && record_->ops->create != nullptr) {
    FDF_LOG(ERROR, "Failed to load driver '%s', both 'bind' and 'create' are defined", url_.data());
    return error(ZX_ERR_INVALID_ARGS);
  }
  record_->driver = ZxDriver();

  // Create logger.
  auto inner_logger = driver::Logger::Create(ns_, dispatcher_, note->payload.name);
  if (inner_logger.is_error()) {
    return error(inner_logger.status_value());
  }
  inner_logger_ = std::move(*inner_logger);

  // Create the devfs exporter.
  auto exporter =
      driver::DevfsExporter::Create(ns_, dispatcher_, outgoing_.vfs(), outgoing_.svc_dir());
  if (exporter.is_error()) {
    return error(exporter.error_value());
  }
  exporter_ = std::move(*exporter);

  return ok();
}

result<void, zx_status_t> Driver::StartDriver() {
  if (record_->ops->init != nullptr) {
    // If provided, run init.
    zx_status_t status = record_->ops->init(&context_);
    if (status != ZX_OK) {
      FDF_LOG(ERROR, "Failed to load driver '%s', 'init' failed: %s", url_.data(),
              zx_status_get_string(status));
      return error(status);
    }
  }
  if (record_->ops->bind != nullptr) {
    // If provided, run bind and return.
    zx_status_t status = record_->ops->bind(context_, device_.ZxDevice());
    if (status != ZX_OK) {
      FDF_LOG(ERROR, "Failed to load driver '%s', 'bind' failed: %s", url_.data(),
              zx_status_get_string(status));
      return error(status);
    }
  } else {
    // Else, run create and return.
    auto client_end = ns_.Connect<fboot::Items>();
    if (client_end.is_error()) {
      return error(client_end.status_value());
    }
    zx_status_t status = record_->ops->create(context_, device_.ZxDevice(), "proxy", "",
                                              client_end->channel().release());
    if (status != ZX_OK) {
      FDF_LOG(ERROR, "Failed to load driver '%s', 'create' failed: %s", url_.data(),
              zx_status_get_string(status));
      return error(status);
    }
  }
  if (!device_.HasChildren()) {
    FDF_LOG(ERROR, "Driver '%s' did not add a child device", url_.data());
    return error(ZX_ERR_BAD_STATE);
  }
  return ok();
}

result<> Driver::StopDriver(const zx_status_t& status) {
  FDF_LOG(ERROR, "Failed to start driver '%s': %s", url_.data(), zx_status_get_string(status));
  device_.Unbind();
  return ok();
}

void* Driver::Context() const { return context_; }

void Driver::Log(FuchsiaLogSeverity severity, const char* tag, const char* file, int line,
                 const char* msg, va_list args) {
  inner_logger_.logvf(severity, tag, file, line, msg, args);
}

zx_status_t Driver::AddDevice(Device* parent, device_add_args_t* args, zx_device_t** out) {
  zx_device_t* child;
  zx_status_t status = parent->Add(args, &child);
  if (status != ZX_OK) {
    return status;
  }
  if (out) {
    *out = child;
  }
  std::string child_protocol = "device-" + std::to_string(next_device_id_);
  next_device_id_++;

  // Create a devfs entry for the new device.
  auto vnode = fbl::MakeRefCounted<DevfsVnode>(child->ZxDevice(), logger_);
  outgoing_.svc_dir()->AddEntry(child_protocol, vnode);

  auto devfs_name = std::string(child->topological_path());
  // TODO(fxdebug.dev/90735): When DriverDevelopment works in DFv2, don't print this.
  FDF_LOG(INFO, "Created /dev/%s", devfs_name.data());

  // Export the devfs entry. Once the export is complete, put a callback on the
  // Device so that when the Device is removed the Vnode will also be removed.
  // This assumes that Driver will always outlive Device.
  auto task =
      exporter_.Export(child_protocol, devfs_name, args->proto_id)
          .and_then([this, child_protocol, child, vnode = std::move(vnode)]() {
            child->SetVnodeTeardownCallback([this, child_protocol, vnode = std::move(vnode)]() {
              outgoing_.svc_dir()->RemoveEntry(child_protocol);
              outgoing_.vfs().CloseAllConnectionsForVnode(*vnode, {});
            });
          })
          .or_else([this](const zx_status_t& status) {
            FDF_LOG(ERROR, "Failed Export to devfs: %s", zx_status_get_string(status));
            return ok();
          })
          .wrap_with(child->scope())
          .wrap_with(scope_);
  executor_.schedule_task(std::move(task));

  return status;
}

}  // namespace compat

FUCHSIA_DRIVER_RECORD_CPP_V1(compat::Driver);
