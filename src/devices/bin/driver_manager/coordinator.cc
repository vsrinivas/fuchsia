// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/devices/bin/driver_manager/coordinator.h"

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <fidl/fuchsia.io/cpp/wire.h>
#include <fidl/fuchsia.pkg/cpp/wire.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/async/cpp/receiver.h>
#include <lib/async/cpp/task.h>
#include <lib/ddk/driver.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/io.h>
#include <lib/fidl-async/bind.h>
#include <lib/fidl-async/cpp/bind.h>
#include <lib/fidl/coding.h>
#include <lib/fidl/llcpp/arena.h>
#include <lib/fidl/llcpp/wire_messaging.h>
#include <lib/fit/defer.h>
#include <lib/fzl/owned-vmo-mapper.h>
#include <lib/service/llcpp/service.h>
#include <lib/zbitl/error-string.h>
#include <lib/zbitl/image.h>
#include <lib/zbitl/item.h>
#include <lib/zbitl/vmo.h>
#include <lib/zircon-internal/ktrace.h>
#include <lib/zx/clock.h>
#include <lib/zx/job.h>
#include <lib/zx/time.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <threads.h>
#include <zircon/assert.h>
#include <zircon/errors.h>
#include <zircon/processargs.h>
#include <zircon/status.h>
#include <zircon/syscalls.h>
#include <zircon/syscalls/policy.h>
#include <zircon/syscalls/system.h>

#include <array>
#include <cstdint>
#include <memory>
#include <string_view>
#include <utility>
#include <vector>

#include <driver-info/driver-info.h>
#include <fbl/string_printf.h>
#include <inspector/inspector.h>
#include <src/bringup/lib/mexec/mexec.h>
#include <src/lib/fsl/vmo/sized_vmo.h>
#include <src/lib/fsl/vmo/vector.h>

#include "src/devices/bin/driver_manager/driver_host_loader_service.h"
#include "src/devices/bin/driver_manager/manifest_parser.h"
#include "src/devices/bin/driver_manager/package_resolver.h"
#include "src/devices/bin/driver_manager/v1/unbind_task.h"
#include "src/devices/lib/log/log.h"
#include "src/lib/storage/vfs/cpp/pseudo_dir.h"

namespace fio = fuchsia_io;

namespace {

namespace fdd = fuchsia_driver_development;
namespace fdm = fuchsia_device_manager;
namespace fdr = fuchsia_driver_registrar;
namespace fpm = fuchsia_power_manager;

constexpr char kDriverHostPath[] = "/pkg/bin/driver_host";
constexpr const char* kItemsPath = fidl::DiscoverableProtocolDefaultPath<fuchsia_boot::Items>;

// The driver_host doesn't just define its own __asan_default_options()
// function because that conflicts with the build-system feature of injecting
// such a function based on the `asan_default_options` GN build argument.
// Since driver_host is only ever launched here, it can always get its
// necessary options through its environment variables.  The sanitizer
// runtime combines the __asan_default_options() and environment settings.
constexpr char kAsanEnvironment[] =
    "ASAN_OPTIONS="

    // All drivers have a pure C ABI.  But each individual driver might
    // statically link in its own copy of some C++ library code.  Since no
    // C++ language relationships leak through the driver ABI, each driver is
    // its own whole program from the perspective of the C++ language rules.
    // But the ASan runtime doesn't understand this and wants to diagnose ODR
    // violations when the same global is defined in multiple drivers, which
    // is likely with C++ library use.  There is no real way to teach the
    // ASan instrumentation or runtime about symbol visibility and isolated
    // worlds within the program, so the only thing to do is suppress the ODR
    // violation detection.  This unfortunately means real ODR violations
    // within a single C++ driver won't be caught either.
    "detect_odr_violation=0";

// Currently we check if DriverManager is built using ASAN.
// If it is, then we assume DriverHost is also ASAN.
//
// We currently assume that either the whole system is ASAN or the whole
// system is non-ASAN. One day we might be able to be more flexible about
// which drivers must get loaded into the same driver_host and thus be able
// to use both ASan and non-ASan driver_hosts at the same time when only
// a subset of drivers use ASan.
bool driver_host_is_asan() {
  bool is_asan = false;
#if defined(__has_feature)
#if __has_feature(address_sanitizer)
  is_asan = true;
#endif
#endif
  return is_asan;
}

// send message to driver_host, requesting the creation of a device
zx_status_t CreateProxyDevice(const fbl::RefPtr<Device>& dev, fbl::RefPtr<DriverHost>& dh,
                              const char* args, zx::channel rpc_proxy) {
  auto coordinator_endpoints = fidl::CreateEndpoints<fdm::Coordinator>();
  if (coordinator_endpoints.is_error()) {
    return coordinator_endpoints.error_value();
  }

  auto device_controller_request = dev->ConnectDeviceController(dev->coordinator->dispatcher());

  fidl::Arena arena;
  if (dev->libname().size() != 0) {
    zx::vmo vmo;
    if (auto status = dev->coordinator->LibnameToVmo(dev->libname(), &vmo); status != ZX_OK) {
      return status;
    }

    auto driver_path = fidl::StringView::FromExternal(dev->libname().data(), dev->libname().size());
    auto args_view = fidl::StringView::FromExternal(args, strlen(args));

    fdm::wire::ProxyDevice proxy{driver_path, std::move(vmo), std::move(rpc_proxy), args_view};
    auto type = fdm::wire::DeviceType::WithProxy(arena, std::move(proxy));

    dh->controller()->CreateDevice(
        std::move(coordinator_endpoints->client), std::move(device_controller_request),
        std::move(type), dev->local_id(),
        [](fidl::WireUnownedResult<fdm::DriverHostController::CreateDevice>& result) {
          if (!result.ok()) {
            LOGF(ERROR, "Failed to create device: %s", result.error().FormatDescription().c_str());
            return;
          }
          if (result->status != ZX_OK) {
            LOGF(ERROR, "Failed to create device: %s", zx_status_get_string(result->status));
          }
        });
  } else {
    fdm::wire::StubDevice stub{dev->protocol_id()};
    auto type = fdm::wire::DeviceType::WithStub(stub);
    dh->controller()->CreateDevice(
        std::move(coordinator_endpoints->client), std::move(device_controller_request),
        std::move(type), dev->local_id(),
        [](fidl::WireUnownedResult<fdm::DriverHostController::CreateDevice>& result) {
          if (!result.ok()) {
            LOGF(ERROR, "Failed to create device: %s", result.error().FormatDescription().c_str());
            return;
          }
          if (result->status != ZX_OK) {
            LOGF(ERROR, "Failed to create device: %s", zx_status_get_string(result->status));
          }
        });
  }

  Device::Bind(dev, dev->coordinator->dispatcher(), std::move(coordinator_endpoints->server));
  return ZX_OK;
}

zx_status_t CreateNewProxyDevice(const fbl::RefPtr<Device>& dev, fbl::RefPtr<DriverHost>& dh,
                                 fidl::ClientEnd<fio::Directory> incoming_dir) {
  auto coordinator_endpoints = fidl::CreateEndpoints<fdm::Coordinator>();
  if (coordinator_endpoints.is_error()) {
    return coordinator_endpoints.error_value();
  }

  auto device_controller_request = dev->ConnectDeviceController(dev->coordinator->dispatcher());

  fdm::wire::NewProxyDevice new_proxy{std::move(incoming_dir)};
  auto type = fdm::wire::DeviceType::WithNewProxy(std::move(new_proxy));

  dh->controller()->CreateDevice(
      std::move(coordinator_endpoints->client), std::move(device_controller_request),
      std::move(type), dev->local_id(),
      [](fidl::WireUnownedResult<fdm::DriverHostController::CreateDevice>& result) {
        if (!result.ok()) {
          LOGF(ERROR, "Failed to create device: %s", result.error().FormatDescription().c_str());
          return;
        }
        if (result->status != ZX_OK) {
          LOGF(ERROR, "Failed to create device: %s", zx_status_get_string(result->status));
        }
      });

  Device::Bind(dev, dev->coordinator->dispatcher(), std::move(coordinator_endpoints->server));
  return ZX_OK;
}

// Binds the driver to the device by sending a request to driver_host.
zx_status_t BindDriverToDevice(const fbl::RefPtr<Device>& dev, const char* libname) {
  zx::vmo vmo;
  zx_status_t status = dev->coordinator->LibnameToVmo(libname, &vmo);
  if (status != ZX_OK) {
    return status;
  }
  dev->device_controller()->BindDriver(
      fidl::StringView::FromExternal(libname, strlen(libname)), std::move(vmo),
      [dev](fidl::WireUnownedResult<fdm::DeviceController::BindDriver>& result) {
        if (!result.ok()) {
          LOGF(ERROR, "Failed to bind driver '%s': %s", dev->name().data(), result.status_string());
          dev->flags &= (~DEV_CTX_BOUND);
          return;
        }
        if (result->status != ZX_OK) {
          LOGF(ERROR, "Failed to bind driver '%s': %s", dev->name().data(),
               zx_status_get_string(result->status));
          dev->flags &= (~DEV_CTX_BOUND);
          return;
        }
      });
  dev->flags |= DEV_CTX_BOUND;
  return ZX_OK;
}

}  // namespace

namespace statecontrol_fidl = fuchsia_hardware_power_statecontrol;

Coordinator::Coordinator(CoordinatorConfig config, InspectManager* inspect_manager,
                         async_dispatcher_t* dispatcher, async_dispatcher_t* firmware_dispatcher)
    : config_(std::move(config)),
      dispatcher_(dispatcher),
      base_resolver_(config_.boot_args),
      driver_loader_(config_.boot_args, std::move(config_.driver_index), &base_resolver_,
                     dispatcher, config_.require_system),
      inspect_manager_(inspect_manager),
      package_resolver_(config.boot_args) {
  if (config_.oom_event) {
    wait_on_oom_event_.set_object(config_.oom_event.get());
    wait_on_oom_event_.set_trigger(ZX_EVENT_SIGNALED);
    wait_on_oom_event_.Begin(dispatcher);
  }
  shutdown_system_state_ = config_.default_shutdown_system_state;

  root_device_ =
      fbl::MakeRefCounted<Device>(this, "root", fbl::String(), "root,", nullptr, ZX_PROTOCOL_ROOT,
                                  zx::vmo(), zx::channel(), fidl::ClientEnd<fio::Directory>());
  root_device_->flags = DEV_CTX_IMMORTAL | DEV_CTX_MUST_ISOLATE | DEV_CTX_MULTI_BIND;

  bind_driver_manager_ =
      std::make_unique<BindDriverManager>(this, fit::bind_member<&Coordinator::AttemptBind>(this));

  device_manager_ = std::make_unique<DeviceManager>(this, config_.crash_policy);

  suspend_resume_manager_ = std::make_unique<SuspendResumeManager>(this, config_.suspend_timeout);
  firmware_loader_ =
      std::make_unique<FirmwareLoader>(this, firmware_dispatcher, config_.path_prefix);
  debug_dump_ = std::make_unique<DebugDump>(this);
}

Coordinator::~Coordinator() {}

void Coordinator::LoadV1Drivers(std::string_view sys_device_driver,
                                fbl::Vector<std::string>& driver_search_paths,
                                fbl::Vector<const char*>& load_drivers) {
  InitCoreDevices(sys_device_driver);

  // Load the drivers.
  for (const std::string& path : driver_search_paths) {
    find_loadable_drivers(boot_args(), path, fit::bind_member<&Coordinator::DriverAddedInit>(this));
  }
  for (const char* driver : load_drivers) {
    load_driver(boot_args(), driver, fit::bind_member<&Coordinator::DriverAddedInit>(this));
  }

  PrepareProxy(sys_device_, nullptr);

  // Bind all the drivers we loaded.
  AddAndBindDrivers(std::move(drivers_));
  DriverLoader::MatchDeviceConfig config;
  bind_driver_manager_->BindAllDevicesDriverIndex(config);

  // Bind the fallback drivers if we don't require the full system.
  if (config_.require_system) {
    LOGF(INFO, "Full system required, fallback drivers will be loaded after '/system' is loaded");
  } else {
    BindFallbackDrivers();
  }

  // Schedule the base drivers to load.
  driver_loader_.WaitForBaseDrivers([this]() {
    DriverLoader::MatchDeviceConfig config;
    config.only_return_base_and_fallback_drivers = true;
    bind_driver_manager_->BindAllDevicesDriverIndex(config);
  });

  devfs_publish(root_device_, sys_device_);
}

void Coordinator::InitCoreDevices(std::string_view sys_device_driver) {
  // If the sys device is not a path, then we try to load it like a URL.
  if (sys_device_driver[0] != '/') {
    auto string = std::string(sys_device_driver.data());
    driver_loader_.LoadDriverUrl(string);
  }

  sys_device_ =
      fbl::MakeRefCounted<Device>(this, "sys", sys_device_driver, "sys,", root_device_, 0,
                                  zx::vmo(), zx::channel(), fidl::ClientEnd<fio::Directory>());
  sys_device_->flags = DEV_CTX_IMMORTAL | DEV_CTX_MUST_ISOLATE;
}

void Coordinator::RegisterWithPowerManager(fidl::ClientEnd<fio::Directory> devfs,
                                           RegisterWithPowerManagerCompletion completion) {
  auto system_state_endpoints = fidl::CreateEndpoints<fdm::SystemStateTransition>();
  if (system_state_endpoints.is_error()) {
    completion(system_state_endpoints.error_value());
    return;
  }
  std::unique_ptr<SystemStateManager> system_state_manager;
  auto status = SystemStateManager::Create(
      dispatcher_, this, std::move(system_state_endpoints->server), &system_state_manager);
  if (status != ZX_OK) {
    completion(status);
    return;
  }
  set_system_state_manager(std::move(system_state_manager));
  auto result = service::Connect<fpm::DriverManagerRegistration>();
  if (result.is_error()) {
    LOGF(ERROR, "Failed to connect to fuchsia.power.manager: %s", result.status_string());
    completion(result.error_value());
    return;
  }

  RegisterWithPowerManager(std::move(*result), std::move(system_state_endpoints->client),
                           std::move(devfs), std::move(completion));
}

void Coordinator::RegisterWithPowerManager(
    fidl::ClientEnd<fpm::DriverManagerRegistration> power_manager,
    fidl::ClientEnd<fdm::SystemStateTransition> system_state_transition,
    fidl::ClientEnd<fio::Directory> devfs, RegisterWithPowerManagerCompletion completion) {
  power_manager_client_.Bind(std::move(power_manager), dispatcher_);
  power_manager_client_->Register(
      std::move(system_state_transition), std::move(devfs),
      [this, completion = std::move(completion)](
          fidl::WireUnownedResult<fpm::DriverManagerRegistration::Register>& result) mutable {
        if (!result.ok()) {
          LOGF(INFO, "Failed to register with power_manager: %s\n",
               result.error().FormatDescription().c_str());
          completion(result.status());
          return;
        }

        if (result->result.is_err()) {
          fpm::wire::RegistrationError err = result->result.err();
          if (err == fpm::wire::RegistrationError::kInvalidHandle) {
            LOGF(ERROR, "Failed to register with power_manager. Invalid handle.\n");
            completion(ZX_ERR_BAD_HANDLE);
            return;
          }
          LOGF(ERROR, "Failed to register with power_manager\n");
          completion(ZX_ERR_INTERNAL);
          return;
        }
        LOGF(INFO, "Registered with power manager successfully");
        set_power_manager_registered(true);
        completion(ZX_OK);
      });
}

const Driver* Coordinator::LibnameToDriver(std::string_view libname) const {
  for (const auto& drv : drivers_) {
    if (libname.compare(drv.libname) == 0) {
      return &drv;
    }
  }

  return driver_loader_.LibnameToDriver(libname);
}

zx_status_t Coordinator::LibnameToVmo(const fbl::String& libname, zx::vmo* out_vmo) const {
  const Driver* drv = LibnameToDriver(libname);
  if (drv == nullptr) {
    LOGF(ERROR, "Cannot find driver '%s'", libname.data());
    return ZX_ERR_NOT_FOUND;
  }

  // Check for cached DSO
  if (drv->dso_vmo != ZX_HANDLE_INVALID) {
    zx_status_t r = drv->dso_vmo.duplicate(
        ZX_RIGHTS_BASIC | ZX_RIGHTS_PROPERTY | ZX_RIGHT_READ | ZX_RIGHT_EXECUTE | ZX_RIGHT_MAP,
        out_vmo);
    if (r != ZX_OK) {
      LOGF(ERROR, "Cannot duplicate cached DSO for '%s' '%s'", drv->name.data(), libname.data());
    }
    return r;
  } else {
    return load_vmo(libname, out_vmo);
  }
}

zx_handle_t get_service_root();

zx_status_t Coordinator::GetTopologicalPath(const fbl::RefPtr<const Device>& dev, char* out,
                                            size_t max) const {
  // TODO: Remove VLA.
  char tmp[max];
  char name_buf[fio::wire::kMaxFilename + strlen("dev/")];
  char* path = tmp + max - 1;
  *path = 0;
  size_t total = 1;

  fbl::RefPtr<const Device> itr = dev;
  while (itr != nullptr) {
    if (itr->flags & DEV_CTX_PROXY) {
      itr = itr->parent();
    }

    const char* name;
    if (&*itr == root_device_.get()) {
      name = "dev";
    } else if (itr->composite() != nullptr) {
      strcpy(name_buf, "dev/");
      strncpy(name_buf + strlen("dev/"), itr->name().data(), fio::wire::kMaxFilename);
      name_buf[sizeof(name_buf) - 1] = 0;
      name = name_buf;
    } else {
      name = itr->name().data();
    }

    size_t len = strlen(name) + 1;
    if (len > (max - total)) {
      return ZX_ERR_BUFFER_TOO_SMALL;
    }

    memcpy(path - len + 1, name, len - 1);
    path -= len;
    *path = '/';
    total += len;
    itr = itr->parent();
  }

  memcpy(out, path, total);
  return ZX_OK;
}

zx_status_t Coordinator::NewDriverHost(const char* name, fbl::RefPtr<DriverHost>* out) {
  std::string root_driver_path_arg;
  std::vector<const char*> env;
  if (driver_host_is_asan()) {
    env.push_back(kAsanEnvironment);
  }

  auto driver_host_env = (*boot_args())->Collect("driver.");
  if (!driver_host_env.ok()) {
    return driver_host_env.status();
  }

  std::vector<std::string> strings;
  for (auto& entry : driver_host_env->results) {
    strings.emplace_back(entry.data(), entry.size());
  }

  // Make the clock backstop boot arg available to drivers that
  // deal with time (RTC).
  // TODO(fxbug.dev/60668): Remove once UTC time is removed from the kernel.
  auto backstop_env = (*boot_args())->GetString("clock.backstop");
  if (!backstop_env.ok()) {
    return backstop_env.status();
  }

  auto backstop_env_value = std::move(backstop_env.value().value);
  if (!backstop_env_value.is_null()) {
    strings.push_back(std::string("clock.backstop=") +
                      std::string(backstop_env_value.data(), backstop_env_value.size()));
  }

  for (auto& entry : strings) {
    env.push_back(entry.data());
  }

  if (config_.log_to_debuglog) {
    env.push_back("devmgr.log-to-debuglog=true");
  }
  if (config_.verbose) {
    env.push_back("devmgr.verbose=true");
  }
  root_driver_path_arg = "devmgr.root_driver_path=" + config_.path_prefix + "driver/";
  env.push_back(root_driver_path_arg.c_str());

  env.push_back(nullptr);

  DriverHostConfig config{
      .name = name,
      .binary = kDriverHostPath,
      .env = env.data(),
      .job = zx::unowned_job(config_.driver_host_job),
      .root_resource = zx::unowned_resource(root_resource()),
      .loader_service_connector = &loader_service_connector_,
      .fs_provider = config_.fs_provider,
      .coordinator = this,
  };
  fbl::RefPtr<DriverHost> dh;
  zx_status_t status = DriverHost::Launch(config, &dh);
  if (status != ZX_OK) {
    return status;
  }
  launched_first_driver_host_ = true;

  VLOGF(1, "New driver_host %p", dh.get());
  *out = std::move(dh);
  return ZX_OK;
}

zx_status_t Coordinator::MakeVisible(const fbl::RefPtr<Device>& dev) {
  if (dev->state() == Device::State::kDead) {
    return ZX_ERR_BAD_STATE;
  }
  if (dev->state() == Device::State::kInitializing) {
    // This should only be called in response to the init hook completing.
    return ZX_ERR_BAD_STATE;
  }
  if (dev->flags & DEV_CTX_INVISIBLE) {
    dev->flags &= ~DEV_CTX_INVISIBLE;
    devfs_advertise(dev);
    zx_status_t r = dev->SignalReadyForBind();
    if (r != ZX_OK) {
      return r;
    }
  }
  return ZX_OK;
}

// Traverse up the device tree to find the metadata with the matching |type|.
// |buffer| can be nullptr, in which case only the size of the metadata is
// returned. This is used by GetMetadataSize method.
zx_status_t Coordinator::GetMetadata(const fbl::RefPtr<Device>& dev, uint32_t type, void* buffer,
                                     size_t buflen, size_t* size) {
  // search dev and its parent devices for a match
  fbl::RefPtr<Device> test = dev;
  while (true) {
    for (const auto& md : test->metadata()) {
      if (md.type == type) {
        if (buffer != nullptr) {
          if (md.length > buflen) {
            return ZX_ERR_BUFFER_TOO_SMALL;
          }
          memcpy(buffer, md.Data(), md.length);
        }
        *size = md.length;
        return ZX_OK;
      }
    }
    if (test->parent() == nullptr) {
      break;
    }
    test = test->parent();
  }

  // search fragments of composite devices
  if (test->composite()) {
    for (auto& fragment : test->composite()->bound_fragments()) {
      auto dev = fragment.bound_device();
      if (dev != nullptr) {
        if (GetMetadata(dev, type, buffer, buflen, size) == ZX_OK) {
          return ZX_OK;
        }
      }
    }
  }

  return ZX_ERR_NOT_FOUND;
}

zx_status_t Coordinator::AddMetadata(const fbl::RefPtr<Device>& dev, uint32_t type,
                                     const void* data, uint32_t length) {
  std::unique_ptr<Metadata> md;
  zx_status_t status = Metadata::Create(length, &md);
  if (status != ZX_OK) {
    return status;
  }

  md->type = type;
  md->length = length;
  memcpy(md->Data(), data, length);
  dev->AddMetadata(std::move(md));
  return ZX_OK;
}

// Create the proxy node for the given device if it doesn't exist and ensure it
// has a driver_host.  If |target_driver_host| is not nullptr and the proxy doesn't have
// a driver_host yet, |target_driver_host| will be used for it.  Otherwise a new driver_host
// will be created.
zx_status_t Coordinator::PrepareProxy(const fbl::RefPtr<Device>& dev,
                                      fbl::RefPtr<DriverHost> target_driver_host) {
  ZX_ASSERT(!(dev->flags & DEV_CTX_PROXY) && (dev->flags & DEV_CTX_MUST_ISOLATE));

  // proxy args are "processname,args"
  const char* arg0 = dev->args().data();
  const char* arg1 = strchr(arg0, ',');
  if (arg1 == nullptr) {
    LOGF(ERROR, "Missing proxy arguments, expected '%s,args' (see fxbug.dev/33674)", arg0);
    return ZX_ERR_INTERNAL;
  }
  size_t arg0len = arg1 - arg0;
  arg1++;

  char driver_hostname[32];
  snprintf(driver_hostname, sizeof(driver_hostname), "driver_host:%.*s", (int)arg0len, arg0);

  zx_status_t r;
  if (dev->proxy() == nullptr && (r = dev->CreateProxy()) != ZX_OK) {
    LOGF(ERROR, "Cannot create proxy device '%s': %s", dev->name().data(), zx_status_get_string(r));
    return r;
  }

  // if this device has no driver_host, first instantiate it
  if (dev->proxy()->host() == nullptr) {
    zx::channel h0, h1;
    // the immortal root devices do not provide proxy rpc
    bool need_proxy_rpc = !(dev->flags & DEV_CTX_IMMORTAL);

    if (need_proxy_rpc || dev == sys_device_) {
      // create rpc channel for proxy device to talk to the busdev it proxys
      if ((r = zx::channel::create(0, &h0, &h1)) < 0) {
        return r;
      }
    }
    if (target_driver_host == nullptr) {
      if ((r = NewDriverHost(driver_hostname, &target_driver_host)) < 0) {
        LOGF(ERROR, "Failed to create driver_host '%s': %s", driver_hostname,
             zx_status_get_string(r));
        return r;
      }
    }

    dev->proxy()->set_host(std::move(target_driver_host));
    if ((r = CreateProxyDevice(dev->proxy(), dev->proxy()->host(), arg1, std::move(h1))) < 0) {
      LOGF(ERROR, "Failed to create proxy device '%s' in driver_host '%s': %s", dev->name().data(),
           driver_hostname, zx_status_get_string(r));
      return r;
    }
    if (need_proxy_rpc) {
      if (auto result = dev->device_controller()->ConnectProxy(std::move(h0)); !result.ok()) {
        LOGF(ERROR, "Failed to connect to proxy device '%s' in driver_host '%s': %s",
             dev->name().data(), driver_hostname, zx_status_get_string(result.status()));
      }
    }
    if (dev == sys_device_) {
      if ((r = fdio_service_connect(kItemsPath, h0.release())) != ZX_OK) {
        LOGF(ERROR, "Failed to connect to %s: %s", kItemsPath, zx_status_get_string(r));
      }
    }
    zx::channel client_remote = dev->take_client_remote();
    if (client_remote.is_valid()) {
      if ((r = devfs_connect(dev->proxy().get(),
                             fidl::ServerEnd<fio::Node>(std::move(client_remote)))) != ZX_OK) {
        LOGF(ERROR, "Failed to connect to service from proxy device '%s' in driver_host '%s': %s",
             dev->name().data(), driver_hostname, zx_status_get_string(r));
      }
    }
  }

  return ZX_OK;
}

zx_status_t Coordinator::PrepareNewProxy(const fbl::RefPtr<Device>& dev,
                                         fbl::RefPtr<DriverHost> target_driver_host) {
  ZX_ASSERT(dev->flags & DEV_CTX_MUST_ISOLATE);

  zx_status_t status;
  if (dev->new_proxy() == nullptr && (status = dev->CreateNewProxy()) != ZX_OK) {
    LOGF(ERROR, "Cannot create new proxy device '%s': %s", dev->name().data(),
         zx_status_get_string(status));
    return status;
  }

  char driver_hostname[32];
  snprintf(driver_hostname, sizeof(driver_hostname), "driver_host:%.*s",
           static_cast<int>(dev->name().size()), dev->name().data());

  if (target_driver_host == nullptr) {
    if (status = NewDriverHost(driver_hostname, &target_driver_host); status != ZX_OK) {
      LOGF(ERROR, "Failed to create driver_host '%s': %s", driver_hostname,
           zx_status_get_string(status));
      return status;
    }
  }
  dev->new_proxy()->set_host(std::move(target_driver_host));
  if (status = CreateNewProxyDevice(dev->new_proxy(), dev->new_proxy()->host(),
                                    dev->take_outgoing_dir());
      status != ZX_OK) {
    LOGF(ERROR, "Failed to create proxy device '%s' in driver_host '%s': %s", dev->name().data(),
         driver_hostname, zx_status_get_string(status));
    return status;
  }

  return ZX_OK;
}

zx_status_t Coordinator::AttemptBind(const Driver* drv, const fbl::RefPtr<Device>& dev) {
  if (!driver_host_is_asan() && drv->flags & ZIRCON_DRIVER_NOTE_FLAG_ASAN) {
    LOGF(ERROR, "%s (%s) requires ASAN, but we are not in an ASAN environment", drv->libname.data(),
         drv->name.data());
    return ZX_ERR_BAD_STATE;
  }

  // cannot bind driver to already bound device
  if (dev->IsAlreadyBound()) {
    return ZX_ERR_ALREADY_BOUND;
  }
  if (!(dev->flags & DEV_CTX_MUST_ISOLATE)) {
    VLOGF(1, "Binding driver to %s in same driver host as parent", dev->name().data());
    // non-busdev is pretty simple
    if (dev->host() == nullptr) {
      LOGF(ERROR, "Cannot bind to device '%s', it has no driver_host", dev->name().data());
      return ZX_ERR_BAD_STATE;
    }
    return BindDriverToDevice(dev, drv->libname.c_str());
  }

  zx_status_t status;
  if (dev->has_outgoing_directory()) {
    VLOGF(1, "Preparing new proxy for %s", dev->name().data());
    status = PrepareNewProxy(dev, nullptr);
    if (status != ZX_OK) {
      return status;
    }
    status = BindDriverToDevice(dev->new_proxy(), drv->libname.c_str());
  } else {
    VLOGF(1, "Preparing old proxy for %s", dev->name().data());
    status = PrepareProxy(dev, nullptr /* target_driver_host */);
    if (status != ZX_OK) {
      return status;
    }
    status = BindDriverToDevice(dev->proxy(), drv->libname.c_str());
  }
  // TODO(swetland): arrange to mark us unbound when the proxy (or its driver_host) goes away
  if ((status == ZX_OK) && !(dev->flags & DEV_CTX_MULTI_BIND)) {
    dev->flags |= DEV_CTX_BOUND;
  }
  return status;
}

zx_status_t Coordinator::SetMexecZbis(zx::vmo kernel_zbi, zx::vmo data_zbi) {
  if (!kernel_zbi.is_valid() || !data_zbi.is_valid()) {
    return ZX_ERR_INVALID_ARGS;
  }

  if (zx_status_t status = mexec::PrepareDataZbi(root_resource().borrow(), data_zbi.borrow());
      status != ZX_OK) {
    LOGF(ERROR, "Failed to prepare mexec data ZBI: %s", zx_status_get_string(status));
    return status;
  }

  fidl::WireSyncClient<fuchsia_boot::Items> items;
  if (auto result = service::Connect<fuchsia_boot::Items>(); result.is_error()) {
    LOGF(ERROR, "Failed to connect to fuchsia.boot::Items: %s", result.status_string());
    return result.error_value();
  } else {
    items = fidl::BindSyncClient(std::move(result).value());
  }

  // Driver metadata that the driver framework generally expects to be present.
  constexpr std::array kItemsToAppend{ZBI_TYPE_DRV_MAC_ADDRESS, ZBI_TYPE_DRV_PARTITION_MAP,
                                      ZBI_TYPE_DRV_BOARD_PRIVATE, ZBI_TYPE_DRV_BOARD_INFO};
  zbitl::Image data_image{data_zbi.borrow()};
  for (uint32_t type : kItemsToAppend) {
    std::string_view name = zbitl::TypeName(type);

    fsl::SizedVmo payload;
    if (auto result = items->Get(type, 0); !result.ok()) {
      return result.status();
    } else if (!result->payload.is_valid()) {
      // Absence is signified with an empty result value.
      LOGF(INFO, "No %.*s item (%#xu) present to append to mexec data ZBI",
           static_cast<int>(name.size()), name.data(), type);
      continue;
    } else {
      payload = {std::move(result->payload), result->length};
    }

    std::vector<char> contents;
    if (!fsl::VectorFromVmo(payload, &contents)) {
      LOGF(ERROR, "Failed to read contents of %.*s item (%#xu)", static_cast<int>(name.size()),
           name.data(), type);
      return ZX_ERR_INTERNAL;
    }

    if (auto result = data_image.Append(zbi_header_t{.type = type}, zbitl::AsBytes(contents));
        result.is_error()) {
      LOGF(ERROR, "Failed to append %.*s item (%#xu) to mexec data ZBI: %s",
           static_cast<int>(name.size()), name.data(), type,
           zbitl::ViewErrorString(result.error_value()).c_str());
      return ZX_ERR_INTERNAL;
    }
  }

  mexec_kernel_zbi_ = std::move(kernel_zbi);
  mexec_data_zbi_ = std::move(data_zbi);
  return ZX_OK;
}

// DriverAdded is called when a driver is added after the
// devcoordinator has started.  The driver is added to the new-drivers
// list and work is queued to process it.
void Coordinator::DriverAdded(Driver* drv, const char* version) {
  fbl::DoublyLinkedList<std::unique_ptr<Driver>> driver_list;
  driver_list.push_back(std::unique_ptr<Driver>(drv));
  async::PostTask(dispatcher_, [this, driver_list = std::move(driver_list)]() mutable {
    AddAndBindDrivers(std::move(driver_list));
  });
}

// DriverAddedInit is called from driver enumeration during
// startup and before the devcoordinator starts running.  Enumerated
// drivers are added directly to the all-drivers or fallback list.
//
// TODO: fancier priorities
void Coordinator::DriverAddedInit(Driver* drv, const char* version) {
  auto driver = std::unique_ptr<Driver>(drv);

  if (driver->fallback) {
    // fallback driver, load only if all else fails
    fallback_drivers_.push_front(std::move(driver));
  } else if (version[0] == '!') {
    // debugging / development hack
    // prioritize drivers with version "!..." over others
    drivers_.push_front(std::move(driver));
  } else {
    drivers_.push_back(std::move(driver));
  }
}

zx_status_t Coordinator::BindDriver(Driver* drv) {
  zx_status_t status = bind_driver_manager_->MatchAndBind(root_device_, drv, true /* autobind */);
  if (status != ZX_ERR_NEXT) {
    return status;
  }
  if (!running_) {
    return ZX_ERR_UNAVAILABLE;
  }
  for (auto& dev : device_manager_->devices()) {
    zx_status_t status =
        bind_driver_manager_->MatchAndBind(fbl::RefPtr(&dev), drv, true /* autobind */);
    if (status == ZX_ERR_NEXT || status == ZX_ERR_ALREADY_BOUND) {
      continue;
    }
    if (status != ZX_OK) {
      return status;
    }
  }
  return ZX_OK;
}

void Coordinator::AddAndBindDrivers(fbl::DoublyLinkedList<std::unique_ptr<Driver>> drivers) {
  std::unique_ptr<Driver> driver;
  while ((driver = drivers.pop_front()) != nullptr) {
    Driver* driver_ptr = driver.get();
    drivers_.push_back(std::move(driver));

    zx_status_t status = BindDriver(driver_ptr);
    if (status != ZX_OK && status != ZX_ERR_UNAVAILABLE) {
      LOGF(ERROR, "Failed to bind driver '%s': %s", driver_ptr->name.data(),
           zx_status_get_string(status));
    }
  }
}

void Coordinator::StartLoadingNonBootDrivers() { driver_loader_.StartSystemLoadingThread(this); }

void Coordinator::BindFallbackDrivers() {
  for (auto& driver : fallback_drivers_) {
    LOGF(INFO, "Fallback driver '%s' is available", driver.name.data());
  }

  AddAndBindDrivers(std::move(fallback_drivers_));
}

zx::status<std::vector<fdd::wire::DriverInfo>> Coordinator::GetDriverInfo(
    fidl::AnyArena& allocator, const std::vector<const Driver*>& drivers) {
  std::vector<fdd::wire::DriverInfo> driver_info_vec;
  // TODO(fxbug.dev/80033): Support base drivers.
  for (const auto& driver : drivers) {
    fdd::wire::DriverInfo driver_info(allocator);
    driver_info.set_name(allocator,
                         fidl::StringView(allocator, {driver->name.data(), driver->name.size()}));
    driver_info.set_url(
        allocator, fidl::StringView(allocator, {driver->libname.data(), driver->libname.size()}));

    if (driver->bytecode_version == 1) {
      auto* binding = std::get_if<std::unique_ptr<zx_bind_inst_t[]>>(&driver->binding);
      if (!binding) {
        return zx::error(ZX_ERR_NOT_FOUND);
      }
      auto binding_insts = binding->get();

      uint32_t count = 0;
      if (driver->binding_size > 0) {
        count = driver->binding_size / sizeof(binding_insts[0]);
      }
      if (count > fdm::wire::kBindRulesInstructionsMax) {
        return zx::error(ZX_ERR_BUFFER_TOO_SMALL);
      }

      using fdm::wire::BindInstruction;
      fidl::VectorView<BindInstruction> instructions(allocator, count);
      for (uint32_t i = 0; i < count; i++) {
        instructions[i] = BindInstruction{
            .op = binding_insts[i].op,
            .arg = binding_insts[i].arg,
            .debug = binding_insts[i].debug,
        };
      }
      driver_info.set_bind_rules(
          allocator, fdd::wire::BindRulesBytecode::WithBytecodeV1(allocator, instructions));

    } else if (driver->bytecode_version == 2) {
      auto* binding = std::get_if<std::unique_ptr<uint8_t[]>>(&driver->binding);
      if (!binding) {
        return zx::error(ZX_ERR_NOT_FOUND);
      }

      fidl::VectorView<uint8_t> bytecode(allocator, driver->binding_size);
      for (uint32_t i = 0; i < driver->binding_size; i++) {
        bytecode[i] = binding->get()[i];
      }

      driver_info.set_bind_rules(allocator,
                                 fdd::wire::BindRulesBytecode::WithBytecodeV2(allocator, bytecode));
    } else {
      return zx::error(ZX_ERR_INVALID_ARGS);
    }
    driver_info_vec.push_back(std::move(driver_info));
  }

  return zx::ok(std::move(driver_info_vec));
}

namespace {
class DriverInfoIterator : public fidl::WireServer<fuchsia_driver_development::DriverInfoIterator> {
 public:
  explicit DriverInfoIterator(std::unique_ptr<fidl::Arena<512>> arena,
                              std::vector<fdd::wire::DriverInfo> list)
      : arena_(std::move(arena)), list_(std::move(list)) {}

  void GetNext(GetNextRequestView request, GetNextCompleter::Sync& completer) {
    constexpr size_t kMaxEntries = 100;
    auto result = cpp20::span(&list_[offset_], std::min(kMaxEntries, list_.size() - offset_));
    offset_ += result.size();

    completer.Reply(
        fidl::VectorView<fdd::wire::DriverInfo>::FromExternal(result.data(), result.size()));
  }

 private:
  size_t offset_ = 0;
  std::unique_ptr<fidl::Arena<512>> arena_;
  std::vector<fdd::wire::DriverInfo> list_;
};
}  // namespace

void Coordinator::GetDriverInfo(GetDriverInfoRequestView request,
                                GetDriverInfoCompleter::Sync& completer) {
  std::vector<const Driver*> driver_list;
  if (request->driver_filter.empty()) {
    for (const auto& driver : drivers()) {
      driver_list.push_back(&driver);
    }
  } else {
    for (const auto& d : request->driver_filter) {
      std::string_view driver_path(d.data(), d.size());
      for (const auto& drv : drivers()) {
        if (driver_path.compare(drv.libname) == 0) {
          driver_list.push_back(&drv);
          break;
        }
      }
    }
  }

  // Check the driver index for drivers.
  auto driver_index_drivers = driver_loader_.GetAllDriverIndexDrivers();
  for (auto driver : driver_index_drivers) {
    if (request->driver_filter.empty()) {
      driver_list.push_back(driver);
    } else {
      for (const auto& d : request->driver_filter) {
        std::string_view driver_path(d.data(), d.size());
        if (driver_path.compare(driver->libname) == 0) {
          driver_list.push_back(driver);
        }
      }
    }
  }

  // If we have driver filters check that we found one driver per filter.
  if (!request->driver_filter.empty()) {
    if (driver_list.size() != request->driver_filter.count()) {
      request->iterator.Close(ZX_ERR_NOT_FOUND);
      return;
    }
  }

  auto arena = std::make_unique<fidl::Arena<512>>();
  auto result = GetDriverInfo(*arena, driver_list);
  if (result.is_error()) {
    request->iterator.Close(result.status_value());
    return;
  }
  auto iterator = std::make_unique<DriverInfoIterator>(std::move(arena), std::move(*result));
  fidl::BindServer(dispatcher(), std::move(request->iterator), std::move(iterator));
}

void Coordinator::Register(RegisterRequestView request, RegisterCompleter::Sync& completer) {
  std::string driver_url_str(request->package_url.url.data(), request->package_url.url.size());
  zx_status_t status = LoadEphemeralDriver(&package_resolver_, driver_url_str);
  if (status != ZX_OK) {
    LOGF(ERROR, "Could not load '%s'", driver_url_str.c_str());
    completer.ReplyError(status);
    return;
  }
  LOGF(INFO, "Loaded driver '%s'", driver_url_str.c_str());
  completer.ReplySuccess();
}

zx_status_t Coordinator::LoadEphemeralDriver(internal::PackageResolverInterface* resolver,
                                             const std::string& package_url) {
  ZX_ASSERT(config_.enable_ephemeral);

  auto result = resolver->FetchDriver(package_url);
  if (!result.is_ok()) {
    return result.status_value();
  }
  fbl::DoublyLinkedList<std::unique_ptr<Driver>> driver_list;
  driver_list.push_back(std::move(result.value()));
  async::PostTask(dispatcher_, [this, driver_list = std::move(driver_list)]() mutable {
    AddAndBindDrivers(std::move(driver_list));
  });

  return ZX_OK;
}

zx::status<std::vector<fdd::wire::DeviceInfo>> Coordinator::GetDeviceInfo(
    fidl::AnyArena& allocator, const std::vector<fbl::RefPtr<Device>>& devices) {
  std::vector<fdd::wire::DeviceInfo> device_info_vec;
  for (const auto& device : devices) {
    if (device->props().size() > fdm::wire::kPropertiesMax) {
      return zx::error(ZX_ERR_BUFFER_TOO_SMALL);
    }
    if (device->str_props().size() > fdm::wire::kPropertiesMax) {
      return zx::error(ZX_ERR_BUFFER_TOO_SMALL);
    }

    fdd::wire::DeviceInfo device_info(allocator);

    // id leaks internal pointers, but since this is a development only API, it shouldn't be
    // a big deal.
    device_info.set_id(allocator, reinterpret_cast<uint64_t>(device.get()));

    // TODO(fxbug.dev/80094): Handle multiple parents case.
    fidl::VectorView<uint64_t> parent_ids(allocator, 1);
    parent_ids[0] = reinterpret_cast<uint64_t>(device->parent().get());
    device_info.set_parent_ids(allocator, parent_ids);

    size_t child_count = 0;
    for (const auto& child __attribute__((unused)) : device->children()) {
      child_count++;
    }
    if (child_count > 0) {
      fidl::VectorView<uint64_t> child_ids(allocator, child_count);
      size_t i = 0;
      for (const auto& child : device->children()) {
        child_ids[i++] = reinterpret_cast<uint64_t>(&child);
      }
      device_info.set_child_ids(allocator, child_ids);
    }

    if (device->host()) {
      device_info.set_driver_host_koid(allocator, device->host()->koid());
    }

    char path[fdm::wire::kDevicePathMax + 1];
    if (auto status = GetTopologicalPath(device, path, sizeof(path)); status != ZX_OK) {
      return zx::error(status);
    }

    device_info.set_topological_path(allocator, fidl::StringView(allocator, {path, strlen(path)}));

    device_info.set_bound_driver_libname(
        allocator,
        fidl::StringView(allocator, {device->libname().data(), device->libname().size()}));

    fidl::VectorView<fdm::wire::DeviceProperty> props(allocator, device->props().size());
    for (size_t i = 0; i < device->props().size(); i++) {
      const auto& prop = device->props()[i];
      props[i] = fdm::wire::DeviceProperty{
          .id = prop.id,
          .reserved = prop.reserved,
          .value = prop.value,
      };
    }

    fidl::VectorView<fdm::wire::DeviceStrProperty> str_props(allocator, device->str_props().size());
    for (size_t i = 0; i < device->str_props().size(); i++) {
      const auto& str_prop = device->str_props()[i];
      if (str_prop.value.valueless_by_exception()) {
        return zx::error(ZX_ERR_INVALID_ARGS);
      }

      auto fidl_str_prop = fdm::wire::DeviceStrProperty{
          .key = fidl::StringView(allocator, str_prop.key),
      };

      if (std::holds_alternative<uint32_t>(str_prop.value)) {
        auto* prop_val = std::get_if<uint32_t>(&str_prop.value);
        fidl_str_prop.value = fdm::wire::PropertyValue::WithIntValue(*prop_val);
      } else if (std::holds_alternative<std::string>(str_prop.value)) {
        auto* prop_val = std::get_if<std::string>(&str_prop.value);
        fidl_str_prop.value = fdm::wire::PropertyValue::WithStrValue(
            allocator, fidl::StringView(allocator, *prop_val));
      } else if (std::holds_alternative<bool>(str_prop.value)) {
        auto* prop_val = std::get_if<bool>(&str_prop.value);
        fidl_str_prop.value = fdm::wire::PropertyValue::WithBoolValue(*prop_val);
      }

      str_props[i] = fidl_str_prop;
    }

    device_info.set_property_list(allocator, fdm::wire::DevicePropertyList{
                                                 .props = props,
                                                 .str_props = str_props,
                                             });

    device_info.set_flags(fdd::wire::DeviceFlags(device->flags));

    device_info_vec.push_back(std::move(device_info));
  }
  return zx::ok(std::move(device_info_vec));
}

namespace {
class DeviceInfoIterator : public fidl::WireServer<fuchsia_driver_development::DeviceInfoIterator> {
 public:
  explicit DeviceInfoIterator(std::unique_ptr<fidl::Arena<512>> arena,
                              std::vector<fdd::wire::DeviceInfo> list)
      : arena_(std::move(arena)), list_(std::move(list)) {}

  void GetNext(GetNextRequestView request, GetNextCompleter::Sync& completer) {
    constexpr size_t kMaxEntries = 100;
    auto result = cpp20::span(&list_[offset_], std::min(kMaxEntries, list_.size() - offset_));
    offset_ += result.size();

    completer.Reply(
        fidl::VectorView<fdd::wire::DeviceInfo>::FromExternal(result.data(), result.size()));
  }

 private:
  size_t offset_ = 0;
  std::unique_ptr<fidl::Arena<512>> arena_;
  std::vector<fdd::wire::DeviceInfo> list_;
};
}  // namespace

void Coordinator::GetDeviceInfo(GetDeviceInfoRequestView request,
                                GetDeviceInfoCompleter::Sync& completer) {
  std::vector<fbl::RefPtr<Device>> device_list;
  if (request->device_filter.empty()) {
    for (auto& device : device_manager_->devices()) {
      device_list.push_back(fbl::RefPtr(&device));
    }
  } else {
    for (const auto& device_path : request->device_filter) {
      fbl::RefPtr<Device> device;
      std::string path(device_path.data(), device_path.size());
      zx_status_t status = devfs_walk(root_device_->devnode(), path.c_str(), &device);
      if (status != ZX_OK) {
        request->iterator.Close(status);
        return;
      }
      device_list.push_back(std::move(device));
    }
  }

  auto arena = std::make_unique<fidl::Arena<512>>();
  auto result = GetDeviceInfo(*arena, device_list);
  if (result.is_error()) {
    request->iterator.Close(result.status_value());
    return;
  }

  auto iterator = std::make_unique<DeviceInfoIterator>(std::move(arena), std::move(*result));
  fidl::BindServer(dispatcher(), std::move(request->iterator), std::move(iterator));
}

void Coordinator::Suspend(SuspendRequestView request, SuspendCompleter::Sync& completer) {
  suspend_resume_manager_->Suspend(
      request->flags,
      [completer = completer.ToAsync()](zx_status_t status) mutable { completer.Reply(status); });
}

void Coordinator::UnregisterSystemStorageForShutdown(
    UnregisterSystemStorageForShutdownRequestView request,
    UnregisterSystemStorageForShutdownCompleter::Sync& completer) {
  suspend_resume_manager_->suspend_handler().UnregisterSystemStorageForShutdown(
      [completer = completer.ToAsync()](zx_status_t status) mutable { completer.Reply(status); });
}

zx::status<> Coordinator::PublishDriverDevelopmentService(
    const fbl::RefPtr<fs::PseudoDir>& svc_dir) {
  const auto driver_dev = [this](fidl::ServerEnd<fdd::DriverDevelopment> request) {
    fidl::BindServer<fidl::WireServer<fdd::DriverDevelopment>>(
        dispatcher_, std::move(request), this,
        [](fidl::WireServer<fdd::DriverDevelopment>* self, fidl::UnbindInfo info,
           fidl::ServerEnd<fdd::DriverDevelopment> server_end) {
          if (info.is_user_initiated()) {
            return;
          }
          if (info.is_peer_closed()) {
            // For this development protocol, the client is free to disconnect
            // at any time.
            return;
          }
          LOGF(ERROR, "Error serving '%s': %s",
               fidl::DiscoverableProtocolName<fdd::DriverDevelopment>,
               info.FormatDescription().c_str());
        });
    return ZX_OK;
  };
  zx_status_t status = svc_dir->AddEntry(fidl::DiscoverableProtocolName<fdd::DriverDevelopment>,
                                         fbl::MakeRefCounted<fs::Service>(driver_dev));
  return zx::make_status(status);
}

zx_status_t Coordinator::InitOutgoingServices(const fbl::RefPtr<fs::PseudoDir>& svc_dir) {
  static_assert(fdm::wire::kSuspendFlagReboot == DEVICE_SUSPEND_FLAG_REBOOT);
  static_assert(fdm::wire::kSuspendFlagPoweroff == DEVICE_SUSPEND_FLAG_POWEROFF);

  const auto admin = [this](fidl::ServerEnd<fdm::Administrator> request) {
    fidl::BindServer<fidl::WireServer<fdm::Administrator>>(dispatcher_, std::move(request), this);
    return ZX_OK;
  };
  zx_status_t status = svc_dir->AddEntry(fidl::DiscoverableProtocolName<fdm::Administrator>,
                                         fbl::MakeRefCounted<fs::Service>(admin));
  if (status != ZX_OK) {
    return status;
  }

  const auto system_state_manager_register =
      [this](fidl::ServerEnd<fdm::SystemStateTransition> request) {
        auto status = fidl::BindSingleInFlightOnly<fidl::WireServer<fdm::SystemStateTransition>>(
            dispatcher_, std::move(request), std::make_unique<SystemStateManager>(this));
        if (status != ZX_OK) {
          LOGF(ERROR, "Failed to bind to client channel for '%s': %s",
               fidl::DiscoverableProtocolName<fdm::SystemStateTransition>,
               zx_status_get_string(status));
        }
        return status;
      };
  status = svc_dir->AddEntry(fidl::DiscoverableProtocolName<fdm::SystemStateTransition>,
                             fbl::MakeRefCounted<fs::Service>(system_state_manager_register));
  if (status != ZX_OK) {
    LOGF(ERROR, "Failed to add entry in service directory for '%s': %s",
         fidl::DiscoverableProtocolName<fdm::SystemStateTransition>, zx_status_get_string(status));
    return status;
  }

  if (config_.enable_ephemeral) {
    const auto driver_registrar = [this](fidl::ServerEnd<fdr::DriverRegistrar> request) {
      driver_registrar_binding_ = fidl::BindServer<fidl::WireServer<fdr::DriverRegistrar>>(
          dispatcher_, std::move(request), this);
      return ZX_OK;
    };
    status = svc_dir->AddEntry(fidl::DiscoverableProtocolName<fdr::DriverRegistrar>,
                               fbl::MakeRefCounted<fs::Service>(driver_registrar));
    if (status != ZX_OK) {
      return status;
    }
  }

  const auto debug = [this](fidl::ServerEnd<fdm::DebugDumper> request) {
    fidl::BindServer<fidl::WireServer<fdm::DebugDumper>>(dispatcher_, std::move(request),
                                                         debug_dump_.get());
    return ZX_OK;
  };
  return svc_dir->AddEntry(fidl::DiscoverableProtocolName<fdm::DebugDumper>,
                           fbl::MakeRefCounted<fs::Service>(debug));
}

void Coordinator::OnOOMEvent(async_dispatcher_t* dispatcher, async::WaitBase* wait,
                             zx_status_t status, const zx_packet_signal_t* signal) {
  suspend_resume_manager_->suspend_handler().ShutdownFilesystems([](zx_status_t status) {});
}

std::string Coordinator::GetFragmentDriverUrl() const { return "#driver/fragment.so"; }

void Coordinator::RestartDriverHosts(RestartDriverHostsRequestView request,
                                     RestartDriverHostsCompleter::Sync& completer) {
  std::string_view driver_path(request->driver_path.data(), request->driver_path.size());

  // Find devices containing the driver.
  uint32_t count = 0;
  for (auto& dev : device_manager_->devices()) {
    // Call remove on the device's driver host if it contains the driver.
    if (dev.libname().compare(driver_path) == 0) {
      LOGF(INFO, "Device %s found in restart driver hosts.", dev.name().data());
      LOGF(INFO, "Shutting down host: %ld.", dev.host()->koid());

      // Unbind and Remove all the devices in the Driver Host.
      device_manager_->ScheduleUnbindRemoveAllDevices(dev.host());
      count++;
    }
  }

  completer.ReplySuccess(count);
}
