// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#include "src/developer/exception_broker/exception_broker.h"

#include <lib/fit/defer.h>
#include <lib/syslog/cpp/macros.h>
#include <zircon/status.h>

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

namespace {

fuchsia::feedback::CrashReport CreateCrashReport(const std::string& process_name,
                                                 const std::optional<std::string> component_url,
                                                 const std::optional<std::string> realm_path,
                                                 zx::vmo minidump_vmo) {
  using namespace fuchsia::feedback;

  CrashReport crash_report;
  const std::string program_name =
      (component_url.has_value()) ? component_url.value() : process_name;
  crash_report.set_program_name(program_name.substr(0, fuchsia::feedback::MAX_PROGRAM_NAME_LENGTH));

  NativeCrashReport native_crash_report;

  // Only add the vmo if it's valid. Otherwise leave the table entry empty.
  if (minidump_vmo.is_valid()) {
    fuchsia::mem::Buffer mem_buffer;
    minidump_vmo.get_size(&mem_buffer.size);
    mem_buffer.vmo = std::move(minidump_vmo);

    native_crash_report.set_minidump(std::move(mem_buffer));
  }

  crash_report.set_specific_report(SpecificCrashReport::WithNative(std::move(native_crash_report)));
  crash_report.mutable_annotations()->push_back(Annotation{
      .key = "crash.process.name",
      .value = process_name,
  });

  if (realm_path.has_value()) {
    crash_report.mutable_annotations()->push_back(Annotation{
        .key = "crash.realm-path",
        .value = realm_path.value(),
    });
  }

  return crash_report;
}

}  // namespace

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
  uint64_t id = next_connection_id_++;
  process_exceptions_[id] = std::move(process_exception);

  auto& exception = process_exceptions_[id];

  if (!exception.has_info()) {
    FileCrashReport(id, std::nullopt, std::nullopt);
    return;
  }

  auto introspect_ptr = services_->Connect<fuchsia::sys::internal::Introspect>();
  introspect_connections_[id] = std::move(introspect_ptr);

  auto& introspect = introspect_connections_[id];

  introspect.set_error_handler([id, broker = GetWeakPtr()](zx_status_t status) {
    FX_PLOGS(ERROR, status) << "Lost connection to fuchsia.sys.internal.Introspect";

    // If the broker is not there anymore, there is nothing more we can do.
    if (!broker)
      return;

    broker->FileCrashReport(id, std::nullopt, std::nullopt);

    // Remove the connection after we have filed the crash report. The connection must be removed at
    // the end of the function because the InterfacePtr that owns the lambda is destroyed when the
    // connection is removed.
    broker->introspect_connections_.erase(id);
  });

  using fuchsia::sys::internal::Introspect_FindComponentByProcessKoid_Result;
  introspect->FindComponentByProcessKoid(
      exception.info().process_koid,
      [id, broker = GetWeakPtr()](Introspect_FindComponentByProcessKoid_Result result) {
        // If the broker is not there anymore, there is nothing more we can do.
        if (!broker)
          return;

        std::optional<std::string> component_url = std::nullopt;
        std::optional<std::string> realm_path = std::nullopt;
        if (result.is_response()) {
          if (!result.response().component_info.has_component_url()) {
            FX_LOGS(ERROR) << "Did not receive a component url";
          } else {
            component_url = result.response().component_info.component_url();
          }

          if (!result.response().component_info.has_realm_path()) {
            FX_LOGS(ERROR) << "Did not receive a realm path";
          } else {
            realm_path = "/" + fxl::JoinStrings(result.response().component_info.realm_path(), "/");
          }
        } else if (result.err() != ZX_ERR_NOT_FOUND) {
          FX_PLOGS(ERROR, result.err()) << "Failed FindComponentByProcessKoid";
        }

        broker->FileCrashReport(id, component_url, realm_path);

        // Remove the connection after we have filed the crash report. The connection must be
        // removed at the end of the function because the InterfacePtr that owns the lambda, and the
        // reference to |broker|, is destroyed when the connection is removed.
        broker->introspect_connections_.erase(id);
      });
}

void ExceptionBroker::FileCrashReport(uint64_t id, std::optional<std::string> component_url,
                                      std::optional<std::string> realm_path) {
  if (process_exceptions_.find(id) == process_exceptions_.end()) {
    return;
  }

  auto& process_exception = process_exceptions_[id];

  std::string process_name;
  zx::vmo minidump_vmo = GenerateMinidumpVMO(process_exception.exception(), &process_name);

  // There is no need to keep the exception around anymore now that the minidump was created.
  process_exception.mutable_exception()->reset();

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

    broker->process_exceptions_.erase(id);

    // Remove the connection after we have removed the exception. The connection must be
    // removed at the end of the function because the InterfacePtr that owns the lambda, and the
    // reference to |broker|, is destroyed when the connection is removed.
    broker->crash_reporter_connections_.erase(id);
  });

  fuchsia::feedback::CrashReport report =
      CreateCrashReport(process_name, component_url, realm_path, std::move(minidump_vmo));

  const std::string program_name =
      (component_url.has_value()) ? component_url.value() : process_name;

  crash_reporter->File(std::move(report), [id, program_name, broker = GetWeakPtr()](
                                              fuchsia::feedback::CrashReporter_File_Result result) {
    if (result.is_err())
      FX_PLOGS(ERROR, result.err()) << "Error filing crash report for " << program_name;

    // If the broker is not there anymore, there is nothing more we can do.
    if (!broker)
      return;

    broker->process_exceptions_.erase(id);

    // Remove the connection after we have removed the exception. The connection must be
    // removed at the end of the function because the InterfacePtr that owns the lambda, and the
    // reference to |broker|, is destroyed when the connection is removed.
    broker->crash_reporter_connections_.erase(id);
  });
}

}  // namespace exception
