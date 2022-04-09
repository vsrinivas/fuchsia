// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/bringup/bin/console-launcher/autorun.h"

#include <fcntl.h>
#include <lib/fdio/io.h>
#include <lib/fdio/spawn.h>
#include <lib/syslog/cpp/macros.h>
#include <zircon/processargs.h>
#include <zircon/status.h>

#include <array>
#include <iostream>

#include <fbl/unique_fd.h>

namespace autorun {

namespace {

// Returns the result of splitting |args| into an argument vector.
class ArgumentVector {
 public:
  static ArgumentVector FromCmdline(const char* cmdline);

  // Returns a nullptr-terminated list of arguments.  Only valid for the
  // lifetime of |this|.
  const char* const* argv() const { return argv_.data(); }

  friend std::ostream& operator<<(std::ostream& os, const ArgumentVector& av);

 private:
  ArgumentVector() = default;

  static constexpr size_t kMaxArgs = 8;
  std::array<const char*, kMaxArgs + 1> argv_;
  std::unique_ptr<char[]> raw_bytes_;
};

ArgumentVector ArgumentVector::FromCmdline(const char* cmdline) {
  ArgumentVector argv;
  const size_t cmdline_len = strlen(cmdline) + 1;
  argv.raw_bytes_.reset(new char[cmdline_len]);
  memcpy(argv.raw_bytes_.get(), cmdline, cmdline_len);

  // Get the full commandline by splitting on '+'.
  size_t argc = 0;
  char* token;
  char* rest = argv.raw_bytes_.get();
  while (argc < argv.argv_.size() && (token = strtok_r(rest, "+", &rest))) {
    argv.argv_[argc++] = token;
  }
  argv.argv_[argc] = nullptr;
  return argv;
}

std::ostream& operator<<(std::ostream& os, const ArgumentVector& av) {
  for (const char* arg : av.argv_) {
    if (arg == nullptr) {
      break;
    }
    os << "' " << arg << "'";
  }
  return os << "...\n";
}

zx_status_t WaitForSystemAvailable() {
  // Block this until /system-delayed is available.
  // fshost will not fulfill this read-request until system is available. It is used as a signalling
  // mechanism.
  fbl::unique_fd fd(open("/system-delayed", O_RDONLY));
  if (!fd) {
    FX_LOGST(ERROR, "autorun") << "failed to open /system-delayed! autorun:system won't work: "
                               << strerror(errno);
    return ZX_ERR_INTERNAL;
  }
  return ZX_OK;
}

zx_status_t Run(const char* process_name, const zx::unowned_job& job, const char* const* args,
                zx::handle stdio, zx::process* out_process) {
  fdio_spawn_action_t actions[] = {
      {
          .action = FDIO_SPAWN_ACTION_SET_NAME,
          .name =
              {
                  .data = process_name,
              },
      },
      {
          .action = FDIO_SPAWN_ACTION_ADD_HANDLE,
          .h =
              {
                  .id = PA_HND(PA_FD, FDIO_FLAG_USE_FOR_STDIO | 0),
                  .handle = stdio.release(),
              },
      },
  };

  constexpr uint32_t flags = FDIO_SPAWN_CLONE_ALL & ~FDIO_SPAWN_CLONE_STDIO;

  char err_msg[FDIO_SPAWN_ERR_MSG_MAX_LENGTH];
  zx::process process;
  zx_status_t status = fdio_spawn_etc(job->get(), flags, args[0], args, nullptr, std::size(actions),
                                      actions, process.reset_and_get_address(), err_msg);
  if (status != ZX_OK) {
    return status;
  }

  *out_process = std::move(process);
  return ZX_OK;
}

}  // namespace

zx_status_t AutoRun::SetupBootCmd(std::string cmd, const zx::job& job, zx::handle stdio) {
  boot_thread_ = std::thread([cmd = std::move(cmd), job = zx::unowned_job(job),
                              stdio = std::move(stdio)]() mutable {
    ArgumentVector args = ArgumentVector::FromCmdline(cmd.c_str());
    FX_LOGST(INFO, "autorun") << "starting" << args;

    zx::process process;
    if (zx_status_t status = Run("autorun:boot", job, args.argv(), std::move(stdio), &process);
        status != ZX_OK) {
      FX_PLOGST(ERROR, "autorun", status) << "failed to run boot cmd";
      return;
    }
    if (zx_status_t status = process.wait_one(ZX_PROCESS_TERMINATED, zx::time::infinite(), nullptr);
        status != ZX_OK) {
      FX_PLOGST(ERROR, "autorun", status) << "failed to wait for boot cmd termination";
    }
  });
  return ZX_OK;
}

zx_status_t AutoRun::SetupSystemCmd(std::string cmd, const zx::job& job, zx::handle stdio) {
  system_thread_ = std::thread([cmd = std::move(cmd), job = zx::unowned_job(job),
                                stdio = std::move(stdio)]() mutable {
    if (zx_status_t status = WaitForSystemAvailable(); status != ZX_OK) {
      FX_PLOGST(ERROR, "autorun", status) << "failed to wait for system available";
      return;
    }

    ArgumentVector args = ArgumentVector::FromCmdline(cmd.c_str());
    FX_LOGST(INFO, "autorun") << "starting" << args;

    zx::process process;
    if (zx_status_t status = Run("autorun:system", job, args.argv(), std::move(stdio), &process);
        status != ZX_OK) {
      FX_PLOGST(ERROR, "autorun", status) << "failed to run system cmd";
      return;
    }
    if (zx_status_t status = process.wait_one(ZX_PROCESS_TERMINATED, zx::time::infinite(), nullptr);
        status != ZX_OK) {
      FX_PLOGST(ERROR, "autorun", status) << "failed to wait for system cmd termination";
    }
  });
  return ZX_OK;
}

AutoRun::~AutoRun() {
  if (boot_thread_.joinable()) {
    boot_thread_.join();
  }
  if (system_thread_.joinable()) {
    system_thread_.join();
  }
}

}  // namespace autorun
