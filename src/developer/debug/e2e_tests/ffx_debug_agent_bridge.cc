// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/e2e_tests/ffx_debug_agent_bridge.h"

#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include <csignal>
#include <filesystem>
#include <memory>

#include "lib/syslog/cpp/macros.h"
#include "src/developer/debug/shared/logging/logging.h"
#include "src/developer/debug/zxdb/common/err.h"

namespace zxdb {

namespace {

constexpr std::string_view kFuchsiaDeviceAddr = "FUCHSIA_DEVICE_ADDR";
constexpr std::string_view kFuchsiaSshKey = "FUCHSIA_SSH_KEY";

Err KillProcessWithSignal(pid_t pid, int signal) {
  if (kill(pid, signal) != 0) {
    const std::string s(strerror(errno));
    return Err("Failed to send signal " + std::to_string(signal) + " to child process: " + s);
  }

  int status;
  const int wait_pid = wait(&status);

  if (wait_pid == -1) {
    const std::string s(strerror(errno));
    return Err("Failed while waiting for child to terminate: " + s);
  }

  // This should be the normal case.
  if (WIFEXITED(status)) {
    return Err();
  } else if (WIFSIGNALED(status)) {
    FX_LOGS(WARNING) << "Child forced to terminate.";
    return Err();
  }

  return Err("Child exited due to an unexpected signal (" + std::string{strsignal(status)} +
             "), this is likely a bug.");
}

std::vector<char*> GetFfxArgV() {
  std::vector<char*> ffx_args = {const_cast<char*>("ffx")};

  // In infra, this environment variable is populated with the device that's been assigned to the
  // infra bot. Locally, a user can also set this to point to a specific device if they choose, but
  // `fx set-device` will also work just as well.
  char* device_addr = std::getenv(kFuchsiaDeviceAddr.data());
  if (device_addr) {
    ffx_args.push_back(const_cast<char*>("--target"));
    ffx_args.push_back(device_addr);
  }

  ffx_args.push_back(const_cast<char*>("debug"));
  ffx_args.push_back(const_cast<char*>("connect"));
  ffx_args.push_back(const_cast<char*>("--agent-only"));
  ffx_args.push_back(nullptr);  // argv must be null-terminated

  return ffx_args;
}

// The environment variable |kFuchsiaSshKey| needs to be a full path for FFX to properly resolve
// the file, but in infra, it's set to a relative path. This function expands the environment
// variable to the full path to the ssh key file, if it exists. Other environment variables are
// copied. The returned strings must be freed properly.
std::vector<char*> GetFfxEnv(char** unix_env) {
  std::vector<char*> new_env = {};

  // Duplicate the strings in the parent environment for us to manage in the child process. The
  // ownership of these strings is kept by the class and they are deallocated when this object goes
  // out of scope.
  for (size_t i = 0; unix_env[i] != nullptr; i++) {
    // Do not duplicate |kFuchsiaSshKey| because we're going to modify it before putting it
    // back into place later.
    if (strstr(unix_env[i], kFuchsiaSshKey.data()) == nullptr) {
      new_env.push_back(strdup(unix_env[i]));
    }
  }

  if (char* ssh_key_path_str = std::getenv(kFuchsiaSshKey.data())) {
    std::string ssh_key_env_var{kFuchsiaSshKey};
    ssh_key_env_var.append("=");
    ssh_key_env_var.append(std::filesystem::absolute(ssh_key_path_str).string());

    new_env.push_back(strdup(ssh_key_env_var.data()));
  }

  new_env.push_back(nullptr);
  return new_env;
}

}  // namespace

Err FfxDebugAgentBridge::Init() {
  Err e = SetupPipeAndFork(unix_env_);
  if (e.has_error()) {
    return e;
  }

  e = ReadDebugAgentSocketPath();
  if (e.has_error()) {
    return e;
  }

  return e;
}

FfxDebugAgentBridge::~FfxDebugAgentBridge() {
  socket_path_.clear();

  if (pipe_read_end_ != 0) {
    close(pipe_read_end_);
  }

  if (child_pid_ != 0) {
    Err e = CleanupChild();
    if (e.has_error()) {
      FX_LOGS(ERROR) << "Error encountered while cleaning up child: " << e.msg();
    }
  }
}

Err FfxDebugAgentBridge::SetupPipeAndFork(char** unix_env) {
  int p[2];

  if (pipe(p) < 0) {
    const std::string s(strerror(errno));
    return Err("Could not create pipe: " + s);
  }

  pipe_read_end_ = p[0];
  pipe_write_end_ = p[1];

  // HACK: try to get the ffx daemon up and running before we setup debug_agent. I'm not sure if
  // this will help.
  // for (size_t i = 0; i < 5; i++) {
  //   if (system("ffx target get-ssh-address") == 0) {
  //     break;
  //   }
  //   usleep(5000);
  // }

  const pid_t child_pid = fork();

  if (child_pid == 0) {
    close(pipe_read_end_);

    const std::filesystem::path me(program_name_);

    // In variant builds that put the test executable in a different directory (potentially
    // something like out/default/host_x64-asan/...), ffx could be in a different directory than the
    // test executable.
    const std::filesystem::path ffx_path = me.parent_path().parent_path() / "host_x64" / "ffx";

    if (!std::filesystem::exists(ffx_path)) {
      FX_LOGS(ERROR) << "Could not locate ffx binary at " << std::filesystem::absolute(ffx_path);
      FX_LOGS(ERROR) << "Note: zxdb_e2e_tests binary is at " << std::filesystem::absolute(me);
      exit(EXIT_FAILURE);
    }

    // |pipe_write_end_| will be closed along with stdout when the child program
    // terminates.
    if (dup2(pipe_write_end_, STDOUT_FILENO) < 0) {
      FX_LOGS(ERROR) << "Failed to dup child stdout to pipe write end: " << strerror(errno);
      exit(EXIT_FAILURE);
    }

    std::vector<char*> env = GetFfxEnv(unix_env);
    execve(ffx_path.c_str(), GetFfxArgV().data(), env.data());

    FX_NOTREACHED();
  } else {
    close(pipe_write_end_);
    child_pid_ = child_pid;
  }

  return Err();
}

Err FfxDebugAgentBridge::ReadDebugAgentSocketPath() {
  FILE* child_stdout = fdopen(pipe_read_end_, "r");
  if (child_stdout == nullptr) {
    const std::string s(strerror(errno));
    return Err("Failed to open pipe_read_end_ fd as FILE*: " + s);
  }

  char c;
  size_t bytes_read = fread(&c, 1, 1, child_stdout);
  while (c != '\n') {
    if (bytes_read == 0) {
      Err e;
      if (int err = feof(child_stdout); err != 0) {
        e = Err("Unexpected EOF while reading stdout from child process " + std::to_string(err));
      } else if (int err = ferror(child_stdout); err != 0) {
        e = Err("Unexpected error while reading stdout from child process " + std::to_string(err));
      } else {
        e = Err("Unknown error occurred while reading from child process, got 0 bytes from fread");
      }
      return e;
    }
    socket_path_.push_back(c);
    bytes_read = fread(&c, 1, 1, child_stdout);
  }

  fclose(child_stdout);

  // Now check to make sure this is actually a path.
  std::filesystem::path ffx_path(socket_path_);
  if (!std::filesystem::exists(ffx_path)) {
    return Err("Output of \"ffx debug connect --agent-only\" is not a valid path: " + socket_path_);
  }

  return Err();
}

Err FfxDebugAgentBridge::CleanupChild() const {
  Err e = KillProcessWithSignal(child_pid_, SIGTERM);
  if (e.has_error()) {
    FX_LOGS(WARNING) << "Failed to kill child [" << child_pid_ << "] with SIGTERM, trying SIGKILL.";
    e = KillProcessWithSignal(child_pid_, SIGKILL);
    if (e.has_error()) {
      FX_LOGS(ERROR) << "Failed to kill child with SIGKILL. There is a zombie process with pid "
                     << child_pid_;
    }
  }

  return e;
}

}  // namespace zxdb
