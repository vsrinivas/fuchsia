// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_SYS_FUZZING_COMMON_CHILD_PROCESS_H_
#define SRC_SYS_FUZZING_COMMON_CHILD_PROCESS_H_

#include <lib/fdio/spawn.h>
#include <lib/zx/process.h>
#include <stddef.h>
#include <stdint.h>

#include <array>
#include <condition_variable>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "src/lib/fxl/synchronization/thread_annotations.h"
#include "src/sys/fuzzing/common/async-deque.h"
#include "src/sys/fuzzing/common/async-types.h"
#include "src/sys/fuzzing/common/status.h"

namespace fuzzing {

// When spawned, cloned file descriptors will inherit from the parent process while transferred
// file descriptors will be piped to or from this class and accessible via the |WriteTo..| and
// |ReadFrom...| methods. Only takes effect on spawning.
enum FdAction : uint32_t {
  kClone = FDIO_SPAWN_ACTION_CLONE_FD,
  kTransfer = FDIO_SPAWN_ACTION_TRANSFER_FD,
};

class ChildProcess final {
 public:
  explicit ChildProcess(ExecutorPtr executor);
  ~ChildProcess();

  bool is_verbose() const { return verbose_; }
  bool is_killed() const { return killed_; }

  void set_verbose(bool verbose) { verbose_ = verbose; }

  // Adds a command line argument for the process. The first |arg| added should be the executable
  // path relative to the "/pkg" directory, i.e. the same value as might be found in
  // `program.binary` field in a component manifest.
  void AddArg(const std::string& arg);

  // Adds all of the given |args|.
  void AddArgs(std::initializer_list<const char*> args);

  // Sets an environment variable. Setting the same variable multiple times updates the value.
  void SetEnvVar(const std::string& name, const std::string& value);

  // Sets how to handle the output file descriptors. See |FdAction| above.
  void SetStdoutFdAction(FdAction action);
  void SetStderrFdAction(FdAction action);

  // Takes a |channel| to be passed as a startup channel to the child process by |Spawn|.
  void AddChannel(zx::channel channel);

  // Spawns the new child process. Returns an error if a previous process was spawned but has not
  // been |Kill|ed and |Reset|, or if spawning fails.
  __WARN_UNUSED_RESULT zx_status_t Spawn();

  // Returns whether the child process has been spawned and is running.
  bool IsAlive();

  // Return a copy of the process.
  __WARN_UNUSED_RESULT zx_status_t Duplicate(zx::process* out);

  // Writes a line to process's stdin. Returns an error if the process is not alive of if stdin has
  // been closed.
  zx_status_t WriteToStdin(const std::string& line);

  // Writes a `line` to process's stdin and closes it.
  zx_status_t WriteAndCloseStdin(const std::string& line);

  // Closes the input pipe to the spawned process.
  void CloseStdin();

  // Returns a promise to read from the process's stdout or stderr, respectively. The promise will
  // read up to a newline or EOF, whichever comes first. The promise will return an error if the
  // file descriptor is closed or was cloned, and will return |ZX_ERR_STOP| on EOF.
  ZxPromise<std::string> ReadFromStdout();
  ZxPromise<std::string> ReadFromStderr();

  // Collect process-related statistics for the child process.
  ZxResult<ProcessStats> GetStats();

  // Promises to wait for the spawned process to terminate and return its return code.
  ZxPromise<int64_t> Wait();

  // Returns a promise that kills the spawned process and waits for it to fully terminate. This
  // leaves the process in a "killed" state; it must be |Reset| before it can be reused.
  ZxPromise<> Kill();

  // Returns this object to an initial state, from which |Spawn| can be called again. This
  // effectively kills the process, but does not wait for it to fully terminate. Callers should
  // prefer to |Kill| and then |Reset|. |AddArgs|, |SetFdAction|, and/or |SetChannels| will need to
  // be called again before respawning.
  void Reset();

 private:
  // Kills the process and waits for the output threads to join.
  void KillSync();

  ExecutorPtr executor_;
  bool spawned_ = false;
  bool verbose_ = false;
  bool killed_ = false;

  // Parameters for |fdio_spawn_etc|.
  std::vector<std::string> args_;
  std::unordered_map<std::string, std::string> envvars_;
  std::vector<zx::channel> channels_;

  // The handle to the spawned process.
  zx::process process_;
  zx_info_process_t info_;

  // Variables to offload writing data to standard input to a dedicated thread.
  std::mutex mutex_;
  std::thread stdin_thread_;
  bool input_closed_ FXL_GUARDED_BY(mutex_) = false;
  std::vector<std::string> input_lines_ FXL_GUARDED_BY(mutex_);
  std::condition_variable input_cond_;

  // Variables to offload reading data from standard output to a dedicated thread.
  AsyncReceiverPtr<std::string> stdout_;
  FdAction stdout_action_ = kTransfer;
  std::thread stdout_thread_;
  zx_status_t stdout_result_ = ZX_OK;

  // Variables to offload reading data from standard error to a dedicated thread.
  AsyncReceiverPtr<std::string> stderr_;
  FdAction stderr_action_ = kTransfer;
  std::thread stderr_thread_;
  zx_status_t stderr_result_ = ZX_OK;

  Scope scope_;

  FXL_DISALLOW_COPY_ASSIGN_AND_MOVE(ChildProcess);
};

}  // namespace fuzzing

#endif  // SRC_SYS_FUZZING_COMMON_CHILD_PROCESS_H_
