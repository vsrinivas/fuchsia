// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#include "src/developer/forensics/exceptions/exception_broker.h"

#include <fuchsia/exception/cpp/fidl.h>
#include <lib/syslog/cpp/macros.h>
#include <zircon/status.h>
#include <zircon/types.h>

#include <string>

#include "src/developer/forensics/exceptions/json_utils.h"
#include "src/lib/files/directory.h"
#include "src/lib/files/file.h"

namespace forensics {
namespace exceptions {

namespace {

using fuchsia::exception::ExceptionInfo;
using fuchsia::exception::ProcessException;

constexpr char kEnableJitdConfigPath[] = "/config/data/exceptions/enable_jitd_on_startup.json";

}  // namespace

std::unique_ptr<ExceptionBroker> ExceptionBroker::Create(async_dispatcher_t* dispatcher,
                                                         size_t max_num_handlers,
                                                         zx::duration exception_ttl,
                                                         const char* override_filepath) {
  auto broker = std::unique_ptr<ExceptionBroker>(
      new ExceptionBroker(dispatcher, max_num_handlers, exception_ttl));

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

ExceptionBroker::ExceptionBroker(async_dispatcher_t* dispatcher, size_t max_num_handlers,
                                 zx::duration exception_ttl)
    : handler_manager_(dispatcher, max_num_handlers, exception_ttl) {}

// OnException -------------------------------------------------------------------------------------

void ExceptionBroker::OnException(zx::exception exception, ExceptionInfo info,
                                  OnExceptionCallback cb) {
  if (!limbo_manager_.active()) {
    handler_manager_.Handle(std::move(exception));
  } else {
    AddToLimbo(std::move(exception), std::move(info));
  }
  cb();
}

void ExceptionBroker::AddToLimbo(zx::exception exception, ExceptionInfo info) {
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

}  // namespace exceptions
}  // namespace forensics
