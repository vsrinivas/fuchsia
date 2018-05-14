// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fdio/io.h>
#include <launchpad/launchpad.h>
#include <lib/zx/handle.h>
#include <lib/zx/time.h>
#include <stdio.h>
#include <zircon/process.h>
#include <zircon/processargs.h>
#include <zircon/syscalls.h>

#include "third_party/crashpad/client/settings.h"
#include "third_party/crashpad/handler/fuchsia/crash_report_exception_handler.h"

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

  crashpad::CrashReportExceptionHandler exception_handler(
      database.get(),
      static_cast<crashpad::CrashReportUploadThread*>(upload_thread.Get()),
      &annotations, nullptr);

  zx_handle_t process = zx_get_startup_handle(PA_HND(PA_USER0, 0));
  zx_handle_t thread = zx_get_startup_handle(PA_HND(PA_USER0, 1));
  return exception_handler.HandleExceptionHandles(process, thread)
             ? EXIT_SUCCESS
             : EXIT_FAILURE;
}
