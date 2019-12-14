// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "coordinator.h"

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <fuchsia/boot/c/fidl.h>
#include <fuchsia/io/c/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/async/cpp/receiver.h>
#include <lib/async/cpp/task.h>
#include <lib/async/cpp/wait.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/io.h>
#include <lib/fdio/spawn.h>
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
#include <inspector/inspector.h>
#include <libzbi/zbi-cpp.h>

#include "composite-device.h"
#include "devfs.h"
#include "devhost-loader-service.h"
#include "env.h"
#include "fdio.h"
#include "fidl.h"
#include "fidl_txn.h"
#include "fuchsia/hardware/power/statecontrol/llcpp/fidl.h"
#include "lib/zx/time.h"
#include "log.h"
#include "vmo-writer.h"

namespace {

constexpr char kBootFirmwarePath[] = "/boot/lib/firmware";
constexpr char kSystemFirmwarePath[] = "/system/lib/firmware";
constexpr char kItemsPath[] = "/svc/" fuchsia_boot_Items_Name;

// Tells VFS to exit by shutting down the fshost. Note that this is called from
// multiple different locations; during suspension, and in a low-memory
// situation. Currently, both of these calls happen on the same dispatcher
// thread, but consider thread safety when refactoring.
void vfs_exit(const zx::event& fshost_event) {
  zx_status_t status;
  if ((status = fshost_event.signal(0, FSHOST_SIGNAL_EXIT)) != ZX_OK) {
    printf("devcoordinator: Failed to signal VFS exit\n");
    return;
  }
  // We used to wait here with a 60 seconds timeout but the storage stack might
  // need more time to flush all the writeback buffers. See bug 38103 for details.
  if ((status = fshost_event.wait_one(FSHOST_SIGNAL_EXIT_DONE, zx::time::infinite(), nullptr)) !=
      ZX_OK) {
    printf("devcoordinator: Failed to wait for VFS exit completion\n");
    return;
  }

  printf("devcoordinator: Successfully waited for VFS exit completion\n");
}

void suspend_fallback(const zx::resource& root_resource, uint32_t flags) {
  log(ERROR, "devcoordinator: suspend fallback with flags 0x%08x\n", flags);
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

namespace devmgr {

namespace power_fidl = llcpp::fuchsia::hardware::power;

const char* kComponentDriverPath = "/boot/driver/component.so";

uint32_t log_flags = LOG_ERROR | LOG_INFO;

Coordinator::Coordinator(CoordinatorConfig config)
    : config_(std::move(config)), outgoing_services_(config_.dispatcher) {
  if (config_.lowmem_event) {
    wait_on_oom_event_.set_object(config_.lowmem_event.get());
    wait_on_oom_event_.set_trigger(ZX_EVENT_SIGNALED);
    wait_on_oom_event_.Begin(config_.dispatcher);
  }

  InitOutgoingServices();
}

Coordinator::~Coordinator() { drivers_.clear(); }

bool Coordinator::InSuspend() const {
  return suspend_context().flags() == SuspendContext::Flags::kSuspend;
}

bool Coordinator::InResume() const {
  return (resume_context().flags() == ResumeContext::Flags::kResume);
}

zx_status_t Coordinator::InitializeCoreDevices(const char* sys_device_driver) {
  root_device_ = fbl::MakeRefCounted<Device>(this, "root", fbl::String(), "root,", nullptr,
                                             ZX_PROTOCOL_ROOT, zx::channel());
  root_device_->flags = DEV_CTX_IMMORTAL | DEV_CTX_MUST_ISOLATE | DEV_CTX_MULTI_BIND;

  misc_device_ = fbl::MakeRefCounted<Device>(this, "misc", fbl::String(), "misc,", root_device_,
                                             ZX_PROTOCOL_MISC_PARENT, zx::channel());
  misc_device_->flags = DEV_CTX_IMMORTAL | DEV_CTX_MUST_ISOLATE | DEV_CTX_MULTI_BIND;

  sys_device_ = fbl::MakeRefCounted<Device>(this, "sys", sys_device_driver, "sys,", root_device_, 0,
                                            zx::channel());
  sys_device_->flags = DEV_CTX_IMMORTAL | DEV_CTX_MUST_ISOLATE;

  test_device_ = fbl::MakeRefCounted<Device>(this, "test", fbl::String(), "test,", root_device_,
                                             ZX_PROTOCOL_TEST_PARENT, zx::channel());
  test_device_->flags = DEV_CTX_IMMORTAL | DEV_CTX_MUST_ISOLATE | DEV_CTX_MULTI_BIND;
  return ZX_OK;
}

const Driver* Coordinator::LibnameToDriver(const fbl::String& libname) const {
  for (const auto& drv : drivers_) {
    if (libname == drv.libname) {
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
    log(ERROR, "devcoordinator: cannot open driver '%s'\n", libname.data());
    return ZX_ERR_IO;
  }
  zx::vmo vmo;
  r = fdio_get_vmo_exec(fd, vmo.reset_and_get_address());
  close(fd);
  if (r != ZX_OK) {
    log(ERROR, "devcoordinator: cannot get driver vmo '%s'\n", libname.data());
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
    log(ERROR, "devcoordinator: cannot set name on driver vmo to '%s'\n", libname.data());
    return r;
  }
  *out_vmo = std::move(vmo);
  return r;
}

zx_status_t Coordinator::LibnameToVmo(const fbl::String& libname, zx::vmo* out_vmo) const {
  const Driver* drv = LibnameToDriver(libname);
  if (drv == nullptr) {
    log(ERROR, "devcoordinator: cannot find driver '%s'\n", libname.data());
    return ZX_ERR_NOT_FOUND;
  }

  // Check for cached DSO
  if (drv->dso_vmo != ZX_HANDLE_INVALID) {
    zx_status_t r = drv->dso_vmo.duplicate(
        ZX_RIGHTS_BASIC | ZX_RIGHTS_PROPERTY | ZX_RIGHT_READ | ZX_RIGHT_EXECUTE | ZX_RIGHT_MAP,
        out_vmo);
    if (r != ZX_OK) {
      log(ERROR, "devcoordinator: cannot duplicate cached dso for '%s' '%s'\n", drv->name.data(),
          libname.data());
    }
    return r;
  } else {
    return load_vmo(libname, out_vmo);
  }
}

void Coordinator::DumpDevice(VmoWriter* vmo, const Device* dev, size_t indent) const {
  zx_koid_t pid = dev->host() ? dev->host()->koid() : 0;
  char extra[256];
  if (log_flags & LOG_DEVLC) {
    snprintf(extra, sizeof(extra), " dev=%p ", dev);
  } else {
    extra[0] = 0;
  }
  if (pid == 0) {
    vmo->Printf("%*s[%s]%s\n", (int)(indent * 3), "", dev->name().data(), extra);
  } else {
    vmo->Printf("%*s%c%s%c pid=%zu%s %s\n", (int)(indent * 3), "",
                dev->flags & DEV_CTX_PROXY ? '<' : '[', dev->name().data(),
                dev->flags & DEV_CTX_PROXY ? '>' : ']', pid, extra, dev->libname().data());
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
    vmo->Printf("ProtoId : '%c%c%c%c' 0x%08x(%u)\n", isprint(a) ? a : '.', isprint(b) ? b : '.',
                isprint(c) ? c : '.', isprint(d) ? d : '.', dev->protocol_id(), dev->protocol_id());

    const auto& props = dev->props();
    vmo->Printf("%zu Propert%s\n", props.size(), props.size() == 1 ? "y" : "ies");
    for (uint32_t i = 0; i < props.size(); ++i) {
      const zx_device_prop_t* p = &props[i];
      const char* param_name = di_bind_param_name(p->id);

      if (param_name) {
        vmo->Printf("[%2u/%2zu] : Value 0x%08x Id %s\n", i, props.size(), p->value, param_name);
      } else {
        vmo->Printf("[%2u/%2zu] : Value 0x%08x Id 0x%04hx\n", i, props.size(), p->value, p->id);
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
    vmo->Printf("Flags   : 0x%08x\n", drv.flags);
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

static const char* get_devhost_bin(bool asan_drivers) {
  // If there are any ASan drivers, use the ASan-supporting devhost for
  // all drivers because even a devhost launched initially with just a
  // non-ASan driver might later load an ASan driver.  One day we might
  // be able to be more flexible about which drivers must get loaded into
  // the same devhost and thus be able to use both ASan and non-ASan
  // devhosts at the same time when only a subset of drivers use ASan.
  return "/boot/bin/devhost";
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

static zx_status_t dc_launch_devhost(Devhost* host, const LoaderServiceConnector& loader_connector,
                                     const char* devhost_bin, const char* name,
                                     const char* const* env, zx_handle_t hrpc,
                                     const zx::resource& root_resource, zx::unowned_job devhost_job,
                                     FsProvider* fs_provider) {
  // Give devhosts the root resource if we have it (in tests, we may not)
  // TODO: limit root resource to root devhost only
  zx::resource resource;
  if (root_resource.is_valid()) {
    zx_status_t status = root_resource.duplicate(ZX_RIGHT_SAME_RIGHTS, &resource);
    if (status != ZX_OK) {
      log(ERROR, "devcoordinator: failed to duplicate root resource: %d\n", status);
    }
  }

  constexpr size_t kMaxActions = 5;
  fdio_spawn_action_t actions[kMaxActions];
  size_t actions_count = 0;
  actions[actions_count++] =
      fdio_spawn_action_t{.action = FDIO_SPAWN_ACTION_SET_NAME, .name = {.data = name}};
  // TODO: constrain to /svc/device
  actions[actions_count++] = fdio_spawn_action_t{
      .action = FDIO_SPAWN_ACTION_ADD_NS_ENTRY,
      .ns = {.prefix = "/svc", .handle = fs_provider->CloneFs("svc").release()},
  };
  actions[actions_count++] = fdio_spawn_action_t{
      .action = FDIO_SPAWN_ACTION_ADD_HANDLE,
      .h = {.id = PA_HND(PA_USER0, 0), .handle = hrpc},
  };
  if (resource.is_valid()) {
    actions[actions_count++] = fdio_spawn_action_t{
        .action = FDIO_SPAWN_ACTION_ADD_HANDLE,
        .h = {.id = PA_HND(PA_RESOURCE, 0), .handle = resource.release()},
    };
  }

  zx::channel loader_connection;
  zx_status_t status = loader_connector(&loader_connection);
  if (status != ZX_OK) {
    log(ERROR, "devcoordinator: failed to get devhost loader connection: %s\n",
        zx_status_get_string(status));
    return status;
  }
  actions[actions_count++] = fdio_spawn_action_t{
      .action = FDIO_SPAWN_ACTION_ADD_HANDLE,
      .h = {.id = PA_HND(PA_LDSVC_LOADER, 0), .handle = loader_connection.release()},
  };
  ZX_ASSERT(actions_count <= kMaxActions);

  zx::process proc;
  char err_msg[FDIO_SPAWN_ERR_MSG_MAX_LENGTH];
  // Inherit devmgr's environment (including kernel cmdline)
  const char* const argv[] = {
      devhost_bin,
      nullptr,
  };
  status = fdio_spawn_etc(devhost_job->get(), FDIO_SPAWN_CLONE_ENVIRON, argv[0], argv, env,
                          actions_count, actions, proc.reset_and_get_address(), err_msg);
  if (status != ZX_OK) {
    log(ERROR, "devcoordinator: launch devhost '%s': failed: %s: %s\n", name,
        zx_status_get_string(status), err_msg);
    return status;
  }

  host->set_proc(std::move(proc));

  zx_info_handle_basic_t info;
  if (host->proc()->get_info(ZX_INFO_HANDLE_BASIC, &info, sizeof(info), nullptr, nullptr) ==
      ZX_OK) {
    host->set_koid(info.koid);
  }
  log(INFO, "devcoordinator: launch devhost '%s': pid=%zu\n", name, host->koid());
  return ZX_OK;
}

zx_status_t Coordinator::NewDevhost(const char* name, Devhost* parent, Devhost** out) {
  auto dh = std::make_unique<Devhost>();
  if (dh == nullptr) {
    return ZX_ERR_NO_MEMORY;
  }

  zx::channel hrpc, dh_hrpc;
  zx_status_t status = zx::channel::create(0, &hrpc, &dh_hrpc);
  if (status != ZX_OK) {
    return status;
  }
  dh->set_hrpc(dh_hrpc.release());

  fbl::Vector<const char*> env;
  boot_args().Collect("driver.", &env);
  env.push_back(nullptr);
  status = dc_launch_devhost(
      dh.get(), loader_service_connector_, get_devhost_bin(config_.asan_drivers), name, env.data(),
      hrpc.release(), root_resource(), zx::unowned_job(config_.devhost_job), config_.fs_provider);
  if (status != ZX_OK) {
    zx_handle_close(dh->hrpc());
    return status;
  }
  launched_first_devhost_ = true;

  if (parent) {
    dh->set_parent(parent);
    dh->parent()->AddRef();
    dh->parent()->children().push_back(dh.get());
  }
  devhosts_.push_back(dh.get());

  log(DEVLC, "devcoordinator: new host %p\n", dh.get());

  *out = dh.release();
  return ZX_OK;
}

void Coordinator::ReleaseDevhost(Devhost* dh) {
  if (!dh->Release()) {
    return;
  }
  log(INFO, "devcoordinator: destroy host %p\n", dh);
  Devhost* parent = dh->parent();
  if (parent != nullptr) {
    dh->parent()->children().erase(*dh);
    dh->set_parent(nullptr);
    ReleaseDevhost(parent);
  }
  devhosts_.erase(*dh);
  zx_handle_close(dh->hrpc());
  dh->proc()->kill();
  delete dh;
}

// Add a new device to a parent device (same devhost)
// New device is published in devfs.
// Caller closes handles on error, so we don't have to.
zx_status_t Coordinator::AddDevice(const fbl::RefPtr<Device>& parent, zx::channel device_controller,
                                   zx::channel coordinator, const uint64_t* props_data,
                                   size_t props_count, fbl::StringPiece name, uint32_t protocol_id,
                                   fbl::StringPiece driver_path, fbl::StringPiece args,
                                   bool invisible, bool do_init, zx::channel client_remote,
                                   fbl::RefPtr<Device>* new_device) {
  // If this is true, then |name_data|'s size is properly bounded.
  static_assert(fuchsia_device_manager_DEVICE_NAME_MAX == ZX_DEVICE_NAME_MAX);
  static_assert(fuchsia_device_manager_PROPERTIES_MAX <= UINT32_MAX);

  if (InSuspend()) {
    log(ERROR, "devcoordinator: rpc: add-device '%.*s' forbidden in suspend\n",
        static_cast<int>(name.size()), name.data());
    return ZX_ERR_BAD_STATE;
  }

  if (InResume()) {
    log(ERROR, "devcoordinator: rpc: add-device '%.*s' forbidden in resume\n",
        static_cast<int>(name.size()), name.data());
    return ZX_ERR_BAD_STATE;
  }

  if (parent->state() == Device::State::kUnbinding) {
    log(ERROR, "devcoordinator: rpc: add-device '%.*s' forbidden while parent is unbinding\n",
        static_cast<int>(name.size()), name.data());
    return ZX_ERR_BAD_STATE;
  }

  log(RPC_IN, "devcoordinator: rpc: add-device '%.*s' args='%.*s'\n", static_cast<int>(name.size()),
      name.data(), static_cast<int>(args.size()), args.data());

  fbl::Array<zx_device_prop_t> props(new zx_device_prop_t[props_count], props_count);
  if (!props) {
    return ZX_ERR_NO_MEMORY;
  }
  static_assert(sizeof(zx_device_prop_t) == sizeof(props_data[0]));
  memcpy(props.data(), props_data, props_count * sizeof(zx_device_prop_t));

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

  fbl::RefPtr<Device> dev;
  zx_status_t status = Device::Create(this, parent, std::move(name_str), std::move(driver_path_str),
                                      std::move(args_str), protocol_id, std::move(props),
                                      std::move(coordinator), std::move(device_controller),
                                      invisible, do_init, std::move(client_remote), &dev);
  if (status != ZX_OK) {
    return status;
  }
  devices_.push_back(dev);

  // Note that |dev->parent()| may not match |parent| here, so we should always
  // use |dev->parent()|.  This case can happen if |parent| refers to a device
  // proxy.

  // If we're creating a device that's using the component driver, inform the
  // component.
  if (component_driver_ != nullptr && dev->libname() == component_driver_->libname) {
    for (auto& cur_component : dev->parent()->components()) {
      if (cur_component.component_device() == nullptr) {
        // Pick the first component that does not have a device added by the component
        // driver.
        cur_component.set_component_device(dev);
        status = cur_component.composite()->TryAssemble();
        if (status != ZX_OK && status != ZX_ERR_SHOULD_WAIT) {
          log(ERROR, "devcoordinator: failed to assemble composite: %s\n",
              zx_status_get_string(status));
        }
        break;
      }
    }
  }

  // If a device has an init hook, it will be made visible once the hook completes.
  if (!invisible && !do_init) {
    log(DEVLC, "devcoord: publish %p '%s' props=%zu args='%s' parent=%p\n", dev.get(),
        dev->name().data(), dev->props().size(), dev->args().data(), dev->parent().get());
    status = dev->SignalReadyForBind();
    if (status != ZX_OK) {
      return status;
    }
  }

  *new_device = std::move(dev);
  return ZX_OK;
}

zx_status_t Coordinator::MakeVisible(const fbl::RefPtr<Device>& dev) {
  if ((dev->state() == Device::State::kDead) || (dev->state() == Device::State::kInitializing)) {
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
      UnbindTaskOpts{.do_unbind = false, .post_on_create = true, .devhost_requested = false});
}

void Coordinator::ScheduleDevhostRequestedRemove(const fbl::RefPtr<Device>& dev, bool do_unbind) {
  dev->CreateUnbindRemoveTasks(
      UnbindTaskOpts{.do_unbind = do_unbind, .post_on_create = true, .devhost_requested = true});
}

void Coordinator::ScheduleDevhostRequestedUnbindChildren(const fbl::RefPtr<Device>& parent) {
  for (auto& child : parent->children()) {
    child.CreateUnbindRemoveTasks(
        UnbindTaskOpts{.do_unbind = true, .post_on_create = true, .devhost_requested = true});
  }
}

// Remove device from parent
// forced indicates this is removal due to a channel close
// or process exit, which means we should remove all other
// devices that share the devhost at the same time
zx_status_t Coordinator::RemoveDevice(const fbl::RefPtr<Device>& dev, bool forced) {
  dev->inc_num_removal_attempts();

  if (dev->state() == Device::State::kDead) {
    // This should not happen
    log(ERROR, "devcoordinator: cannot remove dev %p name='%s' twice!\n", dev.get(),
        dev->name().data());
    return ZX_ERR_BAD_STATE;
  }
  if (dev->flags & DEV_CTX_IMMORTAL) {
    // This too should not happen
    log(ERROR, "devcoordinator: cannot remove dev %p name='%s' (immortal)\n", dev.get(),
        dev->name().data());
    return ZX_ERR_BAD_STATE;
  }

  log(DEVLC, "devcoordinator: remove %p name='%s' parent=%p\n", dev.get(), dev->name().data(),
      dev->parent().get());
  dev->set_state(Device::State::kDead);

  // remove from devfs, preventing further OPEN attempts
  devfs_unpublish(dev.get());

  // Mark any suspend that's in-flight as completed, since if the device is
  // removed it should be in its lowest state.
  // TODO(teisenbe): Should we mark it as failed if this is a forced removal?
  dev->CompleteSuspend(ZX_OK);
  dev->CompleteInit(ZX_ERR_UNAVAILABLE);

  Devhost* dh = dev->host();
  bool devhost_dying = (dh != nullptr && (dh->flags() & Devhost::Flags::kDying));
  if (forced || devhost_dying) {
    // We are force removing all devices in the devhost, so force complete any outstanding tasks.
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

  // Check if this device is a composite component device
  if (component_driver_ != nullptr && dev->libname() == component_driver_->libname) {
    // If it is, then its parent will know about which one (since the parent
    // is the actual device matched by the component description).
    const auto& parent = dev->parent();

    for (auto itr = parent->components().begin(); itr != parent->components().end();) {
      auto& cur_component = *itr;
      // Advance the iterator because we will erase the current element from the list.
      ++itr;
      if (cur_component.component_device() == dev) {
        cur_component.Unbind();
        parent->components().erase(cur_component);
        break;
      }
    }
  }

  // detach from devhost
  if (dh != nullptr) {
    dev->host()->devices().erase(*dev);
    // Acquire an extra reference to the devhost that gets released below.
    // This is necessary to prevent a dh from being freed in the middle of
    // the code below.
    dh->AddRef();
    dev->set_host(nullptr);

    // If we are responding to a disconnect,
    // we'll remove all the other devices on this devhost too.
    // A side-effect of this is that the devhost will be released,
    // as well as any proxy devices.
    if (forced) {
      dh->flags() |= Devhost::Flags::kDying;

      fbl::RefPtr<Device> next;
      fbl::RefPtr<Device> last;
      while (!dh->devices().is_empty()) {
        next = fbl::RefPtr(&dh->devices().front());
        if (last == next) {
          // This shouldn't be possible, but let's not infinite-loop if it happens
          log(ERROR, "devcoordinator: fatal: failed to remove dev %p from devhost\n", next.get());
          abort();
        }
        RemoveDevice(next, false);
        last = std::move(next);
      }

      // TODO: set a timer so if this devhost does not finish dying
      //      in a reasonable amount of time, we fix the glitch.
    }

    ReleaseDevhost(dh);
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
        //      restart and get a new devhost to be launched
        //      when a devhost dies.  It should probably be
        //      more tied to devhost teardown than it is.
        // IF we are the last child of our parent
        // AND our parent is not itself dead
        // AND our parent is a BUSDEV
        // AND our parent's devhost is not dying
        // THEN we will want to rebind our parent
        if ((parent->state() != Device::State::kDead) && (parent->flags & DEV_CTX_MUST_ISOLATE) &&
            ((parent->host() == nullptr) || !(parent->host()->flags() & Devhost::Flags::kDying))) {
          log(DEVLC, "devcoordinator: bus device %p name='%s' is unbound\n", parent.get(),
              parent->name().data());

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
  zx_status_t status = CompositeDevice::Create(name, comp_desc, &new_device);
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
    if (new_device->TryMatchComponents(dev_ref, &index)) {
      log(SPEW, "devcoordinator: dev='%s' matched component %zu of composite='%s'\n",
          dev.name().data(), index, new_device->name().data());
      status = new_device->BindComponent(index, dev_ref);
      if (status != ZX_OK) {
        log(ERROR, "devcoordinator: dev='%s' failed to bind component %zu of composite='%s': %s\n",
            dev.name().data(), index, new_device->name().data(), zx_status_get_string(status));
      }
    }
  }

  composite_devices_.push_back(std::move(new_device));
  return ZX_OK;
}

zx_status_t Coordinator::LoadFirmware(const fbl::RefPtr<Device>& dev, const char* path,
                                      zx::vmo* vmo, size_t* size) {
  static const char* fwdirs[] = {
      kBootFirmwarePath,
      kSystemFirmwarePath,
  };

  // Must be a relative path and no funny business.
  if (path[0] == '/' || path[0] == '.') {
    return ZX_ERR_INVALID_ARGS;
  }

  int fd, fwfd;
  for (unsigned n = 0; n < fbl::count_of(fwdirs); n++) {
    if ((fd = open(fwdirs[n], O_RDONLY, O_DIRECTORY)) < 0) {
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

  // search components of composite devices
  if (test->composite()) {
    for (auto& component : test->composite()->bound_components()) {
      auto dev = component.bound_device();
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
    // Adding metadata to arbitrary paths is restricted to drivers running in the sys devhost.
    while (itr && itr != sys_device_) {
      if (itr->proxy()) {
        // this device is in a child devhost
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

// send message to devhost, requesting the creation of a device
static zx_status_t dh_create_device(const fbl::RefPtr<Device>& dev, Devhost* dh, const char* args,
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
  dh->devices().push_back(dev.get());
  return ZX_OK;
}

// send message to devhost, requesting the binding of a driver to a device
static zx_status_t dh_bind_driver(const fbl::RefPtr<Device>& dev, const char* libname) {
  zx::vmo vmo;
  zx_status_t status = dev->coordinator->LibnameToVmo(libname, &vmo);
  if (status != ZX_OK) {
    return status;
  }
  status = dh_send_bind_driver(
      dev.get(), libname, std::move(vmo), [dev](zx_status_t status, zx::channel test_output) {
        if (status != ZX_OK) {
          log(ERROR, "devcoordinator: rpc: bind-driver '%s' status %d\n", dev->name().data(),
              status);
          return;
        }
        fbl::RefPtr<Device> real_parent;
        if (dev->flags & DEV_CTX_PROXY) {
          real_parent = dev->parent();
        } else {
          real_parent = dev;
        }
        for (auto& child : real_parent->children()) {
          char bootarg[256] = {0};
          const char* drivername =
              dev->coordinator->LibnameToDriver(child.libname().data())->name.data();
          snprintf(bootarg, sizeof(bootarg), "driver.%s.compatibility-tests-enable", drivername);

          if (dev->coordinator->boot_args().GetBool(bootarg, false) &&
              (real_parent->test_state() == Device::TestStateMachine::kTestNotStarted)) {
            snprintf(bootarg, sizeof(bootarg), "driver.%s.compatibility-tests-wait-time",
                     drivername);
            const char* test_timeout = dev->coordinator->boot_args().Get(bootarg);
            zx::duration test_time =
                (test_timeout != nullptr ? zx::msec(atoi(test_timeout)) : kDefaultTestTimeout);
            real_parent->set_test_time(test_time);
            real_parent->DriverCompatibiltyTest();
            break;
          } else if (real_parent->test_state() == Device::TestStateMachine::kTestBindSent) {
            real_parent->test_event().signal(0, TEST_BIND_DONE_SIGNAL);
            break;
          }
        }
        if (test_output.is_valid()) {
          log(ERROR, "devcoordinator: rpc: bind-driver '%s' set test channel\n",
              dev->name().data());
          status = dev->set_test_output(std::move(test_output), dev->coordinator->dispatcher());
          if (status != ZX_OK) {
            log(ERROR,
                "devcoordinator: rpc: bind-driver '%s' failed to start test output wait: %d\n",
                dev->name().data(), status);
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
// has a devhost.  If |target_devhost| is not nullptr and the proxy doesn't have
// a devhost yet, |target_devhost| will be used for it.  Otherwise a new devhost
// will be created.
zx_status_t Coordinator::PrepareProxy(const fbl::RefPtr<Device>& dev, Devhost* target_devhost) {
  ZX_ASSERT(!(dev->flags & DEV_CTX_PROXY) && (dev->flags & DEV_CTX_MUST_ISOLATE));

  // proxy args are "processname,args"
  const char* arg0 = dev->args().data();
  const char* arg1 = strchr(arg0, ',');
  if (arg1 == nullptr) {
    log(ERROR, "invalid proxy args: \"processname,args\" (see also fxb/33674)\n");
    return ZX_ERR_INTERNAL;
  }
  size_t arg0len = arg1 - arg0;
  arg1++;

  char devhostname[32];
  snprintf(devhostname, sizeof(devhostname), "devhost:%.*s", (int)arg0len, arg0);

  zx_status_t r;
  if (dev->proxy() == nullptr && (r = dev->CreateProxy()) != ZX_OK) {
    log(ERROR, "devcoord: cannot create proxy device: %d\n", r);
    return r;
  }

  // if this device has no devhost, first instantiate it
  if (dev->proxy()->host() == nullptr) {
    zx::channel h0, h1;
    // the immortal root devices do not provide proxy rpc
    bool need_proxy_rpc = !(dev->flags & DEV_CTX_IMMORTAL);

    if (need_proxy_rpc || dev == sys_device_) {
      // create rpc channel for proxy device to talk to the busdev it proxys
      if ((r = zx::channel::create(0, &h0, &h1)) < 0) {
        log(ERROR, "devcoordinator: cannot create proxy rpc channel: %d\n", r);
        return r;
      }
    }
    if (target_devhost == nullptr) {
      if ((r = NewDevhost(devhostname, dev->host(), &target_devhost)) < 0) {
        log(ERROR, "devcoordinator: NewDevhost: %d\n", r);
        return r;
      }
    }

    dev->proxy()->set_host(target_devhost);
    if ((r = dh_create_device(dev->proxy(), dev->proxy()->host(), arg1, std::move(h1))) < 0) {
      log(ERROR, "devcoordinator: dh_create_device: %d\n", r);
      return r;
    }
    if (need_proxy_rpc) {
      if ((r = dh_send_connect_proxy(dev.get(), std::move(h0))) < 0) {
        log(ERROR, "devcoordinator: dh_send_connect_proxy: %d\n", r);
      }
    }
    if (dev == sys_device_) {
      if ((r = fdio_service_connect(kItemsPath, h0.release())) != ZX_OK) {
        log(ERROR, "devcoordinator: fdio_service_connect %s: %d\n", kItemsPath, r);
      }
    }
    zx::channel client_remote = dev->take_client_remote();
    if (client_remote.is_valid()) {
      if ((r = devfs_connect(dev->proxy().get(), std::move(client_remote))) != ZX_OK) {
        log(ERROR, "devcoordinator: devfs_connnect: %d\n", r);
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
      log(ERROR, "devcoordinator: can't bind to device without devhost\n");
      return ZX_ERR_BAD_STATE;
    }
    return dh_bind_driver(dev, drv->libname.c_str());
  }

  zx_status_t r;
  if ((r = PrepareProxy(dev, nullptr /* target_devhost */)) < 0) {
    return r;
  }

  r = dh_bind_driver(dev->proxy(), drv->libname.c_str());
  // TODO(swetland): arrange to mark us unbound when the proxy (or its devhost) goes away
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
        log(ERROR, "devcoordinator: devfs_connnect: %d\n", status);
      }
    }
  }
  // TODO(tesienbe): We probably should do something with the return value
  // from this...
  BindDevice(dev, fbl::StringPiece("") /* autobind */, true /* new device */);
}

static void dump_suspend_task_dependencies(const SuspendTask& task, int depth = 0) {
  const char* task_status = "";
  if (task.is_completed()) {
    task_status = zx_status_get_string(task.status());
  } else {
    bool dependence = false;
    for (const auto* dependency : task.Dependencies()) {
      if (!dependency->is_completed()) {
        dependence = true;
        break;
      }
    }
    task_status = dependence ? "<dependence>" : "Stuck <suspending>";
  }
  log(INFO, "%sSuspend %s: %s\n", fbl::String(2 * depth, ' ').data(), task.device().name().data(),
      task_status);
  if (!strcmp(task_status, "Stuck <suspending>")) {
    zx_koid_t pid = task.device().host()->koid();
    if (!pid) {
      return;
    }
    zx::unowned_process process = task.device().host()->proc();
    char process_name[ZX_MAX_NAME_LEN];
    zx_status_t status = process->get_property(ZX_PROP_NAME, process_name, sizeof(process_name));
    if (status != ZX_OK) {
      strlcpy(process_name, "unknown", sizeof(process_name));
    }
    printf("Backtrace of threads of process %lu:%s\n", pid, process_name);
    inspector_print_debug_info_for_all_threads(stdout, process->get());
  }
  for (const auto* dependency : task.Dependencies()) {
    dump_suspend_task_dependencies(*reinterpret_cast<const SuspendTask*>(dependency), depth + 1);
  }
  fflush(stdout);
}

void Coordinator::Suspend(SuspendContext ctx, fit::function<void(zx_status_t)> callback) {
  // TODO(ravoorir) : Change later to queue the suspend when resume is in progress.
  // Similarly, when Suspend is in progress, resume should be queued. When a resume is
  // in queue, and another suspend request comes in, we should nullify the resume that
  // is in queue.
  if (InResume()) {
    return;
  }

  if ((ctx.sflags() & DEVICE_SUSPEND_REASON_MASK) != DEVICE_SUSPEND_FLAG_SUSPEND_RAM) {
    vfs_exit(fshost_event());
  }

  // The sys device should have a proxy. If not, the system hasn't fully initialized yet and
  // cannot go to suspend.
  if (!sys_device_->proxy()) {
    return;
  }
  if (InSuspend()) {
    return;
  }
  suspend_context() = std::move(ctx);
  auto callback_info = fbl::MakeRefCounted<SuspendCallbackInfo>(std::move(callback));

  auto completion = [this, callback_info](zx_status_t status) {
    auto& ctx = suspend_context();
    if (status != ZX_OK) {
      // TODO: unroll suspend
      // do not continue to suspend as this indicates a driver suspend
      // problem and should show as a bug
      log(ERROR, "devcoordinator: failed to suspend: %s\n", zx_status_get_string(status));
      ctx.set_flags(devmgr::SuspendContext::Flags::kRunning);
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
      ctx.set_flags(devmgr::SuspendContext::Flags::kRunning);
    }

    if (callback_info->callback) {
      callback_info->callback(ZX_OK);
      callback_info->callback = nullptr;
    }
  };
  // We don't need to suspend anything except sys_device and it's children,
  // since we do not run suspend hooks for children of test or misc
  auto task = SuspendTask::Create(sys_device(), ctx.sflags(), std::move(completion));
  suspend_context().set_task(std::move(task));

  auto status = async::PostDelayedTask(
      dispatcher(),
      [this, callback_info = std::move(callback_info)] {
        if (!InSuspend()) {
          return;  // Suspend failed to complete.
        }
        auto& ctx = suspend_context();
        log(ERROR, "devcoordinator: DEVICE SUSPEND TIMED OUT\n");
        log(ERROR, "  sflags: 0x%08x\n", ctx.sflags());
        dump_suspend_task_dependencies(ctx.task());
        if (suspend_fallback()) {
          ::suspend_fallback(root_resource(), ctx.sflags());
          // Unless in test env, we should not reach here.
          if (callback_info->callback) {
            callback_info->callback(ZX_ERR_TIMED_OUT);
            callback_info->callback = nullptr;
          }
        }
      },
      zx::sec(30));
  if (status != ZX_OK) {
    log(ERROR, "devcoordinator: Failed to create suspend timeout watchdog\n");
  }
}

void Coordinator::Resume(ResumeContext ctx, std::function<void(zx_status_t)> callback) {
  if (!sys_device_->proxy()) {
    return;
  }
  if (InSuspend()) {
    return;
  }

  resume_context() = std::move(ctx);
  for (auto& dev : devices_) {
    auto completion = [this, &dev, callback](zx_status_t status) {
      auto& ctx = resume_context();
      if (status != ZX_OK) {
        log(ERROR, "devcoordinator: failed to resume: %s\n", zx_status_get_string(status));
        ctx.set_flags(devmgr::ResumeContext::Flags::kSuspended);
        auto task = ctx.take_pending_task(dev);
        callback(status);
        return;
      }
      dev.clear_active_resume();
      std::optional<fbl::RefPtr<ResumeTask>> task = ctx.take_pending_task(dev);
      if (task.has_value()) {
        ctx.push_completed_task(std::move(task.value()));
      } else {
        // Something went wrong
        log(ERROR, "devcoordinator: failed to resume. Cant find matching pending task\n");
        callback(ZX_ERR_INTERNAL);
        return;
      }
      if (ctx.pending_tasks_is_empty()) {
        async::PostTask(dispatcher(), [this, callback] {
          resume_context().reset_completed_tasks();
          callback(ZX_OK);
        });
      }
    };
    auto task = ResumeTask::Create(fbl::RefPtr(&dev),
                                   static_cast<uint32_t>(resume_context().target_state()),
                                   std::move(completion));
    resume_context().push_pending_task(task);
    dev.SetActiveResume(std::move(task));
  }

  // Post a delayed task in case drivers do not complete the resume.
  auto status = async::PostDelayedTask(
      dispatcher(),
      [this, callback] {
        if (!InResume()) {
          return;
        }
        log(ERROR, "devcoordinator: SYSTEM RESUME TIMED OUT\n");
        callback(ZX_ERR_TIMED_OUT);
        // TODO(ravoorir): Figure out what is the best strategy
        // of for recovery here. Should we put back all devices
        // in suspend? In future, this could be more interactive
        // with the UI.
      },
      config_.resume_timeout);
  if (status != ZX_OK) {
    log(ERROR, "devcoordinator: Failure to create resume timeout watchdog\n");
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
    if (launched_first_devhost_) {
      log(ERROR,
          "%s (%s) requires ASan: cannot load after boot;"
          " consider devmgr.devhost.asan=true\n",
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
  async::PostTask(dispatcher(), [this, drv = driver.release()] {
    drivers_.push_back(drv);
    zx_status_t status = BindDriver(drv);
    if (status != ZX_OK && status != ZX_ERR_UNAVAILABLE) {
      log(ERROR, "devcoordinator: failed to bind driver '%s': %s\n", drv->name.data(),
          zx_status_get_string(status));
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

  // Record the special component driver when we see it
  if (!strcmp(driver->libname.data(), kComponentDriverPath)) {
    component_driver_ = driver.get();
    driver->never_autoselect = true;
  }

  if (version[0] == '*') {
    // fallback driver, load only if all else fails
    fallback_drivers_.push_front(driver.release());
  } else if (version[0] == '!') {
    // debugging / development hack
    // prioritize drivers with version "!..." over others
    drivers_.push_front(driver.release());
  } else {
    drivers_.push_back(driver.release());
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
  log(INFO, "devcoordinator: adding system driver '%s' '%s'\n", driver->name.data(),
      driver->libname.data());
  if (load_vmo(driver->libname.data(), &driver->dso_vmo)) {
    log(ERROR, "devcoordinator: system driver '%s' '%s' could not cache DSO\n", driver->name.data(),
        driver->libname.data());
  }
  if (version[0] == '*') {
    // de-prioritize drivers that are "fallback"
    system_drivers_.push_back(driver.release());
  } else {
    system_drivers_.push_front(driver.release());
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
    log(ERROR, "devcoordinator: failed to bind drv='%s' to dev='%s': %s\n", drv->name.data(),
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
      if (composite.TryMatchComponents(dev, &index)) {
        log(SPEW, "devcoordinator: dev='%s' matched component %zu of composite='%s'\n",
            dev->name().data(), index, composite.name().data());
        status = composite.BindComponent(index, dev);
        if (status != ZX_OK) {
          log(ERROR, "composite bind component failed\n");
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
  // This avoids deadlocks between the devhosts hosting the block devices that
  // these drivers may be served from and the devcoordinator loading them.
  thrd_t t;
  auto callback = [](void* arg) {
    auto coordinator = static_cast<Coordinator*>(arg);
    find_loadable_drivers("/system/driver",
                          fit::bind_member(coordinator, &Coordinator::DriverAddedSys));
    async::PostTask(coordinator->dispatcher(), [coordinator] { coordinator->BindSystemDrivers(); });
    return 0;
  };
  int ret = thrd_create_with_name(&t, callback, this, "system-driver-loader");
  if (ret != thrd_success) {
    log(ERROR, "devcoordinator: failed to create system driver scanning thread\n");
    return ZX_ERR_NO_RESOURCES;
  }
  thrd_detach(t);
  return ZX_OK;
}

void Coordinator::BindSystemDrivers() {
  Driver* drv;
  // Bind system drivers.
  while ((drv = system_drivers_.pop_front()) != nullptr) {
    drivers_.push_back(drv);
    zx_status_t status = BindDriver(drv);
    if (status != ZX_OK && status != ZX_ERR_UNAVAILABLE) {
      log(ERROR, "devcoordinator: failed to bind driver '%s': %s\n", drv->name.data(),
          zx_status_get_string(status));
    }
  }
  // Bind remaining fallback drivers.
  while ((drv = fallback_drivers_.pop_front()) != nullptr) {
    printf("devcoordinator: fallback driver '%s' is available\n", drv->name.data());
    drivers_.push_back(drv);
    zx_status_t status = BindDriver(drv);
    if (status != ZX_OK && status != ZX_ERR_UNAVAILABLE) {
      log(ERROR, "devcoordinator: failed to bind driver '%s': %s\n", drv->name.data(),
          zx_status_get_string(status));
    }
  }
}

void Coordinator::BindDrivers() {
  for (Driver& drv : drivers_) {
    zx_status_t status = BindDriver(&drv);
    if (status != ZX_OK && status != ZX_ERR_UNAVAILABLE) {
      log(ERROR, "devcoordinator: failed to bind driver '%s': %s\n", drv.name.data(),
          zx_status_get_string(status));
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
    power_fidl::statecontrol::Admin_Suspend_Response response;
    if (status != ZX_OK) {
      result.set_err(&status);
    } else {
      result.set_response(&response);
    }
    completer.Reply(std::move(result));
  };

  Suspend(
      SuspendContext(SuspendContext::Flags::kSuspend, GetSuspendFlagsFromSystemPowerState(state)),
      std::move(callback));
}

void Coordinator::InitOutgoingServices() {
  const auto& svc_dir = outgoing_services_.svc_dir();

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

    const auto status =
        fidl_bind(this->config_.dispatcher, request.release(),
                  reinterpret_cast<fidl_dispatch_t*>(fuchsia_device_manager_Administrator_dispatch),
                  this, &kOps);
    if (status != ZX_OK) {
      printf("Failed to bind to client channel: %d \n", status);
    }
    return status;
  };
  svc_dir->AddEntry(fuchsia_device_manager_Administrator_Name,
                    fbl::MakeRefCounted<fs::Service>(admin));

  const auto admin2 = [this](zx::channel request) {
    auto status = fidl::Bind(this->config_.dispatcher, std::move(request), this);
    if (status != ZX_OK) {
      printf("Failed to bind to client channel: %d \n", status);
    }
    return status;
  };

  svc_dir->AddEntry(power_fidl::statecontrol::Admin::Name,
                    fbl::MakeRefCounted<fs::Service>(admin2));

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
        fidl_bind(this->config_.dispatcher, request.release(),
                  reinterpret_cast<fidl_dispatch_t*>(fuchsia_device_manager_DebugDumper_dispatch),
                  this, &kOps);
    if (status != ZX_OK) {
      printf("Failed to bind to client channel: %d \n", status);
    }
    return status;
  };
  svc_dir->AddEntry(fuchsia_device_manager_DebugDumper_Name,
                    fbl::MakeRefCounted<fs::Service>(debug));
}  // namespace devmgr

void Coordinator::OnOOMEvent(async_dispatcher_t* dispatcher, async::WaitBase* wait,
                             zx_status_t status, const zx_packet_signal_t* signal) {
  vfs_exit(fshost_event());
}

zx_status_t Coordinator::BindOutgoingServices(zx::channel listen_on) {
  return outgoing_services_.Serve(std::move(listen_on));
}

}  // namespace devmgr
