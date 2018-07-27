// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <utility>

#include <fuchsia/crash/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/component/cpp/startup_context.h>
#include <lib/fdio/io.h>
#include <lib/fxl/files/file.h>
#include <lib/fxl/logging.h>
#include <lib/fxl/strings/trim.h>
#include <lib/zx/handle.h>
#include <lib/zx/log.h>
#include <lib/zx/time.h>
#include <lib/zx/vmo.h>
#include <stdio.h>
#include <third_party/crashpad/client/settings.h>
#include <third_party/crashpad/handler/fuchsia/crash_report_exception_handler.h>
#include <third_party/crashpad/minidump/minidump_file_writer.h>
#include <third_party/crashpad/third_party/mini_chromium/mini_chromium/base/files/scoped_file.h>
#include <third_party/crashpad/util/misc/metrics.h>
#include <third_party/crashpad/util/misc/uuid.h>
#include <third_party/crashpad/util/net/http_body.h>
#include <third_party/crashpad/util/net/http_headers.h>
#include <third_party/crashpad/util/net/http_multipart_builder.h>
#include <third_party/crashpad/util/net/http_transport.h>
#include <third_party/crashpad/util/net/url.h>
#include <zircon/process.h>
#include <zircon/processargs.h>
#include <zircon/status.h>
#include <zircon/syscalls.h>
#include <zircon/syscalls/log.h>
#include <zircon/syscalls/object.h>

namespace {

const char kURL[] = "https://clients2.google.com/cr/report";

class ScopedStoppable {
 public:
  ScopedStoppable() = default;

  ~ScopedStoppable() {
    if (stoppable_) {
      stoppable_->Stop();
    }
  }

  void Reset(crashpad::Stoppable* stoppable) { stoppable_.reset(stoppable); }

  crashpad::Stoppable* Get() { return stoppable_.get(); }

 private:
  std::unique_ptr<crashpad::Stoppable> stoppable_;

  DISALLOW_COPY_AND_ASSIGN(ScopedStoppable);
};

class ScopedUnlink {
 public:
  ScopedUnlink(const std::string& filename) : filename_(filename) {}
  ~ScopedUnlink() { unlink(filename_.c_str()); }

  bool is_valid() const { return !filename_.empty(); }
  const std::string& get() const { return filename_; }

 private:
  std::string filename_;
  DISALLOW_COPY_AND_ASSIGN(ScopedUnlink);
};

std::string GetSystemLogToFile() {
  char filename[] = "/data/crashes/log.XXXXXX";
  base::ScopedFD fd(mkstemp(filename));
  if (fd.get() < 0) {
    FXL_LOG(ERROR) << "could not create temp file";
    return std::string();
  }

  zx::log log;
  zx_status_t status = zx::log::create(ZX_LOG_FLAG_READABLE, &log);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "zx::log::create failed " << status;
    return std::string();
  }

  char buf[ZX_LOG_RECORD_MAX + 1];
  zx_log_record_t* rec = (zx_log_record_t*)buf;
  while (log.read(ZX_LOG_RECORD_MAX, rec, 0) > 0) {
    if (rec->datalen && (rec->data[rec->datalen - 1] == '\n')) {
      rec->datalen--;
    }
    rec->data[rec->datalen] = 0;

    dprintf(fd.get(), "[%05d.%03d] %05" PRIu64 ".%05" PRIu64 "> %s\n",
            (int)(rec->timestamp / 1000000000ULL),
            (int)((rec->timestamp / 1000000ULL) % 1000ULL), rec->pid, rec->tid,
            rec->data);
  }
  return std::string(filename);
}

std::string GetVersion() {
  const char kFilepath[] = "/system/data/build/last-update";
  std::string build_timestamp;
  if (!files::ReadFileToString(kFilepath, &build_timestamp)) {
    FXL_LOG(ERROR) << "Failed to read build timestamp from '" << kFilepath
                   << "'.";
    return "unknown";
  }
  return fxl::TrimString(build_timestamp, "\r\n").ToString();
}

std::string GetPackageName(const zx::process& process) {
  char name[ZX_MAX_NAME_LEN];
  if (process.get_property(ZX_PROP_NAME, name, sizeof(name)) == ZX_OK) {
    return std::string(name);
  }
  return std::string("unknown-package");
}

}  // namespace

int HandleException(zx::process process, zx::thread thread) {
  // On Fuchsia, the crash reporter does not stay resident, so we don't run
  // crashpad_handler here. Instead, directly use CrashReportExceptionHandler
  // and terminate when it has completed.

  std::unique_ptr<crashpad::CrashReportDatabase> database(
      crashpad::CrashReportDatabase::Initialize(
          base::FilePath("/data/crashes")));
  if (!database) {
    return EXIT_FAILURE;
  }

  database->GetSettings()->SetUploadsEnabled(true);

  ScopedStoppable upload_thread;
  crashpad::CrashReportUploadThread::Options upload_thread_options;
  upload_thread_options.identify_client_via_url = true;
  upload_thread_options.rate_limit = false;
  upload_thread_options.upload_gzip = true;
  upload_thread_options.watch_pending_reports = true;

  upload_thread.Reset(new crashpad::CrashReportUploadThread(
      database.get(), kURL, upload_thread_options));
  upload_thread.Get()->Start();

  std::map<std::string, std::string> annotations;
  annotations["product"] = "Fuchsia";
  annotations["version"] = GetVersion();
  // We use ptype to benefit from Chrome's "Process type" handling in the UI.
  annotations["ptype"] = GetPackageName(process);

  std::map<std::string, base::FilePath> attachments;
  ScopedUnlink temp_log_file(GetSystemLogToFile());
  if (temp_log_file.is_valid()) {
    attachments["log"] = base::FilePath(temp_log_file.get());
  }

  crashpad::CrashReportExceptionHandler exception_handler(
      database.get(),
      static_cast<crashpad::CrashReportUploadThread*>(upload_thread.Get()),
      &annotations, &attachments, nullptr);

  return exception_handler.HandleExceptionHandles(process.get(), thread.get())
             ? EXIT_SUCCESS
             : EXIT_FAILURE;
}

int Process(fuchsia::crash::Buffer crashlog) {
  std::unique_ptr<crashpad::CrashReportDatabase> database(
      crashpad::CrashReportDatabase::Initialize(
          base::FilePath("/data/kernel_crashes")));
  if (!database) {
    return EXIT_FAILURE;
  }
  database->GetSettings()->SetUploadsEnabled(true);

  crashpad::CrashReportDatabase::OperationStatus database_status;

  // Create report.
  std::unique_ptr<crashpad::CrashReportDatabase::NewReport> report;
  database_status = database->PrepareNewCrashReport(&report);
  if (database_status != crashpad::CrashReportDatabase::kNoError) {
    return EXIT_FAILURE;
  }

  // Add annotations.
  std::map<std::string, std::string> annotations = {
      {"product", "Fuchsia"},
      // Technically the version after reboot, not when it crashed.
      {"version", GetVersion()},
      // We use ptype to benefit from Chrome's "Process type" handling in the
      // UI.
      {"ptype", "kernel"},
  };

  // Add attachments.
  crashpad::FileWriter* writer = report->AddAttachment("log");
  if (!writer) {
    return EXIT_FAILURE;
  }
  // TODO(frousseau): make crashpad::FileWriter VMO-aware.
  std::unique_ptr<void, decltype(&free)> buffer(malloc(crashlog.size), &free);
  zx_status_t status = crashlog.vmo.read(buffer.get(), 0u, crashlog.size);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "error writing VMO crashlog to buffer: "
                   << zx_status_get_string(status);
    return EXIT_FAILURE;
  }
  writer->Write(buffer.get(), crashlog.size);

  // Finish new report.
  crashpad::UUID local_report_id;
  database_status =
      database->FinishedWritingCrashReport(std::move(report), &local_report_id);
  if (database_status != crashpad::CrashReportDatabase::kNoError) {
    return EXIT_FAILURE;
  }

  // Switch to an "upload" report.
  std::unique_ptr<const crashpad::CrashReportDatabase::UploadReport>
      upload_report;
  database_status =
      database->GetReportForUploading(local_report_id, &upload_report);
  if (database_status != crashpad::CrashReportDatabase::kNoError) {
    return EXIT_FAILURE;
  }

  // Upload report.
  // We have to build the MIME multipart message ourselves as all the Crashpad
  // helpers expect some process to build a minidump from and we don't have one.
  crashpad::HTTPMultipartBuilder http_multipart_builder;
  http_multipart_builder.SetGzipEnabled(true);
  for (const auto& kv : annotations) {
    http_multipart_builder.SetFormData(kv.first, kv.second);
  }
  for (const auto& kv : upload_report->GetAttachments()) {
    http_multipart_builder.SetFileAttachment(kv.first, kv.first, kv.second,
                                             "application/octet-stream");
  }
  std::unique_ptr<crashpad::HTTPTransport> http_transport(
      crashpad::HTTPTransport::Create());
  crashpad::HTTPHeaders content_headers;
  http_multipart_builder.PopulateContentHeaders(&content_headers);
  for (const auto& content_header : content_headers) {
    http_transport->SetHeader(content_header.first, content_header.second);
  }
  http_transport->SetBodyStream(http_multipart_builder.GetBodyStream());
  http_transport->SetTimeout(60.0);  // 1 minute.
  http_transport->SetURL(kURL);

  std::string server_report_id;
  if (!http_transport->ExecuteSynchronously(&server_report_id)) {
    database->SkipReportUpload(
        local_report_id, crashpad::Metrics::CrashSkippedReason::kUploadFailed);
    return EXIT_FAILURE;
  }
  database->RecordUploadComplete(std::move(upload_report), server_report_id);
  FXL_LOG(INFO) << "Successfully uploaded crash report at "
                   "https://crash.corp.google.com/"
                << server_report_id;

  return EXIT_SUCCESS;
}

class AnalyzerImpl : public fuchsia::crash::Analyzer {
 public:
  // fuchsia::crash::Analyzer:
  void Analyze(::zx::process process, ::zx::thread thread,
               AnalyzeCallback callback) override {
    callback();
    HandleException(std::move(process), std::move(thread));
  }

  void Process(fuchsia::crash::Buffer crashlog,
               ProcessCallback callback) override {
    callback();
    if (::Process(fbl::move(crashlog)) == EXIT_FAILURE) {
      FXL_LOG(ERROR) << "Failed to process VMO crashlog. Won't retry.";
    }
  }
};

int main(int argc, const char** argv) {
  async::Loop loop(&kAsyncLoopConfigAttachToThread);
  std::unique_ptr<component::StartupContext> app_context(
      component::StartupContext::CreateFromStartupInfo());

  AnalyzerImpl analyzer;

  fidl::BindingSet<fuchsia::crash::Analyzer> bindings;

  app_context->outgoing().AddPublicService(bindings.GetHandler(&analyzer));

  loop.Run();

  return 0;
}
