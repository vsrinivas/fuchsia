// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "driver_host.h"

#include <dlfcn.h>
#include <fidl/fuchsia.device.manager/cpp/wire.h>
#include <inttypes.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/async/cpp/receiver.h>
#include <lib/async/cpp/wait.h>
#include <lib/ddk/binding.h>
#include <lib/ddk/debug.h>
#include <lib/ddk/device.h>
#include <lib/ddk/driver.h>
#include <lib/fdio/fdio.h>
#include <lib/fidl/coding.h>
#include <lib/zx/debuglog.h>
#include <lib/zx/process.h>
#include <lib/zx/resource.h>
#include <lib/zx/vmo.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <zircon/assert.h>
#include <zircon/dlfcn.h>
#include <zircon/process.h>
#include <zircon/processargs.h>
#include <zircon/status.h>
#include <zircon/syscalls.h>
#include <zircon/syscalls/log.h>

#include <memory>
#include <new>
#include <utility>
#include <vector>

#include <fbl/auto_lock.h>
#include <fbl/function.h>
#include <fbl/string_printf.h>

#include "async_loop_owned_rpc_handler.h"
#include "composite_device.h"
#include "device_controller_connection.h"
#include "env.h"
#include "fidl_txn.h"
#include "log.h"
#include "main.h"
#include "proxy_iostate.h"
#include "scheduler_profile.h"
#include "tracing.h"

namespace {

bool property_value_type_valid(uint32_t value_type) {
  return value_type > ZX_DEVICE_PROPERTY_VALUE_UNDEFINED &&
         value_type <= ZX_DEVICE_PROPERTY_VALUE_BOOL;
}

fuchsia_device_manager::wire::DeviceProperty convert_device_prop(const zx_device_prop_t& prop) {
  return fuchsia_device_manager::wire::DeviceProperty{
      .id = prop.id,
      .reserved = prop.reserved,
      .value = prop.value,
  };
}

fuchsia_device_manager::wire::DeviceStrProperty convert_device_str_prop(
    const zx_device_str_prop_t& prop, fidl::AnyArena& allocator) {
  ZX_ASSERT(property_value_type_valid(prop.property_value.value_type));

  auto str_property = fuchsia_device_manager::wire::DeviceStrProperty{
      .key = fidl::StringView(allocator, prop.key),
  };

  if (prop.property_value.value_type == ZX_DEVICE_PROPERTY_VALUE_INT) {
    str_property.value = fuchsia_device_manager::wire::PropertyValue::WithIntValue(
        prop.property_value.value.int_val);
  } else if (prop.property_value.value_type == ZX_DEVICE_PROPERTY_VALUE_STRING) {
    str_property.value = fuchsia_device_manager::wire::PropertyValue::WithStrValue(
        fidl::ObjectView<fidl::StringView>(allocator, allocator,
                                           prop.property_value.value.str_val));
  } else if (prop.property_value.value_type == ZX_DEVICE_PROPERTY_VALUE_BOOL) {
    str_property.value = fuchsia_device_manager::wire::PropertyValue::WithBoolValue(
        prop.property_value.value.bool_val);
  }

  return str_property;
}

static fx_log_severity_t log_min_severity(const char* name, const char* flag) {
  if (!strcasecmp(flag, "error")) {
    return FX_LOG_ERROR;
  }
  if (!strcasecmp(flag, "warning")) {
    return FX_LOG_WARNING;
  }
  if (!strcasecmp(flag, "info")) {
    return FX_LOG_INFO;
  }
  if (!strcasecmp(flag, "debug")) {
    return FX_LOG_DEBUG;
  }
  if (!strcasecmp(flag, "trace")) {
    return FX_LOG_TRACE;
  }
  if (!strcasecmp(flag, "serial")) {
    return DDK_LOG_SERIAL;
  }
  LOGF(WARNING, "Invalid minimum log severity '%s' for driver '%s', will log all", flag, name);
  return FX_LOG_ALL;
}

zx_status_t log_rpc_result(const fbl::RefPtr<zx_device_t>& dev, const char* opname,
                           zx_status_t status, zx_status_t call_status = ZX_OK) {
  if (status != ZX_OK) {
    constexpr char kLogFormat[] = "Failed %s RPC: %s";
    if (status == ZX_ERR_PEER_CLOSED) {
      // TODO(https://fxbug.dev/52627): change to an ERROR log once driver
      // manager can shut down gracefully.
      LOGD(WARNING, *dev, kLogFormat, opname, zx_status_get_string(status));
    } else {
      LOGD(ERROR, *dev, kLogFormat, opname, zx_status_get_string(status));
    }
    return status;
  }
  if (call_status != ZX_OK && call_status != ZX_ERR_NOT_FOUND) {
    LOGD(ERROR, *dev, "Failed %s: %s", opname, zx_status_get_string(call_status));
  }
  return call_status;
}

}  // namespace

const char* mkdevpath(const zx_device_t& dev, char* const path, size_t max) {
  if (max == 0) {
    return "";
  }
  char* end = path + max;
  char sep = 0;

  auto append_name = [&end, &path, &sep](const zx_device_t& dev) {
    *(--end) = sep;

    size_t len = strlen(dev.name());
    if (len > static_cast<size_t>(end - path)) {
      return;
    }
    end -= len;
    memcpy(end, dev.name(), len);
    sep = '/';
  };

  append_name(dev);

  fbl::RefPtr<zx_device> itr_dev = dev.parent();
  while (itr_dev && end > path) {
    append_name(*itr_dev);
    itr_dev = itr_dev->parent();
  }

  // If devpath is longer than |max|, add an ellipsis.
  constexpr char ellipsis[] = "...";
  constexpr size_t ellipsis_len = sizeof(ellipsis) - 1;
  if (*end == sep && max > ellipsis_len) {
    if (ellipsis_len > static_cast<size_t>(end - path)) {
      end = path;
    } else {
      end -= ellipsis_len;
    }
    memcpy(end, ellipsis, ellipsis_len);
  }

  return end;
}

zx_status_t zx_driver::Create(std::string_view libname, InspectNodeCollection& drivers,
                              fbl::RefPtr<zx_driver>* out_driver) {
  char process_name[ZX_MAX_NAME_LEN] = {};
  zx::process::self()->get_property(ZX_PROP_NAME, process_name, sizeof(process_name));
  const char* tags[] = {process_name, "driver"};
  fx_logger_config_t config{
      .min_severity = FX_LOG_SEVERITY_DEFAULT,
      .console_fd = getenv_bool("devmgr.log-to-debuglog", false) ? dup(STDOUT_FILENO) : -1,
      .log_service_channel = ZX_HANDLE_INVALID,
      .tags = tags,
      .num_tags = std::size(tags),
  };
  fx_logger_t* logger;
  zx_status_t status = fx_logger_create(&config, &logger);
  if (status != ZX_OK) {
    return status;
  }

  *out_driver = fbl::AdoptRef(new zx_driver(logger, libname, drivers));
  return ZX_OK;
}

zx_driver::zx_driver(fx_logger_t* logger, std::string_view libname, InspectNodeCollection& drivers)
    : logger_(logger), libname_(libname), inspect_(drivers, std::string(libname)) {}

zx_driver::~zx_driver() { fx_logger_destroy(logger_); }

zx_status_t zx_driver::ReconfigureLogger(cpp20::span<const char* const> tags) const {
  char process_name[ZX_MAX_NAME_LEN] = {};
  zx::process::self()->get_property(ZX_PROP_NAME, process_name, sizeof(process_name));
  std::vector<const char*> new_tags = {name(), process_name, "driver"};
  new_tags.insert(new_tags.end(), tags.begin(), tags.end());
  fx_logger_config_t config{
      .min_severity = FX_LOG_SEVERITY_DEFAULT,
      .console_fd = getenv_bool("devmgr.log-to-debuglog", false) ? dup(STDOUT_FILENO) : -1,
      .log_service_channel = ZX_HANDLE_INVALID,
      .tags = std::data(new_tags),
      .num_tags = std::size(new_tags),
  };
  return fx_logger_reconfigure(logger(), &config);
}

void DriverHostContext::SetupDriverHostController(
    fidl::ServerEnd<fuchsia_device_manager::DriverHostController> request) {
  auto conn = std::make_unique<internal::DriverHostControllerConnection>(this);

  internal::DriverHostControllerConnection::Bind(std::move(conn), std::move(request),
                                                 loop_.dispatcher());
}

// Send message to driver_manager asking to add child device to
// parent device.  Called under the api lock.
zx_status_t DriverHostContext::DriverManagerAdd(const fbl::RefPtr<zx_device_t>& parent,
                                                const fbl::RefPtr<zx_device_t>& child,
                                                const char* proxy_args,
                                                const zx_device_prop_t* props, uint32_t prop_count,
                                                const zx_device_str_prop_t* str_props,
                                                uint32_t str_prop_count, zx::vmo inspect,
                                                zx::channel client_remote) {
  using fuchsia_device_manager::wire::AddDeviceConfig;
  AddDeviceConfig add_device_config;

  if (child->flags() & DEV_FLAG_ALLOW_MULTI_COMPOSITE) {
    add_device_config |= AddDeviceConfig::kAllowMultiComposite;
  }
  if (child->flags() & DEV_FLAG_UNBINDABLE) {
    add_device_config |= AddDeviceConfig::kSkipAutobind;
  }

  auto coordinator_endpoints = fidl::CreateEndpoints<fuchsia_device_manager::Coordinator>();
  if (coordinator_endpoints.is_error()) {
    return coordinator_endpoints.status_value();
  }

  auto controller_endpoints = fidl::CreateEndpoints<fuchsia_device_manager::DeviceController>();
  if (controller_endpoints.is_error()) {
    return controller_endpoints.status_value();
  }

  auto coordinator =
      fidl::WireSharedClient(std::move(coordinator_endpoints->client), loop_.dispatcher());
  auto conn = DeviceControllerConnection::Create(this, child, std::move(coordinator));

  std::vector<fuchsia_device_manager::wire::DeviceProperty> props_list = {};
  for (size_t i = 0; i < prop_count; i++) {
    props_list.push_back(convert_device_prop(props[i]));
  }

  fidl::Arena allocator;
  std::vector<fuchsia_device_manager::wire::DeviceStrProperty> str_props_list = {};
  for (size_t i = 0; i < str_prop_count; i++) {
    if (!property_value_type_valid(str_props[i].property_value.value_type)) {
      return ZX_ERR_INVALID_ARGS;
    }
    str_props_list.push_back(convert_device_str_prop(str_props[i], allocator));
  }

  const auto& coordinator_client = parent->coordinator_client;
  if (!coordinator_client) {
    return ZX_ERR_IO_REFUSED;
  }
  size_t proxy_args_len = proxy_args ? strlen(proxy_args) : 0;
  zx_status_t call_status = ZX_OK;
  static_assert(sizeof(zx_device_prop_t) == sizeof(uint64_t));
  uint64_t device_id = 0;

  ::fuchsia_device_manager::wire::DevicePropertyList property_list = {
      .props = ::fidl::VectorView<fuchsia_device_manager::wire::DeviceProperty>::FromExternal(
          props_list),
      .str_props =
          ::fidl::VectorView<fuchsia_device_manager::wire::DeviceStrProperty>::FromExternal(
              str_props_list),
  };

  auto response = coordinator_client->AddDevice_Sync(
      std::move(coordinator_endpoints->server), std::move(controller_endpoints->client),
      property_list, ::fidl::StringView::FromExternal(child->name()), child->protocol_id(),
      ::fidl::StringView::FromExternal(child->driver->libname()),
      ::fidl::StringView::FromExternal(proxy_args, proxy_args_len), add_device_config,
      child->ops()->init /* has_init */, std::move(inspect), std::move(client_remote));
  zx_status_t status = response.status();
  if (status == ZX_OK) {
    if (response.Unwrap()->result.is_response()) {
      device_id = response.Unwrap()->result.response().local_device_id;
      if (child->ops()->init) {
        // Mark child as invisible until the init function is replied.
        child->set_flag(DEV_FLAG_INVISIBLE);
      }
    } else {
      call_status = response.Unwrap()->result.err();
    }
  }

  status = log_rpc_result(parent, "add-device", status, call_status);
  if (status != ZX_OK) {
    return status;
  }

  child->set_local_id(device_id);
  DeviceControllerConnection::Bind(std::move(conn), std::move(controller_endpoints->server),
                                   loop_.dispatcher());
  return ZX_OK;
}

// Send message to driver_manager informing it that this device
// is being removed.  Called under the api lock.
zx_status_t DriverHostContext::DriverManagerRemove(fbl::RefPtr<zx_device_t> dev) {
  fbl::AutoLock al(&dev->controller_lock);
  if (!dev->controller_binding) {
    LOGD(ERROR, *dev, "Invalid device controller connection");
    return ZX_ERR_INTERNAL;
  }
  VLOGD(1, *dev, "Removing device %p", dev.get());

  // Close all connections to the device vnode and drop it, since no one should be able to
  // open connections anymore. This will break the reference cycle between the DevfsVnode
  // and the zx_device.
  vfs_.CloseAllConnectionsForVnode(*(dev->vnode), [dev]() { dev->vnode.reset(); });

  // respond to the remove fidl call
  dev->removal_cb(ZX_OK);

  // Forget our local ID, to release the reference stored by the local ID map
  dev->set_local_id(0);

  // Forget about our coordinator channel since after the Unbind below it may be
  // closed.
  dev->coordinator_client = {};

  // queue an event to destroy the connection
  dev->controller_binding->Unbind();
  dev->controller_binding.reset();

  // shut down our proxy rpc channel if it exists
  ProxyIosDestroy(dev);

  return ZX_OK;
}

void DriverHostContext::ProxyIosDestroy(const fbl::RefPtr<zx_device_t>& dev) {
  fbl::AutoLock guard(&dev->proxy_ios_lock);

  if (dev->proxy_ios) {
    dev->proxy_ios->CancelLocked(loop_.dispatcher());
  }
}

zx_status_t DriverHostContext::FindDriver(std::string_view libname, zx::vmo vmo,
                                          fbl::RefPtr<zx_driver_t>* out) {
  // check for already-loaded driver first
  for (auto& drv : drivers_) {
    if (!libname.compare(drv.libname())) {
      *out = fbl::RefPtr(&drv);
      return drv.status();
    }
  }

  fbl::RefPtr<zx_driver> new_driver;
  zx_status_t status = zx_driver::Create(libname, inspect().drivers(), &new_driver);
  if (status != ZX_OK) {
    return status;
  }

  // Let the |drivers_| list and our out parameter each have a refcount.
  drivers_.push_back(new_driver);
  *out = new_driver;

  const char* c_libname = new_driver->libname().c_str();

  void* dl = dlopen_vmo(vmo.get(), RTLD_NOW);
  if (dl == nullptr) {
    LOGF(ERROR, "Cannot load '%s': %s", c_libname, dlerror());
    new_driver->set_status(ZX_ERR_IO);
    return new_driver->status();
  }

  auto dn = static_cast<const zircon_driver_note_t*>(dlsym(dl, "__zircon_driver_note__"));
  if (dn == nullptr) {
    LOGF(ERROR, "Driver '%s' missing __zircon_driver_note__ symbol", c_libname);
    new_driver->set_status(ZX_ERR_IO);
    return new_driver->status();
  }
  auto ops = static_cast<const zx_driver_ops_t**>(dlsym(dl, "__zircon_driver_ops__"));
  auto dr = static_cast<zx_driver_rec_t*>(dlsym(dl, "__zircon_driver_rec__"));
  if (dr == nullptr) {
    LOGF(ERROR, "Driver '%s' missing __zircon_driver_rec__ symbol", c_libname);
    new_driver->set_status(ZX_ERR_IO);
    return new_driver->status();
  }
  // TODO(kulakowski) Eventually just check __zircon_driver_ops__,
  // when bind programs are standalone.
  if (ops == nullptr) {
    ops = &dr->ops;
  }
  if (!(*ops)) {
    LOGF(ERROR, "Driver '%s' has nullptr ops", c_libname);
    new_driver->set_status(ZX_ERR_INVALID_ARGS);
    return new_driver->status();
  }
  if ((*ops)->version != DRIVER_OPS_VERSION) {
    LOGF(ERROR, "Driver '%s' has bad driver ops version %#lx, expecting %#lx", c_libname,
         (*ops)->version, DRIVER_OPS_VERSION);
    new_driver->set_status(ZX_ERR_INVALID_ARGS);
    return new_driver->status();
  }

  new_driver->set_driver_rec(dr);
  new_driver->set_name(dn->payload.name);
  new_driver->set_ops(*ops);
  dr->driver = new_driver.get();

  // Check for minimum log severity of driver.
  const auto flag_name = fbl::StringPrintf("driver.%s.log", new_driver->name());
  const char* flag_value = getenv(flag_name.data());
  if (flag_value != nullptr) {
    fx_log_severity_t min_severity = log_min_severity(new_driver->name(), flag_value);
    status = fx_logger_set_min_severity(new_driver->logger(), min_severity);
    if (status != ZX_OK) {
      LOGF(ERROR, "Failed to set minimum log severity for driver '%s': %s", new_driver->name(),
           zx_status_get_string(status));
    } else {
      LOGF(INFO, "Driver '%s' set minimum log severity to %d", new_driver->name(), min_severity);
    }
  }

  if (new_driver->has_init_op()) {
    new_driver->set_status(new_driver->InitOp());
    if (new_driver->status() != ZX_OK) {
      LOGF(ERROR, "Driver '%s' failed in init: %s", c_libname,
           zx_status_get_string(new_driver->status()));
    }
  } else {
    new_driver->set_status(ZX_OK);
  }

  return new_driver->status();
}

namespace internal {

namespace {

// We need a global pointer to a DriverHostContext so that we can implement the functions exported
// to drivers.  Some of these functions unfortunately do not take an argument that can be used to
// find a context.
DriverHostContext* kContextForApi = nullptr;

}  // namespace

void RegisterContextForApi(DriverHostContext* context) {
  ZX_ASSERT((context == nullptr) != (kContextForApi == nullptr));
  kContextForApi = context;
}
DriverHostContext* ContextForApi() { return kContextForApi; }

void DriverHostControllerConnection::CreateDevice(CreateDeviceRequestView request,
                                                  CreateDeviceCompleter::Sync& completer) {
  std::string_view driver_path(request->driver_path.data(), request->driver_path.size());
  // This does not operate under the driver_host api lock,
  // since the newly created device is not visible to
  // any API surface until a driver is bound to it.
  // (which can only happen via another message on this thread)

  // named driver -- ask it to create the device
  fbl::RefPtr<zx_driver_t> drv;
  zx_status_t r = driver_host_context_->FindDriver(driver_path, std::move(request->driver), &drv);
  if (r != ZX_OK) {
    LOGF(ERROR, "Failed to load driver '%.*s': %s", static_cast<int>(driver_path.size()),
         driver_path.data(), zx_status_get_string(r));
    return;
  }
  if (!drv->has_create_op()) {
    LOGF(ERROR, "Driver does not support create operation");
    return;
  }

  auto coordinator = fidl::WireSharedClient(std::move(request->coordinator_rpc),
                                            driver_host_context_->loop().dispatcher());

  // Create a dummy parent device for use in this call to Create
  fbl::RefPtr<zx_device> parent;
  r = zx_device::Create(driver_host_context_, "device_create dummy", drv.get(), &parent);
  if (r != ZX_OK) {
    LOGF(ERROR, "Failed to create device: %s", zx_status_get_string(r));
    return;
  }
  // magic cookie for device create handshake
  CreationContext creation_context = {
      .parent = std::move(parent),
      .child = nullptr,
      .coordinator_client = coordinator.Clone(),
  };

  r = drv->CreateOp(&creation_context, creation_context.parent, "proxy", request->proxy_args.data(),
                    request->parent_proxy.release());

  // Suppress a warning about dummy device being in a bad state.  The
  // message is spurious in this case, since the dummy parent never
  // actually begins its device lifecycle.  This flag is ordinarily
  // set by device_remove().
  creation_context.parent->set_flag(DEV_FLAG_DEAD);

  if (r != ZX_OK) {
    constexpr char kLogFormat[] = "Failed to create driver: %s";
    if (r == ZX_ERR_PEER_CLOSED) {
      // TODO(https://fxbug.dev/52627): change to an ERROR log once driver
      // manager can shut down gracefully.
      LOGF(WARNING, kLogFormat, zx_status_get_string(r));
    } else {
      LOGF(ERROR, kLogFormat, zx_status_get_string(r));
    }
    return;
  }

  auto new_device = std::move(creation_context.child);
  if (new_device == nullptr) {
    LOGF(ERROR, "Driver did not create a device");
    return;
  }

  new_device->set_local_id(request->local_device_id);
  auto newconn = DeviceControllerConnection::Create(driver_host_context_, std::move(new_device),
                                                    std::move(coordinator));

  // TODO: inform devcoord
  VLOGF(1, "Created device %p '%.*s'", new_device.get(), static_cast<int>(driver_path.size()),
        driver_path.data());

  DeviceControllerConnection::Bind(std::move(newconn), std::move(request->device_controller_rpc),
                                   driver_host_context_->loop().dispatcher());
}

void DriverHostControllerConnection::CreateCompositeDevice(
    CreateCompositeDeviceRequestView request, CreateCompositeDeviceCompleter::Sync& completer) {
  // Convert the fragment IDs into zx_device references
  CompositeFragments fragments_list(new CompositeFragment[request->fragments.count()],
                                    request->fragments.count());
  {
    // Acquire the API lock so that we don't have to worry about concurrent
    // device removes
    fbl::AutoLock lock(&driver_host_context_->api_lock());

    for (size_t i = 0; i < request->fragments.count(); ++i) {
      const auto& fragment = request->fragments.data()[i];
      uint64_t local_id = fragment.id;
      fbl::RefPtr<zx_device_t> dev = zx_device::GetDeviceFromLocalId(local_id);
      if (dev == nullptr || (dev->flags() & DEV_FLAG_DEAD)) {
        completer.Reply(ZX_ERR_NOT_FOUND);
        return;
      }
      fragments_list[i].name = std::string(fragment.name.data(), fragment.name.size());
      fragments_list[i].device = std::move(dev);
    }
  }

  auto driver = GetCompositeDriver(driver_host_context_);
  if (driver == nullptr) {
    completer.Reply(ZX_ERR_INTERNAL);
    return;
  }

  fbl::RefPtr<zx_device_t> dev;
  static_assert(fuchsia_device_manager::wire::kDeviceNameMax + 1 >= sizeof(dev->name()));
  zx_status_t status = zx_device::Create(driver_host_context_,
                                         std::string(request->name.data(), request->name.size()),
                                         driver.get(), &dev);
  if (status != ZX_OK) {
    completer.Reply(status);
    return;
  }
  dev->set_local_id(request->local_device_id);

  auto coordinator = fidl::WireSharedClient(std::move(request->coordinator_rpc),
                                            driver_host_context_->loop().dispatcher());
  auto newconn =
      DeviceControllerConnection::Create(driver_host_context_, dev, std::move(coordinator));

  status = InitializeCompositeDevice(dev, std::move(fragments_list));
  if (status != ZX_OK) {
    completer.Reply(status);
    return;
  }

  VLOGF(1, "Created composite device %p '%s'", dev.get(), dev->name());
  DeviceControllerConnection::Bind(std::move(newconn), std::move(request->device_controller_rpc),
                                   driver_host_context_->loop().dispatcher());
  completer.Reply(ZX_OK);
}

void DriverHostControllerConnection::CreateDeviceStub(CreateDeviceStubRequestView request,
                                                      CreateDeviceStubCompleter::Sync& completer) {
  // This method is used for creating driverless proxies in case of misc, root, test devices.
  // Since there are no proxy drivers backing the device, a dummy proxy driver will be used for
  // device creation.
  if (!proxy_driver_) {
    auto status =
        zx_driver::Create("proxy", driver_host_context_->inspect().drivers(), &proxy_driver_);
    if (status != ZX_OK) {
      return;
    }
  }

  fbl::RefPtr<zx_device_t> dev;
  zx_status_t r = zx_device::Create(driver_host_context_, "proxy", proxy_driver_.get(), &dev);
  // TODO: dev->ops() and other lifecycle bits
  // no name means a dummy proxy device
  if (r != ZX_OK) {
    return;
  }
  dev->set_protocol_id(request->protocol_id);
  dev->set_ops(&kDeviceDefaultOps);
  dev->set_local_id(request->local_device_id);

  auto coordinator = fidl::WireSharedClient(std::move(request->coordinator_rpc),
                                            driver_host_context_->loop().dispatcher());
  auto newconn =
      DeviceControllerConnection::Create(driver_host_context_, dev, std::move(coordinator));
  VLOGF(1, "Created device stub %p '%s'", dev.get(), dev->name());
  DeviceControllerConnection::Bind(std::move(newconn), std::move(request->device_controller_rpc),
                                   driver_host_context_->loop().dispatcher());
}

// TODO(fxbug.dev/68309): Implement Restart.
void DriverHostControllerConnection::Restart(RestartRequestView request,
                                             RestartCompleter::Sync& completer) {
  completer.Reply(ZX_OK);
}

void DriverHostControllerConnection::Bind(
    std::unique_ptr<DriverHostControllerConnection> conn,
    fidl::ServerEnd<fuchsia_device_manager::DriverHostController> request,
    async_dispatcher_t* dispatcher) {
  fidl::BindServer(dispatcher, std::move(request), std::move(conn),
                   [](DriverHostControllerConnection* self, fidl::UnbindInfo info,
                      fidl::ServerEnd<fuchsia_device_manager::DriverHostController> server_end) {
                     switch (info.reason()) {
                       case fidl::Reason::kUnbind:
                       case fidl::Reason::kClose:
                         // These are initiated by ourself.
                         break;
                       case fidl::Reason::kPeerClosed:
                         // This is expected in test environments where driver_manager has
                         // terminated.
                         // TODO(fxbug.dev/52627): Support graceful termination.
                         LOGF(WARNING, "Disconnected %p from driver_manager", self);
                         zx_process_exit(1);
                         break;
                       case fidl::Reason::kDispatcherError:
                       case fidl::Reason::kDecodeError:
                       case fidl::Reason::kUnexpectedMessage:
                         LOGF(FATAL, "Failed to handle RPC on %p from driver_manager: %s", self,
                              info.FormatDescription().c_str());
                         break;
                       case fidl::Reason::kEncodeError:
                         LOGF(FATAL, "Failed to encode message on %p: %s", self,
                              info.FormatDescription().c_str());
                         break;
                       default:
                         LOGF(FATAL, "Unknown fidl error on %p: %s", self,
                              info.FormatDescription().c_str());
                     }
                   });
}

int main(int argc, char** argv) {
  char process_name[ZX_MAX_NAME_LEN] = {};
  zx::process::self()->get_property(ZX_PROP_NAME, process_name, sizeof(process_name));
  const char* tags[] = {process_name, "device"};
  fx_logger_config_t config{
      .min_severity = getenv_bool("devmgr.verbose", false) ? FX_LOG_ALL : FX_LOG_SEVERITY_DEFAULT,
      .console_fd = getenv_bool("devmgr.log-to-debuglog", false) ? dup(STDOUT_FILENO) : -1,
      .log_service_channel = ZX_HANDLE_INVALID,
      .tags = tags,
      .num_tags = std::size(tags),
  };
  zx_status_t status = fx_log_reconfigure(&config);
  if (status != ZX_OK) {
    return status;
  }

  zx::resource root_resource(zx_take_startup_handle(PA_HND(PA_RESOURCE, 0)));
  if (!root_resource.is_valid()) {
    LOGF(WARNING, "No root resource handle");
  }

  fidl::ServerEnd<fuchsia_device_manager::DriverHostController> controller_request(
      zx::channel(zx_take_startup_handle(PA_HND(PA_USER0, 0))));
  if (!controller_request.is_valid()) {
    LOGF(ERROR, "Invalid root connection to driver_manager");
    return ZX_ERR_BAD_HANDLE;
  }

  DriverHostContext ctx(&kAsyncLoopConfigAttachToCurrentThread, std::move(root_resource));

  const char* root_driver_path = getenv("devmgr.root_driver_path");
  if (root_driver_path != nullptr) {
    ctx.set_root_driver_path(root_driver_path);
  }

  RegisterContextForApi(&ctx);

  status = connect_scheduler_profile_provider();
  if (status != ZX_OK) {
    LOGF(INFO, "Failed to connect to profile provider: %s", zx_status_get_string(status));
    return status;
  }

  if (getenv_bool("driver.tracing.enable", true)) {
    status = start_trace_provider();
    if (status != ZX_OK) {
      LOGF(INFO, "Failed to register trace provider: %s", zx_status_get_string(status));
      // This is not a fatal error.
    }
  }
  auto stop_tracing = fit::defer([]() { stop_trace_provider(); });

  ctx.SetupDriverHostController(std::move(controller_request));

  status = ctx.inspect().Serve(zx::channel(zx_take_startup_handle(PA_DIRECTORY_REQUEST)),
                               ctx.loop().dispatcher());
  if (status != ZX_OK) {
    LOGF(WARNING, "driver_host: error serving diagnostics directory: %s\n",
         zx_status_get_string(status));
    // This is not a fatal error
  }

  return ctx.loop().Run(zx::time::infinite(), false /* once */);
}

}  // namespace internal

zx_status_t DriverHostContext::ScheduleRemove(const fbl::RefPtr<zx_device_t>& dev,
                                              bool unbind_self) {
  const auto& client = dev->coordinator_client;
  ZX_ASSERT(client);
  VLOGD(1, *dev, "schedule-remove");
  auto resp = client->ScheduleRemove(unbind_self);
  log_rpc_result(dev, "schedule-remove", resp.status());
  return resp.status();
}

zx_status_t DriverHostContext::ScheduleUnbindChildren(const fbl::RefPtr<zx_device_t>& dev) {
  const auto& client = dev->coordinator_client;
  ZX_ASSERT(client);
  VLOGD(1, *dev, "schedule-unbind-children");
  auto resp = client->ScheduleUnbindChildren();
  log_rpc_result(dev, "schedule-unbind-children", resp.status());
  return resp.status();
}

zx_status_t DriverHostContext::GetTopoPath(const fbl::RefPtr<zx_device_t>& dev, char* path,
                                           size_t max, size_t* actual) {
  fbl::RefPtr<zx_device_t> remote_dev = dev;
  if (dev->flags() & DEV_FLAG_INSTANCE) {
    // Instances cannot be opened a second time. If dev represents an instance, return the path
    // to its parent, prefixed with an '@'.
    if (max < 1) {
      return ZX_ERR_BUFFER_TOO_SMALL;
    }
    path[0] = '@';
    path++;
    max--;
    remote_dev = dev->parent();
  }

  const auto& client = remote_dev->coordinator_client;
  if (!client) {
    return ZX_ERR_IO_REFUSED;
  }

  VLOGD(1, *remote_dev, "get-topo-path");
  auto response = client->GetTopologicalPath_Sync();
  zx_status_t status = response.status();
  zx_status_t call_status = ZX_OK;
  if (status == ZX_OK) {
    if (response.Unwrap()->result.is_err()) {
      call_status = response.Unwrap()->result.err();
    } else {
      auto& r = response.Unwrap()->result.response();
      memcpy(path, r.path.data(), r.path.size());
      *actual = r.path.size();
    }
  }

  log_rpc_result(dev, "get-topo-path", status, call_status);
  if (status != ZX_OK) {
    return status;
  }
  if (call_status != ZX_OK) {
    return status;
  }

  path[*actual] = 0;
  *actual += 1;

  // Account for the prefixed '@' we may have added above.
  if (dev->flags() & DEV_FLAG_INSTANCE) {
    *actual += 1;
  }
  return ZX_OK;
}

zx_status_t DriverHostContext::DeviceBind(const fbl::RefPtr<zx_device_t>& dev,
                                          const char* drv_libname) {
  const auto& client = dev->coordinator_client;
  if (!client) {
    return ZX_ERR_IO_REFUSED;
  }
  VLOGD(1, *dev, "bind-device");
  auto driver_path = ::fidl::StringView::FromExternal(drv_libname);
  auto response = client->BindDevice_Sync(std::move(driver_path));
  zx_status_t status = response.status();
  zx_status_t call_status = ZX_OK;
  if (status == ZX_OK && response.Unwrap()->result.is_err()) {
    call_status = response.Unwrap()->result.err();
  }
  log_rpc_result(dev, "bind-device", status, call_status);
  if (status != ZX_OK) {
    return status;
  }

  return call_status;
}

zx_status_t DriverHostContext::DeviceRunCompatibilityTests(const fbl::RefPtr<zx_device_t>& dev,
                                                           int64_t hook_wait_time,
                                                           fit::callback<void(zx_status_t)> cb) {
  const auto& client = dev->coordinator_client;
  if (!client) {
    return ZX_ERR_IO_REFUSED;
  }
  VLOGD(1, *dev, "run-compatibility-test");
  client->RunCompatibilityTests(
      hook_wait_time,
      [cb = std::move(cb),
       dev](fidl::WireUnownedResult<fuchsia_device_manager::Coordinator::RunCompatibilityTests>&
                result) mutable {
        log_rpc_result(dev, "run-compatibility-test", result.status());
        if (!result.ok()) {
          cb(result.status());
          return;
        }
        if (result->result.is_err()) {
          cb(result->result.err());
        } else {
          cb(static_cast<zx_status_t>(result->result.response().status));
        }
      });
  return ZX_OK;
}

zx_status_t DriverHostContext::LoadFirmware(const zx_driver_t* drv,
                                            const fbl::RefPtr<zx_device_t>& dev, const char* path,
                                            zx_handle_t* vmo_handle, size_t* size) {
  if ((vmo_handle == nullptr) || (size == nullptr)) {
    return ZX_ERR_INVALID_ARGS;
  }

  zx::vmo vmo;
  const auto& client = dev->coordinator_client;
  if (!client) {
    return ZX_ERR_IO_REFUSED;
  }
  VLOGD(1, *dev, "load-firmware");
  auto drv_libname = ::fidl::StringView::FromExternal(drv->libname());
  auto str_path = ::fidl::StringView::FromExternal(path);
  auto response = client->LoadFirmware_Sync(std::move(drv_libname), std::move(str_path));
  zx_status_t status = response.status();
  zx_status_t call_status = ZX_OK;
  auto result = std::move(response.Unwrap()->result);
  if (result.is_err()) {
    call_status = result.err();
  } else {
    auto resp = std::move(result.mutable_response());
    *size = resp.size;
    vmo = std::move(resp.vmo);
  }
  log_rpc_result(dev, "load-firmware", status, call_status);
  if (status != ZX_OK) {
    return status;
  }
  *vmo_handle = vmo.release();
  if (call_status == ZX_OK && *vmo_handle == ZX_HANDLE_INVALID) {
    return ZX_ERR_INTERNAL;
  }
  return call_status;
}

void DriverHostContext::LoadFirmwareAsync(const zx_driver_t* drv,
                                          const fbl::RefPtr<zx_device_t>& dev, const char* path,
                                          load_firmware_callback_t callback, void* context) {
  ZX_DEBUG_ASSERT(callback);

  const auto& client = dev->coordinator_client;
  if (!client) {
    callback(context, ZX_ERR_IO_REFUSED, ZX_HANDLE_INVALID, 0);
    return;
  }
  VLOGD(1, *dev, "load-firmware-async");
  auto drv_libname = ::fidl::StringView::FromExternal(drv->libname());
  auto str_path = ::fidl::StringView::FromExternal(path);
  client->LoadFirmware(
      drv_libname, str_path,
      [callback, context, dev = dev](
          fidl::WireUnownedResult<fuchsia_device_manager::Coordinator::LoadFirmware>& result) {
        if (!result.ok()) {
          log_rpc_result(dev, "load-firmware-async", result.status(), ZX_OK);
          callback(context, result.status(), ZX_HANDLE_INVALID, 0);
          return;
        }
        zx_status_t call_status = ZX_OK;
        size_t size = 0;
        zx::vmo vmo;

        if (result->result.is_err()) {
          call_status = result->result.err();
        } else {
          auto& resp = result->result.mutable_response();
          size = resp.size;
          vmo = std::move(resp.vmo);
        }
        log_rpc_result(dev, "load-firmware-async", ZX_OK, call_status);
        if (call_status == ZX_OK && !vmo.is_valid()) {
          call_status = ZX_ERR_INTERNAL;
        }

        callback(context, call_status, vmo.release(), size);
      });
}

zx_status_t DriverHostContext::GetMetadata(const fbl::RefPtr<zx_device_t>& dev, uint32_t type,
                                           void* buf, size_t buflen, size_t* actual) {
  if (!buf) {
    return ZX_ERR_INVALID_ARGS;
  }

  const auto& client = dev->coordinator_client;
  if (!client) {
    return ZX_ERR_IO_REFUSED;
  }
  VLOGD(1, *dev, "get-metadata");
  auto response = client->GetMetadata_Sync(type);
  zx_status_t status = response.status();
  zx_status_t call_status = ZX_OK;
  if (status == ZX_OK) {
    if (response->result.is_response()) {
      const auto& r = response.Unwrap()->result.mutable_response();
      if (r.data.count() > buflen) {
        return ZX_ERR_BUFFER_TOO_SMALL;
      }
      memcpy(buf, r.data.data(), r.data.count());
      if (actual != nullptr) {
        *actual = r.data.count();
      }
    } else {
      call_status = response->result.err();
    }
  }
  return log_rpc_result(dev, "get-metadata", status, call_status);
}

zx_status_t DriverHostContext::GetMetadataSize(const fbl::RefPtr<zx_device_t>& dev, uint32_t type,
                                               size_t* out_length) {
  const auto& client = dev->coordinator_client;
  if (!client) {
    return ZX_ERR_IO_REFUSED;
  }
  VLOGD(1, *dev, "get-metadata-size");
  auto response = client->GetMetadataSize_Sync(type);
  zx_status_t status = response.status();
  zx_status_t call_status = ZX_OK;
  if (status == ZX_OK) {
    if (response->result.is_response()) {
      *out_length = response->result.response().size;
    } else {
      call_status = response->result.err();
    }
  }
  return log_rpc_result(dev, "get-metadata-size", status, call_status);
}

zx_status_t DriverHostContext::AddMetadata(const fbl::RefPtr<zx_device_t>& dev, uint32_t type,
                                           const void* data, size_t length) {
  if (!data && length) {
    return ZX_ERR_INVALID_ARGS;
  }
  const auto& client = dev->coordinator_client;
  if (!client) {
    return ZX_ERR_IO_REFUSED;
  }
  VLOGD(1, *dev, "add-metadata");
  auto response = client->AddMetadata_Sync(
      type, ::fidl::VectorView<uint8_t>::FromExternal(
                reinterpret_cast<uint8_t*>(const_cast<void*>(data)), length));
  zx_status_t status = response.status();
  zx_status_t call_status = ZX_OK;
  if (status == ZX_OK && response->result.is_err()) {
    call_status = response->result.err();
  }
  return log_rpc_result(dev, "add-metadata", status, call_status);
}

zx_status_t DriverHostContext::PublishMetadata(const fbl::RefPtr<zx_device_t>& dev,
                                               const char* path, uint32_t type, const void* data,
                                               size_t length) {
  if (!path || (!data && length)) {
    return ZX_ERR_INVALID_ARGS;
  }
  const auto& client = dev->coordinator_client;
  if (!client) {
    return ZX_ERR_IO_REFUSED;
  }
  VLOGD(1, *dev, "publish-metadata");
  auto response = client->PublishMetadata_Sync(
      ::fidl::StringView::FromExternal(path), type,
      ::fidl::VectorView<uint8_t>::FromExternal(reinterpret_cast<uint8_t*>(const_cast<void*>(data)),
                                                length));
  zx_status_t status = response.status();
  zx_status_t call_status = ZX_OK;
  if (status == ZX_OK && response->result.is_err()) {
    call_status = response->result.err();
  }
  return log_rpc_result(dev, "publish-metadata", status, call_status);
}

zx_status_t DriverHostContext::DeviceAddComposite(const fbl::RefPtr<zx_device_t>& dev,
                                                  const char* name,
                                                  const composite_device_desc_t* comp_desc) {
  if (comp_desc == nullptr || (comp_desc->props == nullptr && comp_desc->props_count > 0) ||
      comp_desc->fragments == nullptr || name == nullptr ||
      comp_desc->primary_fragment == nullptr) {
    return ZX_ERR_INVALID_ARGS;
  }
  const auto& client = dev->coordinator_client;
  if (!client) {
    return ZX_ERR_IO_REFUSED;
  }

  VLOGD(1, *dev, "create-composite");
  fidl::Arena allocator;
  std::vector<fuchsia_device_manager::wire::DeviceFragment> compvec = {};
  for (size_t i = 0; i < comp_desc->fragments_count; i++) {
    fuchsia_device_manager::wire::DeviceFragment dc;
    dc.name = ::fidl::StringView::FromExternal(comp_desc->fragments[i].name,
                                               strnlen(comp_desc->fragments[i].name, 32));
    dc.parts.Allocate(allocator, comp_desc->fragments[i].parts_count);

    for (uint32_t j = 0; j < comp_desc->fragments[i].parts_count; j++) {
      dc.parts[j].match_program.Allocate(allocator,
                                         comp_desc->fragments[i].parts[j].instruction_count);

      for (uint32_t k = 0; k < comp_desc->fragments[i].parts[j].instruction_count; k++) {
        dc.parts[j].match_program[k] = fuchsia_device_manager::wire::BindInstruction{
            .op = comp_desc->fragments[i].parts[j].match_program[k].op,
            .arg = comp_desc->fragments[i].parts[j].match_program[k].arg,
            .debug = comp_desc->fragments[i].parts[j].match_program[k].debug,
        };
      }
    }
    compvec.push_back(std::move(dc));
  }

  std::vector<fuchsia_device_manager::wire::DeviceMetadata> metadata = {};
  for (size_t i = 0; i < comp_desc->metadata_count; i++) {
    auto meta = fuchsia_device_manager::wire::DeviceMetadata{
        .key = comp_desc->metadata_list[i].type,
        .data = fidl::VectorView<uint8_t>::FromExternal(
            reinterpret_cast<uint8_t*>(const_cast<void*>(comp_desc->metadata_list[i].data)),
            comp_desc->metadata_list[i].length)};
    metadata.emplace_back(std::move(meta));
  }

  std::vector<fuchsia_device_manager::wire::DeviceProperty> props = {};
  for (size_t i = 0; i < comp_desc->props_count; i++) {
    props.push_back(convert_device_prop(comp_desc->props[i]));
  }

  std::vector<fuchsia_device_manager::wire::DeviceStrProperty> str_props = {};
  for (size_t i = 0; i < comp_desc->str_props_count; i++) {
    if (!property_value_type_valid(comp_desc->str_props[i].property_value.value_type)) {
      return ZX_ERR_INVALID_ARGS;
    }
    str_props.push_back(convert_device_str_prop(comp_desc->str_props[i], allocator));
  }

  uint32_t primary_fragment_index = UINT32_MAX;
  for (size_t i = 0; i < comp_desc->fragments_count; i++) {
    if (strcmp(comp_desc->primary_fragment, comp_desc->fragments[i].name) == 0) {
      primary_fragment_index = i;
      break;
    }
  }
  if (primary_fragment_index == UINT32_MAX) {
    return ZX_ERR_INVALID_ARGS;
  }

  fuchsia_device_manager::wire::CompositeDeviceDescriptor comp_dev = {
      .props =
          ::fidl::VectorView<fuchsia_device_manager::wire::DeviceProperty>::FromExternal(props),
      .str_props =
          ::fidl::VectorView<fuchsia_device_manager::wire::DeviceStrProperty>::FromExternal(
              str_props),
      .fragments =
          ::fidl::VectorView<fuchsia_device_manager::wire::DeviceFragment>::FromExternal(compvec),
      .primary_fragment_index = primary_fragment_index,
      .spawn_colocated = comp_desc->spawn_colocated,
      .metadata =
          ::fidl::VectorView<fuchsia_device_manager::wire::DeviceMetadata>::FromExternal(metadata)};

  static_assert(sizeof(comp_desc->props[0]) == sizeof(uint64_t));
  auto response =
      client->AddCompositeDevice_Sync(::fidl::StringView::FromExternal(name), std::move(comp_dev));
  zx_status_t status = response.status();
  zx_status_t call_status = ZX_OK;
  if (status == ZX_OK && response->result.is_err()) {
    call_status = response->result.err();
  }
  return log_rpc_result(dev, "create-composite", status, call_status);
}
