// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fdio/io.h>
#include <launchpad/launchpad.h>
#include <lib/zx/handle.h>
#include <lib/zx/log.h>
#include <lib/zx/time.h>
#include <stdio.h>
#include <zircon/process.h>
#include <zircon/processargs.h>
#include <zircon/syscalls.h>
#include <zircon/syscalls/log.h>

#include "third_party/crashpad/client/settings.h"
#include "third_party/crashpad/handler/fuchsia/crash_report_exception_handler.h"
#include "third_party/crashpad/third_party/mini_chromium/mini_chromium/base/files/scoped_file.h"

namespace {

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
    printf("crashpad_analyzer: could not create temp file\n");
    return std::string();
  }

  zx::log log;
  zx_status_t status = zx::log::create(ZX_LOG_FLAG_READABLE, &log);
  if (status != ZX_OK) {
    printf("zx::log::create failed %d\n", status);
    return std::string();
  }

  std::vector<std::string> result;
  char buf[ZX_LOG_RECORD_MAX + 1];
  zx_log_record_t* rec = (zx_log_record_t*)buf;
  for (;;) {
    if (log.read(ZX_LOG_RECORD_MAX, rec, 0) > 0) {
      if (rec->datalen && (rec->data[rec->datalen - 1] == '\n')) {
        rec->datalen--;
      }
      rec->data[rec->datalen] = 0;

      dprintf(fd.get(), "[%05d.%03d] %05" PRIu64 ".%05" PRIu64 "> %s\n",
              (int)(rec->timestamp / 1000000000ULL),
              (int)((rec->timestamp / 1000000ULL) % 1000ULL), rec->pid,
              rec->tid, rec->data);
    } else {
      return std::string(filename);
    }
  }
}

}  // namespace

int main(int argc, char* const argv[]) {
  // On Fuchsia, the crash reporter does not stay resident, so we don't run
  // crashpad_handler here. Instead, directly use CrashReportExceptionHandler
  // and terminate when it has completed.

  std::unique_ptr<crashpad::CrashReportDatabase> database(
      crashpad::CrashReportDatabase::Initialize(
          base::FilePath("/data/crashes")));
  if (!database) {
    return EXIT_FAILURE;
  }

  printf("WARNING: In test configuration, opting in to crash report upload.\n");
  database->GetSettings()->SetUploadsEnabled(true);

  ScopedStoppable upload_thread;
  crashpad::CrashReportUploadThread::Options upload_thread_options;
  upload_thread_options.identify_client_via_url = true;
  upload_thread_options.rate_limit = false;
  upload_thread_options.upload_gzip = true;
  upload_thread_options.watch_pending_reports = true;

  upload_thread.Reset(new crashpad::CrashReportUploadThread(
      database.get(), "http://clients2.google.com/cr/report",
      upload_thread_options));
  upload_thread.Get()->Start();

  std::map<std::string, std::string> annotations;
  annotations["product"] = "Fuchsia";
  annotations["version"] = "unknown";
  char version[64] = {};
  if (zx_system_get_version(version, sizeof(version)) == ZX_OK) {
    annotations["version"] = version;
  }

  std::map<std::string, base::FilePath> attachments;
  ScopedUnlink temp_log_file(GetSystemLogToFile());
  if (temp_log_file.is_valid()) {
    attachments["log"] = base::FilePath(temp_log_file.get());
  }

  crashpad::CrashReportExceptionHandler exception_handler(
      database.get(),
      static_cast<crashpad::CrashReportUploadThread*>(upload_thread.Get()),
      &annotations, &attachments, nullptr);

  zx_handle_t process = zx_get_startup_handle(PA_HND(PA_USER0, 0));
  zx_handle_t thread = zx_get_startup_handle(PA_HND(PA_USER0, 1));
  return exception_handler.HandleExceptionHandles(process, thread)
             ? EXIT_SUCCESS
             : EXIT_FAILURE;
}
