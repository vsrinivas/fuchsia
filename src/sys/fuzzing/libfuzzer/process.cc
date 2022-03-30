// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/sys/fuzzing/libfuzzer/process.h"

#include <lib/async/cpp/executor.h>
#include <lib/fdio/spawn.h>
#include <lib/syslog/cpp/macros.h>
#include <poll.h>
#include <stdio.h>
#include <unistd.h>
#include <zircon/status.h>

#include <iostream>

#include "src/lib/files/eintr_wrapper.h"

namespace fuzzing {
namespace {

ZxResult<fdio_spawn_action_t> MakeSpawnAction(SpawnAction action, int target_fd,
                                              int* piped_fd = nullptr) {
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

Process::Process(ExecutorPtr executor) : executor_(std::move(executor)) {
  static_assert(STDERR_FILENO + 1 == kNumStreams);
  streams_[STDOUT_FILENO].buf = std::make_unique<Stream::Buffer>();
  streams_[STDERR_FILENO].buf = std::make_unique<Stream::Buffer>();
  Reset();
}

void Process::SetStdoutSpawnAction(SpawnAction action) {
  streams_[STDOUT_FILENO].spawn_action = action;
}

void Process::SetStderrSpawnAction(SpawnAction action) {
  streams_[STDERR_FILENO].spawn_action = action;
}

ZxPromise<> Process::Spawn(const std::vector<std::string>& args) {
  return fpromise::make_promise([this, args = std::vector<std::string>(args)]() -> ZxResult<> {
           // Convert args to C-style strings.
           std::vector<const char*> argv;
           argv.reserve(args.size());
           for (const auto& arg : args) {
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
               FX_LOGS(ERROR) << "Process must be reset before it can be respawned.";
               return fpromise::error(ZX_ERR_BAD_STATE);
             }
             auto result = MakeSpawnAction(stream.spawn_action, fileno, &stream.fd);
             if (result.is_error()) {
               return fpromise::error(result.error());
             }
             actions.emplace_back(result.take_value());
           }
           auto flags = FDIO_SPAWN_CLONE_ALL & (~FDIO_SPAWN_CLONE_STDIO);

           // Spawn the process!
           auto* handle = process_.reset_and_get_address();
           char err_msg[FDIO_SPAWN_ERR_MSG_MAX_LENGTH];
           auto result =
               AsZxResult(fdio_spawn_etc(ZX_HANDLE_INVALID, flags, argv[0], &argv[0], nullptr,
                                         actions.size(), actions.data(), handle, err_msg));
           if (result.is_error()) {
             FX_LOGS(ERROR) << "Failed to spawn process: " << err_msg;
           }
           return result;
         })
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

ZxPromise<size_t> Process::WriteToStdin(const void* buf, size_t count) {
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

ZxPromise<size_t> Process::WriteAndCloseStdin(const void* buf, size_t count) {
  return WriteToStdin(buf, count).inspect([this](const ZxResult<size_t>& result) { CloseStdin(); });
}

void Process::CloseStdin() {
  auto& stream = streams_[STDIN_FILENO];
  close(stream.fd);
  auto discarded = std::move(stream.previous);
}

ZxPromise<std::string> Process::ReadFromStdout() { return ReadLine(STDOUT_FILENO); }

ZxPromise<std::string> Process::ReadFromStderr() { return ReadLine(STDERR_FILENO); }

ZxPromise<std::string> Process::ReadLine(int fd) {
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

ZxPromise<> Process::AwaitPrevious(int fd, ZxConsumer<> consumer) {
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

ZxPromise<> Process::Kill() {
  return fpromise::make_promise(
             [this, kill = ZxFuture<zx_packet_signal_t>()](Context& context) mutable -> ZxResult<> {
               if (!process_) {
                 return fpromise::ok();
               }
               if (!kill) {
                 process_.kill();
                 for (auto& stream : streams_) {
                   close(stream.fd);
                 }
                 kill = executor_->MakePromiseWaitHandle(zx::unowned_handle(process_.get()),
                                                         ZX_TASK_TERMINATED);
               }
               if (!kill(context)) {
                 return fpromise::pending();
               }
               if (kill.is_error()) {
                 return fpromise::error(kill.error());
               }
               process_.reset();
               for (auto& stream : streams_) {
                 auto discarded = std::move(stream.previous);
               }
               return fpromise::ok();
             })
      .wrap_with(scope_);
}

void Process::Reset() {
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
}

}  // namespace fuzzing
