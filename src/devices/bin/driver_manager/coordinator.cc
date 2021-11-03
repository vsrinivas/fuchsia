// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/devices/bin/driver_manager/coordinator.h"

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <fidl/fuchsia.boot/cpp/wire.h>
#include <fidl/fuchsia.device.manager/cpp/wire.h>
#include <fidl/fuchsia.hardware.power.statecontrol/cpp/wire.h>
#include <fidl/fuchsia.io/cpp/wire.h>
#include <fidl/fuchsia.pkg/cpp/wire.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/async/cpp/receiver.h>
#include <lib/async/cpp/task.h>
#include <lib/async/cpp/wait.h>
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

#include <cstdint>
#include <memory>
#include <string_view>
#include <utility>

#include <driver-info/driver-info.h>
#include <fbl/string_printf.h>
#include <inspector/inspector.h>
#include <src/bringup/lib/mexec/mexec.h>

#include "src/devices/bin/driver_manager/composite_device.h"
#include "src/devices/bin/driver_manager/devfs.h"
#include "src/devices/bin/driver_manager/driver_host_loader_service.h"
#include "src/devices/bin/driver_manager/manifest_parser.h"
#include "src/devices/bin/driver_manager/package_resolver.h"
#include "src/devices/bin/driver_manager/v1/unbind_task.h"
#include "src/devices/bin/driver_manager/vmo_writer.h"
#include "src/devices/lib/log/log.h"

namespace fio = fuchsia_io;

namespace {

namespace fdd = fuchsia_driver_development;
namespace fdm = fuchsia_device_manager;
namespace fdr = fuchsia_driver_registrar;
namespace fpm = fuchsia_power_manager;

constexpr char kDriverHostPath[] = "bin/driver_host";
constexpr char kBootFirmwarePath[] = "lib/firmware";
constexpr char kSystemPrefix[] = "/system/";
constexpr char kSystemFirmwarePath[] = "/system/lib/firmware";
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

}  // namespace

namespace statecontrol_fidl = fuchsia_hardware_power_statecontrol;

Coordinator::Coordinator(CoordinatorConfig config, InspectManager* inspect_manager,
                         async_dispatcher_t* dispatcher)
    : config_(std::move(config)),
      dispatcher_(dispatcher),
      base_resolver_(config_.boot_args),
      driver_loader_(config_.boot_args, std::move(config_.driver_index), &base_resolver_,
                     dispatcher, config_.require_system),
      suspend_handler_(this, config.suspend_timeout),
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
}

Coordinator::~Coordinator() {}

bool Coordinator::InSuspend() const { return suspend_handler().InSuspend(); }

bool Coordinator::InResume() const {
  return (resume_context().flags() == ResumeContext::Flags::kResume);
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

zx_status_t Coordinator::InitCoreDevices(std::string_view sys_device_driver) {
  // If the sys device is not a path, then we try to load it like a URL.
  if (sys_device_driver[0] != '/') {
    auto string = std::string(sys_device_driver.data());
    driver_loader_.LoadDriverUrl(string);
  }

  sys_device_ =
      fbl::MakeRefCounted<Device>(this, "sys", sys_device_driver, "sys,", root_device_, 0,
                                  zx::vmo(), zx::channel(), fidl::ClientEnd<fio::Directory>());
  sys_device_->flags = DEV_CTX_IMMORTAL | DEV_CTX_MUST_ISOLATE;

  return ZX_OK;
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

void Coordinator::DumpDevice(VmoWriter* vmo, const Device* dev, size_t indent) const {
  zx_koid_t pid = dev->host() ? dev->host()->koid() : 0;
  if (pid == 0) {
    vmo->Printf("%*s[%s]\n", (int)(indent * 3), "", dev->name().data());
  } else {
    vmo->Printf("%*s%c%s%c pid=%zu %s\n", (int)(indent * 3), "",
                dev->flags & DEV_CTX_PROXY ? '<' : '[', dev->name().data(),
                dev->flags & DEV_CTX_PROXY ? '>' : ']', pid, dev->libname().data());
  }
  if (dev->proxy()) {
    indent++;
    DumpDevice(vmo, dev->proxy().get(), indent);
  }
  for (const auto& child : dev->children()) {
    DumpDevice(vmo, &child, indent + 1);
  }
}

void Coordinator::DumpState(VmoWriter* vmo) const {
  DumpDevice(vmo, root_device_.get(), 0);
  DumpDevice(vmo, sys_device_.get(), 1);
}

void Coordinator::DumpDeviceProps(VmoWriter* vmo, const Device* dev) const {
  if (dev->host()) {
    vmo->Printf("Name [%s]%s%s%s\n", dev->name().data(), dev->libname().empty() ? "" : " Driver [",
                dev->libname().empty() ? "" : dev->libname().data(),
                dev->libname().empty() ? "" : "]");
    vmo->Printf("Flags   :%s%s%s%s%s%s\n", dev->flags & DEV_CTX_IMMORTAL ? " Immortal" : "",
                dev->flags & DEV_CTX_MUST_ISOLATE ? " Isolate" : "",
                dev->flags & DEV_CTX_MULTI_BIND ? " MultiBind" : "",
                dev->flags & DEV_CTX_BOUND ? " Bound" : "",
                (dev->state() == Device::State::kDead) ? " Dead" : "",
                dev->flags & DEV_CTX_PROXY ? " Proxy" : "");

    char a = (char)((dev->protocol_id() >> 24) & 0xFF);
    char b = (char)((dev->protocol_id() >> 16) & 0xFF);
    char c = (char)((dev->protocol_id() >> 8) & 0xFF);
    char d = (char)(dev->protocol_id() & 0xFF);
    vmo->Printf("ProtoId : '%c%c%c%c' %#08x(%u)\n", isprint(a) ? a : '.', isprint(b) ? b : '.',
                isprint(c) ? c : '.', isprint(d) ? d : '.', dev->protocol_id(), dev->protocol_id());

    const auto& props = dev->props();
    vmo->Printf("%zu Propert%s\n", props.size(), props.size() == 1 ? "y" : "ies");
    for (uint32_t i = 0; i < props.size(); ++i) {
      const zx_device_prop_t* p = &props[i];
      const char* param_name = di_bind_param_name(p->id);

      if (param_name) {
        vmo->Printf("[%2u/%2zu] : Value %#08x Id %s\n", i, props.size(), p->value, param_name);
      } else {
        vmo->Printf("[%2u/%2zu] : Value %#08x Id %#04hx\n", i, props.size(), p->value, p->id);
      }
    }

    const auto& str_props = dev->str_props();
    vmo->Printf("%zu String Propert%s\n", str_props.size(), str_props.size() == 1 ? "y" : "ies");
    for (uint32_t i = 0; i < str_props.size(); ++i) {
      const StrProperty* p = &str_props[i];
      vmo->Printf("[%2u/%2zu] : %s=", i, str_props.size(), p->key.data());
      std::visit(
          [vmo](auto&& arg) {
            using T = std::decay_t<decltype(arg)>;

            if constexpr (std::is_same_v<T, uint32_t>) {
              vmo->Printf("%#08x\n", arg);
            } else if constexpr (std::is_same_v<T, std::string>) {
              vmo->Printf("\"%s\"\n", arg.data());
            } else if constexpr (std::is_same_v<T, bool>) {
              vmo->Printf("%s\n", arg ? "true" : "false");
            } else {
              vmo->Printf("(unknown value type!)\n");
            }
          },
          p->value);
    }
    vmo->Printf("\n");
  }

  if (dev->proxy()) {
    DumpDeviceProps(vmo, dev->proxy().get());
  }
  for (const auto& child : dev->children()) {
    DumpDeviceProps(vmo, &child);
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
  std::string binary = config_.path_prefix + kDriverHostPath;
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
      .binary = binary.c_str(),
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

// Add a new device to a parent device (same driver_host)
// New device is published in devfs.
// Caller closes handles on error, so we don't have to.
zx_status_t Coordinator::AddDevice(
    const fbl::RefPtr<Device>& parent,
    fidl::ClientEnd<fuchsia_device_manager::DeviceController> device_controller,
    fidl::ServerEnd<fuchsia_device_manager::Coordinator> coordinator,
    const fdm::wire::DeviceProperty* props_data, size_t props_count,
    const fdm::wire::DeviceStrProperty* str_props_data, size_t str_props_count,
    std::string_view name, uint32_t protocol_id, std::string_view driver_path,
    std::string_view args, bool skip_autobind, bool has_init, bool always_init, zx::vmo inspect,
    zx::channel client_remote, fidl::ClientEnd<fio::Directory> outgoing_dir,
    fbl::RefPtr<Device>* new_device) {
  // If this is true, then |name_data|'s size is properly bounded.
  static_assert(fdm::wire::kDeviceNameMax == ZX_DEVICE_NAME_MAX);
  static_assert(fdm::wire::kPropertiesMax <= UINT32_MAX);

  if (InSuspend()) {
    LOGF(ERROR, "Add device '%.*s' forbidden in suspend", static_cast<int>(name.size()),
         name.data());
    return ZX_ERR_BAD_STATE;
  }

  if (InResume()) {
    LOGF(ERROR, "Add device '%.*s' forbidden in resume", static_cast<int>(name.size()),
         name.data());
    return ZX_ERR_BAD_STATE;
  }

  if (parent->state() == Device::State::kUnbinding) {
    LOGF(ERROR, "Add device '%.*s' forbidden while parent is unbinding",
         static_cast<int>(name.size()), name.data());
    return ZX_ERR_BAD_STATE;
  }

  fbl::Array<zx_device_prop_t> props(new zx_device_prop_t[props_count], props_count);
  if (!props) {
    return ZX_ERR_NO_MEMORY;
  }
  for (uint32_t i = 0; i < props_count; i++) {
    props[i] = zx_device_prop_t{
        .id = props_data[i].id,
        .reserved = props_data[i].reserved,
        .value = props_data[i].value,
    };
  }

  fbl::Array<StrProperty> str_props(new StrProperty[str_props_count], str_props_count);
  if (!str_props) {
    return ZX_ERR_NO_MEMORY;
  }
  for (uint32_t i = 0; i < str_props_count; i++) {
    str_props[i].key = str_props_data[i].key.get();
    if (str_props_data[i].value.is_int_value()) {
      str_props[i].value = str_props_data[i].value.int_value();
    } else if (str_props_data[i].value.is_str_value()) {
      str_props[i].value = std::string(str_props_data[i].value.str_value().get());
    } else if (str_props_data[i].value.is_bool_value()) {
      str_props[i].value = str_props_data[i].value.bool_value();
    }
  }

  fbl::String name_str(name);
  fbl::String driver_path_str(driver_path);
  fbl::String args_str(args);

  // TODO(fxbug.dev/43370): remove this check once init tasks can be enabled for all devices.
  bool want_init_task = has_init || always_init;
  fbl::RefPtr<Device> dev;
  zx_status_t status = Device::Create(
      this, parent, std::move(name_str), std::move(driver_path_str), std::move(args_str),
      protocol_id, std::move(props), std::move(str_props), std::move(coordinator),
      std::move(device_controller), want_init_task, skip_autobind, std::move(inspect),
      std::move(client_remote), std::move(outgoing_dir), &dev);
  if (status != ZX_OK) {
    return status;
  }
  devices_.push_back(dev);

  // Note that |dev->parent()| may not match |parent| here, so we should always
  // use |dev->parent()|.  This case can happen if |parent| refers to a device
  // proxy.

  // If we're creating a device that's using the fragment driver, inform the
  // fragment.
  if (dev->libname() == GetFragmentDriverUrl()) {
    for (auto& cur_fragment : dev->parent()->fragments()) {
      if (cur_fragment.fragment_device() == nullptr) {
        // Pick the first fragment that does not have a device added by the fragment
        // driver.
        cur_fragment.set_fragment_device(dev);
        status = cur_fragment.composite()->TryAssemble();
        if (status != ZX_OK && status != ZX_ERR_SHOULD_WAIT) {
          LOGF(ERROR, "Failed to assemble composite device: %s", zx_status_get_string(status));
        }
        break;
      }
    }
  }

  VLOGF(1, "Added device %p '%s'", dev.get(), dev->name().data());
  // TODO(fxbug.dev/43370): remove this once init tasks can be enabled for all devices.
  if (!want_init_task) {
    status = dev->SignalReadyForBind();
    if (status != ZX_OK) {
      return status;
    }
    VLOGF(1, "Published device %p '%s' args='%s' props=%zu parent=%p", dev.get(),
          dev->name().data(), dev->args().data(), dev->props().size(), dev->parent().get());
  }

  *new_device = std::move(dev);
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

void Coordinator::ScheduleRemove(const fbl::RefPtr<Device>& dev) {
  dev->CreateUnbindRemoveTasks(
      UnbindTaskOpts{.do_unbind = false, .post_on_create = true, .driver_host_requested = false});
}

void Coordinator::ScheduleDriverHostRequestedRemove(const fbl::RefPtr<Device>& dev,
                                                    bool do_unbind) {
  dev->CreateUnbindRemoveTasks(UnbindTaskOpts{
      .do_unbind = do_unbind, .post_on_create = true, .driver_host_requested = true});
}

void Coordinator::ScheduleDriverHostRequestedUnbindChildren(const fbl::RefPtr<Device>& parent) {
  for (auto& child : parent->children()) {
    child.CreateUnbindRemoveTasks(
        UnbindTaskOpts{.do_unbind = true, .post_on_create = true, .driver_host_requested = true});
  }
}

// Remove device from parent
// forced indicates this is removal due to a channel close
// or process exit, which means we should remove all other
// devices that share the driver_host at the same time
zx_status_t Coordinator::RemoveDevice(const fbl::RefPtr<Device>& dev, bool forced) {
  if (forced && config_.crash_policy == DriverHostCrashPolicy::kRebootSystem) {
    // TODO(fxbug.dev/67168): Trigger system restart more gracefully.
    ZX_ASSERT(false);
  }
  dev->inc_num_removal_attempts();

  if (dev->state() == Device::State::kDead) {
    // This should not happen
    LOGF(ERROR, "Cannot remove device %p '%s' twice", dev.get(), dev->name().data());
    return ZX_ERR_BAD_STATE;
  }
  if (dev->flags & DEV_CTX_IMMORTAL) {
    // This too should not happen
    LOGF(ERROR, "Cannot remove device %p '%s' (immortal)", dev.get(), dev->name().data());
    return ZX_ERR_BAD_STATE;
  }

  LOGF(INFO, "Removing device %p '%s' parent=%p", dev.get(), dev->name().data(),
       dev->parent().get());
  dev->set_state(Device::State::kDead);

  // remove from devfs, preventing further OPEN attempts
  devfs_unpublish(dev.get());

  // Mark any suspend that's in-flight as completed, since if the device is
  // removed it should be in its lowest state.
  // TODO(teisenbe): Should we mark it as failed if this is a forced removal?
  dev->CompleteSuspend(ZX_OK);
  dev->CompleteInit(ZX_ERR_UNAVAILABLE);

  fbl::RefPtr<DriverHost> dh = dev->host();
  bool driver_host_dying = (dh != nullptr && (dh->flags() & DriverHost::Flags::kDying));
  if (forced || driver_host_dying) {
    // We are force removing all devices in the driver_host, so force complete any outstanding
    // tasks.
    dev->CompleteUnbind(ZX_ERR_UNAVAILABLE);
    dev->CompleteRemove(ZX_ERR_UNAVAILABLE);

    // If there is a device proxy, we need to create a new unbind task for it.
    // For non-forced removals, the unbind task will handle scheduling the proxy removal.
    if (dev->proxy()) {
      ScheduleRemove(dev->proxy());
    }
  } else {
    // We should not be removing a device while the unbind task is still running.
    ZX_ASSERT(dev->GetActiveUnbind() == nullptr);
  }

  // Check if this device is a composite device, and if so disconnects from it
  if (dev->composite()) {
    dev->composite()->Remove();
  }

  // Check if this device is a composite fragment device
  if (dev->libname() == GetFragmentDriverUrl()) {
    // If it is, then its parent will know about which one (since the parent
    // is the actual device matched by the fragment description).
    const auto& parent = dev->parent();

    for (auto itr = parent->fragments().begin(); itr != parent->fragments().end();) {
      auto& cur_fragment = *itr;
      // Advance the iterator because we will erase the current element from the list.
      ++itr;
      if (cur_fragment.fragment_device() == dev) {
        cur_fragment.Unbind();
        parent->fragments().erase(cur_fragment);
        break;
      }
    }
  }

  // detach from driver_host
  if (dh != nullptr) {
    // We're holding on to a reference to the driver_host through |dh|.
    // This is necessary to prevent it from being freed in the middle of
    // the code below.
    dev->set_host(nullptr);

    // If we are responding to a disconnect,
    // we'll remove all the other devices on this driver_host too.
    // A side-effect of this is that the driver_host will be released,
    // as well as any proxy devices.
    if (forced) {
      dh->flags() |= DriverHost::Flags::kDying;

      fbl::RefPtr<Device> next;
      fbl::RefPtr<Device> last;
      while (!dh->devices().is_empty()) {
        next = fbl::RefPtr(&dh->devices().front());
        if (last == next) {
          // This shouldn't be possible, but let's not infinite-loop if it happens
          LOGF(FATAL, "Failed to remove device %p '%s' from driver_host", next.get(),
               next->name().data());
        }
        RemoveDevice(next, false);
        last = std::move(next);
      }

      // TODO: set a timer so if this driver_host does not finish dying
      //      in a reasonable amount of time, we fix the glitch.
    }

    dh.reset();
  }

  // if we have a parent, disconnect and downref it
  fbl::RefPtr<Device> parent = dev->parent();
  if (parent != nullptr) {
    Device* real_parent;
    if (parent->flags & DEV_CTX_PROXY) {
      real_parent = parent->parent().get();
    } else {
      real_parent = parent.get();
    }
    dev->DetachFromParent();
    if (!(dev->flags & DEV_CTX_PROXY)) {
      if (parent->children().is_empty()) {
        parent->flags &= (~DEV_CTX_BOUND);
        if (real_parent->test_state() == Device::TestStateMachine::kTestUnbindSent) {
          real_parent->test_event().signal(0, TEST_REMOVE_DONE_SIGNAL);
          if (!(dev->flags & DEV_CTX_PROXY)) {
            // remove from list of all devices
            devices_.erase(*dev);
          }
          return ZX_OK;
        }

        // TODO: This code is to cause the bind process to
        //      restart and get a new driver_host to be launched
        //      when a driver_host dies.  It should probably be
        //      more tied to driver_host teardown than it is.
        // IF the policy is set such that we take action
        // AND we are the last child of our parent
        // AND our parent is not itself dead
        // AND our parent is a BUSDEV
        // AND our parent's driver_host is not dying
        // THEN we will want to rebind our parent
        if ((config_.crash_policy == DriverHostCrashPolicy::kRestartDriverHost) &&
            (parent->state() != Device::State::kDead) && (parent->flags & DEV_CTX_MUST_ISOLATE) &&
            ((parent->host() == nullptr) ||
             !(parent->host()->flags() & DriverHost::Flags::kDying))) {
          VLOGF(1, "Bus device %p '%s' is unbound", parent.get(), parent->name().data());

          if (parent->retries > 0) {
            LOGF(INFO, "Suspected crash: attempting to re-bind %s", parent->name().data());
            // Add device with an exponential backoff.
            zx_status_t r = parent->SignalReadyForBind(parent->backoff);
            if (r != ZX_OK) {
              return r;
            }
            parent->backoff *= 2;
            parent->retries--;
          }
        }
      }
    }
  }

  if (!(dev->flags & DEV_CTX_PROXY)) {
    // remove from list of all devices
    devices_.erase(*dev);
  }

  return ZX_OK;
}

zx_status_t Coordinator::AddCompositeDevice(const fbl::RefPtr<Device>& dev, std::string_view name,
                                            fdm::wire::CompositeDeviceDescriptor comp_desc) {
  std::unique_ptr<CompositeDevice> new_device;
  zx_status_t status = CompositeDevice::Create(name, std::move(comp_desc), &new_device);
  if (status != ZX_OK) {
    return status;
  }

  // Try to bind the new composite device specification against existing
  // devices.
  for (auto& dev : devices_) {
    if (!dev.is_bindable() && !dev.is_composite_bindable()) {
      continue;
    }

    auto dev_ref = fbl::RefPtr(&dev);
    size_t index;
    if (new_device->TryMatchFragments(dev_ref, &index)) {
      LOGF(INFO, "Device '%s' matched fragment %zu of composite '%s'", dev.name().data(), index,
           new_device->name().data());
      status = new_device->BindFragment(index, dev_ref);
      if (status != ZX_OK) {
        LOGF(ERROR, "Device '%s' failed to bind fragment %zu of composite '%s': %s",
             dev.name().data(), index, new_device->name().data(), zx_status_get_string(status));
      }
    }
  }

  composite_devices_.push_back(std::move(new_device));
  return ZX_OK;
}

static zx_status_t LoadFirmwareAt(int fd, const char* path, zx::vmo* vmo, size_t* size) {
  fbl::unique_fd firmware_fd(openat(fd, path, O_RDONLY));
  if (firmware_fd.get() < 0) {
    if (errno != ENOENT) {
      return ZX_ERR_IO;
    }
    return ZX_ERR_NOT_FOUND;
  }

  *size = lseek(firmware_fd.get(), 0, SEEK_END);
  zx_status_t status = fdio_get_vmo_clone(firmware_fd.get(), vmo->reset_and_get_address());
  return status;
}

zx_status_t Coordinator::LoadFirmware(const fbl::RefPtr<Device>& dev, const char* driver_libname,
                                      const char* path, zx::vmo* vmo, size_t* size) {
  const std::string fwdirs[] = {
      config_.path_prefix + kBootFirmwarePath,
      kSystemFirmwarePath,
  };

  // Must be a relative path and no funny business.
  if (path[0] == '/' || path[0] == '.') {
    return ZX_ERR_INVALID_ARGS;
  }

  // We are only going to check /system/ if the driver was loaded out of /system.
  // This ensures that /system is available and loaded, as otherwise touching /system
  // will wait, potentially forever.
  size_t directories_to_check = 1;
  if (strncmp(driver_libname, kSystemPrefix, std::size(kSystemPrefix) - 1) == 0) {
    directories_to_check = std::size(fwdirs);
  }

  for (unsigned n = 0; n < directories_to_check; n++) {
    fbl::unique_fd fd(open(fwdirs[n].c_str(), O_RDONLY, O_DIRECTORY));
    if (fd.get() < 0) {
      continue;
    }
    zx_status_t status = LoadFirmwareAt(fd.get(), path, vmo, size);
    if (status == ZX_OK || status != ZX_ERR_NOT_FOUND) {
      return status;
    }
  }

  const Driver* driver = LibnameToDriver(driver_libname);
  if (driver == nullptr || !driver->package_dir.is_valid()) {
    return ZX_ERR_NOT_FOUND;
  }
  auto package_path = std::string("lib/firmware/") + path;
  return LoadFirmwareAt(driver->package_dir.get(), package_path.c_str(), vmo, size);
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

namespace {

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

// send message to driver_host, requesting the binding of a driver to a device
zx_status_t BindDriver(const fbl::RefPtr<Device>& dev, const char* libname) {
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

        fbl::RefPtr<Device> real_parent;
        if (dev->flags & DEV_CTX_PROXY) {
          real_parent = dev->parent();
        } else {
          real_parent = dev;
        }
        for (auto& child : real_parent->children()) {
          const char* drivername =
              dev->coordinator->LibnameToDriver(child.libname().data())->name.data();
          auto bootarg = fbl::StringPrintf("driver.%s.compatibility-tests-enable", drivername);

          auto compat_test_enabled = (*dev->coordinator->boot_args())
                                         ->GetBool(fidl::StringView::FromExternal(bootarg), false);
          if (compat_test_enabled.ok() && compat_test_enabled->value &&
              (real_parent->test_state() == Device::TestStateMachine::kTestNotStarted)) {
            bootarg = fbl::StringPrintf("driver.%s.compatibility-tests-wait-time", drivername);
            auto test_wait_time = (*dev->coordinator->boot_args())
                                      ->GetString(fidl::StringView::FromExternal(bootarg));
            zx::duration test_time = kDefaultTestTimeout;
            if (test_wait_time.ok() && !test_wait_time->value.is_null()) {
              auto test_timeout =
                  std::string{test_wait_time->value.data(), test_wait_time->value.size()};
              test_time = zx::msec(atoi(test_timeout.data()));
            }
            real_parent->DriverCompatibilityTest(test_time, std::nullopt);
            break;
          } else if (real_parent->test_state() == Device::TestStateMachine::kTestBindSent) {
            real_parent->test_event().signal(0, TEST_BIND_DONE_SIGNAL);
            break;
          }
        }
        if (result->test_output.is_valid()) {
          LOGF(INFO, "Setting test channel for driver '%s'", dev->name().data());
          auto status =
              dev->set_test_output(std::move(result->test_output), dev->coordinator->dispatcher());
          if (status != ZX_OK) {
            LOGF(ERROR, "Failed to wait on test output for driver '%s': %s", dev->name().data(),
                 zx_status_get_string(status));
          }
        }
      });
  dev->flags |= DEV_CTX_BOUND;
  return ZX_OK;
}

}  // namespace

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
    return ::BindDriver(dev, drv->libname.c_str());
  }

  zx_status_t status;
  if (dev->has_outgoing_directory()) {
    VLOGF(1, "Preparing new proxy for %s", dev->name().data());
    status = PrepareNewProxy(dev, nullptr);
    if (status != ZX_OK) {
      return status;
    }
    status = ::BindDriver(dev->new_proxy(), drv->libname.c_str());
  } else {
    VLOGF(1, "Preparing old proxy for %s", dev->name().data());
    status = PrepareProxy(dev, nullptr /* target_driver_host */);
    if (status != ZX_OK) {
      return status;
    }
    status = ::BindDriver(dev->proxy(), drv->libname.c_str());
  }
  // TODO(swetland): arrange to mark us unbound when the proxy (or its driver_host) goes away
  if ((status == ZX_OK) && !(dev->flags & DEV_CTX_MULTI_BIND)) {
    dev->flags |= DEV_CTX_BOUND;
  }
  return status;
}

void Coordinator::HandleNewDevice(const fbl::RefPtr<Device>& dev) {
  // If the device has a proxy, we actually want to wait for the proxy device to be
  // created and connect to that.
  if (!(dev->flags & DEV_CTX_MUST_ISOLATE)) {
    zx::channel client_remote = dev->take_client_remote();
    if (client_remote.is_valid()) {
      zx_status_t status =
          devfs_connect(dev.get(), fidl::ServerEnd<fio::Node>(std::move(client_remote)));
      if (status != ZX_OK) {
        LOGF(ERROR, "Failed to connect to service from proxy device '%s': %s", dev->name().data(),
             zx_status_get_string(status));
      }
    }
  }

  // TODO(tesienbe): We probably should do something with the return value
  // from this...
  BindDevice(dev, {} /* libdrvname */, true /* new device */);
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

  mexec_kernel_zbi_ = std::move(kernel_zbi);
  mexec_data_zbi_ = std::move(data_zbi);
  return ZX_OK;
}

void Coordinator::Suspend(uint32_t flags, SuspendCallback callback) {
  // TODO(ravoorir) : Change later to queue the suspend when resume is in progress.
  // Similarly, when Suspend is in progress, resume should be queued. When a resume is
  // in queue, and another suspend request comes in, we should nullify the resume that
  // is in queue.
  if (InResume()) {
    LOGF(ERROR, "Aborting system-suspend, a system resume is in progress");
    if (callback) {
      callback(ZX_ERR_UNAVAILABLE);
    }
    return;
  }

  suspend_handler_.Suspend(flags, std::move(callback));
}

void Coordinator::Resume(ResumeContext ctx, std::function<void(zx_status_t)> callback) {
  if (!sys_device_->proxy()) {
    return;
  }
  if (InSuspend()) {
    return;
  }

  auto schedule_resume = [this, callback](fbl::RefPtr<Device> dev) {
    auto completion = [this, dev, callback](zx_status_t status) {
      dev->clear_active_resume();

      auto& ctx = resume_context();
      if (status != ZX_OK) {
        LOGF(ERROR, "Failed to resume: %s", zx_status_get_string(status));
        ctx.set_flags(ResumeContext::Flags::kSuspended);
        auto task = ctx.take_pending_task(*dev);
        callback(status);
        return;
      }
      std::optional<fbl::RefPtr<ResumeTask>> task = ctx.take_pending_task(*dev);
      if (task.has_value()) {
        ctx.push_completed_task(std::move(task.value()));
      } else {
        // Something went wrong
        LOGF(ERROR, "Failed to resume, cannot find matching pending task");
        callback(ZX_ERR_INTERNAL);
        return;
      }
      if (ctx.pending_tasks_is_empty()) {
        async::PostTask(dispatcher_, [this, callback] {
          resume_context().reset_completed_tasks();
          callback(ZX_OK);
        });
      }
    };
    auto task = ResumeTask::Create(dev, static_cast<uint32_t>(resume_context().target_state()),
                                   std::move(completion));
    resume_context().push_pending_task(task);
    dev->SetActiveResume(std::move(task));
  };

  resume_context() = std::move(ctx);
  for (auto& dev : devices_) {
    schedule_resume(fbl::RefPtr(&dev));
    if (dev.proxy()) {
      schedule_resume(dev.proxy());
    }
  }
  schedule_resume(sys_device_);
  schedule_resume(sys_device_->proxy());

  // Post a delayed task in case drivers do not complete the resume.
  auto status = async::PostDelayedTask(
      dispatcher_,
      [this, callback] {
        if (!InResume()) {
          return;
        }
        LOGF(ERROR, "System resume timed out");
        callback(ZX_ERR_TIMED_OUT);
        // TODO(ravoorir): Figure out what is the best strategy
        // of for recovery here. Should we put back all devices
        // in suspend? In future, this could be more interactive
        // with the UI.
      },
      config_.resume_timeout);
  if (status != ZX_OK) {
    LOGF(ERROR, "Failure to create resume timeout watchdog");
  }
}

void Coordinator::Resume(SystemPowerState target_state, ResumeCallback callback) {
  Resume(ResumeContext(ResumeContext::Flags::kResume, target_state), std::move(callback));
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

zx_status_t Coordinator::MatchAndBindDriverToDevice(const fbl::RefPtr<Device>& dev,
                                                    const Driver* drv, bool autobind,
                                                    const AttemptBindFunc& attempt_bind) {
  auto driver = MatchedDriver{.driver = drv};
  zx_status_t status = MatchDeviceToDriver(dev, drv, autobind);
  if (status != ZX_OK) {
    return status;
  }
  return BindDriverToDevice(dev, driver, attempt_bind);
}

zx_status_t Coordinator::BindDriverToDevice(const fbl::RefPtr<Device>& dev,
                                            const MatchedDriver& driver,
                                            const AttemptBindFunc& attempt_bind) {
  if (driver.composite) {
    std::string name(driver.driver->libname.c_str());
    if (driver_index_composite_devices_.count(name) == 0) {
      std::unique_ptr<CompositeDevice> dev;
      zx_status_t status = CompositeDevice::CreateFromDriverIndex(driver, &dev);
      if (status != ZX_OK) {
        LOGF(ERROR, "%s: Failed to create CompositeDevice from DriverIndex: %s", __func__,
             zx_status_get_string(status));
        return status;
      }
      driver_index_composite_devices_[name] = std::move(dev);
    }
    auto& composite = driver_index_composite_devices_[name];
    zx_status_t status = composite->BindFragment(driver.composite->node, dev);
    if (status != ZX_OK) {
      LOGF(ERROR, "%s: Failed to BindFragment for '%s': %s", __func__, dev->name().data(),
           zx_status_get_string(status));
      return status;
    }
  } else {
    zx_status_t status = attempt_bind(driver.driver, dev);
    // If we get this here it means we've successfully bound one driver
    // and the device isn't multi-bind.
    if (status == ZX_ERR_ALREADY_BOUND) {
      return ZX_OK;
    }
    if (status != ZX_OK) {
      LOGF(ERROR, "%s: Failed to bind driver '%s' to device '%s': %s", __func__,
           driver.driver->libname.data(), dev->name().data(), zx_status_get_string(status));
    }
  }
  return ZX_OK;
}

// BindDriver is called when a new driver becomes available to
// the Coordinator.  Existing devices are inspected to see if the
// new driver is bindable to them (unless they are already bound).
zx_status_t Coordinator::BindDriver(Driver* drv, const AttemptBindFunc& attempt_bind) {
  zx_status_t status =
      MatchAndBindDriverToDevice(root_device_, drv, true /* autobind */, attempt_bind);
  if (status != ZX_ERR_NEXT) {
    return status;
  }
  if (!running_) {
    return ZX_ERR_UNAVAILABLE;
  }
  for (auto& dev : devices_) {
    zx_status_t status =
        MatchAndBindDriverToDevice(fbl::RefPtr(&dev), drv, true /* autobind */, attempt_bind);
    if (status == ZX_ERR_NEXT || status == ZX_ERR_ALREADY_BOUND) {
      continue;
    }
    if (status != ZX_OK) {
      return status;
    }
  }
  return ZX_OK;
}

zx_status_t Coordinator::MatchDeviceToDriver(const fbl::RefPtr<Device>& dev, const Driver* driver,
                                             bool autobind) {
  if (dev->IsAlreadyBound()) {
    return ZX_ERR_ALREADY_BOUND;
  }

  if (autobind && dev->should_skip_autobind()) {
    return ZX_ERR_NEXT;
  }

  if (!dev->is_bindable() && !(dev->is_composite_bindable())) {
    return ZX_ERR_NEXT;
  }
  if (!driver_is_bindable(driver, dev->protocol_id(), dev->props(), dev->str_props(), autobind)) {
    return ZX_ERR_NEXT;
  }
  return ZX_OK;
}

void Coordinator::BindAllDevicesDriverIndex(const DriverLoader::MatchDeviceConfig& config) {
  zx_status_t status = MatchAndBindDeviceDriverIndex(root_device_, config);
  if (status != ZX_OK && status != ZX_ERR_NEXT) {
    LOGF(ERROR, "DriverIndex failed to match root_device: %d", status);
    return;
  }

  for (auto& dev : devices_) {
    auto dev_ref = fbl::RefPtr(&dev);
    zx_status_t status = MatchAndBindDeviceDriverIndex(dev_ref, config);
    if (status == ZX_ERR_NEXT || status == ZX_ERR_ALREADY_BOUND) {
      continue;
    }
    if (status != ZX_OK) {
      return;
    }
  }
}

void Coordinator::ScheduleBaseDriverLoading() {
  driver_loader_.WaitForBaseDrivers([this]() {
    DriverLoader::MatchDeviceConfig config;
    config.only_return_base_and_fallback_drivers = true;
    BindAllDevicesDriverIndex(config);
  });
}

zx_status_t Coordinator::MatchAndBindDeviceDriverIndex(
    const fbl::RefPtr<Device>& dev, const DriverLoader::MatchDeviceConfig& config) {
  if (dev->IsAlreadyBound()) {
    return ZX_ERR_ALREADY_BOUND;
  }

  if (dev->should_skip_autobind()) {
    return ZX_ERR_NEXT;
  }

  if (!dev->is_bindable() && !(dev->is_composite_bindable())) {
    return ZX_ERR_NEXT;
  }

  auto drivers = driver_loader_.MatchDeviceDriverIndex(dev, config);
  for (auto driver : drivers) {
    zx_status_t status =
        BindDriverToDevice(dev, driver, fit::bind_member(this, &Coordinator::AttemptBind));
    // If we get this here it means we've successfully bound one driver
    // and the device isn' multi-bind.
    if (status == ZX_ERR_ALREADY_BOUND) {
      return ZX_OK;
    }
  }
  return ZX_OK;
}

zx::status<std::vector<MatchedDriver>> Coordinator::MatchDevice(const fbl::RefPtr<Device>& dev,
                                                                std::string_view drvlibname) {
  // shouldn't be possible to get a bind request for a proxy device
  if (dev->flags & DEV_CTX_PROXY) {
    return zx::error(ZX_ERR_NOT_SUPPORTED);
  }

  std::vector<MatchedDriver> matched_drivers;

  // A libname of "" means a general rebind request
  // instead of a specific request
  bool autobind = drvlibname.size() == 0;

  for (const Driver& driver : drivers_) {
    if (!autobind && drvlibname.compare(driver.libname)) {
      continue;
    }
    zx_status_t status = MatchDeviceToDriver(dev, &driver, autobind);
    if (status == ZX_ERR_ALREADY_BOUND) {
      return zx::error(ZX_ERR_ALREADY_BOUND);
    }
    if (status == ZX_ERR_NEXT) {
      continue;
    }

    if (status == ZX_OK) {
      auto matched = MatchedDriver{.driver = &driver};
      matched_drivers.push_back(std::move(matched));
    }

    // If the device doesn't support multibind (this is a devmgr-internal setting),
    // then return on first match or failure.
    // Otherwise, keep checking all the drivers.
    if (!(dev->flags & DEV_CTX_MULTI_BIND)) {
      if (status != ZX_OK) {
        return zx::error(status);
      }
      return zx::ok(std::move(matched_drivers));
    }
  }

  // Check the Driver Index for a driver.
  {
    DriverLoader::MatchDeviceConfig config;
    config.libname = drvlibname;
    auto drivers = driver_loader_.MatchDeviceDriverIndex(dev, config);
    for (auto driver : drivers) {
      if (dev->IsAlreadyBound()) {
        return zx::error(ZX_ERR_ALREADY_BOUND);
      }

      matched_drivers.push_back(driver);
    }
  }

  return zx::ok(std::move(matched_drivers));
}

zx_status_t Coordinator::BindDevice(const fbl::RefPtr<Device>& dev, std::string_view drvlibname,
                                    bool new_device) {
  // shouldn't be possible to get a bind request for a proxy device
  if (dev->flags & DEV_CTX_PROXY) {
    return ZX_ERR_NOT_SUPPORTED;
  }

  // A libname of "" means a general rebind request
  // instead of a specific request
  bool autobind = drvlibname.size() == 0;

  // Attempt composite device matching first.  This is unnecessary if a
  // specific driver has been requested.
  if (autobind) {
    zx_status_t status;
    for (auto& composite : composite_devices_) {
      size_t index;
      if (composite.TryMatchFragments(dev, &index)) {
        LOGF(INFO, "Device '%s' matched fragment %zu of composite '%s'", dev->name().data(), index,
             composite.name().data());
        status = composite.BindFragment(index, dev);
        if (status != ZX_OK) {
          LOGF(ERROR, "Device '%s' failed to bind fragment %zu of composite '%s': %s",
               dev->name().data(), index, composite.name().data(), zx_status_get_string(status));
          return status;
        }
      }
    }
  }

  // TODO: disallow if we're in the middle of enumeration, etc
  zx::status<std::vector<MatchedDriver>> result = MatchDevice(dev, drvlibname);
  if (!result.is_ok()) {
    return result.error_value();
  }

  auto drivers = std::move(result.value());
  for (auto& driver : drivers) {
    zx_status_t status =
        BindDriverToDevice(dev, driver, fit::bind_member(this, &Coordinator::AttemptBind));
    if (status != ZX_OK) {
      return status;
    }
  }

  // Notify observers that this device is available again
  // Needed for non-auto-binding drivers like GPT against block, etc
  if (!new_device && autobind) {
    devfs_advertise_modified(dev);
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

void Coordinator::BindDrivers() {
  AddAndBindDrivers(std::move(drivers_));

  DriverLoader::MatchDeviceConfig config;
  BindAllDevicesDriverIndex(config);
}

// TODO(fxbug.dev/42257): Temporary helper to convert state to flags.
// Will be removed eventually.
uint32_t Coordinator::GetSuspendFlagsFromSystemPowerState(
    statecontrol_fidl::wire::SystemPowerState state) {
  switch (state) {
    case statecontrol_fidl::wire::SystemPowerState::kFullyOn:
      return 0;
    case statecontrol_fidl::wire::SystemPowerState::kReboot:
      return statecontrol_fidl::wire::kSuspendFlagReboot;
    case statecontrol_fidl::wire::SystemPowerState::kRebootBootloader:
      return statecontrol_fidl::wire::kSuspendFlagRebootBootloader;
    case statecontrol_fidl::wire::SystemPowerState::kRebootRecovery:
      return statecontrol_fidl::wire::kSuspendFlagRebootRecovery;
    case statecontrol_fidl::wire::SystemPowerState::kPoweroff:
      return statecontrol_fidl::wire::kSuspendFlagPoweroff;
    case statecontrol_fidl::wire::SystemPowerState::kMexec:
      return statecontrol_fidl::wire::kSuspendFlagMexec;
    case statecontrol_fidl::wire::SystemPowerState::kSuspendRam:
      return statecontrol_fidl::wire::kSuspendFlagSuspendRam;
    default:
      return 0;
  }
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

    device_info.set_flags(allocator, device->flags);

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
    for (auto& device : devices()) {
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
  Suspend(request->flags, [completer = completer.ToAsync()](zx_status_t status) mutable {
    completer.Reply(status);
  });
}

void Coordinator::UnregisterSystemStorageForShutdown(
    UnregisterSystemStorageForShutdownRequestView request,
    UnregisterSystemStorageForShutdownCompleter::Sync& completer) {
  suspend_handler().UnregisterSystemStorageForShutdown(
      [completer = completer.ToAsync()](zx_status_t status) mutable { completer.Reply(status); });
}

void Coordinator::DumpTree(DumpTreeRequestView request, DumpTreeCompleter::Sync& completer) {
  VmoWriter writer{std::move(request->output)};
  DumpState(&writer);
  completer.Reply(writer.status(), writer.written(), writer.available());
}

static void DumpDriver(const Driver& drv, VmoWriter& writer) {
  writer.Printf("Name    : %s\n", drv.name.c_str());
  writer.Printf("Driver  : %s\n", !drv.libname.empty() ? drv.libname.c_str() : "(null)");
  writer.Printf("Flags   : %#08x\n", drv.flags);
  writer.Printf("Bytecode Version   : %u\n", drv.bytecode_version);

  if (!drv.binding_size) {
    return;
  }

  if (drv.bytecode_version == 1) {
    auto* binding = std::get_if<std::unique_ptr<zx_bind_inst_t[]>>(&drv.binding);
    if (!binding) {
      return;
    }

    char line[256];
    uint32_t count = drv.binding_size / static_cast<uint32_t>(sizeof(binding->get()[0]));
    writer.Printf("Binding : %u instruction%s (%u bytes)\n", count, (count == 1) ? "" : "s",
                  drv.binding_size);
    for (uint32_t i = 0; i < count; ++i) {
      di_dump_bind_inst(&binding->get()[i], line, sizeof(line));
      writer.Printf("[%u/%u]: %s\n", i + 1, count, line);
    }
  } else if (drv.bytecode_version == 2) {
    auto* binding = std::get_if<std::unique_ptr<uint8_t[]>>(&drv.binding);
    if (!binding) {
      return;
    }

    writer.Printf("Bytecode (%u byte%s): \n", drv.binding_size, (drv.binding_size == 1) ? "" : "s");
    for (uint32_t i = 0; i < drv.binding_size; ++i) {
      writer.Printf("0x%02x", (binding->get()[i]));
    }
    writer.Printf("\n\n");
  }
}

void Coordinator::DumpDrivers(DumpDriversRequestView request,
                              DumpDriversCompleter::Sync& completer) {
  VmoWriter writer{std::move(request->output)};
  for (const auto& drv : drivers_) {
    DumpDriver(drv, writer);
  }

  auto drivers = driver_loader_.GetAllDriverIndexDrivers();
  for (const auto& drv : drivers) {
    DumpDriver(*drv, writer);
  }

  completer.Reply(writer.status(), writer.written(), writer.available());
}

void Coordinator::DumpBindingProperties(DumpBindingPropertiesRequestView request,
                                        DumpBindingPropertiesCompleter::Sync& completer) {
  VmoWriter writer{std::move(request->output)};
  DumpDeviceProps(&writer, root_device_.get());
  DumpDeviceProps(&writer, sys_device_.get());
  completer.Reply(writer.status(), writer.written(), writer.available());
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

  const auto driver_dev = [this](fidl::ServerEnd<fdd::DriverDevelopment> request) {
    fidl::BindServer<fidl::WireServer<fdd::DriverDevelopment>>(
        dispatcher_, std::move(request), this,
        [](fidl::WireServer<fdd::DriverDevelopment>* self, fidl::UnbindInfo info,
           fidl::ServerEnd<fdd::DriverDevelopment> server_end) {
          if (info.ok()) {
            return;
          }
          if (info.reason() == fidl::Reason::kPeerClosed) {
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
  status = svc_dir->AddEntry(fidl::DiscoverableProtocolName<fdd::DriverDevelopment>,
                             fbl::MakeRefCounted<fs::Service>(driver_dev));
  if (status != ZX_OK) {
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
    fidl::BindServer<fidl::WireServer<fdm::DebugDumper>>(dispatcher_, std::move(request), this);
    return ZX_OK;
  };
  return svc_dir->AddEntry(fidl::DiscoverableProtocolName<fdm::DebugDumper>,
                           fbl::MakeRefCounted<fs::Service>(debug));
}

void Coordinator::OnOOMEvent(async_dispatcher_t* dispatcher, async::WaitBase* wait,
                             zx_status_t status, const zx_packet_signal_t* signal) {
  suspend_handler_.ShutdownFilesystems([](zx_status_t status) {});
}

std::string Coordinator::GetFragmentDriverUrl() const { return "#driver/fragment.so"; }

void Coordinator::RestartDriverHosts(RestartDriverHostsRequestView request,
                                     RestartDriverHostsCompleter::Sync& completer) {
  std::string_view driver_path(request->driver_path.data(), request->driver_path.size());

  // Find devices containing the driver.
  for (auto& dev : devices_) {
    // Call remove on the device's driver host if it contains the driver.
    if (dev.libname().compare(driver_path) == 0) {
      LOGF(INFO, "Device %s found in restart driver hosts.", dev.name().data());
      LOGF(INFO, "Shutting down host: %ld.", dev.host()->koid());

      // Unbind and Remove all the devices in the Driver Host.
      ScheduleUnbindRemoveAllDevices(dev.host());
    }
  }

  completer.ReplySuccess();
}

void Coordinator::ScheduleUnbindRemoveAllDevices(const fbl::RefPtr<DriverHost> driver_host) {
  for (auto& dev : driver_host->devices()) {
    // This will also call on all the children of the device.
    dev.CreateUnbindRemoveTasks(
        UnbindTaskOpts{.do_unbind = true, .post_on_create = true, .driver_host_requested = false});
  }
}
