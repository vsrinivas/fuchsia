// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#include "src/developer/exception_broker/exception_broker.h"

#include <lib/fit/defer.h>
#include <zircon/status.h>

#include <third_party/crashpad/util/file/string_file.h>

#include "src/developer/exception_broker/crash_report_generation.h"
#include "src/developer/exception_broker/json_utils.h"
#include "src/lib/files/directory.h"
#include "src/lib/files/file.h"
#include "src/lib/syslog/cpp/logger.h"

namespace fuchsia {
namespace exception {

namespace {

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
  FXL_DCHECK(services_);
}

fxl::WeakPtr<ExceptionBroker> ExceptionBroker::GetWeakPtr() { return weak_factory_.GetWeakPtr(); }

// OnException -------------------------------------------------------------------------------------

namespace {

fuchsia::feedback::CrashReport CreateCrashReport(const std::string& process_name,
                                                 zx::vmo minidump_vmo) {
  using namespace fuchsia::feedback;

  CrashReport crash_report;
  crash_report.set_program_name(process_name);

  NativeCrashReport native_crash_report;

  // Only add the vmo if it's valid. Otherwise leave the table entry empty.
  if (minidump_vmo.is_valid()) {
    fuchsia::mem::Buffer mem_buffer;
    minidump_vmo.get_size(&mem_buffer.size);
    mem_buffer.vmo = std::move(minidump_vmo);

    native_crash_report.set_minidump(std::move(mem_buffer));
  }

  crash_report.set_specific_report(SpecificCrashReport::WithNative(std::move(native_crash_report)));

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
  std::string process_name;
  zx::vmo minidump_vmo = GenerateMinidumpVMO(process_exception.exception(), &process_name);

  // Create a new connection to the crash reporter and keep track of it.
  uint64_t id = next_connection_id_++;
  auto crash_reporter_ptr = services_->Connect<fuchsia::feedback::CrashReporter>();
  connections_[id] = std::move(crash_reporter_ptr);

  // Get the ref to the crash reporter.
  auto& crash_reporter = connections_[id];

  crash_reporter.set_error_handler([id, broker = GetWeakPtr()](zx_status_t status) {
    FX_PLOGS(ERROR, status) << "Lost connection to fuchsia.feedback.CrashReporter";

    // If the broker is not there anymore, there is nothing more we can do.
    if (!broker)
      return;

    // Remove the connection.
    broker->connections_.erase(id);
  });

  fuchsia::feedback::CrashReport report = CreateCrashReport(process_name, std::move(minidump_vmo));
  crash_reporter->File(std::move(report), [id, process_name, broker = GetWeakPtr()](
                                              fuchsia::feedback::CrashReporter_File_Result result) {
    if (result.is_err())
      FX_PLOGS(ERROR, result.err()) << "Error filing crash report for " << process_name;

    // If the broker is not there anymore, there is nothing more we can do.
    if (!broker)
      return;

    // Remove the connection.
    broker->connections_.erase(id);
  });
}

}  // namespace exception
}  // namespace fuchsia
