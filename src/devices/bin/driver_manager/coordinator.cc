// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "coordinator.h"

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <fuchsia/boot/c/fidl.h>
#include <fuchsia/io/c/fidl.h>
#include <fuchsia/power/manager/llcpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/async/cpp/receiver.h>
#include <lib/async/cpp/task.h>
#include <lib/async/cpp/wait.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/io.h>
#include <lib/fidl-async/bind.h>
#include <lib/fidl-async/cpp/bind.h>
#include <lib/fidl/coding.h>
#include <lib/fit/defer.h>
#include <lib/fzl/owned-vmo-mapper.h>
#include <lib/zircon-internal/ktrace.h>
#include <lib/zx/clock.h>
#include <lib/zx/job.h>
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
#include <utility>

#include <ddk/driver.h>
#include <driver-info/driver-info.h>
#include <fbl/auto_call.h>
#include <fbl/string_printf.h>
#include <inspector/inspector.h>

#include "composite_device.h"
#include "devfs.h"
#include "driver_host_loader_service.h"
#include "env.h"
#include "fidl.h"
#include "fidl_txn.h"
#include "fuchsia/hardware/power/statecontrol/llcpp/fidl.h"
#include "lib/zx/time.h"
#include "src/devices/lib/log/log.h"
#include "vmo_writer.h"

namespace {

constexpr char kDriverHostPath[] = "bin/driver_host";
constexpr char kBootFirmwarePath[] = "lib/firmware";
constexpr char kSystemFirmwarePath[] = "/system/lib/firmware";
constexpr char kItemsPath[] = "/svc/" fuchsia_boot_Items_Name;
constexpr char kFshostAdminPath[] = "/svc/fuchsia.fshost.Admin";
constexpr zx::duration kPowerManagerConnectionTimeout = zx::sec(40);

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

std::unique_ptr<llcpp::fuchsia::fshost::Admin::SyncClient> ConnectToFshostAdminServer() {
  zx::channel local, remote;
  zx_status_t status = zx::channel::create(0, &local, &remote);
  if (status != ZX_OK) {
    return std::make_unique<llcpp::fuchsia::fshost::Admin::SyncClient>(zx::channel());
  }
  status = fdio_service_connect(kFshostAdminPath, remote.release());
  if (status != ZX_OK) {
    LOGF(ERROR, "Failed to connect to fuchsia.fshost.Admin: %s", zx_status_get_string(status));
    return std::make_unique<llcpp::fuchsia::fshost::Admin::SyncClient>(zx::channel());
  }
  return std::make_unique<llcpp::fuchsia::fshost::Admin::SyncClient>(std::move(local));
}

void suspend_fallback(const zx::resource& root_resource, uint32_t flags) {
  LOGF(INFO, "Suspend fallback with flags %#08x", flags);
  if (flags == DEVICE_SUSPEND_FLAG_REBOOT) {
    zx_system_powerctl(root_resource.get(), ZX_SYSTEM_POWERCTL_REBOOT, nullptr);
  } else if (flags == DEVICE_SUSPEND_FLAG_REBOOT_BOOTLOADER) {
    zx_system_powerctl(root_resource.get(), ZX_SYSTEM_POWERCTL_REBOOT_BOOTLOADER, nullptr);
  } else if (flags == DEVICE_SUSPEND_FLAG_REBOOT_RECOVERY) {
    zx_system_powerctl(root_resource.get(), ZX_SYSTEM_POWERCTL_REBOOT_RECOVERY, nullptr);
  } else if (flags == DEVICE_SUSPEND_FLAG_POWEROFF) {
    zx_system_powerctl(root_resource.get(), ZX_SYSTEM_POWERCTL_SHUTDOWN, nullptr);
  }
}

}  // namespace

namespace power_fidl = llcpp::fuchsia::hardware::power;
namespace power_manager_fidl = llcpp::fuchsia::power::manager;

Coordinator::Coordinator(CoordinatorConfig config, async_dispatcher_t* dispatcher)
    : config_(std::move(config)),
      dispatcher_(dispatcher),
      inspect_manager_(dispatcher),
      reboot_watcher_manager_(dispatcher) {
  if (config_.oom_event) {
    wait_on_oom_event_.set_object(config_.oom_event.get());
    wait_on_oom_event_.set_trigger(ZX_EVENT_SIGNALED);
    wait_on_oom_event_.Begin(dispatcher);
  }
  shutdown_system_state_ = config_.default_shutdown_system_state;
}

Coordinator::~Coordinator() {}

bool Coordinator::InSuspend() const {
  return suspend_context().flags() == SuspendContext::Flags::kSuspend;
}

bool Coordinator::InResume() const {
  return (resume_context().flags() == ResumeContext::Flags::kResume);
}

void Coordinator::ShutdownFilesystems() {
  // TODO(dgonyeo): we should connect to this service eagerly when Coordinator
  // is created, since we want to do as little work here as possible
  if (fshost_admin_client_.get() == nullptr) {
    fshost_admin_client_ = ConnectToFshostAdminServer();
  }
  auto result = fshost_admin_client_->Shutdown();
  if (result.status() != ZX_OK) {
    LOGF(ERROR, "Failed to cause VFS exit: %s", zx_status_get_string(result.status()));
    return;
  }

  LOGF(INFO, "Successfully waited for VFS exit completion");
}

zx_status_t Coordinator::RegisterWithPowerManager(zx::channel power_manager_client,
                                                  zx::channel system_state_transition_client,
                                                  zx::channel devfs_handle) {
  using RegisterRequest = power_manager_fidl::DriverManagerRegistration::RegisterRequest;
  using RegisterResponse = power_manager_fidl::DriverManagerRegistration::RegisterResponse;

  // This request is manually sent to allow timeout for the fidl::Call, until fxb/53240
  // is resolved.
  RegisterRequest request(0, system_state_transition_client, devfs_handle);
  fidl::BytePart request_bytes(reinterpret_cast<uint8_t*>(&request), sizeof(request),
                               sizeof(request));
  fidl::DecodedMessage<RegisterRequest> msg(std::move(request_bytes));

  fidl::EncodeResult<RegisterRequest> encode_result = fidl::Encode(std::move(msg));
  if (encode_result.status != ZX_OK) {
    LOGF(ERROR, "Failed to register with power_manager.Encoding failed:%d Encode error:%s",
         encode_result.status, encode_result.error);
    return encode_result.status;
  }

  fidl::Buffer<RegisterResponse> response_buffer;
  auto result = fidl::Call<RegisterRequest, RegisterResponse>(
      power_manager_client, std::move(encode_result.message), response_buffer.view(),
      zx::deadline_after(kPowerManagerConnectionTimeout));
  if (result.status != ZX_OK) {
    LOGF(ERROR, "Failed to register with power_manager.Call failed:%s",
         zx_status_get_string(result.status));
    return result.status;
  }

  auto decode_result = fidl::Decode(std::move(result.message));
  if (decode_result.status != ZX_OK) {
    LOGF(ERROR, "Failed to register with power_manager.Decode failed:%d\n", decode_result.status);
    return decode_result.status;
  }

  if (decode_result.message.message()->result.is_err()) {
    power_manager_fidl::RegistrationError err = decode_result.message.message()->result.err();
    if (err == power_manager_fidl::RegistrationError::INVALID_HANDLE) {
      LOGF(ERROR, "Failed to register with power_manager.Invalid handle.\n");
      return ZX_ERR_INVALID_ARGS;
    }
    LOGF(ERROR, "Failed to register with power_manager\n");
    return ZX_ERR_INTERNAL;
  }

  LOGF(INFO, "Registered with power manager successfully");
  return ZX_OK;
}

zx_status_t Coordinator::InitCoreDevices(std::string_view sys_device_driver) {
  root_device_ = fbl::MakeRefCounted<Device>(this, "root", fbl::String(), "root,", nullptr,
                                             ZX_PROTOCOL_ROOT, zx::vmo(), zx::channel());
  root_device_->flags = DEV_CTX_IMMORTAL | DEV_CTX_MUST_ISOLATE | DEV_CTX_MULTI_BIND;

  misc_device_ = fbl::MakeRefCounted<Device>(this, "misc", fbl::String(), "misc,", root_device_,
                                             ZX_PROTOCOL_MISC_PARENT, zx::vmo(), zx::channel());
  misc_device_->flags = DEV_CTX_IMMORTAL | DEV_CTX_MUST_ISOLATE | DEV_CTX_MULTI_BIND;

  sys_device_ = fbl::MakeRefCounted<Device>(this, "sys", sys_device_driver, "sys,", root_device_, 0,
                                            zx::vmo(), zx::channel());
  sys_device_->flags = DEV_CTX_IMMORTAL | DEV_CTX_MUST_ISOLATE;

  test_device_ = fbl::MakeRefCounted<Device>(this, "test", fbl::String(), "test,", root_device_,
                                             ZX_PROTOCOL_TEST_PARENT, zx::vmo(), zx::channel());
  test_device_->flags = DEV_CTX_IMMORTAL | DEV_CTX_MUST_ISOLATE | DEV_CTX_MULTI_BIND;
  return ZX_OK;
}

const Driver* Coordinator::LibnameToDriver(const fbl::StringPiece& libname) const {
  for (const auto& drv : drivers_) {
    if (libname.compare(drv.libname) == 0) {
      return &drv;
    }
  }
  return nullptr;
}

static zx_status_t load_vmo(const fbl::String& libname, zx::vmo* out_vmo) {
  int fd = -1;
  zx_status_t r = fdio_open_fd(
      libname.data(), fuchsia_io_OPEN_RIGHT_READABLE | fuchsia_io_OPEN_RIGHT_EXECUTABLE, &fd);
  if (r != ZX_OK) {
    LOGF(ERROR, "Cannot open driver '%s'", libname.data());
    return ZX_ERR_IO;
  }
  zx::vmo vmo;
  r = fdio_get_vmo_exec(fd, vmo.reset_and_get_address());
  close(fd);
  if (r != ZX_OK) {
    LOGF(ERROR, "Cannot get driver VMO '%s'", libname.data());
    return r;
  }
  const char* vmo_name = strrchr(libname.data(), '/');
  if (vmo_name != nullptr) {
    ++vmo_name;
  } else {
    vmo_name = libname.data();
  }
  r = vmo.set_property(ZX_PROP_NAME, vmo_name, strlen(vmo_name));
  if (r != ZX_OK) {
    LOGF(ERROR, "Cannot set name on driver VMO to '%s'", libname.data());
    return r;
  }
  *out_vmo = std::move(vmo);
  return r;
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
  DumpDevice(vmo, misc_device_.get(), 1);
  DumpDevice(vmo, sys_device_.get(), 1);
  DumpDevice(vmo, test_device_.get(), 1);
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
    vmo->Printf("\n");
  }

  if (dev->proxy()) {
    DumpDeviceProps(vmo, dev->proxy().get());
  }
  for (const auto& child : dev->children()) {
    DumpDeviceProps(vmo, &child);
  }
}

void Coordinator::DumpGlobalDeviceProps(VmoWriter* vmo) const {
  DumpDeviceProps(vmo, root_device_.get());
  DumpDeviceProps(vmo, misc_device_.get());
  DumpDeviceProps(vmo, sys_device_.get());
  DumpDeviceProps(vmo, test_device_.get());
}

void Coordinator::DumpDrivers(VmoWriter* vmo) const {
  bool first = true;
  for (const auto& drv : drivers_) {
    vmo->Printf("%sName    : %s\n", first ? "" : "\n", drv.name.c_str());
    vmo->Printf("Driver  : %s\n", !drv.libname.empty() ? drv.libname.c_str() : "(null)");
    vmo->Printf("Flags   : %#08x\n", drv.flags);
    if (drv.binding_size) {
      char line[256];
      uint32_t count = drv.binding_size / static_cast<uint32_t>(sizeof(drv.binding[0]));
      vmo->Printf("Binding : %u instruction%s (%u bytes)\n", count, (count == 1) ? "" : "s",
                  drv.binding_size);
      for (uint32_t i = 0; i < count; ++i) {
        di_dump_bind_inst(&drv.binding[i], line, sizeof(line));
        vmo->Printf("[%u/%u]: %s\n", i + 1, count, line);
      }
    }
    first = false;
  }
}

zx_handle_t get_service_root();

zx_status_t Coordinator::GetTopologicalPath(const fbl::RefPtr<const Device>& dev, char* out,
                                            size_t max) const {
  // TODO: Remove VLA.
  char tmp[max];
  char name_buf[fuchsia_io_MAX_FILENAME + strlen("dev/")];
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
      strncpy(name_buf + strlen("dev/"), itr->name().data(), fuchsia_io_MAX_FILENAME);
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
  std::string program = config_.path_prefix + kDriverHostPath;
  std::vector<const char*> env;
  if (config_.asan_drivers) {
    // If there are any ASan drivers, use the ASan-supporting driver_host for
    // all drivers because even a driver_host launched initially with just a
    // non-ASan driver might later load an ASan driver.  One day we might be
    // able to be more flexible about which drivers must get loaded into the
    // same driver_host and thus be able to use both ASan and non-ASan driver_hosts
    // at the same time when only a subset of drivers use ASan.
    //
    // TODO(44814): The build logic to install the asan-ready driver_host
    // under the alternate name is currently broken.  So things only work
    // if the build chose an asan-ready variant for the "main" driver_host.
    // When this is restored in the build, this should select the right name.
    // program = kDriverHostAsanPath;
    env.push_back(kAsanEnvironment);
  }

  auto driver_host_env = boot_args()->Collect("driver.");
  if (!driver_host_env.ok()) {
    return driver_host_env.status();
  }

  std::vector<std::string> strings;
  for (auto& entry : driver_host_env->results) {
    strings.emplace_back(entry.data(), entry.size());
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

  env.push_back(nullptr);

  fbl::RefPtr<DriverHost> dh;
  zx_status_t status = DriverHost::Launch(
      this, loader_service_connector_, program.c_str(), name, env.data(), root_resource(),
      zx::unowned_job(config_.driver_host_job), config_.fs_provider, &dh);
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
    const fbl::RefPtr<Device>& parent, zx::channel device_controller, zx::channel coordinator,
    const llcpp::fuchsia::device::manager::DeviceProperty* props_data, size_t props_count,
    fbl::StringPiece name, uint32_t protocol_id, fbl::StringPiece driver_path,
    fbl::StringPiece args, bool invisible, bool has_init, bool always_init, zx::vmo inspect,
    zx::channel client_remote, fbl::RefPtr<Device>* new_device) {
  // If this is true, then |name_data|'s size is properly bounded.
  static_assert(fuchsia_device_manager_DEVICE_NAME_MAX == ZX_DEVICE_NAME_MAX);
  static_assert(fuchsia_device_manager_PROPERTIES_MAX <= UINT32_MAX);

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

  fbl::AllocChecker ac;
  fbl::String name_str(name, &ac);
  if (!ac.check()) {
    return ZX_ERR_NO_MEMORY;
  }
  fbl::String driver_path_str(driver_path, &ac);
  if (!ac.check()) {
    return ZX_ERR_NO_MEMORY;
  }
  fbl::String args_str(args, &ac);
  if (!ac.check()) {
    return ZX_ERR_NO_MEMORY;
  }

  // TODO(fxb/43370): remove this check once init tasks can be enabled for all devices.
  bool want_init_task = has_init || always_init;
  // We use the legacy invisible / device_make_visible behavior if the device is added
  // as invisible and the device has not implemented the init hook.
  // TODO(fxb/43261): remove |has_init| once device_make_visible() is deprecated.
  bool init_wait_make_visible = invisible && !has_init;
  fbl::RefPtr<Device> dev;
  zx_status_t status = Device::Create(
      this, parent, std::move(name_str), std::move(driver_path_str), std::move(args_str),
      protocol_id, std::move(props), std::move(coordinator), std::move(device_controller),
      init_wait_make_visible, want_init_task, std::move(inspect), std::move(client_remote), &dev);
  if (status != ZX_OK) {
    return status;
  }
  devices_.push_back(dev);

  // Note that |dev->parent()| may not match |parent| here, so we should always
  // use |dev->parent()|.  This case can happen if |parent| refers to a device
  // proxy.

  // If we're creating a device that's using the fragment driver, inform the
  // fragment.
  if (fragment_driver_ != nullptr && dev->libname() == fragment_driver_->libname) {
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
  // TODO(fxb/43370): remove this once init tasks can be enabled for all devices.
  if (!invisible && !want_init_task) {
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
  // We will make the device visible once the init hook completes.
  if (dev->state() == Device::State::kInitializing) {
    dev->clear_wait_make_visible();
    return ZX_ERR_SHOULD_WAIT;
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
  if (fragment_driver_ != nullptr && dev->libname() == fragment_driver_->libname) {
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
        // IF we are the last child of our parent
        // AND our parent is not itself dead
        // AND our parent is a BUSDEV
        // AND our parent's driver_host is not dying
        // THEN we will want to rebind our parent
        if ((parent->state() != Device::State::kDead) && (parent->flags & DEV_CTX_MUST_ISOLATE) &&
            ((parent->host() == nullptr) ||
             !(parent->host()->flags() & DriverHost::Flags::kDying))) {
          VLOGF(1, "Bus device %p '%s' is unbound", parent.get(), parent->name().data());

          if (parent->retries > 0) {
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

zx_status_t Coordinator::AddCompositeDevice(
    const fbl::RefPtr<Device>& dev, fbl::StringPiece name,
    llcpp::fuchsia::device::manager::CompositeDeviceDescriptor comp_desc) {
  // Only the platform bus driver should be able to use this.  It is the
  // descendant of the sys device node.
  if (dev->parent() != sys_device_) {
    return ZX_ERR_ACCESS_DENIED;
  }

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

zx_status_t Coordinator::LoadFirmware(const fbl::RefPtr<Device>& dev, const char* path,
                                      zx::vmo* vmo, size_t* size) {
  const std::string fwdirs[] = {
      config_.path_prefix + kBootFirmwarePath,
      kSystemFirmwarePath,
  };

  // Must be a relative path and no funny business.
  if (path[0] == '/' || path[0] == '.') {
    return ZX_ERR_INVALID_ARGS;
  }

  int fd, fwfd;
  for (unsigned n = 0; n < std::size(fwdirs); n++) {
    if ((fd = open(fwdirs[n].c_str(), O_RDONLY, O_DIRECTORY)) < 0) {
      continue;
    }
    fwfd = openat(fd, path, O_RDONLY);
    close(fd);
    if (fwfd >= 0) {
      *size = lseek(fwfd, 0, SEEK_END);
      zx_status_t r = fdio_get_vmo_clone(fwfd, vmo->reset_and_get_address());
      close(fwfd);
      return r;
    }
    if (errno != ENOENT) {
      return ZX_ERR_IO;
    }
  }
  return ZX_ERR_NOT_FOUND;
}

// Returns true if the parent path is equal to or specifies a child device of the parent.
static bool path_is_child(const char* parent_path, const char* child_path) {
  size_t parent_length = strlen(parent_path);
  return (!strncmp(parent_path, child_path, parent_length) &&
          (child_path[parent_length] == 0 || child_path[parent_length] == '/'));
}

zx_status_t Coordinator::GetMetadataRecurse(const fbl::RefPtr<Device>& dev, uint32_t type,
                                            void* buffer, size_t buflen, size_t* size) {
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
        if (GetMetadataRecurse(dev, type, buffer, buflen, size) == ZX_OK) {
          return ZX_OK;
        }
      }
    }
  }

  return ZX_ERR_NOT_FOUND;
}

// Traverse up the device tree to find the metadata with the matching |type|.
// If not found, check the published metadata list for metadata with matching
// topological path.
// |buffer| can be nullptr, in which case only the size of the metadata is
// returned. This is used by GetMetadataSize method.
zx_status_t Coordinator::GetMetadata(const fbl::RefPtr<Device>& dev, uint32_t type, void* buffer,
                                     size_t buflen, size_t* size) {
  ZX_ASSERT(size != nullptr);
  auto status = GetMetadataRecurse(dev, type, buffer, buflen, size);
  if (status == ZX_OK) {
    return ZX_OK;
  }

  // if no metadata is found, check list of metadata added via device_publish_metadata()
  char path[fuchsia_device_manager_DEVICE_PATH_MAX];
  status = GetTopologicalPath(dev, path, sizeof(path));
  if (status != ZX_OK) {
    return status;
  }

  for (const auto& md : published_metadata_) {
    const char* md_path = md.Data() + md.length;
    if (md.type == type && path_is_child(md_path, path)) {
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

zx_status_t Coordinator::PublishMetadata(const fbl::RefPtr<Device>& dev, const char* path,
                                         uint32_t type, const void* data, uint32_t length) {
  char caller_path[fuchsia_device_manager_DEVICE_PATH_MAX];
  zx_status_t status = GetTopologicalPath(dev, caller_path, sizeof(caller_path));
  if (status != ZX_OK) {
    return status;
  }

  // Check to see if the specified path is a child of the caller's path
  if (path_is_child(caller_path, path)) {
    // Caller is adding a path that matches itself or one of its children, which is allowed.
  } else {
    fbl::RefPtr<Device> itr = dev;
    // Adding metadata to arbitrary paths is restricted to drivers running in the sys driver_host.
    while (itr && itr != sys_device_) {
      if (itr->proxy()) {
        // this device is in a child driver_host
        return ZX_ERR_ACCESS_DENIED;
      }
      itr = itr->parent();
    }
    if (!itr) {
      return ZX_ERR_ACCESS_DENIED;
    }
  }

  std::unique_ptr<Metadata> md;
  status = Metadata::Create(length + strlen(path) + 1, &md);
  if (status != ZX_OK) {
    return status;
  }

  md->type = type;
  md->length = length;
  md->has_path = true;
  memcpy(md->Data(), data, length);
  strcpy(md->Data() + length, path);
  published_metadata_.push_front(std::move(md));
  return ZX_OK;
}

// send message to driver_host, requesting the creation of a device
static zx_status_t dh_create_device(const fbl::RefPtr<Device>& dev,
                                    const fbl::RefPtr<DriverHost>& dh, const char* args,
                                    zx::handle rpc_proxy) {
  zx_status_t r;

  zx::channel hcoordinator, hcoordinator_remote;
  if ((r = zx::channel::create(0, &hcoordinator, &hcoordinator_remote)) != ZX_OK) {
    return r;
  }

  auto hdevice_controller_remote = dev->ConnectDeviceController(dev->coordinator->dispatcher());

  if (dev->libname().size() != 0) {
    zx::vmo vmo;
    if ((r = dev->coordinator->LibnameToVmo(dev->libname(), &vmo)) != ZX_OK) {
      return r;
    }

    r = dh_send_create_device(dev.get(), dh, std::move(hcoordinator_remote),
                              hdevice_controller_remote.TakeChannel(), std::move(vmo), args,
                              std::move(rpc_proxy));
    if (r != ZX_OK) {
      return r;
    }
  } else {
    r = dh_send_create_device_stub(dev.get(), dh, std::move(hcoordinator_remote),
                                   hdevice_controller_remote.TakeChannel(), dev->protocol_id());
    if (r != ZX_OK) {
      return r;
    }
  }

  dev->set_channel(std::move(hcoordinator));
  if ((r = Device::BeginWait(dev, dev->coordinator->dispatcher())) != ZX_OK) {
    return r;
  }
  return ZX_OK;
}

// send message to driver_host, requesting the binding of a driver to a device
static zx_status_t dh_bind_driver(const fbl::RefPtr<Device>& dev, const char* libname) {
  zx::vmo vmo;
  zx_status_t status = dev->coordinator->LibnameToVmo(libname, &vmo);
  if (status != ZX_OK) {
    return status;
  }
  status = dh_send_bind_driver(
      dev.get(), libname, std::move(vmo), [dev](zx_status_t status, zx::channel test_output) {
        if (status != ZX_OK) {
          LOGF(ERROR, "Failed to bind driver '%s': %s", dev->name().data(),
               zx_status_get_string(status));
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

          auto compat_test_enabled =
              dev->coordinator->boot_args()->GetBool(fidl::unowned_str(bootarg), false);
          if (compat_test_enabled.ok() && compat_test_enabled->value &&
              (real_parent->test_state() == Device::TestStateMachine::kTestNotStarted)) {
            bootarg = fbl::StringPrintf("driver.%s.compatibility-tests-wait-time", drivername);
            auto test_wait_time =
                dev->coordinator->boot_args()->GetString(fidl::unowned_str(bootarg));
            zx::duration test_time = kDefaultTestTimeout;
            if (test_wait_time.ok() && !test_wait_time->value.is_null()) {
              auto test_timeout =
                  std::string{test_wait_time->value.data(), test_wait_time->value.size()};
              test_time = zx::msec(atoi(test_timeout.data()));
            }
            real_parent->set_test_time(test_time);
            real_parent->DriverCompatibiltyTest();
            break;
          } else if (real_parent->test_state() == Device::TestStateMachine::kTestBindSent) {
            real_parent->test_event().signal(0, TEST_BIND_DONE_SIGNAL);
            break;
          }
        }
        if (test_output.is_valid()) {
          LOGF(INFO, "Setting test channel for driver '%s'", dev->name().data());
          status = dev->set_test_output(std::move(test_output), dev->coordinator->dispatcher());
          if (status != ZX_OK) {
            LOGF(ERROR, "Failed to wait on test output for driver '%s': %s", dev->name().data(),
                 zx_status_get_string(status));
          }
        }
      });
  if (status != ZX_OK) {
    return status;
  }
  dev->flags |= DEV_CTX_BOUND;
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
    LOGF(ERROR, "Missing proxy arguments, expected '%s,args' (see fxb/33674)", arg0);
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
    if ((r = dh_create_device(dev->proxy(), dev->proxy()->host(), arg1, std::move(h1))) < 0) {
      LOGF(ERROR, "Failed to create proxy device '%s' in driver_host '%s': %s", dev->name().data(),
           driver_hostname, zx_status_get_string(r));
      return r;
    }
    if (need_proxy_rpc) {
      if ((r = dh_send_connect_proxy(dev.get(), std::move(h0))) < 0) {
        LOGF(ERROR, "Failed to connect to proxy device '%s' in driver_host '%s': %s",
             dev->name().data(), driver_hostname, zx_status_get_string(r));
      }
    }
    if (dev == sys_device_) {
      if ((r = fdio_service_connect(kItemsPath, h0.release())) != ZX_OK) {
        LOGF(ERROR, "Failed to connect to %s: %s", kItemsPath, zx_status_get_string(r));
      }
    }
    zx::channel client_remote = dev->take_client_remote();
    if (client_remote.is_valid()) {
      if ((r = devfs_connect(dev->proxy().get(), std::move(client_remote))) != ZX_OK) {
        LOGF(ERROR, "Failed to connect to service from proxy device '%s' in driver_host '%s': %s",
             dev->name().data(), driver_hostname, zx_status_get_string(r));
      }
    }
  }

  return ZX_OK;
}

zx_status_t Coordinator::AttemptBind(const Driver* drv, const fbl::RefPtr<Device>& dev) {
  // cannot bind driver to already bound device
  if ((dev->flags & DEV_CTX_BOUND) &&
      !(dev->flags & (DEV_CTX_MULTI_BIND | DEV_CTX_ALLOW_MULTI_COMPOSITE))) {
    return ZX_ERR_BAD_STATE;
  }
  if (!(dev->flags & DEV_CTX_MUST_ISOLATE)) {
    // non-busdev is pretty simple
    if (dev->host() == nullptr) {
      LOGF(ERROR, "Cannot bind to device '%s', it has no driver_host", dev->name().data());
      return ZX_ERR_BAD_STATE;
    }
    return dh_bind_driver(dev, drv->libname.c_str());
  }

  zx_status_t r;
  if ((r = PrepareProxy(dev, nullptr /* target_driver_host */)) < 0) {
    return r;
  }

  r = dh_bind_driver(dev->proxy(), drv->libname.c_str());
  // TODO(swetland): arrange to mark us unbound when the proxy (or its driver_host) goes away
  if ((r == ZX_OK) && !(dev->flags & DEV_CTX_MULTI_BIND)) {
    dev->flags |= DEV_CTX_BOUND;
  }
  return r;
}

void Coordinator::HandleNewDevice(const fbl::RefPtr<Device>& dev) {
  // If the device has a proxy, we actually want to wait for the proxy device to be
  // created and connect to that.
  if (!(dev->flags & DEV_CTX_MUST_ISOLATE)) {
    zx::channel client_remote = dev->take_client_remote();
    if (client_remote.is_valid()) {
      zx_status_t status = devfs_connect(dev.get(), std::move(client_remote));
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

static void dump_suspend_task_dependencies(const SuspendTask* task, int depth = 0) {
  if (!task) {
    return;
  }
  const char* task_status = "";
  if (task->is_completed()) {
    task_status = zx_status_get_string(task->status());
  } else {
    bool dependence = false;
    for (const auto* dependency : task->Dependencies()) {
      if (!dependency->is_completed()) {
        dependence = true;
        break;
      }
    }
    task_status = dependence ? "<dependence>" : "Stuck <suspending>";
    if (!dependence) {
      zx_koid_t pid = task->device().host()->koid();
      if (!pid) {
        return;
      }
      zx::unowned_process process = task->device().host()->proc();
      char process_name[ZX_MAX_NAME_LEN];
      zx_status_t status = process->get_property(ZX_PROP_NAME, process_name, sizeof(process_name));
      if (status != ZX_OK) {
        strlcpy(process_name, "unknown", sizeof(process_name));
      }
      printf("Backtrace of threads of process %lu:%s\n", pid, process_name);
      inspector_print_debug_info_for_all_threads(stdout, process->get());
      fflush(stdout);
    }
  }
  LOGF(INFO, "%*cSuspend %s: %s", 2 * depth, ' ', task->device().name().data(), task_status);
  for (const auto* dependency : task->Dependencies()) {
    dump_suspend_task_dependencies(reinterpret_cast<const SuspendTask*>(dependency), depth + 1);
  }
}

void Coordinator::Suspend(SuspendContext ctx, fit::function<void(zx_status_t)> callback) {
  // TODO(ravoorir) : Change later to queue the suspend when resume is in progress.
  // Similarly, when Suspend is in progress, resume should be queued. When a resume is
  // in queue, and another suspend request comes in, we should nullify the resume that
  // is in queue.
  if (InResume()) {
    LOGF(ERROR, "Aborting system-suspend, a system resume is in progresss");
    if (callback) {
      callback(ZX_ERR_UNAVAILABLE);
    }
    return;
  }

  // A suspend is already in progress.
  if (InSuspend()) {
    LOGF(ERROR, "Aborting system-suspend, a system suspend is already in progress");
    if (callback) {
      callback(ZX_ERR_ALREADY_EXISTS);
    }
    return;
  }

  // The sys device should have a proxy. If not, the system hasn't fully initialized yet and
  // cannot go to suspend.
  if (!sys_device_->proxy()) {
    LOGF(ERROR, "Aborting system-suspend, system is not fully initialized yet");
    if (callback) {
      callback(ZX_ERR_UNAVAILABLE);
    }
    return;
  }

  suspend_context() = std::move(ctx);
  auto perform_suspend = [this, callback = std::move(callback)]() mutable {
    if ((suspend_context().sflags() & DEVICE_SUSPEND_REASON_MASK) !=
        DEVICE_SUSPEND_FLAG_SUSPEND_RAM) {
      log_to_debuglog();
      LOGF(INFO, "Shutting down filesystems to prepare for system-suspend");
      ShutdownFilesystems();
    }
    LOGF(INFO, "Filesystem shutdown complete, creating a suspend timeout-watchdog");

    auto callback_info = fbl::MakeRefCounted<SuspendCallbackInfo>(std::move(callback));

    auto status = async::PostDelayedTask(
        dispatcher_,
        [this, callback_info] {
          if (!InSuspend()) {
            return;  // Suspend failed to complete.
          }
          auto& ctx = suspend_context();
          LOGF(ERROR, "Device suspend timed out, suspend flags: %#08x", ctx.sflags());
          if (ctx.task() != nullptr) {
            dump_suspend_task_dependencies(ctx.task());
          }
          if (suspend_fallback()) {
            ::suspend_fallback(root_resource(), ctx.sflags());
            // Unless in test env, we should not reach here.
            if (callback_info->callback) {
              callback_info->callback(ZX_ERR_TIMED_OUT);
              callback_info->callback = nullptr;
            }
          }
        },
        config_.suspend_timeout);
    if (status != ZX_OK) {
      LOGF(ERROR, "Failed to create timeout watchdog for suspend: %s\n",
           zx_status_get_string(status));
    }
    auto completion = [this, callback_info = std::move(callback_info)](zx_status_t status) {
      auto& ctx = suspend_context();
      if (status != ZX_OK) {
        // TODO: unroll suspend
        // do not continue to suspend as this indicates a driver suspend
        // problem and should show as a bug
        LOGF(ERROR, "Failed to suspend: %s", zx_status_get_string(status));
        ctx.set_flags(SuspendContext::Flags::kRunning);
        reboot_watcher_manager_.ClearRebootReason();
        if (callback_info->callback) {
          callback_info->callback(status);
          callback_info->callback = nullptr;
        }
        return;
      }
      if (ctx.sflags() != DEVICE_SUSPEND_FLAG_MEXEC) {
        // should never get here on x86
        // on arm, if the platform driver does not implement
        // suspend go to the kernel fallback
        ::suspend_fallback(root_resource(), ctx.sflags());
        // if we get here the system did not suspend successfully
        ctx.set_flags(SuspendContext::Flags::kRunning);
        reboot_watcher_manager_.ClearRebootReason();
      }

      if (callback_info->callback) {
        callback_info->callback(ZX_OK);
        callback_info->callback = nullptr;
      }
    };
    // We don't need to suspend anything except sys_device and it's children,
    // since we do not run suspend hooks for children of test or misc

    auto task =
        SuspendTask::Create(sys_device(), suspend_context().sflags(), std::move(completion));
    suspend_context().set_task(std::move(task));
    LOGF(INFO, "Successfully created suspend task on device 'sys'");
  };

  if (!reboot_watcher_manager_.ShouldNotifyWatchers()) {
    perform_suspend();
  } else {
    reboot_watcher_manager_.NotifyAll(
        /*watchdog=*/std::move(perform_suspend),
        /*on_last_reply=*/[this] { reboot_watcher_manager_.ExecuteWatchdog(); });
  }
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

void Coordinator::Suspend(uint32_t flags) {
  Suspend(SuspendContext(SuspendContext::Flags::kSuspend, flags), [](zx_status_t) {});
}

void Coordinator::Resume(SystemPowerState target_state, ResumeCallback callback) {
  Resume(ResumeContext(ResumeContext::Flags::kResume, target_state), std::move(callback));
}

std::unique_ptr<Driver> Coordinator::ValidateDriver(std::unique_ptr<Driver> drv) {
  if ((drv->flags & ZIRCON_DRIVER_NOTE_FLAG_ASAN) && !config_.asan_drivers) {
    if (launched_first_driver_host_) {
      LOGF(ERROR, "%s (%s) requires ASan, cannot load after boot; use devmgr.devhost.asan=true",
           drv->libname.data(), drv->name.data());
      return nullptr;
    }
    config_.asan_drivers = true;
  }
  return drv;
}

// DriverAdded is called when a driver is added after the
// devcoordinator has started.  The driver is added to the new-drivers
// list and work is queued to process it.
void Coordinator::DriverAdded(Driver* drv, const char* version) {
  auto driver = ValidateDriver(std::unique_ptr<Driver>(drv));
  if (!driver) {
    return;
  }
  async::PostTask(dispatcher_, [this, drv = std::move(driver)]() mutable {
    Driver* borrow_ref = drv.get();
    drivers_.push_back(std::move(drv));
    zx_status_t status = BindDriver(borrow_ref);
    if (status != ZX_OK && status != ZX_ERR_UNAVAILABLE) {
      LOGF(ERROR, "Failed to bind driver '%s': %s", drv->name.data(), zx_status_get_string(status));
    }
  });
}

// DriverAddedInit is called from driver enumeration during
// startup and before the devcoordinator starts running.  Enumerated
// drivers are added directly to the all-drivers or fallback list.
//
// TODO: fancier priorities
void Coordinator::DriverAddedInit(Driver* drv, const char* version) {
  auto driver = ValidateDriver(std::unique_ptr<Driver>(drv));
  if (!driver) {
    return;
  }

  // Record the special fragment driver when we see it
  if (driver->libname.data() == GetFragmentDriverPath()) {
    fragment_driver_ = driver.get();
    driver->never_autoselect = true;
  }

  if (version[0] == '*') {
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

// Drivers added during system scan (from the dedicated thread)
// are added to system_drivers for bulk processing once
// CTL_ADD_SYSTEM is sent.
//
// TODO: fancier priority management
void Coordinator::DriverAddedSys(Driver* drv, const char* version) {
  auto driver = ValidateDriver(std::unique_ptr<Driver>(drv));
  if (!driver) {
    return;
  }
  LOGF(INFO, "Adding system driver '%s' '%s'", driver->name.data(), driver->libname.data());
  if (load_vmo(driver->libname.data(), &driver->dso_vmo)) {
    LOGF(ERROR, "System driver '%s' '%s' could not cache DSO", driver->name.data(),
         driver->libname.data());
  }
  if (version[0] == '*') {
    // de-prioritize drivers that are "fallback"
    system_drivers_.push_back(std::move(driver));
  } else {
    system_drivers_.push_front(std::move(driver));
  }
}

zx_status_t Coordinator::BindDriverToDevice(const fbl::RefPtr<Device>& dev, const Driver* drv,
                                            bool autobind, const AttemptBindFunc& attempt_bind) {
  if ((dev->flags & DEV_CTX_BOUND) && !(dev->flags & DEV_CTX_ALLOW_MULTI_COMPOSITE) &&
      !(dev->flags & DEV_CTX_MULTI_BIND)) {
    return ZX_ERR_ALREADY_BOUND;
  }

  if (!dev->is_bindable() && !(dev->is_composite_bindable())) {
    return ZX_ERR_NEXT;
  }
  if (!driver_is_bindable(drv, dev->protocol_id(), dev->props(), autobind)) {
    return ZX_ERR_NEXT;
  }

  zx_status_t status = attempt_bind(drv, dev);
  if (status != ZX_OK) {
    LOGF(ERROR, "Failed to bind driver '%s' to device '%s': %s", drv->name.data(),
         dev->name().data(), zx_status_get_string(status));
  }
  if (status == ZX_ERR_NEXT) {
    // Convert ERR_NEXT to avoid confusing the caller
    status = ZX_ERR_INTERNAL;
  }
  return status;
}

// BindDriver is called when a new driver becomes available to
// the Coordinator.  Existing devices are inspected to see if the
// new driver is bindable to them (unless they are already bound).
zx_status_t Coordinator::BindDriver(Driver* drv, const AttemptBindFunc& attempt_bind) {
  if (drv->never_autoselect) {
    return ZX_OK;
  }
  zx_status_t status = BindDriverToDevice(root_device_, drv, true /* autobind */, attempt_bind);
  if (status != ZX_ERR_NEXT) {
    return status;
  }
  status = BindDriverToDevice(misc_device_, drv, true /* autobind */, attempt_bind);
  if (status != ZX_ERR_NEXT) {
    return status;
  }
  status = BindDriverToDevice(test_device_, drv, true /* autobind */, attempt_bind);
  if (status != ZX_ERR_NEXT) {
    return status;
  }
  if (!running_) {
    return ZX_ERR_UNAVAILABLE;
  }
  for (auto& dev : devices_) {
    zx_status_t status =
        BindDriverToDevice(fbl::RefPtr(&dev), drv, true /* autobind */, attempt_bind);
    if (status == ZX_ERR_NEXT || status == ZX_ERR_ALREADY_BOUND) {
      continue;
    }
    if (status != ZX_OK) {
      return status;
    }
  }
  return ZX_OK;
}

zx_status_t Coordinator::BindDevice(const fbl::RefPtr<Device>& dev, fbl::StringPiece drvlibname,
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
  for (const auto& drv : drivers_) {
    if (!autobind && drvlibname.compare(drv.libname)) {
      continue;
    }
    if (drv.never_autoselect) {
      continue;
    }
    zx_status_t status = BindDriverToDevice(dev, &drv, autobind);
    if (status == ZX_ERR_ALREADY_BOUND) {
      return status;
    }
    if (status == ZX_ERR_NEXT) {
      continue;
    }

    // If the device supports multibind (this is a devmgr-internal setting),
    // keep trying to match more drivers even if one fails.
    if (!(dev->flags & DEV_CTX_MULTI_BIND)) {
      if (status != ZX_OK) {
        return status;
      } else {
        break;
      }
    }
  }

  // Notify observers that this device is available again
  // Needed for non-auto-binding drivers like GPT against block, etc
  if (!new_device && autobind) {
    devfs_advertise_modified(dev);
  }

  return ZX_OK;
}

zx_status_t Coordinator::ScanSystemDrivers() {
  if (system_loaded_) {
    return ZX_ERR_BAD_STATE;
  }
  system_loaded_ = true;
  // Fire up a thread to scan/load system drivers.
  // This avoids deadlocks between the driver_hosts hosting the block devices that
  // these drivers may be served from and the devcoordinator loading them.
  thrd_t t;
  auto callback = [](void* arg) {
    auto coordinator = static_cast<Coordinator*>(arg);
    find_loadable_drivers("/system/driver",
                          fit::bind_member(coordinator, &Coordinator::DriverAddedSys));
    async::PostTask(coordinator->dispatcher_, [coordinator] { coordinator->BindSystemDrivers(); });
    return 0;
  };
  int ret = thrd_create_with_name(&t, callback, this, "system-driver-loader");
  if (ret != thrd_success) {
    LOGF(ERROR, "Failed to create system driver scanning thread: %d", ret);
    return ZX_ERR_NO_RESOURCES;
  }
  thrd_detach(t);
  return ZX_OK;
}

void Coordinator::BindSystemDrivers() {
  std::unique_ptr<Driver> drv;
  // Bind system drivers.
  while ((drv = system_drivers_.pop_front()) != nullptr) {
    Driver* borrow_ref = drv.get();
    drivers_.push_back(std::move(drv));
    zx_status_t status = BindDriver(borrow_ref);
    if (status != ZX_OK && status != ZX_ERR_UNAVAILABLE) {
      LOGF(ERROR, "Failed to bind driver '%s': %s", drv->name.data(), zx_status_get_string(status));
    }
  }
  // Bind remaining fallback drivers.
  while ((drv = fallback_drivers_.pop_front()) != nullptr) {
    LOGF(INFO, "Fallback driver '%s' is available", drv->name.data());
    Driver* borrow_ref = drv.get();
    drivers_.push_back(std::move(drv));
    zx_status_t status = BindDriver(borrow_ref);
    if (status != ZX_OK && status != ZX_ERR_UNAVAILABLE) {
      LOGF(ERROR, "Failed to bind driver '%s': %s", drv->name.data(), zx_status_get_string(status));
    }
  }
}

void Coordinator::BindDrivers() {
  for (Driver& drv : drivers_) {
    zx_status_t status = BindDriver(&drv);
    if (status != ZX_OK && status != ZX_ERR_UNAVAILABLE) {
      LOGF(ERROR, "Failed to bind driver '%s': %s", drv.name.data(), zx_status_get_string(status));
    }
  }
}

void Coordinator::UseFallbackDrivers() { drivers_.splice(drivers_.end(), fallback_drivers_); }

// TODO(fxb/42257): Temporary helper to convert state to flags.
// Will be removed eventually.
uint32_t Coordinator::GetSuspendFlagsFromSystemPowerState(
    power_fidl::statecontrol::SystemPowerState state) {
  switch (state) {
    case power_fidl::statecontrol::SystemPowerState::FULLY_ON:
      return 0;
    case power_fidl::statecontrol::SystemPowerState::REBOOT:
      return power_fidl::statecontrol::SUSPEND_FLAG_REBOOT;
    case power_fidl::statecontrol::SystemPowerState::REBOOT_BOOTLOADER:
      return power_fidl::statecontrol::SUSPEND_FLAG_REBOOT_BOOTLOADER;
    case power_fidl::statecontrol::SystemPowerState::REBOOT_RECOVERY:
      return power_fidl::statecontrol::SUSPEND_FLAG_REBOOT_RECOVERY;
    case power_fidl::statecontrol::SystemPowerState::POWEROFF:
      return power_fidl::statecontrol::SUSPEND_FLAG_POWEROFF;
    case power_fidl::statecontrol::SystemPowerState::MEXEC:
      return power_fidl::statecontrol::SUSPEND_FLAG_MEXEC;
    case power_fidl::statecontrol::SystemPowerState::SUSPEND_RAM:
      return power_fidl::statecontrol::SUSPEND_FLAG_SUSPEND_RAM;
    default:
      return 0;
  }
}

void Coordinator::Suspend(
    power_fidl::statecontrol::SystemPowerState state,
    power_fidl::statecontrol::Admin::Interface::SuspendCompleter::Sync completer) {
  auto callback = [completer = completer.ToAsync()](zx_status_t status) mutable {
    power_fidl::statecontrol::Admin_Suspend_Result result;
    fidl::aligned<power_fidl::statecontrol::Admin_Suspend_Response> response;
    if (status != ZX_OK) {
      result.set_err(fidl::unowned_ptr(&status));
    } else {
      result.set_response(fidl::unowned_ptr(&response));
    }
    completer.Reply(std::move(result));
  };

  Suspend(
      SuspendContext(SuspendContext::Flags::kSuspend, GetSuspendFlagsFromSystemPowerState(state)),
      std::move(callback));
}

void Coordinator::PowerFullyOn(
    power_fidl::statecontrol::Admin::Interface::PowerFullyOnCompleter::Sync completer) {
  auto callback = [completer = completer.ToAsync()](zx_status_t status) mutable {
    power_fidl::statecontrol::Admin_PowerFullyOn_Result result;
    fidl::aligned<power_fidl::statecontrol::Admin_PowerFullyOn_Response> response;
    if (status != ZX_OK) {
      result.set_err(fidl::unowned_ptr(&status));
    } else {
      result.set_response(fidl::unowned_ptr(&response));
    }
    completer.Reply(std::move(result));
  };

  Suspend(SuspendContext(SuspendContext::Flags::kSuspend,
                         GetSuspendFlagsFromSystemPowerState(
                             power_fidl::statecontrol::SystemPowerState::FULLY_ON)),
          std::move(callback));
}

void Coordinator::Reboot(
    power_fidl::statecontrol::RebootReason reason,
    power_fidl::statecontrol::Admin::Interface::RebootCompleter::Sync completer) {
  auto callback = [completer = completer.ToAsync()](zx_status_t status) mutable {
    power_fidl::statecontrol::Admin_Reboot_Result result;
    fidl::aligned<power_fidl::statecontrol::Admin_Reboot_Response> response;
    if (status != ZX_OK) {
      result.set_err(fidl::unowned_ptr(&status));
    } else {
      result.set_response(fidl::unowned_ptr(&response));
    }
    completer.Reply(std::move(result));
  };

  if (InSuspend()) {
    // Reboot already in progress.
    LOGF(ERROR, "Aborting reboot, a system suspend is already in progress");
    callback(ZX_ERR_ALREADY_EXISTS);
    return;
  }

  reboot_watcher_manager_.SetRebootReason(reason);
  Suspend(SuspendContext(SuspendContext::Flags::kSuspend,
                         GetSuspendFlagsFromSystemPowerState(
                             power_fidl::statecontrol::SystemPowerState::REBOOT)),
          std::move(callback));
}

void Coordinator::RebootToBootloader(
    power_fidl::statecontrol::Admin::Interface::RebootToBootloaderCompleter::Sync completer) {
  auto callback = [completer = completer.ToAsync()](zx_status_t status) mutable {
    power_fidl::statecontrol::Admin_RebootToBootloader_Result result;
    fidl::aligned<power_fidl::statecontrol::Admin_RebootToBootloader_Response> response;
    if (status != ZX_OK) {
      result.set_err(fidl::unowned_ptr(&status));
    } else {
      result.set_response(fidl::unowned_ptr(&response));
    }
    completer.Reply(std::move(result));
  };

  Suspend(SuspendContext(SuspendContext::Flags::kSuspend,
                         GetSuspendFlagsFromSystemPowerState(
                             power_fidl::statecontrol::SystemPowerState::REBOOT_BOOTLOADER)),
          std::move(callback));
}

void Coordinator::RebootToRecovery(
    power_fidl::statecontrol::Admin::Interface::RebootToRecoveryCompleter::Sync completer) {
  auto callback = [completer = completer.ToAsync()](zx_status_t status) mutable {
    power_fidl::statecontrol::Admin_RebootToRecovery_Result result;
    fidl::aligned<power_fidl::statecontrol::Admin_RebootToRecovery_Response> response;
    if (status != ZX_OK) {
      result.set_err(fidl::unowned_ptr(&status));
    } else {
      result.set_response(fidl::unowned_ptr(&response));
    }
    completer.Reply(std::move(result));
  };

  Suspend(SuspendContext(SuspendContext::Flags::kSuspend,
                         GetSuspendFlagsFromSystemPowerState(
                             power_fidl::statecontrol::SystemPowerState::REBOOT_RECOVERY)),
          std::move(callback));
}

void Coordinator::Poweroff(
    power_fidl::statecontrol::Admin::Interface::PoweroffCompleter::Sync completer) {
  auto callback = [completer = completer.ToAsync()](zx_status_t status) mutable {
    power_fidl::statecontrol::Admin_Poweroff_Result result;
    fidl::aligned<power_fidl::statecontrol::Admin_Poweroff_Response> response;
    if (status != ZX_OK) {
      result.set_err(fidl::unowned_ptr(&status));
    } else {
      result.set_response(fidl::unowned_ptr(&response));
    }
    completer.Reply(std::move(result));
  };

  Suspend(SuspendContext(SuspendContext::Flags::kSuspend,
                         GetSuspendFlagsFromSystemPowerState(
                             power_fidl::statecontrol::SystemPowerState::POWEROFF)),
          std::move(callback));
}

void Coordinator::Mexec(
    power_fidl::statecontrol::Admin::Interface::MexecCompleter::Sync completer) {
  auto callback = [completer = completer.ToAsync()](zx_status_t status) mutable {
    power_fidl::statecontrol::Admin_Mexec_Result result;
    fidl::aligned<power_fidl::statecontrol::Admin_Mexec_Response> response;
    if (status != ZX_OK) {
      result.set_err(fidl::unowned_ptr(&status));
    } else {
      result.set_response(fidl::unowned_ptr(&response));
    }
    completer.Reply(std::move(result));
  };

  Suspend(SuspendContext(SuspendContext::Flags::kSuspend,
                         GetSuspendFlagsFromSystemPowerState(
                             power_fidl::statecontrol::SystemPowerState::MEXEC)),
          std::move(callback));
}

void Coordinator::SuspendToRam(
    power_fidl::statecontrol::Admin::Interface::SuspendToRamCompleter::Sync completer) {
  auto callback = [completer = completer.ToAsync()](zx_status_t status) mutable {
    power_fidl::statecontrol::Admin_SuspendToRam_Result result;
    fidl::aligned<power_fidl::statecontrol::Admin_SuspendToRam_Response> response;
    if (status != ZX_OK) {
      result.set_err(fidl::unowned_ptr(&status));
    } else {
      result.set_response(fidl::unowned_ptr(&response));
    }
    completer.Reply(std::move(result));
  };

  Suspend(SuspendContext(SuspendContext::Flags::kSuspend,
                         GetSuspendFlagsFromSystemPowerState(
                             power_fidl::statecontrol::SystemPowerState::SUSPEND_RAM)),
          std::move(callback));
}

void Coordinator::GetBindProgram(::fidl::StringView driver_path_view,
                                 GetBindProgramCompleter::Sync completer) {
  fbl::StringPiece driver_path(driver_path_view.data(), driver_path_view.size());
  const Driver* driver = LibnameToDriver(driver_path);
  if (driver == nullptr) {
    completer.ReplyError(ZX_ERR_NOT_FOUND);
    return;
  }

  uint32_t count = 0;
  if (driver->binding_size > 0) {
    count = driver->binding_size / sizeof(driver->binding[0]);
  }
  if (count > fuchsia_device_manager_BIND_PROGRAM_INSTRUCTIONS_MAX) {
    completer.ReplyError(ZX_ERR_BUFFER_TOO_SMALL);
    return;
  }

  std::vector<llcpp::fuchsia::device::manager::BindInstruction> instructions;
  for (uint32_t i = 0; i < count; i++) {
    instructions.push_back(llcpp::fuchsia::device::manager::BindInstruction{
        .op = driver->binding[i].op,
        .arg = driver->binding[i].arg,
        .debug = driver->binding[i].debug,
    });
  }
  completer.ReplySuccess(::fidl::unowned_vec(instructions));
}

void Coordinator::GetDeviceProperties(::fidl::StringView device_path,
                                      GetDevicePropertiesCompleter::Sync completer) {
  fbl::RefPtr<Device> device;
  zx_status_t status = devfs_walk(root_device_->devnode(), device_path.data(), &device);
  if (status != ZX_OK) {
    completer.ReplyError(status);
    return;
  }

  if (device->props().size() > fuchsia_device_manager_PROPERTIES_MAX) {
    completer.ReplyError(ZX_ERR_BUFFER_TOO_SMALL);
    return;
  }

  std::vector<llcpp::fuchsia::device::manager::DeviceProperty> props;
  for (const auto& prop : device->props()) {
    props.push_back(llcpp::fuchsia::device::manager::DeviceProperty{
        .id = prop.id,
        .reserved = prop.reserved,
        .value = prop.value,
    });
  }
  completer.ReplySuccess(::fidl::unowned_vec(props));
}

zx_status_t Coordinator::InitOutgoingServices(const fbl::RefPtr<fs::PseudoDir>& svc_dir) {
  const auto admin = [this](zx::channel request) {
    static_assert(fuchsia_device_manager_SUSPEND_FLAG_REBOOT == DEVICE_SUSPEND_FLAG_REBOOT);
    static_assert(fuchsia_device_manager_SUSPEND_FLAG_POWEROFF == DEVICE_SUSPEND_FLAG_POWEROFF);

    static constexpr fuchsia_device_manager_Administrator_ops_t kOps = {
        .Suspend =
            [](void* ctx, uint32_t flags, fidl_txn_t* txn) {
              auto* async_txn = fidl_async_txn_create(txn);
              static_cast<Coordinator*>(ctx)->Suspend(
                  SuspendContext(SuspendContext::Flags::kSuspend, flags),
                  [async_txn](zx_status_t status) {
                    fuchsia_device_manager_AdministratorSuspend_reply(
                        fidl_async_txn_borrow(async_txn), status);
                    fidl_async_txn_complete(async_txn, true);
                  });
              return ZX_ERR_ASYNC;
            },
    };

    zx_status_t status =
        fidl_bind(dispatcher_, request.release(),
                  reinterpret_cast<fidl_dispatch_t*>(fuchsia_device_manager_Administrator_dispatch),
                  this, &kOps);
    if (status != ZX_OK) {
      LOGF(ERROR, "Failed to bind to client channel for '%s': %s",
           fuchsia_device_manager_Administrator_Name, zx_status_get_string(status));
    }
    return status;
  };
  zx_status_t status = svc_dir->AddEntry(fuchsia_device_manager_Administrator_Name,
                                         fbl::MakeRefCounted<fs::Service>(admin));
  if (status != ZX_OK) {
    return status;
  }

  const auto admin2 = [this](zx::channel request) {
    auto status = fidl::Bind<llcpp::fuchsia::hardware::power::statecontrol::Admin::Interface>(
        dispatcher_, std::move(request), this);
    if (status != ZX_OK) {
      LOGF(ERROR, "Failed to bind to client channel for '%s': %s",
           power_fidl::statecontrol::Admin::Name, zx_status_get_string(status));
    }
    return status;
  };
  status = svc_dir->AddEntry(power_fidl::statecontrol::Admin::Name,
                             fbl::MakeRefCounted<fs::Service>(admin2));
  if (status != ZX_OK) {
    return status;
  }

  const auto reboot_methods_watcher_register = [this](zx::channel request) {
    auto status = fidl::Bind<
        llcpp::fuchsia::hardware::power::statecontrol::RebootMethodsWatcherRegister::Interface>(
        dispatcher_, std::move(request), &(this->reboot_watcher_manager_));
    if (status != ZX_OK) {
      LOGF(ERROR, "Failed to bind to client channel for '%s': %s",
           power_fidl::statecontrol::RebootMethodsWatcherRegister::Name,
           zx_status_get_string(status));
    }

    return status;
  };
  status = svc_dir->AddEntry(power_fidl::statecontrol::RebootMethodsWatcherRegister::Name,
                             fbl::MakeRefCounted<fs::Service>(reboot_methods_watcher_register));
  if (status != ZX_OK) {
    LOGF(ERROR, "Failed to add entry in service directory for '%s': %s",
         power_fidl::statecontrol::RebootMethodsWatcherRegister::Name,
         zx_status_get_string(status));
    return status;
  }

  const auto system_state_manager_register = [this](zx::channel request) {
    auto status = fidl::Bind<llcpp::fuchsia::device::manager::SystemStateTransition::Interface>(
        dispatcher_, std::move(request), std::make_unique<SystemStateManager>(this));
    if (status != ZX_OK) {
      LOGF(ERROR, "Failed to bind to client channel for '%s': %s",
           llcpp::fuchsia::device::manager::SystemStateTransition::Name,
           zx_status_get_string(status));
    }
    return status;
  };
  status = svc_dir->AddEntry(llcpp::fuchsia::device::manager::SystemStateTransition::Name,
                             fbl::MakeRefCounted<fs::Service>(system_state_manager_register));
  if (status != ZX_OK) {
    LOGF(ERROR, "Failed to add entry in service directory for '%s': %s",
         llcpp::fuchsia::device::manager::SystemStateTransition::Name,
         zx_status_get_string(status));
    return status;
  }

  const auto bind_debugger = [this](zx::channel request) {
    auto status = fidl::Bind<llcpp::fuchsia::device::manager::BindDebugger::Interface>(
        dispatcher_, std::move(request), this);
    if (status != ZX_OK) {
      LOGF(ERROR, "Failed to bind to client channel for '%s': %s",
           llcpp::fuchsia::device::manager::BindDebugger::Name, zx_status_get_string(status));
    }
    return status;
  };
  status = svc_dir->AddEntry(llcpp::fuchsia::device::manager::BindDebugger::Name,
                             fbl::MakeRefCounted<fs::Service>(bind_debugger));
  if (status != ZX_OK) {
    return status;
  }

  const auto debug = [this](zx::channel request) {
    static constexpr fuchsia_device_manager_DebugDumper_ops_t kOps = {
        .DumpTree =
            [](void* ctx, zx_handle_t vmo, fidl_txn_t* txn) {
              VmoWriter writer{zx::vmo(vmo)};
              static_cast<Coordinator*>(ctx)->DumpState(&writer);
              return fuchsia_device_manager_DebugDumperDumpTree_reply(
                  txn, writer.status(), writer.written(), writer.available());
            },
        .DumpDrivers =
            [](void* ctx, zx_handle_t vmo, fidl_txn_t* txn) {
              VmoWriter writer{zx::vmo(vmo)};
              static_cast<Coordinator*>(ctx)->DumpDrivers(&writer);
              return fuchsia_device_manager_DebugDumperDumpDrivers_reply(
                  txn, writer.status(), writer.written(), writer.available());
            },
        .DumpBindingProperties =
            [](void* ctx, zx_handle_t vmo, fidl_txn_t* txn) {
              VmoWriter writer{zx::vmo(vmo)};
              static_cast<Coordinator*>(ctx)->DumpGlobalDeviceProps(&writer);
              return fuchsia_device_manager_DebugDumperDumpBindingProperties_reply(
                  txn, writer.status(), writer.written(), writer.available());
            },
    };

    auto status =
        fidl_bind(dispatcher_, request.release(),
                  reinterpret_cast<fidl_dispatch_t*>(fuchsia_device_manager_DebugDumper_dispatch),
                  this, &kOps);
    if (status != ZX_OK) {
      LOGF(ERROR, "Failed to bind to client channel for '%s': %s",
           fuchsia_device_manager_DebugDumper_Name, zx_status_get_string(status));
    }
    return status;
  };
  return svc_dir->AddEntry(fuchsia_device_manager_DebugDumper_Name,
                           fbl::MakeRefCounted<fs::Service>(debug));
}

void Coordinator::OnOOMEvent(async_dispatcher_t* dispatcher, async::WaitBase* wait,
                             zx_status_t status, const zx_packet_signal_t* signal) {
  this->ShutdownFilesystems();
}

std::string Coordinator::GetFragmentDriverPath() const {
  return config_.path_prefix + "driver/fragment.so";
}
