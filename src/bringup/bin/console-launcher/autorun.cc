// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/bringup/bin/console-launcher/autorun.h"

#include <lib/fdio/directory.h>
#include <lib/fdio/spawn.h>
#include <lib/fdio/unsafe.h>
#include <lib/fdio/watcher.h>
#include <lib/zx/debuglog.h>
#include <lib/zx/process.h>
#include <zircon/processargs.h>

#include <array>

#include <fbl/algorithm.h>
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

  void Print(const char* prefix) const;

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

void ArgumentVector::Print(const char* prefix) const {
  printf("%s: starting", prefix);
  for (const char* arg : argv_) {
    if (arg == nullptr) {
      break;
    }
    printf(" '%s'", arg);
  }
  printf("...\n");
}

zx_status_t WaitForSystemAvailable() {
  // Block this until /system-delayed is available.
  // fshost will not fufill this read-request until system is available. It is used as a signalling
  // mechanism.
  fbl::unique_fd fd(open("/system-delayed", O_RDONLY));
  if (!fd) {
    fprintf(stderr, "autorun: failed to open /system-delayed! autorun:system won't work!\n");
    return ZX_ERR_INTERNAL;
  }
  return ZX_OK;
}

zx_status_t Run(const char* process_name, const zx::unowned_job& job, const char* const* args,
                zx::handle stdio, zx::process* out_process) {
  fdio_spawn_action_t actions[2] = {};
  actions[0].action = FDIO_SPAWN_ACTION_SET_NAME;
  actions[0].name.data = process_name;
  actions[1].action = FDIO_SPAWN_ACTION_ADD_HANDLE;
  actions[1].h = {.id = PA_HND(PA_FD, FDIO_FLAG_USE_FOR_STDIO | 0), .handle = stdio.release()};

  uint32_t flags = FDIO_SPAWN_CLONE_ALL & ~FDIO_SPAWN_CLONE_STDIO;

  char err_msg[FDIO_SPAWN_ERR_MSG_MAX_LENGTH];
  zx::process process;
  zx_status_t status = fdio_spawn_etc(job->get(), flags, args[0], args, nullptr, 2, actions,
                                      process.reset_and_get_address(), err_msg);
  if (status != ZX_OK) {
    return status;
  }

  *out_process = std::move(process);
  return ZX_OK;
}

}  // namespace

zx_status_t AutoRun::SetupBootCmd(std::string cmd, const zx::job& job, zx::handle stdio) {
  boot_thread_ = std::thread(
      [cmd = std::move(cmd), job = zx::unowned_job(job), stdio = std::move(stdio)]() mutable {
        ArgumentVector args = ArgumentVector::FromCmdline(cmd.data());
        args.Print("autorun");

        zx::process process;
        zx_status_t status = Run("autorun:boot", job, args.argv(), std::move(stdio), &process);
        if (status != ZX_OK) {
          printf("autorun: running boot_cmd failed: %d", status);
          return;
        }
        status = process.wait_one(ZX_PROCESS_TERMINATED, zx::time::infinite(), nullptr);
        if (status != ZX_OK) {
          fprintf(stderr, "autorun: failed to wait for system_cmd termination (%s)\n",
                  zx_status_get_string(status));
        }
      });
  return ZX_OK;
}

zx_status_t AutoRun::SetupSystemCmd(std::string cmd, const zx::job& job, zx::handle stdio) {
  system_thread_ = std::thread(
      [cmd = std::move(cmd), job = zx::unowned_job(job), stdio = std::move(stdio)]() mutable {
        zx_status_t status = WaitForSystemAvailable();
        if (status != ZX_OK) {
          printf("autorun: WaitForSystemAvailable failed: %d", status);
          return;
        }

        ArgumentVector args = ArgumentVector::FromCmdline(cmd.data());
        args.Print("autorun");
        zx::process process;
        status = Run("autorun:system", job, args.argv(), std::move(stdio), &process);
        if (status != ZX_OK) {
          printf("autorun: running system_cmd failed: %d", status);
          return;
        }
        status = process.wait_one(ZX_PROCESS_TERMINATED, zx::time::infinite(), nullptr);
        if (status != ZX_OK) {
          fprintf(stderr, "autorun: failed to wait for system_cmd termination (%s)\n",
                  zx_status_get_string(status));
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
