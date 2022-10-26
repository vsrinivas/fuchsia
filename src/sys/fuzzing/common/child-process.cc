// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/sys/fuzzing/common/child-process.h"

#include <lib/async/cpp/executor.h>
#include <lib/fdio/spawn.h>
#include <lib/syslog/cpp/macros.h>
#include <poll.h>
#include <stdio.h>
#include <unistd.h>
#include <zircon/process.h>
#include <zircon/processargs.h>
#include <zircon/status.h>

#include <iostream>

#include "src/lib/files/eintr_wrapper.h"

namespace fuzzing {
namespace {

constexpr const size_t kBufSize = 0x400;

zx_status_t CreatePipe(int* rpipe, int* wpipe) {
  int fds[2];
  if (pipe(fds) != 0) {
    FX_LOGS(ERROR) << "Failed to transfer file descriptor: " << strerror(errno);
    return ZX_ERR_IO;
  }
  *rpipe = fds[0];
  *wpipe = fds[1];
  return ZX_OK;
}

zx_status_t ReadAndSend(int fd, AsyncSender<std::string> sender) {
  if (fd < 0) {
    FX_LOGS(ERROR) << "Invalid fd: " << fd;
    return ZX_ERR_INVALID_ARGS;
  }
  std::array<char, kBufSize> buf;
  auto start = buf.begin();
  auto end = start;
  std::string line;
  while (true) {
    // Has data; repeatedly send strings up to a newline.
    while (start != end) {
      auto newline = std::find(start, end, '\n');
      if (newline == end) {
        break;
      }
      line += std::string(start, newline - start);
      start = newline + 1;
      if (auto status = sender.Send(std::move(line)); status != ZX_OK) {
        return status;
      }
      line.clear();
    }
    // Need more data. First move any remaining data to the start of the buffer.
    if (start != buf.begin()) {
      auto tmp = start;
      start = buf.begin();
      memmove(&*start, &*tmp, end - tmp);
      end -= tmp - start;
    } else if (end == buf.end()) {
      // A log line filled the buffer. Add it to `line` and keep going.
      line += std::string(start, end - start);
      end = start;
    }
    // Now try to read more data from the file descriptor.
    auto bytes_read = HANDLE_EINTR(read(fd, &*end, buf.end() - end));
    if (bytes_read < 0) {
      if (errno == EBADF) {
        // Stream was closed because process exited.
        return ZX_ERR_PEER_CLOSED;
      }
      FX_LOGS(ERROR) << "Failed to read output from process (fd=" << fd << "): " << strerror(errno);
      return ZX_ERR_IO;
    }
    if (bytes_read == 0) {
      // File descriptor is closed.just send whatever's left.
      if (start != end) {
        line += std::string(start, end - start);
        if (auto status = sender.Send(std::move(line)); status != ZX_OK) {
          return status;
        }
      }
      return ZX_ERR_PEER_CLOSED;
    }
    end += bytes_read;
  }
}

}  // namespace

ChildProcess::ChildProcess(ExecutorPtr executor) : executor_(std::move(executor)) { Reset(); }

ChildProcess::~ChildProcess() { KillSync(); }

void ChildProcess::AddArg(const std::string& arg) {
  if (args_.empty()) {
    args_.emplace_back(std::string("/pkg/") + arg);
  } else {
    args_.emplace_back(arg);
  }
}

void ChildProcess::AddArgs(std::initializer_list<const char*> args) {
  for (const auto* arg : args) {
    AddArg(arg);
  }
}

void ChildProcess::SetEnvVar(const std::string& name, const std::string& value) {
  envvars_[name] = value;
}

void ChildProcess::SetStdoutFdAction(FdAction action) { stdout_action_ = action; }

void ChildProcess::SetStderrFdAction(FdAction action) { stderr_action_ = action; }

void ChildProcess::AddChannel(zx::channel channel) { channels_.emplace_back(std::move(channel)); }

zx_status_t ChildProcess::Spawn() {
  // Convert args and envvars to C-style strings.
  // The envvars vector holds the constructed strings backing the pointers in environ.
  std::vector<std::string> envvars;
  std::vector<const char*> environ;
  for (const auto& [key, value] : envvars_) {
    std::ostringstream oss;
    oss << key << "=" << value;
    envvars.push_back(oss.str());
    if (verbose_) {
      std::cerr << envvars.back() << " ";
    }
    environ.push_back(envvars.back().c_str());
  }
  environ.push_back(nullptr);

  std::vector<const char*> argv;
  argv.reserve(args_.size() + 1);
  for (const auto& arg : args_) {
    if (verbose_) {
      std::cerr << arg << " ";
    }
    argv.push_back(arg.c_str());
  }
  if (verbose_) {
    std::cerr << std::endl;
  }
  argv.push_back(nullptr);

  // Build spawn actions
  if (spawned_) {
    FX_LOGS(ERROR) << "ChildProcess must be reset before it can be respawned.";
    return ZX_ERR_BAD_STATE;
  }
  spawned_ = true;

  auto flags = FDIO_SPAWN_CLONE_ALL & (~FDIO_SPAWN_CLONE_STDIO);
  std::vector<fdio_spawn_action_t> actions(3);

  auto& stdin_action = actions[0];
  stdin_action.action = kTransfer;
  int stdin_wpipe = -1;
  if (auto status = CreatePipe(&stdin_action.fd.local_fd, &stdin_wpipe); status != ZX_OK) {
    FX_LOGS(ERROR) << "Failed to create pipe to process stdin: " << zx_status_get_string(status);
    return status;
  }
  stdin_action.fd.target_fd = STDIN_FILENO;

  auto& stdout_action = actions[1];
  stdout_action.action = stdout_action_;
  int stdout_rpipe = -1;
  if (auto status = CreatePipe(&stdout_rpipe, &stdout_action.fd.local_fd); status != ZX_OK) {
    FX_LOGS(ERROR) << "Failed to create pipe from process stdout: " << zx_status_get_string(status);
    return status;
  }
  stdout_action.fd.target_fd = STDOUT_FILENO;

  auto& stderr_action = actions[2];
  stderr_action.action = stderr_action_;
  int stderr_rpipe = -1;
  if (auto status = CreatePipe(&stderr_rpipe, &stderr_action.fd.local_fd); status != ZX_OK) {
    FX_LOGS(ERROR) << "Failed to create pipe from process stderr: " << zx_status_get_string(status);
    return status;
  }
  stderr_action.fd.target_fd = STDERR_FILENO;

  // Build channel actions.
  uint32_t i = 0;
  for (auto& channel : channels_) {
    fdio_spawn_action_t action{};
    action.action = FDIO_SPAWN_ACTION_ADD_HANDLE;
    action.h.id = PA_HND(PA_USER0, i++);
    action.h.handle = channel.release();
    actions.emplace_back(std::move(action));
  }

  // Spawn the process!
  auto* handle = process_.reset_and_get_address();
  char err_msg[FDIO_SPAWN_ERR_MSG_MAX_LENGTH];
  if (auto status = fdio_spawn_etc(ZX_HANDLE_INVALID, flags, argv[0], &argv[0], &environ[0],
                                   actions.size(), actions.data(), handle, err_msg);
      status != ZX_OK) {
    FX_LOGS(ERROR) << "Failed to spawn process: " << err_msg << " (" << zx_status_get_string(status)
                   << ")";
    return status;
  }

  // Start threads to handle stdio.
  stdin_thread_ = std::thread([this, stdin_wpipe] {
    bool close_input = false;
    std::vector<std::string> input_lines;
    while (!close_input) {
      {
        std::unique_lock lock(mutex_);
        input_cond_.wait(lock, [this, &close_input, &input_lines]() FXL_REQUIRE(mutex_) {
          close_input = input_closed_;
          input_lines = std::move(input_lines_);
          input_lines_.clear();
          return true;
        });
      }
      for (auto& line : input_lines) {
        const char* buf = line.data();
        size_t off = 0;
        size_t len = line.size();
        while (off < len) {
          auto num_written = HANDLE_EINTR(write(stdin_wpipe, &buf[off], len - off));
          if (num_written < 0) {
            FX_LOGS(ERROR) << "Failed to write input to process: " << strerror(errno);
            close_input = true;
            break;
          }
          off += num_written;
        }
      }
    }
    close(stdin_wpipe);
  });

  if (stdout_action_ == kTransfer) {
    AsyncSender<std::string> sender;
    stdout_ = AsyncReceiver<std::string>::MakePtr(&sender);
    stdout_thread_ = std::thread([this, stdout_rpipe, sender = std::move(sender)]() mutable {
      stdout_result_ = ReadAndSend(stdout_rpipe, std::move(sender));
    });
  }

  if (stderr_action_ == kTransfer) {
    AsyncSender<std::string> sender;
    stderr_ = AsyncReceiver<std::string>::MakePtr(&sender);
    stderr_thread_ = std::thread([this, stderr_rpipe, sender = std::move(sender)]() mutable {
      stderr_result_ = ReadAndSend(stderr_rpipe, std::move(sender));
    });
  }

  return ZX_OK;
}

bool ChildProcess::IsAlive() {
  if (!process_) {
    return false;
  }
  auto status = process_.get_info(ZX_INFO_PROCESS, &info_, sizeof(info_), nullptr, nullptr);
  if (status == ZX_ERR_BAD_HANDLE) {
    return false;
  }
  FX_CHECK(status == ZX_OK);
  return (info_.flags & ZX_INFO_PROCESS_FLAG_EXITED) == 0;
}

zx_status_t ChildProcess::Duplicate(zx::process* out) {
  return process_.duplicate(ZX_RIGHT_SAME_RIGHTS, out);
}

zx_status_t ChildProcess::WriteToStdin(const std::string& line) {
  if (!IsAlive()) {
    FX_LOGS(WARNING) << "Cannot write to process standard input: not running";
    return ZX_ERR_BAD_STATE;
  }
  {
    std::lock_guard lock(mutex_);
    if (input_closed_) {
      FX_LOGS(WARNING) << "Cannot write to process standard input: closed";
      return ZX_ERR_PEER_CLOSED;
    }
    input_lines_.push_back(line);
  }
  input_cond_.notify_one();
  return ZX_OK;
}

zx_status_t ChildProcess::WriteAndCloseStdin(const std::string& line) {
  if (auto status = WriteToStdin(line); status != ZX_OK) {
    return status;
  }
  CloseStdin();
  return ZX_OK;
}

void ChildProcess::CloseStdin() {
  {
    std::lock_guard lock(mutex_);
    input_closed_ = true;
  }
  input_cond_.notify_one();
}

ZxPromise<std::string> ChildProcess::ReadFromStdout() {
  if (!stdout_) {
    return fpromise::make_promise(
        []() -> ZxResult<std::string> { return fpromise::error(ZX_ERR_BAD_STATE); });
  }
  return stdout_->Receive().or_else([this]() -> ZxResult<std::string> {
    if (stdout_thread_.joinable()) {
      stdout_thread_.join();
    }
    return fpromise::error(stdout_result_);
  });
}

ZxPromise<std::string> ChildProcess::ReadFromStderr() {
  if (!stderr_) {
    return fpromise::make_promise(
        []() -> ZxResult<std::string> { return fpromise::error(ZX_ERR_BAD_STATE); });
  }
  return stderr_->Receive().or_else([this]() -> ZxResult<std::string> {
    if (stderr_thread_.joinable()) {
      stderr_thread_.join();
    }
    return fpromise::error(stderr_result_);
  });
}

ZxResult<ProcessStats> ChildProcess::GetStats() {
  ProcessStats stats;
  if (auto status = GetStatsForProcess(process_, &stats); status != ZX_OK) {
    return fpromise::error(status);
  }
  return fpromise::ok(std::move(stats));
}

ZxPromise<int64_t> ChildProcess::Wait() {
  return fpromise::make_promise([this, terminated = ZxFuture<zx_packet_signal_t>()](
                                    Context& context) mutable -> ZxResult<int64_t> {
           if (!IsAlive()) {
             return fpromise::ok(info_.return_code);
           }
           if (!terminated) {
             terminated = executor_->MakePromiseWaitHandle(zx::unowned_handle(process_.get()),
                                                           ZX_PROCESS_TERMINATED);
           }
           if (!terminated(context)) {
             return fpromise::pending();
           }
           if (terminated.is_error()) {
             auto status = terminated.error();
             FX_LOGS(WARNING) << "Failed to wait for process to terminate: "
                              << zx_status_get_string(status);
             return fpromise::error(status);
           }
           if (IsAlive()) {
             FX_LOGS(WARNING) << "Failed to terminate process.";
             return fpromise::error(ZX_ERR_BAD_STATE);
           }
           return fpromise::ok(info_.return_code);
         })
      .wrap_with(scope_);
}

ZxPromise<> ChildProcess::Kill() {
  return fpromise::make_promise(
             [this, wait = ZxFuture<int64_t>()](Context& context) mutable -> ZxResult<> {
               if (!wait) {
                 KillSync();
                 wait = Wait();
               }
               if (!wait(context)) {
                 return fpromise::pending();
               }
               if (wait.is_error()) {
                 return fpromise::error(wait.take_error());
               }
               return fpromise::ok();
             })
      .wrap_with(scope_);
}

void ChildProcess::KillSync() {
  process_.kill();

  CloseStdin();
  if (stdin_thread_.joinable()) {
    stdin_thread_.join();
  }

  if (stdout_thread_.joinable()) {
    stdout_thread_.join();
  }

  if (stderr_thread_.joinable()) {
    stderr_thread_.join();
  }

  killed_ = true;
}

void ChildProcess::Reset() {
  KillSync();
  spawned_ = false;
  killed_ = false;
  args_.clear();
  envvars_.clear();
  channels_.clear();
  process_.reset();
  memset(&info_, 0, sizeof(info_));
  {
    std::lock_guard lock(mutex_);
    input_closed_ = false;
    input_lines_.clear();
  }
  stdout_.reset();
  stdout_result_ = ZX_OK;
  stderr_.reset();
  stderr_result_ = ZX_OK;
}

}  // namespace fuzzing
