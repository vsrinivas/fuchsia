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
#include <memory>
#include <string>
#include <vector>

#include "src/lib/fsl/tasks/fd_waiter.h"
#include "src/sys/fuzzing/common/async-types.h"

namespace fuzzing {

// When spawned, cloned file descriptors will inherit from the parent process while transferred
// file descriptors will be piped to or from this class and accessible via the |WriteTo..| and
// |ReadFrom...| methods. Only takes effect on spawning.
enum FdAction : uint32_t {
  kClone = FDIO_SPAWN_ACTION_CLONE_FD,
  kTransfer = FDIO_SPAWN_ACTION_TRANSFER_FD,
};

class ChildProcess {
 public:
  explicit ChildProcess(ExecutorPtr executor);
  ~ChildProcess() = default;

  bool is_verbose() const { return verbose_; }
  void set_verbose(bool verbose) { verbose_ = verbose; }

  // Adds a command line argument for the process. The first |arg| added should be the executable
  // path relative to the "/pkg" directory, i.e. the same value as might be found in
  // `program.binary` field in a component manifest.
  void AddArg(const std::string& arg);

  // Adds all of the given |args|.
  void AddArgs(std::initializer_list<const char*> args);

  // Sets how to handle the output file descriptors. See |FdAction| above.
  void SetStdoutFdAction(FdAction action);
  void SetStderrFdAction(FdAction action);

  // Takes a |channel| to be passed as a startup channel to the child process by |Spawn|.
  void AddChannel(zx::channel channel);

  // Spawns the new child process. Returns an error if a previous process was spawned but has not
  // been |Kill|ed and |Reset|, or if spawning fails.
  __WARN_UNUSED_RESULT zx_status_t Spawn();

  // Returns a promise to |Spawn| a new child process.
  ZxPromise<> SpawnAsync();

  // Return a copy of the process.
  __WARN_UNUSED_RESULT zx_status_t Duplicate(zx::process* out);

  // Returns a promise to wait for the promise to be spawned and then write data to the its stdin.
  // The promise will return an error if stdin has already been closed by |CloseStdin| or |Kill|.
  ZxPromise<size_t> WriteToStdin(const void* buf, size_t len);

  // Combines a promise from |WriteStdin| with a call to |CloseStdin| after it completes.
  ZxPromise<size_t> WriteAndCloseStdin(const void* buf, size_t len);

  // Closes the input pipe to the spawned process.
  void CloseStdin();

  // Returns a promise to read from the process's stdout or stderr, respectively. The promise will
  // read up to a newline or EOF, whichever comes first. The promise will return an error if the
  // file descriptor is closed or was cloned, and will return |ZX_ERR_STOP| on EOF.
  ZxPromise<std::string> ReadFromStdout();
  ZxPromise<std::string> ReadFromStderr();

  // Promises to wait for the spawned process to terminate.
  ZxPromise<> Wait();

  // Returns a promise that kills the spawned process and waits for it to fully terminate. This
  // leaves the process in a "killed" state; it must be |Reset| before it can be reused.
  ZxPromise<> Kill();

  // Returns this object to an initial state, from which |Spawn| can be called again. This
  // effectively kills the process, but does not wait for it to fully terminate. Callers should
  // prefer to |Kill| and then |Reset|. |AddArgs|, |SetFdAction|, and/or |SetChannels| will need to
  // be called again before respawning.
  void Reset();

 private:
  // Returns a promise that does not complete before a previous promise for the |stream| completes,
  // e.g. a previous call to |ReadFrom..| or |WriteTo...|, as appropriate.
  ZxPromise<> AwaitPrevious(int fd, ZxConsumer<> consumer);

  // Returns a promise to read from the given file descriptor. Multiple calls to this method happen
  // are sequential for the same |stream|.
  ZxPromise<std::string> ReadLine(int fd);

  ExecutorPtr executor_;
  bool verbose_ = false;

  // Parameters for |fdio_spawn_etc|.
  std::vector<std::string> args_;
  std::vector<zx::channel> channels_;

  // The handle to the spawned process.
  zx::process process_;

  // Stream-related variables for stdin, stdout, and stderr.
  static constexpr int kNumStreams = 3;
  struct Stream {
    // Piped file descriptor connected to the process.
    int fd = -1;

    // How to create the file descriptor in the spawned process. See |FdAction|.
    FdAction action = kTransfer;

    // Blocks reads or writes until the process is spawned.
    ZxCompleter<> on_spawn;

    // Ensures calls to |ReadFrom...| or |WriteTo...| happen sequentially.
    ZxConsumer<> previous;

    // Used to asynchronously wait for file descriptors to become readable.
    std::unique_ptr<fsl::FDWaiter> fd_waiter;

    // An internal buffer used when reading from the piped file descriptors.
    using Buffer = std::array<char, 0x400>;
    std::unique_ptr<Buffer> buf;

    // Location in the buffer where the next line begins.
    Buffer::iterator start;

    // Location in the buffer where the received data ends.
    Buffer::iterator end;
  } streams_[kNumStreams];

  Scope scope_;

  FXL_DISALLOW_COPY_ASSIGN_AND_MOVE(ChildProcess);
};

}  // namespace fuzzing

#endif  // SRC_SYS_FUZZING_COMMON_CHILD_PROCESS_H_
