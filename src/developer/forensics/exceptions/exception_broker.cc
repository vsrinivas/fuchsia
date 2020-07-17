// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#include "src/developer/forensics/exceptions/exception_broker.h"

#include <fuchsia/exception/cpp/fidl.h>
#include <lib/fdio/spawn.h>
#include <lib/fit/defer.h>
#include <lib/syslog/cpp/macros.h>
#include <zircon/processargs.h>
#include <zircon/status.h>
#include <zircon/types.h>

#include <array>
#include <string>

#include "src/developer/forensics/exceptions/json_utils.h"
#include "src/lib/files/directory.h"
#include "src/lib/files/file.h"
#include "src/lib/fxl/strings/string_printf.h"

namespace forensics {
namespace exceptions {

namespace {

using fuchsia::exception::ExceptionInfo;
using fuchsia::exception::ProcessException;

constexpr char kEnableJitdConfigPath[] = "/config/data/enable_jitd_on_startup.json";

// Spawn a dedicated handler for |exception|. This way if the exception handling logic
// were to crash, e.g., while generating the minidump from the process, only the sub-process would
// be in an exception and exceptions.cmx could still handle exceptions in separate sub-processes.
void SpawnExceptionHandler(zx::exception exception) {
  static size_t process_num{1};

  const std::string process_name(fxl::StringPrintf("exception_%03zu", process_num++));
  const std::array<const char*, 2> args = {
      process_name.c_str(),
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

  char err_msg[FDIO_SPAWN_ERR_MSG_MAX_LENGTH] = {};
  zx_handle_t process;
  if (const zx_status_t status = fdio_spawn_etc(
          ZX_HANDLE_INVALID, FDIO_SPAWN_CLONE_ALL, "/pkg/bin/exception_handler", args.data(),
          /*environ=*/nullptr, actions.size(), actions.data(), &process, err_msg);
      status != ZX_OK) {
    FX_PLOGS(ERROR, status) << "Failed to launch exception handler process: " << err_msg;
  }
}

}  // namespace

std::unique_ptr<ExceptionBroker> ExceptionBroker::Create(const char* override_filepath) {
  auto broker = std::make_unique<ExceptionBroker>();

  // Check if JITD should be enabled at startup. For now existence means it's activated.
  if (!override_filepath)
    override_filepath = kEnableJitdConfigPath;

  if (files::IsFile(override_filepath)) {
    broker->limbo_manager().SetActive(true);

    std::string file_content;
    if (!files::ReadFileToString(override_filepath, &file_content)) {
      FX_LOGS(WARNING) << "Could not read the config file.";
    } else {
      broker->limbo_manager().set_filters(ExtractFilters(file_content));
    }
  }

  return broker;
}

// OnException -------------------------------------------------------------------------------------

void ExceptionBroker::OnException(zx::exception exception, ExceptionInfo info,
                                  OnExceptionCallback cb) {
  // Always call the callback when we're done.
  auto defer_cb = fit::defer([cb = std::move(cb)]() { cb(); });

  if (!limbo_manager_.active()) {
    SpawnExceptionHandler(std::move(exception));
  } else {
    ProcessException process_exception = {};
    process_exception.set_exception(std::move(exception));
    process_exception.set_info(std::move(info));

    zx_status_t status;
    zx::process process;
    status = process_exception.exception().get_process(&process);
    if (status != ZX_OK) {
      FX_PLOGS(WARNING, status) << "Could not obtain process handle for exception.";
    } else {
      process_exception.set_process(std::move(process));
    }

    zx::thread thread;
    status = process_exception.exception().get_thread(&thread);
    if (status != ZX_OK) {
      FX_PLOGS(WARNING, status) << "Could not obtain thread handle for exception.";
    } else {
      process_exception.set_thread(std::move(thread));
    }
    limbo_manager_.AddToLimbo(std::move(process_exception));
  }
}

}  // namespace exceptions
}  // namespace forensics
