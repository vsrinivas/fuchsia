// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_ZXDUMP_PIPED_COMMAND_H_
#define SRC_LIB_ZXDUMP_PIPED_COMMAND_H_

#include <lib/fit/result.h>
#include <lib/stdcompat/span.h>

#include <map>
#include <string>
#include <utility>
#include <vector>

#include <fbl/unique_fd.h>

#ifdef __Fuchsia__
#include <lib/fdio/spawn.h>
#include <lib/zx/process.h>
#endif

namespace zxdump {

// This handles a spawned subprocess with file descriptor redirection,
// usually redirected to pipes (hence the name).
class PipedCommand {
 public:
  PipedCommand() = default;
  PipedCommand(const PipedCommand&) = delete;
  PipedCommand(PipedCommand&&) = default;

  ~PipedCommand();

  // Set up redirections for when the command is launched.
  void Redirect(int number, fbl::unique_fd fd) { redirect_.emplace(number, std::move(fd)); }

#ifdef __Fuchsia__
  // Call this before Start to change the fdio_spawn details.
  void SetSpawnActions(uint32_t flags, std::vector<fdio_spawn_action_t> actions) {
    spawn_flags_ = flags;
    spawn_actions_ = std::move(actions);
  }
#endif

  // Start the command running with argv {command, args...}.
  fit::result<std::string> Start(const std::string& command, const std::vector<std::string>& args);

  // Once the command is started, the destructor will wait for it to finish
  // unless std::move(obj).process() takes ownership.  Note, it's best to close
  // any file descriptors to pipes whose other ends were passed into Redirect
  // before the process is destroyed in case it blocks on them.
#ifdef __Fuchsia__
  const zx::process& process() const& { return process_; }
  zx::process process() && { return std::move(process_); }
#else
  int process() const& { return pid_; }
  int process() && { return std::exchange(pid_, -1); }
#endif

 private:
  static std::vector<const char*> MakeArgv(const std::string& command,
                                           const std::vector<std::string>& args);
  fit::result<std::string> StartArgv(cpp20::span<const char*> argv);
#ifdef __Fuchsia__
  fit::result<std::string> StartArgv(cpp20::span<const char*> argv, uint32_t flags,
                                     std::vector<fdio_spawn_action_t> actions);
#endif

  std::map<int, fbl::unique_fd> redirect_;
#ifdef __Fuchsia__
  zx::process process_;
  uint32_t spawn_flags_ = FDIO_SPAWN_CLONE_ALL;
  std::vector<fdio_spawn_action_t> spawn_actions_;
#else
  int pid_ = -1;
#endif
};

}  // namespace zxdump

#endif  // SRC_LIB_ZXDUMP_PIPED_COMMAND_H_
