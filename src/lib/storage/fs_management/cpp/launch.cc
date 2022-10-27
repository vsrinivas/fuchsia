// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/lib/storage/fs_management/cpp/launch.h"

#include <errno.h>
#include <fcntl.h>
#include <fidl/fuchsia.boot/cpp/wire.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/fd.h>
#include <lib/fdio/fdio.h>
#include <lib/fdio/io.h>
#include <lib/fdio/spawn.h>
#include <lib/sys/component/cpp/service_client.h>
#include <lib/zx/debuglog.h>
#include <lib/zx/process.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>
#include <zircon/compiler.h>
#include <zircon/processargs.h>
#include <zircon/status.h>
#include <zircon/syscalls.h>

namespace fs_management {
namespace {

namespace fboot = fuchsia_boot;

void InitActions(std::vector<std::pair<uint32_t, zx::handle>> handles,
                 fdio_spawn_action_t* actions_out) {
  for (auto& [type, handle] : handles) {
    actions_out->action = FDIO_SPAWN_ACTION_ADD_HANDLE;
    actions_out->h.id = type;
    actions_out->h.handle = handle.release();
    ++actions_out;
  }
}

constexpr size_t kMaxStdioActions = 1;

zx::result<zx::debuglog> RetriveWriteOnlyDebuglogHandle() {
  zx::result local = component::Connect<fboot::WriteOnlyLog>();
  if (local.is_error()) {
    return local.take_error();
  }

  fidl::WireResult result = fidl::WireCall(local.value())->Get();
  if (!result.ok()) {
    return zx::error(result.status());
  }

  return zx::ok(std::move(result.value().log));
}

// Initializes Stdio.
//
// If necessary, updates the |actions| which will be sent to fdio_spawn.
// |action_count| is an in/out parameter which may be increased if an action is
// added.
// |flags| is an in/out parameter which may be modified to alter the cloning of
// STDIO.
void InitStdio(const LaunchOptions& options, fdio_spawn_action_t* actions, size_t* action_count,
               uint32_t* flags) {
  switch (options.logging) {
    case LaunchOptions::Logging::kSyslog: {
      zx::result h = RetriveWriteOnlyDebuglogHandle();
      if (h.is_error()) {
        fprintf(stderr, "fs-management: Failed to retrieve WriteOnlyLog: %d (%s)\n",
                h.status_value(), h.status_string());
      } else {
        actions[*action_count].action = FDIO_SPAWN_ACTION_ADD_HANDLE;
        actions[*action_count].h.id = PA_HND(PA_FD, FDIO_FLAG_USE_FOR_STDIO);
        actions[*action_count].h.handle = h.value().release();
        *action_count += 1;
      }
      *flags &= ~FDIO_SPAWN_CLONE_STDIO;
      break;
    }
    case LaunchOptions::Logging::kStdio:
      *flags |= FDIO_SPAWN_CLONE_STDIO;
      break;
    case LaunchOptions::Logging::kSilent:
      *flags &= ~FDIO_SPAWN_CLONE_STDIO;
      break;
  }
}

// Spawns a process.
//
// Optionally blocks, waiting for the process to terminate, depending
// the value provided in |block|.
zx_status_t Spawn(const LaunchOptions& options, uint32_t flags,
                  const std::vector<std::string>& argv, size_t action_count,
                  const fdio_spawn_action_t* actions) {
  std::vector<const char*> argv_cstr;

  argv_cstr.reserve(argv.size() + 1);
  for (const std::string& arg : argv) {
    argv_cstr.push_back(arg.c_str());
  }
  argv_cstr.push_back(nullptr);

  zx::process proc;
  char err_msg[FDIO_SPAWN_ERR_MSG_MAX_LENGTH];
  zx_status_t status =
      fdio_spawn_etc(ZX_HANDLE_INVALID, flags, argv_cstr[0], argv_cstr.data(), nullptr,
                     action_count, actions, proc.reset_and_get_address(), err_msg);
  if (status != ZX_OK) {
    fprintf(stderr, "fs-management: Cannot spawn %s: %d (%s): %s\n", argv_cstr[0], status,
            zx_status_get_string(status), err_msg);
    return status;
  }

  if (options.sync) {
    status = proc.wait_one(ZX_PROCESS_TERMINATED, zx::time::infinite(), nullptr);
    if (status != ZX_OK) {
      fprintf(stderr, "spawn: Error waiting for process to terminate\n");
      return status;
    }

    zx_info_process_t info;
    status = proc.get_info(ZX_INFO_PROCESS, &info, sizeof(info), nullptr, nullptr);
    if (status != ZX_OK) {
      fprintf(stderr, "spawn: Failed to get process info\n");
      return status;
    }

    if (!(info.flags & ZX_INFO_PROCESS_FLAG_EXITED) || info.return_code != 0) {
      return ZX_ERR_BAD_STATE;
    }
  }
  return ZX_OK;
}

}  // namespace

__EXPORT
zx_status_t Launch(const std::vector<std::string>& argv,
                   std::vector<std::pair<uint32_t, zx::handle>> handles,
                   const LaunchOptions& options) {
  size_t action_count = handles.size();
  fdio_spawn_action_t actions[action_count + kMaxStdioActions];
  InitActions(std::move(handles), actions);

  uint32_t flags = FDIO_SPAWN_CLONE_ALL;
  InitStdio(options, actions, &action_count, &flags);

  return Spawn(options, flags, argv, action_count, actions);
}

__EXPORT
zx_status_t LaunchSilentSync(const std::vector<std::string>& args,
                             std::vector<std::pair<uint32_t, zx::handle>> handles) {
  return Launch(args, std::move(handles),
                LaunchOptions{.sync = true, .logging = LaunchOptions::Logging::kSilent});
}

__EXPORT
zx_status_t LaunchSilentAsync(const std::vector<std::string>& args,
                              std::vector<std::pair<uint32_t, zx::handle>> handles) {
  return Launch(args, std::move(handles),
                LaunchOptions{.sync = false, .logging = LaunchOptions::Logging::kSilent});
}

__EXPORT
zx_status_t LaunchStdioSync(const std::vector<std::string>& args,
                            std::vector<std::pair<uint32_t, zx::handle>> handles) {
  return Launch(args, std::move(handles),
                LaunchOptions{.sync = true, .logging = LaunchOptions::Logging::kStdio});
}

__EXPORT
zx_status_t LaunchStdioAsync(const std::vector<std::string>& args,
                             std::vector<std::pair<uint32_t, zx::handle>> handles) {
  return Launch(args, std::move(handles),
                LaunchOptions{.sync = false, .logging = LaunchOptions::Logging::kStdio});
}

__EXPORT
zx_status_t LaunchLogsAsync(const std::vector<std::string>& args,
                            std::vector<std::pair<uint32_t, zx::handle>> handles) {
  return Launch(args, std::move(handles),
                LaunchOptions{.sync = false, .logging = LaunchOptions::Logging::kSyslog});
}

}  // namespace fs_management
