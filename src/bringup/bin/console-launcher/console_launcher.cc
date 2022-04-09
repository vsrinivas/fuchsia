// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/bringup/bin/console-launcher/console_launcher.h"

#include <fcntl.h>
#include <fidl/fuchsia.boot/cpp/wire.h>
#include <fidl/fuchsia.hardware.virtioconsole/cpp/wire.h>
#include <fidl/fuchsia.kernel/cpp/wire.h>
#include <lib/fdio/cpp/caller.h>
#include <lib/fdio/fd.h>
#include <lib/fdio/namespace.h>
#include <lib/fdio/spawn.h>
#include <lib/fdio/watcher.h>
#include <lib/fit/defer.h>
#include <lib/service/llcpp/service.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/zircon-internal/paths.h>
#include <zircon/compiler.h>
#include <zircon/status.h>

namespace console_launcher {

namespace {

// Wait for the requested file.  Its parent directory must exist.
zx_status_t WaitForFile(const char* path, zx::time deadline) {
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

}  // namespace

zx::status<ConsoleLauncher> ConsoleLauncher::Create() {
  ConsoleLauncher launcher;

  // TODO(fxbug.dev/33957): Remove all uses of the root job.
  zx::status client_end = service::Connect<fuchsia_kernel::RootJob>();
  if (client_end.is_error()) {
    FX_PLOGS(ERROR, client_end.status_value())
        << "failed to connect to " << fidl::DiscoverableProtocolName<fuchsia_kernel::RootJob>;
    return client_end.take_error();
  }
  const fidl::WireResult result = fidl::WireCall(client_end.value())->Get();
  if (!result.ok()) {
    FX_PLOGS(ERROR, result.status()) << "failed to get root job";
    return zx::error(result.status());
  }
  if (zx_status_t status = zx::job::create(result.value().job, 0u, &launcher.shell_job_);
      status != ZX_OK) {
    FX_PLOGS(ERROR, status) << "failed to create shell job";
    return zx::error(status);
  }
  constexpr char name[] = "zircon-shell";
  if (zx_status_t status = launcher.shell_job_.set_property(ZX_PROP_NAME, name, sizeof(name));
      status != ZX_OK) {
    FX_PLOGS(ERROR, status) << "failed to set shell job name";
    return zx::error(status);
  }

  return zx::ok(std::move(launcher));
}

zx::status<Arguments> GetArguments(const fidl::ClientEnd<fuchsia_boot::Arguments>& client) {
  Arguments ret;

  {
    fuchsia_boot::wire::BoolPair bool_keys[]{
        {"console.shell", false},
        {"kernel.shell", false},
        {"console.is_virtio", false},
    };
    const fidl::WireResult bool_resp = fidl::WireCall(client)->GetBools(
        fidl::VectorView<fuchsia_boot::wire::BoolPair>::FromExternal(bool_keys));
    if (!bool_resp.ok()) {
      FX_PLOGS(ERROR, bool_resp.status()) << "failed to get boot bools";
      return zx::error(bool_resp.status());
    }
    ret.run_shell = bool_resp->values[0];
    // If the kernel console is running a shell we can't launch our own shell.
    ret.run_shell = ret.run_shell && !bool_resp->values[1];
    ret.is_virtio = bool_resp->values[2];
  }

  fidl::StringView vars[]{
      "TERM",
      "console.path",
      "zircon.autorun.boot",
      "zircon.autorun.system",
  };
  const fidl::WireResult resp =
      fidl::WireCall(client)->GetStrings(fidl::VectorView<fidl::StringView>::FromExternal(vars));
  if (!resp.ok()) {
    FX_PLOGS(ERROR, resp.status()) << "failed to get boot strings";
    return zx::error(resp.status());
  }

  if (resp->values[0].is_null()) {
    ret.term += "uart";
  } else {
    ret.term += std::string{resp->values[0].data(), resp->values[0].size()};
  }
  if (!resp->values[1].is_null()) {
    ret.device = std::string{resp->values[1].data(), resp->values[1].size()};
  }
  if (!resp->values[2].is_null()) {
    ret.autorun_boot = std::string{resp->values[2].data(), resp->values[2].size()};
  }
  if (!resp->values[3].is_null()) {
    ret.autorun_system = std::string{resp->values[3].data(), resp->values[3].size()};
  }

  return zx::ok(ret);
}

zx_status_t ConsoleLauncher::LaunchShell(const Arguments& args) {
  if (zx_status_t status = WaitForFile(args.device.c_str(), zx::time::infinite());
      status != ZX_OK) {
    FX_PLOGS(ERROR, status) << "failed to wait for console '" << args.device << "'";
    return status;
  }

  fbl::unique_fd fd(open(args.device.c_str(), O_RDWR));
  if (!fd.is_valid()) {
    FX_LOGS(ERROR) << "failed to open console '" << args.device << "': " << strerror(errno);
    return ZX_ERR_INVALID_ARGS;
  }

  // If the console is a virtio connection, then speak the fuchsia.hardware.virtioconsole.Device
  // interface to get the real fuchsia.io.File connection
  //
  // TODO(fxbug.dev/33183): Clean this up once devhost stops speaking fuchsia.io.File
  // on behalf of drivers.  Once that happens, the virtio-console driver
  // should just speak that instead of this shim interface.
  if (args.is_virtio) {
    zx::status endpoints = fidl::CreateEndpoints<fuchsia_hardware_pty::Device>();
    if (endpoints.is_error()) {
      FX_PLOGS(ERROR, endpoints.status_value()) << "failed to create pty endpoints";
      return endpoints.status_value();
    }
    fdio_cpp::FdioCaller caller(std::move(fd));
    const fidl::WireResult result =
        fidl::WireCall(caller.borrow_as<fuchsia_hardware_virtioconsole::Device>())
            ->GetChannel(std::move(endpoints->server));
    if (!result.ok()) {
      FX_PLOGS(ERROR, result.status()) << "failed to get virtio console channel";
      return result.status();
    }
    if (zx_status_t status =
            fdio_fd_create(endpoints->client.TakeChannel().release(), fd.reset_and_get_address());
        status != ZX_OK) {
      FX_PLOGS(ERROR, status) << "failed to create virtio console fd";
      return status;
    }
  }

  const char* argv[] = {ZX_SHELL_DEFAULT, nullptr};
  const char* environ[] = {args.term.c_str(), nullptr};

  std::vector<fdio_spawn_action_t> actions;
  // Add an action to set the new process name.
  actions.emplace_back(fdio_spawn_action_t{
      .action = FDIO_SPAWN_ACTION_SET_NAME,
      .name =
          {
              .data = "sh:console",
          },
  });

  // Get our current namespace so we can pass it to the shell process.
  fdio_flat_namespace_t* flat = nullptr;
  if (zx_status_t status = fdio_ns_export_root(&flat); status != ZX_OK) {
    FX_PLOGS(ERROR, status) << "failed to get namespace root";
    return status;
  }
  auto free_flat = fit::defer([&flat]() { fdio_ns_free_flat_ns(flat); });

  // Go through each directory in our namespace and copy all of them except /system-delayed.
  for (size_t i = 0; i < flat->count; i++) {
    if (strcmp(flat->path[i], "/system-delayed") == 0) {
      continue;
    }
    actions.emplace_back(fdio_spawn_action_t{
        .action = FDIO_SPAWN_ACTION_ADD_NS_ENTRY,
        .ns =
            {
                .prefix = flat->path[i],
                .handle = flat->handle[i],
            },
    });
  }

  // Add an action to transfer the STDIO handle.
  actions.emplace_back(fdio_spawn_action_t{
      .action = FDIO_SPAWN_ACTION_TRANSFER_FD,
      .fd =
          {
              .local_fd = fd.release(),
              .target_fd = FDIO_FLAG_USE_FOR_STDIO,
          },
  });

  constexpr uint32_t flags =
      FDIO_SPAWN_CLONE_ALL & ~FDIO_SPAWN_CLONE_STDIO & ~FDIO_SPAWN_CLONE_NAMESPACE;

  FX_LOGS(INFO) << "launching " << argv[0] << " (" << actions[0].name.data << ")";
  char err_msg[FDIO_SPAWN_ERR_MSG_MAX_LENGTH];
  if (zx_status_t status =
          fdio_spawn_etc(shell_job_.get(), flags, argv[0], argv, environ, actions.size(),
                         actions.data(), shell_process_.reset_and_get_address(), err_msg);
      status != ZX_OK) {
    FX_PLOGS(ERROR, status) << "failed to launch console shell: " << err_msg;
    return status;
  }
  return ZX_OK;
}

zx_status_t ConsoleLauncher::WaitForShellExit() {
  if (zx_status_t status =
          shell_process_.wait_one(ZX_PROCESS_TERMINATED, zx::time::infinite(), nullptr);
      status != ZX_OK) {
    FX_PLOGS(ERROR, status) << "failed to wait for console shell termination";
    return status;
  }
  zx_info_process_t proc_info;
  if (zx_status_t status =
          shell_process_.get_info(ZX_INFO_PROCESS, &proc_info, sizeof(proc_info), nullptr, nullptr);
      status != ZX_OK) {
    FX_PLOGS(ERROR, status) << "failed to get console shell termination cause";
    return status;
  }
  const bool started = (proc_info.flags & ZX_INFO_PROCESS_FLAG_STARTED) != 0;
  const bool exited = (proc_info.flags & ZX_INFO_PROCESS_FLAG_EXITED) != 0;
  FX_LOGS(INFO) << "console shell exited (started=" << started << " exited=" << exited
                << ", return_code=" << proc_info.return_code << ")";
  return ZX_OK;
}

}  // namespace console_launcher
