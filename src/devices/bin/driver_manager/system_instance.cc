// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "system_instance.h"

#include <dirent.h>
#include <fcntl.h>
#include <fuchsia/boot/llcpp/fidl.h>
#include <fuchsia/hardware/virtioconsole/llcpp/fidl.h>
#include <fuchsia/power/manager/llcpp/fidl.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/spawn.h>
#include <lib/fdio/unsafe.h>
#include <lib/fdio/watcher.h>
#include <lib/zircon-internal/paths.h>
#include <lib/zx/debuglog.h>
#include <sys/stat.h>
#include <unistd.h>
#include <zircon/boot/image.h>
#include <zircon/errors.h>
#include <zircon/status.h>
#include <zircon/syscalls/log.h>
#include <zircon/syscalls/policy.h>

#include <fbl/string_printf.h>
#include <fbl/unique_fd.h>
#include <fbl/vector.h>

#include "devfs.h"
#include "fdio.h"
#include "src/devices/lib/log/log.h"
#include "system_state_manager.h"

struct ConsoleStarterArgs {
  SystemInstance* instance;
  llcpp::fuchsia::boot::Arguments::SyncClient* boot_args;
};

struct ServiceStarterArgs {
  SystemInstance* instance;
  Coordinator* coordinator;
};

struct ServiceStarterParams {
  std::string netsvc_interface;
  std::string clock_backstop;
  bool netsvc_disable = true;
  bool netsvc_netboot = false;
  bool netsvc_advertise = true;
  bool netsvc_all_features = false;
  bool virtcon_disable = false;
};

struct ConsoleParams {
  std::string term = "TERM=";
  std::string device = "/svc/console";
  bool valid = false;
};

ServiceStarterParams GetServiceStarterParams(llcpp::fuchsia::boot::Arguments::SyncClient* client) {
  fidl::StringView string_keys[]{
      "netsvc.interface",
      "clock.backstop",
  };

  auto string_resp = client->GetStrings(fidl::unowned_vec(string_keys));
  ServiceStarterParams ret;
  if (string_resp.ok()) {
    auto& values = string_resp->values;
    ret.netsvc_interface = std::string{values[0].data(), values[0].size()};
    ret.clock_backstop = std::string{values[1].data(), values[1].size()};
  }

  llcpp::fuchsia::boot::BoolPair bool_keys[]{
      {"netsvc.disable", true},       {"netsvc.netboot", false},  {"netsvc.advertise", true},
      {"netsvc.all-features", false}, {"virtcon.disable", false},
  };

  auto bool_resp = client->GetBools(fidl::unowned_vec(bool_keys));
  if (bool_resp.ok()) {
    ret.netsvc_disable = bool_resp->values[0];
    ret.netsvc_netboot = bool_resp->values[1];
    ret.netsvc_advertise = bool_resp->values[2];
    ret.netsvc_all_features = bool_resp->values[3];
    ret.virtcon_disable = bool_resp->values[4];
  }

  return ret;
}

ConsoleParams GetConsoleParams(llcpp::fuchsia::boot::Arguments::SyncClient* client) {
  fidl::StringView vars[]{"TERM", "console.path"};
  auto resp = client->GetStrings(fidl::unowned_vec(vars));
  ConsoleParams ret;
  if (!resp.ok()) {
    return ret;
  }

  if (resp->values[0].is_null()) {
    ret.term += "uart";
  } else {
    ret.term += std::string{resp->values[0].data(), resp->values[0].size()};
  }
  if (!resp->values[1].is_null()) {
    ret.device = std::string{resp->values[1].data(), resp->values[1].size()};
  }

  ret.valid = true;
  return ret;
}

// Wait for the requested file.  Its parent directory must exist.
zx_status_t wait_for_file(const char* path, zx::time deadline) {
  char path_copy[PATH_MAX];
  if (strlen(path) >= PATH_MAX) {
    return ZX_ERR_INVALID_ARGS;
  }
  strcpy(path_copy, path);

  char* last_slash = strrchr(path_copy, '/');
  // Waiting on the root of the fs or paths with no slashes is not supported by this function
  if (last_slash == path_copy || last_slash == nullptr) {
    return ZX_ERR_NOT_SUPPORTED;
  }
  last_slash[0] = 0;
  char* dirname = path_copy;
  char* basename = last_slash + 1;

  auto watch_func = [](int dirfd, int event, const char* fn, void* cookie) -> zx_status_t {
    auto basename = static_cast<const char*>(cookie);
    if (event != WATCH_EVENT_ADD_FILE) {
      return ZX_OK;
    }
    if (!strcmp(fn, basename)) {
      return ZX_ERR_STOP;
    }
    return ZX_OK;
  };

  fbl::unique_fd dirfd(open(dirname, O_RDONLY));
  if (!dirfd.is_valid()) {
    return ZX_ERR_INVALID_ARGS;
  }
  zx_status_t status = fdio_watch_directory(dirfd.get(), watch_func, deadline.get(),
                                            reinterpret_cast<void*>(basename));
  if (status == ZX_ERR_STOP) {
    return ZX_OK;
  }
  return status;
}

SystemInstance::SystemInstance() : launcher_(this) {}

zx_status_t SystemInstance::CreateSvcJob(const zx::job& root_job) {
  zx::job svc_job;
  zx_status_t status = zx::job::create(root_job, 0u, &svc_job);
  if (status != ZX_OK) {
    LOGF(ERROR, "Failed to create svc_job: %s", zx_status_get_string(status));
    return status;
  }
  // TODO(fxbug.dev/53125): This currently manually restricts AMBIENT_MARK_VMO_EXEC and NEW_PROCESS
  // since this job is created from the root job. The processes spawned under this job will
  // eventually move out of driver_manager into their own components.
  static const zx_policy_basic_v2_t policy[] = {
      {ZX_POL_AMBIENT_MARK_VMO_EXEC, ZX_POL_ACTION_DENY, ZX_POL_OVERRIDE_DENY},
      {ZX_POL_NEW_PROCESS, ZX_POL_ACTION_DENY, ZX_POL_OVERRIDE_DENY}};
  status = svc_job.set_policy(ZX_JOB_POL_RELATIVE, ZX_JOB_POL_BASIC_V2, &policy, std::size(policy));
  if (status != ZX_OK) {
    LOGF(ERROR, "Failed to set svc_job job policy: %s", zx_status_get_string(status));
    return status;
  }
  status = svc_job.set_property(ZX_PROP_NAME, "zircon-services", 16);
  if (status != ZX_OK) {
    LOGF(ERROR, "Failed to set svc_job job name: %s", zx_status_get_string(status));
    return status;
  }

  // Set member variable now that we're sure all job creation steps succeeded.
  svc_job_ = std::move(svc_job);
  return ZX_OK;
}

zx_status_t SystemInstance::CreateDriverHostJob(const zx::job& root_job,
                                                zx::job* driver_host_job_out) {
  zx::job driver_host_job;
  zx_status_t status = zx::job::create(root_job, 0u, &driver_host_job);
  if (status != ZX_OK) {
    LOGF(ERROR, "Unable to create driver_host job: %s", zx_status_get_string(status));
    return status;
  }
  // TODO(fxbug.dev/53125): This currently manually restricts AMBIENT_MARK_VMO_EXEC and NEW_PROCESS
  // since this job is created from the root job. The driver_host job should move to being created
  // from something other than the root job. (Although note that it can't simply be created from
  // driver_manager's own job, because that has timer slack job policy automatically applied by the
  // ELF runner.)
  static const zx_policy_basic_v2_t policy[] = {
      {ZX_POL_BAD_HANDLE, ZX_POL_ACTION_ALLOW_EXCEPTION, ZX_POL_OVERRIDE_DENY},
      {ZX_POL_AMBIENT_MARK_VMO_EXEC, ZX_POL_ACTION_DENY, ZX_POL_OVERRIDE_DENY},
      {ZX_POL_NEW_PROCESS, ZX_POL_ACTION_DENY, ZX_POL_OVERRIDE_DENY}};
  status = driver_host_job.set_policy(ZX_JOB_POL_RELATIVE, ZX_JOB_POL_BASIC_V2, &policy,
                                      std::size(policy));
  if (status != ZX_OK) {
    LOGF(ERROR, "Failed to set driver_host job policy: %s", zx_status_get_string(status));
    return status;
  }
  status = driver_host_job.set_property(ZX_PROP_NAME, "zircon-drivers", 15);
  if (status != ZX_OK) {
    LOGF(ERROR, "Failed to set driver_host job property: %s", zx_status_get_string(status));
    return status;
  }
  *driver_host_job_out = std::move(driver_host_job);
  return ZX_OK;
}

zx_status_t SystemInstance::StartSvchost(const zx::job& root_job, const zx::channel& root_dir,
                                         bool require_system, Coordinator* coordinator) {
  zx::channel dir_request, svchost_local;
  zx_status_t status = zx::channel::create(0, &dir_request, &svchost_local);
  if (status != ZX_OK) {
    return status;
  }

  zx::debuglog logger;
  status = zx::debuglog::create(coordinator->root_resource(), 0, &logger);
  if (status != ZX_OK) {
    return status;
  }

  zx::job root_job_copy;
  status =
      root_job.duplicate(ZX_RIGHTS_BASIC | ZX_RIGHTS_IO | ZX_RIGHTS_PROPERTY | ZX_RIGHT_ENUMERATE |
                             ZX_RIGHT_MANAGE_PROCESS | ZX_RIGHT_MANAGE_THREAD,
                         &root_job_copy);
  if (status != ZX_OK) {
    return status;
  }

  // TODO(fxbug.dev/33324): svchost needs the root resource to talk to
  // zx_debug_send_command. Remove this once zx_debug_send_command no longer
  // requires the root resource.
  zx::resource root_resource_copy;
  if (coordinator->root_resource().is_valid()) {
    status = coordinator->root_resource().duplicate(ZX_RIGHT_TRANSFER, &root_resource_copy);
    if (status != ZX_OK) {
      return status;
    }
  }

  zx::channel coordinator_client;
  {
    zx::channel coordinator_server;
    status = zx::channel::create(0, &coordinator_server, &coordinator_client);
    if (status != ZX_OK) {
      return status;
    }

    status = fdio_service_connect_at(root_dir.get(), "svc", coordinator_server.release());
    if (status != ZX_OK) {
      return status;
    }
  }

  zx::channel virtcon_client;
  {
    zx::channel virtcon_client_remote;
    status = zx::channel::create(0, &virtcon_client, &virtcon_client_remote);
    if (status != ZX_OK) {
      printf("Unable to create virtcon channel.\n");
      return status;
    }

    zx::channel virtcon_svc;
    status = zx::channel::create(0, &virtcon_svc, &virtcon_fidl_);
    if (status != ZX_OK) {
      printf("Unable to create virtcon channel.\n");
      return status;
    }

    // XXX: Investigate how virtcon_svc leaves scope and gets closed.
    status = fdio_service_connect_at(virtcon_svc.get(), "svc", virtcon_client_remote.release());
    if (status != ZX_OK) {
      printf("Unable to connect to virtcon channel.\n");
      return status;
    }
  }

  zx::channel devcoordinator_svc;
  {
    zx::channel devcoordinator_svc_req;
    status = zx::channel::create(0, &devcoordinator_svc_req, &devcoordinator_svc);
    if (status != ZX_OK) {
      return status;
    }

    // This connects to the /svc in devcoordinator's namespace.
    status = fdio_service_connect("/svc", devcoordinator_svc_req.release());
    if (status != ZX_OK) {
      return status;
    }
  }

  zx::job svc_job_copy;
  status = svc_job_.duplicate(ZX_RIGHTS_BASIC | ZX_RIGHT_MANAGE_JOB | ZX_RIGHT_MANAGE_PROCESS,
                              &svc_job_copy);
  if (status != ZX_OK) {
    return status;
  }

  const char* name = "svchost";
  const char* argv[3] = {
      "/boot/bin/svchost",
      require_system ? "--require-system" : nullptr,
      nullptr,
  };

  fbl::Vector<fdio_spawn_action_t> actions;

  actions.push_back((fdio_spawn_action_t){
      .action = FDIO_SPAWN_ACTION_SET_NAME,
      .name = {.data = name},
  });

  actions.push_back((fdio_spawn_action_t){
      .action = FDIO_SPAWN_ACTION_ADD_HANDLE,
      .h = {.id = PA_DIRECTORY_REQUEST, .handle = dir_request.release()},
  });
  actions.push_back((fdio_spawn_action_t){
      .action = FDIO_SPAWN_ACTION_ADD_HANDLE,
      .h = {.id = PA_HND(PA_FD, FDIO_FLAG_USE_FOR_STDIO), .handle = logger.release()},
  });

  // Give svchost a restricted root job handle. svchost is already a privileged system service
  // as it controls system-wide process launching. With the root job it can consolidate a few
  // services such as crashsvc and the profile service.
  actions.push_back((fdio_spawn_action_t){
      .action = FDIO_SPAWN_ACTION_ADD_HANDLE,
      .h = {.id = PA_HND(PA_USER0, 1), .handle = root_job_copy.release()},
  });

  // Also give svchost a restricted root resource handle, this allows it to run the kernel-debug
  // service.
  if (root_resource_copy.is_valid()) {
    actions.push_back((fdio_spawn_action_t){
        .action = FDIO_SPAWN_ACTION_ADD_HANDLE,
        .h = {.id = PA_HND(PA_USER0, 2), .handle = root_resource_copy.release()},
    });
  }

  // Add handle to channel to allow svchost to proxy fidl services to us.
  actions.push_back((fdio_spawn_action_t){
      .action = FDIO_SPAWN_ACTION_ADD_HANDLE,
      .h = {.id = PA_HND(PA_USER0, 3), .handle = coordinator_client.release()},
  });

  auto resp = coordinator->boot_args()->GetBool(fidl::StringView{"virtcon.disable"}, false);
  if (resp.ok() && !resp->value) {
    // Add handle to channel to allow svchost to proxy fidl services to
    // virtcon.
    actions.push_back((fdio_spawn_action_t){
        .action = FDIO_SPAWN_ACTION_ADD_HANDLE,
        .h = {.id = PA_HND(PA_USER0, 5), .handle = virtcon_client.release()},
    });
  }

  // Add handle to channel to allow svchost to connect to services from devcoordinator's /svc, which
  // is hosted by fragment_manager and includes services routed from other fragments; see
  // "devcoordinator.cml".
  actions.push_back((fdio_spawn_action_t){
      .action = FDIO_SPAWN_ACTION_ADD_HANDLE,
      .h = {.id = PA_HND(PA_USER0, 7), .handle = devcoordinator_svc.release()},
  });

  // Give svchost access to /dev/class/sysmem, to enable svchost to forward sysmem service
  // requests to the sysmem driver.  Create a namespace containing /dev/class/sysmem.
  zx::channel fs_handle = CloneFs("dev/class/sysmem");
  if (!fs_handle.is_valid()) {
    LOGF(ERROR, "Failed to clone '/dev/class/sysmem'");
    return ZX_ERR_BAD_STATE;
  }
  actions.push_back((fdio_spawn_action_t){
      .action = FDIO_SPAWN_ACTION_ADD_NS_ENTRY,
      .ns = {.prefix = "/sysmem", .handle = fs_handle.release()},
  });

  uint32_t spawn_flags =
      FDIO_SPAWN_CLONE_JOB | FDIO_SPAWN_DEFAULT_LDSVC | FDIO_SPAWN_CLONE_UTC_CLOCK;

  char errmsg[FDIO_SPAWN_ERR_MSG_MAX_LENGTH];
  zx_handle_t proc = ZX_HANDLE_INVALID;
  status = fdio_spawn_etc(svc_job_copy.get(), spawn_flags, argv[0], argv, NULL, actions.size(),
                          actions.data(), &proc, errmsg);
  if (status != ZX_OK) {
    LOGF(ERROR, "Failed to launch %s (%s): %s", argv[0], name, errmsg);
    return status;
  } else {
    LOGF(INFO, "Launching %s (%s)", argv[0], name);
  }

  zx::channel svchost_public_remote;
  status = zx::channel::create(0, &svchost_public_remote, &svchost_outgoing_);
  if (status != ZX_OK) {
    return status;
  }

  return fdio_service_connect_at(svchost_local.get(), "svc", svchost_public_remote.release());
}

zx_status_t SystemInstance::ReuseExistingSvchost() {
  // This path is only used in integration tests that start an "isolated" devmgr/devcoordinator.
  // Rather than start another svchost process - which won't work for a couple reasons - we
  // clone the /svc in devcoordinator's namespace when devcoordinator launches other processes.
  // This may or may not work well, depending on the services those processes require and whether
  // they happen to be in the /svc exposed to this test instance of devcoordinator.
  // TODO(bryanhenry): This can go away once we move the processes devcoordinator spawns today out
  // into separate fragments.
  zx::channel dir_request;
  zx_status_t status = zx::channel::create(0, &dir_request, &svchost_outgoing_);
  if (status != ZX_OK) {
    return status;
  }
  status = fdio_service_connect("/svc", dir_request.release());
  if (status != ZX_OK) {
    LOGF(ERROR, "Failed to connect to '/svc': %s", zx_status_get_string(status));
    return status;
  }

  return ZX_OK;
}

void SystemInstance::devmgr_vfs_init() {
  fdio_ns_t* ns;
  zx_status_t r;
  r = fdio_ns_get_installed(&ns);
  ZX_ASSERT_MSG(r == ZX_OK, "driver_manager: cannot get namespace: %s\n", zx_status_get_string(r));
  r = fdio_ns_bind(ns, "/dev", CloneFs("dev").release());
  ZX_ASSERT_MSG(r == ZX_OK, "driver_manager: cannot bind /dev to namespace: %s\n",
                zx_status_get_string(r));
}

// Thread trampoline for ServiceStarter
int service_starter(void* arg) {
  auto args = std::unique_ptr<ServiceStarterArgs>(static_cast<ServiceStarterArgs*>(arg));
  return args->instance->ServiceStarter(args->coordinator);
}

void SystemInstance::start_services(Coordinator& coordinator) {
  thrd_t t;
  auto service_starter_args = std::make_unique<ServiceStarterArgs>();
  service_starter_args->instance = this;
  service_starter_args->coordinator = &coordinator;
  int ret =
      thrd_create_with_name(&t, service_starter, service_starter_args.release(), "service-starter");
  if (ret == thrd_success) {
    thrd_detach(t);
  }
}

// Thread trampoline for WaitForSystemAvailable, which ServiceStarter spawns
int wait_for_system_available(void* arg) {
  auto args = std::unique_ptr<ServiceStarterArgs>(static_cast<ServiceStarterArgs*>(arg));
  return args->instance->WaitForSystemAvailable(args->coordinator);
}

int SystemInstance::ServiceStarter(Coordinator* coordinator) {
  bool netboot = false;
  bool vruncmd = false;

  auto params = GetServiceStarterParams(coordinator->boot_args());
  const char* interface =
      params.netsvc_interface.empty() ? nullptr : params.netsvc_interface.data();

  if (!params.netsvc_disable && !coordinator->disable_netsvc()) {
    const char* args[] = {"/boot/bin/netsvc", nullptr, nullptr, nullptr, nullptr, nullptr};
    int argc = 1;

    if (params.netsvc_netboot) {
      args[argc++] = "--netboot";
      netboot = true;
      vruncmd = true;
    }

    if (params.netsvc_advertise) {
      args[argc++] = "--advertise";
    }

    if (params.netsvc_all_features) {
      args[argc++] = "--all-features";
    }

    if (interface != nullptr) {
      args[argc++] = "--interface";
      args[argc++] = interface;
    }

    zx_status_t status =
        launcher_.Launch(svc_job_, "netsvc", args, nullptr, -1, coordinator->root_resource(),
                         nullptr, nullptr, 0, nullptr, FS_ALL);
    if (status != ZX_OK) {
      vruncmd = false;
    }
  }

  if (!params.virtcon_disable) {
    // pass virtcon.* options along
    fbl::Vector<const char*> env;
    std::vector<std::string> strings;
    auto resp = coordinator->boot_args()->Collect(fidl::StringView{"virtcon."});
    if (!resp.ok()) {
      return resp.status();
    }
    for (auto& v : resp->results) {
      strings.emplace_back(v.data(), v.size());
      env.push_back(strings.back().data());
    }
    env.push_back(nullptr);

    const char* num_shells = coordinator->require_system() && !netboot ? "0" : "3";
    size_t handle_count = 0;
    zx_handle_t handles[2];
    uint32_t types[2];

    handles[handle_count] = virtcon_fidl_.release();
    types[handle_count] = PA_DIRECTORY_REQUEST;
    ++handle_count;

    zx::debuglog debuglog;
    zx_status_t status =
        zx::debuglog::create(coordinator->root_resource(), ZX_LOG_FLAG_READABLE, &debuglog);
    if (status == ZX_OK) {
      handles[handle_count] = debuglog.release();
      types[handle_count] = PA_HND(PA_USER0, 1);
      ++handle_count;
    }

    const char* args[] = {
        "/boot/bin/virtual-console", "--shells", num_shells, nullptr, nullptr, nullptr};
    if (vruncmd) {
      args[3] = "--run";
      args[4] = "dlog -f -t";
    }
    launcher_.Launch(svc_job_, "virtual-console", args, env.data(), -1,
                     coordinator->root_resource(), handles, types, handle_count, nullptr, FS_ALL);
  }

  if (!params.clock_backstop.empty()) {
    auto offset = zx::sec(atoi(params.clock_backstop.data()));
    zx_status_t status =
        zx_clock_adjust(coordinator->root_resource().get(), ZX_CLOCK_UTC, offset.get());
    if (status != ZX_OK) {
      LOGF(ERROR, "Failed to set UTC backstop: %s", zx_status_get_string(status));
    } else {
      LOGF(INFO, "Set UTC backstop to %ld", offset.get());
    }
  }

  auto starter_args = std::make_unique<ServiceStarterArgs>();
  starter_args->instance = this;
  starter_args->coordinator = coordinator;
  thrd_t t;
  int ret = thrd_create_with_name(&t, wait_for_system_available, starter_args.release(),
                                  "wait-for-system-available");
  if (ret == thrd_success) {
    thrd_detach(t);
  }

  return 0;
}

int SystemInstance::WaitForSystemAvailable(Coordinator* coordinator) {
  // Block this thread until /system-delayed is available. Note that this is
  // only used for coordinating events between fshost and devcoordinator, the
  // /system path is used for loading drivers and appmgr below.
  // TODO: It's pretty wasteful to create a thread just so it can sit blocked in
  // sync I/O opening '/system-delayed'. Once a simple async I/O wrapper exists
  // this should switch to use that
  int fd = open("/system-delayed", O_RDONLY);
  if (fd < 0) {
    LOGF(WARNING, "Unabled to open '/system-delayed', system drivers are disabled");
    return ZX_ERR_IO;
  }
  close(fd);

  // Load in drivers from /system
  coordinator->set_system_available(true);
  coordinator->ScanSystemDrivers();

  zx::channel system_state_transition_client, system_state_transition_server;
  zx_status_t status =
      zx::channel::create(0, &system_state_transition_client, &system_state_transition_server);
  if (status != ZX_OK) {
    return status;
  }
  std::unique_ptr<SystemStateManager> system_state_manager;
  status =
      SystemStateManager::Create(coordinator->dispatcher(), coordinator,
                                 std::move(system_state_transition_server), &system_state_manager);
  if (status != ZX_OK) {
    return status;
  }
  coordinator->set_system_state_manager(std::move(system_state_manager));
  zx::channel dev_handle = CloneFs("dev");
  zx::channel local, remote;
  status = zx::channel::create(0, &local, &remote);
  if (status != ZX_OK) {
    return status;
  }
  std::string registration_svc =
      "/svc/" + std::string(llcpp::fuchsia::power::manager::DriverManagerRegistration::Name);

  status = fdio_service_connect(registration_svc.c_str(), remote.release());
  if (status != ZX_OK) {
    LOGF(ERROR, "Failed to connect to fuchsia.power.manager: %s", zx_status_get_string(status));
  }

  status = coordinator->RegisterWithPowerManager(
      std::move(local), std::move(system_state_transition_client), std::move(dev_handle));
  if (status == ZX_OK) {
    coordinator->set_power_manager_registered(true);
  }
  return 0;
}

// TODO(fxbug.dev/34633): DEPRECATED. Do not add new dependencies on the fshost loader service!
zx_status_t SystemInstance::clone_fshost_ldsvc(zx::channel* loader) {
  zx::channel remote;
  zx_status_t status = zx::channel::create(0, loader, &remote);
  if (status != ZX_OK) {
    return status;
  }
  return fdio_service_connect("/svc/fuchsia.fshost.Loader", remote.release());
}

zx_status_t DirectoryFilter::Initialize(const zx::channel& forwarding_directory,
                                        fbl::Span<const char*> allow_filter) {
  for (const auto& name : allow_filter) {
    zx_status_t status = root_dir_->AddEntry(
        name, fbl::MakeRefCounted<fs::Service>([&forwarding_directory, name](zx::channel request) {
          return fdio_service_connect_at(forwarding_directory.get(), name, request.release());
        }));
    if (status != ZX_OK) {
      return status;
    }
  }
  return ZX_OK;
}

zx_status_t SystemInstance::InitializeDriverHostSvcDir() {
  if (driver_host_svc_) {
    return ZX_OK;
  }
  zx_status_t status = loop_.StartThread("driver_host_svc_loop");
  if (status != ZX_OK) {
    return status;
  }
  driver_host_svc_.emplace(loop_.dispatcher());
  const char* kAllowedServices[] = {
      "fuchsia.logger.LogSink",
      "fuchsia.scheduler.ProfileProvider",
      "fuchsia.tracing.provider.Registry",
  };
  return driver_host_svc_->Initialize(svchost_outgoing_, fbl::Span(kAllowedServices));
}

zx::channel SystemInstance::CloneFs(const char* path) {
  if (!strcmp(path, "dev")) {
    return devfs_root_clone();
  }
  zx::channel h0, h1;
  if (zx::channel::create(0, &h0, &h1) != ZX_OK) {
    return zx::channel();
  }
  zx_status_t status = ZX_OK;
  if (!strcmp(path, "svc")) {
    zx::unowned_channel fs = zx::unowned_channel(svchost_outgoing_);
    status = fdio_service_clone_to(fs->get(), h1.release());
  } else if (!strcmp(path, "driver_host_svc")) {
    status = InitializeDriverHostSvcDir();
    if (status == ZX_OK) {
      status = driver_host_svc_->Serve(std::move(h1));
    }
  } else if (!strncmp(path, "dev/", 4)) {
    zx::unowned_channel fs = devfs_root_borrow();
    path += 4;
    status = fdio_open_at(fs->get(), path, FS_READ_WRITE_DIR_FLAGS, h1.release());
  }
  if (status != ZX_OK) {
    LOGF(ERROR, "CloneFs failed for '%s': %s", path, zx_status_get_string(status));
    return zx::channel();
  }
  return h0;
}
