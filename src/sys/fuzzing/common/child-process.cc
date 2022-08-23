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

ZxResult<fdio_spawn_action_t> MakeFdAction(FdAction action, int target_fd, int* piped_fd) {
  fdio_spawn_action_t tmp;
  tmp.action = action;
  switch (action) {
    case kTransfer: {
      int fds[2];
      if (pipe(fds) != 0) {
        FX_LOGS(ERROR) << "Failed to transfer file descriptor: " << strerror(errno);
        return fpromise::error(ZX_ERR_IO);
      }
      int local_fd = target_fd == STDIN_FILENO ? fds[0] : fds[1];
      FX_DCHECK(piped_fd);
      *piped_fd = target_fd == STDIN_FILENO ? fds[1] : fds[0];
      tmp.fd.local_fd = local_fd;
      tmp.fd.target_fd = target_fd;
      break;
    }
    case kClone: {
      tmp.fd.local_fd = target_fd;
      tmp.fd.target_fd = target_fd;
      break;
    }
  }
  return fpromise::ok(std::move(tmp));
}

}  // namespace

ChildProcess::ChildProcess(ExecutorPtr executor) : executor_(std::move(executor)) {
  static_assert(STDERR_FILENO + 1 == kNumStreams);
  streams_[STDOUT_FILENO].buf = std::make_unique<Stream::Buffer>();
  streams_[STDERR_FILENO].buf = std::make_unique<Stream::Buffer>();
  Reset();
}

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

void ChildProcess::SetStdoutFdAction(FdAction action) { streams_[STDOUT_FILENO].action = action; }

void ChildProcess::SetStderrFdAction(FdAction action) { streams_[STDERR_FILENO].action = action; }

void ChildProcess::AddChannel(zx::channel channel) { channels_.emplace_back(std::move(channel)); }

zx_status_t ChildProcess::Spawn() {
  // Convert args to C-style strings.
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
  std::vector<fdio_spawn_action_t> actions;
  for (int fileno = STDIN_FILENO; fileno < kNumStreams; ++fileno) {
    auto& stream = streams_[fileno];
    if (!stream.on_spawn) {
      FX_LOGS(ERROR) << "ChildProcess must be reset before it can be respawned.";
      return ZX_ERR_BAD_STATE;
    }
    auto result = MakeFdAction(stream.action, fileno, &stream.fd);
    if (result.is_error()) {
      return result.error();
    }
    actions.emplace_back(result.take_value());
  }
  auto flags = FDIO_SPAWN_CLONE_ALL & (~FDIO_SPAWN_CLONE_STDIO);

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
  if (auto status = fdio_spawn_etc(ZX_HANDLE_INVALID, flags, argv[0], &argv[0], nullptr,
                                   actions.size(), actions.data(), handle, err_msg);
      status != ZX_OK) {
    FX_LOGS(ERROR) << "Failed to spawn process: " << err_msg << " (" << zx_status_get_string(status)
                   << ")";
    return status;
  }
  return ZX_OK;
}

ZxPromise<> ChildProcess::SpawnAsync() {
  return fpromise::make_promise([this]() -> ZxResult<> { return AsZxResult(Spawn()); })
      .inspect([this](const ZxResult<>& result) {
        for (auto& stream : streams_) {
          if (result.is_ok()) {
            stream.on_spawn.complete_ok();
          } else if (stream.on_spawn) {
            stream.on_spawn.complete_error(result.error());
          }
        }
      })
      .wrap_with(scope_);
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

ZxPromise<size_t> ChildProcess::WriteToStdin(const void* buf, size_t count) {
  ZxBridge<> bridge;
  return AwaitPrevious(STDIN_FILENO, std::move(bridge.consumer))
      .and_then([this, buf, count]() -> ZxResult<size_t> {
        auto fd = streams_[STDIN_FILENO].fd;
        auto num_written = HANDLE_EINTR(write(fd, buf, count));
        if (num_written < 0) {
          FX_LOGS(ERROR) << "Failed to write input to process: " << strerror(errno);
          return fpromise::error(ZX_ERR_IO);
        }
        return fpromise::ok(static_cast<size_t>(num_written));
      })
      .inspect([completer = std::move(bridge.completer)](const ZxResult<size_t>& result) mutable {
        completer.complete_ok();
      })
      .wrap_with(scope_);
}

ZxPromise<size_t> ChildProcess::WriteAndCloseStdin(const void* buf, size_t count) {
  return WriteToStdin(buf, count).inspect([this](const ZxResult<size_t>& result) { CloseStdin(); });
}

void ChildProcess::CloseStdin() {
  auto& stream = streams_[STDIN_FILENO];
  close(stream.fd);
  auto discarded = std::move(stream.previous);
}

ZxPromise<std::string> ChildProcess::ReadFromStdout() { return ReadLine(STDOUT_FILENO); }

ZxPromise<std::string> ChildProcess::ReadFromStderr() { return ReadLine(STDERR_FILENO); }

ZxPromise<std::string> ChildProcess::ReadLine(int fd) {
  ZxBridge<> bridge;
  auto& stream = streams_[fd];
  return AwaitPrevious(fd, std::move(bridge.consumer))
      .and_then([&stream, ready = ZxFuture<>()](Context& context) mutable -> ZxResult<std::string> {
        auto fd = stream.fd;
        auto* buf = stream.buf.get();
        if (fd < 0 || !buf) {
          FX_LOGS(ERROR) << "Invalid fd: " << fd;
          return fpromise::error(ZX_ERR_INVALID_ARGS);
        }
        while (true) {
          if (stream.start != stream.end) {
            auto newline = std::find(stream.start, stream.end, '\n');
            if (newline != stream.end) {
              // Found a newline; return the string up to it and search again.
              std::string line(stream.start, newline - stream.start);
              stream.start = newline + 1;
              return fpromise::ok(std::move(line));
            }
          }
          if (!ready) {
            // Need more data. First move any remaining data to the start of the buffer.
            if (stream.start != buf->begin()) {
              auto tmp = stream.start;
              stream.start = buf->begin();
              memmove(&*stream.start, &*tmp, stream.end - tmp);
              stream.end -= tmp - stream.start;
            } else if (stream.end == buf->end()) {
              FX_LOGS(WARNING) << "a single log line exceeds " << buf->size()
                               << " characters; skipping...";
              stream.end = stream.start;
            }
            // Now create a future to wait for the file descriptor to be readable.
            ZxBridge<> bridge;
            auto task = [completer = std::move(bridge.completer)](zx_status_t status,
                                                                  uint32_t events) mutable {
              if (status != ZX_OK) {
                completer.complete_error(status);
                return;
              }
              completer.complete_ok();
            };
            stream.fd_waiter->Wait(std::move(task), fd, POLLIN);
            ready = bridge.consumer.promise_or(fpromise::error(ZX_ERR_CANCELED));
          }
          if (!ready(context)) {
            return fpromise::pending();
          }
          if (ready.is_error()) {
            auto status = ready.error();
            if (status == ZX_ERR_CANCELED) {
              // Stream was closed due to process exiting.
              return fpromise::error(ZX_ERR_STOP);
            }
            FX_LOGS(ERROR) << "Failed to wait for output from process: "
                           << zx_status_get_string(ready.error());
            return fpromise::error(ready.error());
          }
          // File descriptor is readable; read from it!
          auto bytes_read = HANDLE_EINTR(read(fd, &*stream.end, buf->end() - stream.end));
          if (bytes_read < 0) {
            if (errno == EBADF) {
              // Stream was closed due to process exiting.
              return fpromise::error(ZX_ERR_STOP);
            }
            FX_LOGS(ERROR) << "Failed to read output from process: " << strerror(errno);
            return fpromise::error(ZX_ERR_IO);
          }
          if (bytes_read == 0 && stream.start != stream.end) {
            // File descriptor is closed; just return whatever's left.
            std::string line(stream.start, stream.end - stream.start);
            stream.start = stream.end;
            return fpromise::ok(std::move(line));
          }
          if (bytes_read == 0) {
            // Return an error to let the caller know there's no more data available.
            return fpromise::error(ZX_ERR_STOP);
          }
          stream.end += bytes_read;
          ready = nullptr;
        }
      })
      .inspect([completer = std::move(bridge.completer),
                verbose = verbose_](const ZxResult<std::string>& result) mutable {
        if (result.is_error()) {
          completer.complete_error(result.error());
          return;
        }
        if (verbose) {
          std::cerr << result.value() << std::endl;
        }
        completer.complete_ok();
      })
      .wrap_with(scope_);
}

ZxPromise<> ChildProcess::AwaitPrevious(int fd, ZxConsumer<> consumer) {
  auto previous = std::move(streams_[fd].previous);
  if (!previous) {
    return fpromise::make_promise([]() -> ZxResult<> {
      FX_LOGS(ERROR) << "Stream has been closed.";
      return fpromise::error(ZX_ERR_BAD_STATE);
    });
  }
  streams_[fd].previous = std::move(consumer);
  return previous.promise();
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
           for (auto& stream : streams_) {
             auto discarded = std::move(stream.previous);
           }
           return fpromise::ok(info_.return_code);
         })
      .wrap_with(scope_);
}

ZxPromise<> ChildProcess::Kill() {
  return fpromise::make_promise(
             [this, wait = ZxFuture<int64_t>()](Context& context) mutable -> ZxResult<> {
               if (!IsAlive()) {
                 return fpromise::ok();
               }
               if (!wait) {
                 process_.kill();
                 for (auto& stream : streams_) {
                   close(stream.fd);
                 }
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

void ChildProcess::Reset() {
  args_.clear();
  channels_.clear();
  process_.reset();
  for (auto& stream : streams_) {
    if (stream.buf) {
      stream.start = stream.buf->begin();
      stream.end = stream.start;
    }
    ZxBridge<> bridge;
    close(stream.fd);
    stream.fd = -1;
    stream.on_spawn = std::move(bridge.completer);
    stream.previous = std::move(bridge.consumer);
    stream.fd_waiter = std::make_unique<fsl::FDWaiter>(executor_->dispatcher());
  }
  memset(&info_, 0, sizeof(info_));
}

}  // namespace fuzzing
