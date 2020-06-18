// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#include "src/developer/exception_broker/exception_broker.h"

#include <lib/fit/defer.h>
#include <lib/syslog/cpp/macros.h>
#include <zircon/status.h>
#include <zircon/types.h>

#include <third_party/crashpad/util/file/string_file.h>

#include "fuchsia/exception/cpp/fidl.h"
#include "src/developer/exception_broker/crash_report_generation.h"
#include "src/developer/exception_broker/json_utils.h"
#include "src/lib/files/directory.h"
#include "src/lib/files/file.h"
#include "src/lib/fxl/strings/join_strings.h"

namespace exception {

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

  if (!limbo_manager_.active()) {
    FileCrashReport(std::move(process_exception));
  } else {
    limbo_manager_.AddToLimbo(std::move(process_exception));
  }
}

// ExceptionBroker implementation ------------------------------------------------------------------

void ExceptionBroker::FileCrashReport(ProcessException process_exception) {
  std::string process_name;
  zx::vmo minidump_vmo = GenerateMinidumpVMO(process_exception.exception(), &process_name);

  CrashReportBuilder builder(process_name);
  if (minidump_vmo.is_valid()) {
    builder.SetMinidump(std::move(minidump_vmo));
  }

  uint64_t id = next_connection_id_++;
  crash_report_builders_.emplace(id, std::move(builder));

  auto introspect_ptr = services_->Connect<fuchsia::sys::internal::Introspect>();
  introspect_connections_[id] = std::move(introspect_ptr);

  auto& introspect = introspect_connections_[id];

  introspect.set_error_handler([id, broker = GetWeakPtr()](zx_status_t status) {
    FX_PLOGS(ERROR, status) << "Lost connection to fuchsia.sys.internal.Introspect";

    // If the broker is not there anymore, there is nothing more we can do.
    if (!broker)
      return;

    broker->FileCrashReport(id);

    // Remove the connection after we have filed the crash report. The connection must be removed at
    // the end of the function because the InterfacePtr that owns the lambda is destroyed when the
    // connection is removed.
    broker->introspect_connections_.erase(id);
  });

  using fuchsia::sys::internal::Introspect_FindComponentByProcessKoid_Result;

  const zx_koid_t process_koid = process_exception.info().process_koid;
  introspect->FindComponentByProcessKoid(
      process_koid,
      // |process_exception| is moved into the call back to keep it alive until after the component
      // information of the crashed process has been collected or has failed to be collected,
      // otherwise the kernel would terminate the process.
      [id, process_exception = std::move(process_exception),
       broker = GetWeakPtr()](Introspect_FindComponentByProcessKoid_Result result) {
        // If the broker is not there anymore, there is nothing more we can do.
        if (!broker)
          return;

        auto& builder = broker->crash_report_builders_.at(id);

        if (result.is_response()) {
          if (!result.response().component_info.has_component_url()) {
            FX_LOGS(ERROR) << "Did not receive a component url";
          } else {
            builder.SetComponentUrl(result.response().component_info.component_url());
          }

          if (!result.response().component_info.has_realm_path()) {
            FX_LOGS(ERROR) << "Did not receive a realm path";
          } else {
            builder.SetRealmPath(
                "/" + fxl::JoinStrings(result.response().component_info.realm_path(), "/"));
          }
        } else if (result.err() != ZX_ERR_NOT_FOUND) {
          FX_PLOGS(ERROR, result.err()) << "Failed FindComponentByProcessKoid";
        }

        broker->FileCrashReport(id);

        // Remove the connection after we have filed the crash report. The connection must be
        // removed at the end of the function because the InterfacePtr that owns the lambda, and the
        // reference to |broker|, is destroyed when the connection is removed.
        broker->introspect_connections_.erase(id);
      });
}

void ExceptionBroker::FileCrashReport(uint64_t id) {
  if (crash_report_builders_.find(id) == crash_report_builders_.end()) {
    return;
  }

  // Create a new connection to the crash reporter and keep track of it.
  auto crash_reporter_ptr = services_->Connect<fuchsia::feedback::CrashReporter>();
  crash_reporter_connections_[id] = std::move(crash_reporter_ptr);

  // Get the ref to the crash reporter.
  auto& crash_reporter = crash_reporter_connections_[id];

  crash_reporter.set_error_handler([id, broker = GetWeakPtr()](zx_status_t status) {
    FX_PLOGS(ERROR, status) << "Lost connection to fuchsia.feedback.CrashReporter";

    // If the broker is not there anymore, there is nothing more we can do.
    if (!broker)
      return;

    // Remove the connection after we have removed the exception. The connection must be
    // removed at the end of the function because the InterfacePtr that owns the lambda, and the
    // reference to |broker|, is destroyed when the connection is removed.
    broker->crash_reporter_connections_.erase(id);
  });

  auto report = crash_report_builders_.at(id).Consume();
  crash_report_builders_.erase(id);

  crash_reporter->File(
      std::move(report), [id, program_name = report.program_name(), broker = GetWeakPtr()](
                             fuchsia::feedback::CrashReporter_File_Result result) {
        if (result.is_err())
          FX_PLOGS(ERROR, result.err()) << "Error filing crash report for " << program_name;

        // If the broker is not there anymore, there is nothing more we can do.
        if (!broker)
          return;

        // Remove the connection after we have removed the exception. The connection must be
        // removed at the end of the function because the InterfacePtr that owns the lambda, and the
        // reference to |broker|, is destroyed when the connection is removed.
        broker->crash_reporter_connections_.erase(id);
      });
}

}  // namespace exception
