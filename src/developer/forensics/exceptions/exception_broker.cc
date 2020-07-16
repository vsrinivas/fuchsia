// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#include "src/developer/forensics/exceptions/exception_broker.h"

#include <lib/fit/defer.h>
#include <lib/syslog/cpp/macros.h>
#include <zircon/status.h>
#include <zircon/types.h>

#include <third_party/crashpad/util/file/string_file.h>

#include "fuchsia/exception/cpp/fidl.h"
#include "src/developer/forensics/exceptions/json_utils.h"
#include "src/lib/files/directory.h"
#include "src/lib/files/file.h"

namespace forensics {
namespace exceptions {

namespace {

using fuchsia::exception::ExceptionInfo;
using fuchsia::exception::ProcessException;

constexpr char kEnableJitdConfigPath[] = "/config/data/enable_jitd_on_startup.json";

}  // namespace

std::unique_ptr<ExceptionBroker> ExceptionBroker::Create(
    async_dispatcher_t* dispatcher, std::shared_ptr<sys::ServiceDirectory> services,
    const char* override_filepath) {
  auto broker = std::unique_ptr<ExceptionBroker>(new ExceptionBroker(services));

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

ExceptionBroker::ExceptionBroker(std::shared_ptr<sys::ServiceDirectory> services)
    : services_(std::move(services)), weak_factory_(this) {
  FX_DCHECK(services_);
}

fxl::WeakPtr<ExceptionBroker> ExceptionBroker::GetWeakPtr() { return weak_factory_.GetWeakPtr(); }

// OnException -------------------------------------------------------------------------------------

void ExceptionBroker::OnException(zx::exception exception, ExceptionInfo info,
                                  OnExceptionCallback cb) {
  // Always call the callback when we're done.
  auto defer_cb = fit::defer([cb = std::move(cb)]() { cb(); });

  if (limbo_manager_.active()) {
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
  } else {
    uint64_t id = next_crash_reporter_id_++;
    crash_reporters_.emplace(id, services_);

    crash_reporters_.at(id).FileCrashReport(std::move(exception), [id, broker = GetWeakPtr()] {
      if (!broker)
        return;

      broker->crash_reporters_.erase(id);
    });
  }
}

}  // namespace exceptions
}  // namespace forensics
