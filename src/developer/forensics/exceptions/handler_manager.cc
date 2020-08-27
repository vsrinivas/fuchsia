// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#include "src/developer/forensics/exceptions/handler_manager.h"

#include <lib/async/cpp/wait.h>
#include <lib/fdio/spawn.h>
#include <lib/syslog/cpp/macros.h>
#include <zircon/processargs.h>
#include <zircon/status.h>
#include <zircon/types.h>

#include <array>
#include <optional>

#include "src/lib/fxl/strings/string_printf.h"

namespace forensics {
namespace exceptions {
namespace {

std::string MakeCrashedProcessName(const std::string& crashed_process_name) {
  static size_t handler_num{1};
  return fxl::StringPrintf("crashed_%s_%03zu", crashed_process_name.c_str(), handler_num++);
}

template <size_t argc, size_t actionc>
std::optional<zx::process> SpawnHandler(std::array<const char*, argc> args,
                                        std::array<fdio_spawn_action_t, actionc> actions) {
  char err_msg[FDIO_SPAWN_ERR_MSG_MAX_LENGTH] = {};
  zx::process handler;
  if (const zx_status_t status = fdio_spawn_etc(ZX_HANDLE_INVALID, FDIO_SPAWN_CLONE_ALL,
                                                "/pkg/bin/exception_handler", args.data(),
                                                /*environ=*/nullptr, actions.size(), actions.data(),
                                                handler.reset_and_get_address(), err_msg);
      status != ZX_OK) {
    FX_PLOGS(ERROR, status) << "Failed to launch exception handler process: " << err_msg;
    return std::nullopt;
  }

  return handler;
}

// Spawn a dedicated handler that will file a crash report for |crashed_process_name|, without a
// minidump.
std::optional<zx::process> SpawnHandler(const std::string& crashed_process_name,
                                        const std::string& crashed_process_koid) {
  const std::string handler_name(MakeCrashedProcessName(crashed_process_name));
  const std::array<const char*, 5> args = {
      handler_name.c_str() /* process name */,
      crashed_process_name.c_str(),
      crashed_process_koid.c_str(),
      nullptr,
  };

  return SpawnHandler(args, std::array<fdio_spawn_action_t, 0>{});
}

// Spawn a dedicated handler that will generate a minidump for |exception| and file a crash report
// for it.
std::optional<zx::process> SpawnHandler(zx::exception exception,
                                        const std::string& crashed_process_name) {
  const std::string handler_name(MakeCrashedProcessName(crashed_process_name));
  const std::array<const char*, 3> args = {
      handler_name.c_str() /* process name */,
      nullptr,
  };
  const std::array actions = {
      fdio_spawn_action_t{
          .action = FDIO_SPAWN_ACTION_ADD_HANDLE,
          .h =
              {
                  .id = PA_HND(PA_USER0, 0),
                  .handle = exception.release(),
              },
      },
  };

  return SpawnHandler(args, actions);
}

}  // namespace

HandlerManager::HandlerManager(async_dispatcher_t* dispatcher, size_t max_num_handlers,
                               zx::duration exception_ttl)
    : dispatcher_(dispatcher),
      max_num_subprocesses_(max_num_handlers),
      exception_ttl_(exception_ttl) {}

HandlerManager::Handler::Handler(async_dispatcher_t* dispatcher, zx::process subprocess,
                                 async::WaitOnce::Handler on_subprocess_exit)
    : subprocess_(std::move(subprocess)),
      on_subprocess_exit_(subprocess_.get(), ZX_TASK_TERMINATED) {
  on_subprocess_exit_.Begin(dispatcher, std::move(on_subprocess_exit));
}

void HandlerManager::Handle(zx::exception exception) {
  pending_exceptions_.emplace_back(dispatcher_, exception_ttl_, std::move(exception));
  HandleNextPendingException();
}

void HandlerManager::HandleNextPendingException() {
  if (pending_exceptions_.empty() || num_active_subprocesses_ >= max_num_subprocesses_) {
    return;
  }

  PendingException& pending_exception = pending_exceptions_.front();
  zx::exception exception = pending_exception.TakeException();

  std::optional<zx::process> process{};
  if (exception.is_valid()) {
    process = SpawnHandler(std::move(exception), pending_exception.CrashedProcessName());
  } else {
    process = SpawnHandler(pending_exception.CrashedProcessName(),
                           pending_exception.CrashedProcessKoid());
  }

  // If we failed to spawn a sub-process to handle the exception, call the callback and move on to
  // the next exception in the queue.
  if (!process.has_value()) {
    pending_exceptions_.pop_front();
    HandleNextPendingException();
    return;
  }

  handlers_.emplace(next_handler_id_,
                    std::make_unique<Handler>(dispatcher_, std::move(process.value()),
                                              [this, id = next_handler_id_](...) {
                                                --num_active_subprocesses_;
                                                HandleNextPendingException();

                                                // We do this last as it will tear down the lambda.
                                                handlers_.erase(id);
                                              }));

  pending_exceptions_.pop_front();
  ++num_active_subprocesses_;
  ++next_handler_id_;
}

}  // namespace exceptions
}  // namespace forensics
