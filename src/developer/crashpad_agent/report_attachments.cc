// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/crashpad_agent/report_attachments.h"

#include <memory>
#include <string>

#include <fuchsia/mem/cpp/fidl.h>
#include <src/lib/fxl/strings/concatenate.h>
#include <src/lib/fxl/strings/trim.h>
#include <lib/syslog/cpp/logger.h>
#include <lib/zx/debuglog.h>
#include <zircon/errors.h>
#include <zircon/status.h>
#include <zircon/syscalls/log.h>
#include <zircon/types.h>

#include "src/lib/files/file.h"
#include "src/lib/files/path.h"
#include "third_party/crashpad/minidump/minidump_file_writer.h"
#include "third_party/crashpad/util/file/file_writer.h"

namespace fuchsia {
namespace crash {

const char kAttachmentKernelLog[] = "kernel_log";
const char kAttachmentBuildInfoSnapshot[] = "build.snapshot";

std::string WriteKernelLogToFile(const std::string& dir) {
  std::string filename =
      files::SimplifyPath(fxl::Concatenate({dir, "/kernel_log.XXXXXX"}));
  base::ScopedFD fd(mkstemp(filename.data()));
  if (fd.get() < 0) {
    FX_LOGS(ERROR) << "could not create temp file";
    return std::string();
  }

  zx::debuglog log;
  zx_status_t status =
      zx::debuglog::create(zx::resource(), ZX_LOG_FLAG_READABLE, &log);
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "zx::debuglog::create failed " << status;
    return std::string();
  }

  char buf[ZX_LOG_RECORD_MAX + 1];
  zx_log_record_t* rec = (zx_log_record_t*)buf;
  while (log.read(/*options=*/0, /*buffer=*/rec,
                  /*buffer_size=*/ZX_LOG_RECORD_MAX) > 0) {
    if (rec->datalen && (rec->data[rec->datalen - 1] == '\n')) {
      rec->datalen--;
    }
    rec->data[rec->datalen] = 0;

    dprintf(fd.get(), "[%05d.%03d] %05" PRIu64 ".%05" PRIu64 "> %s\n",
            (int)(rec->timestamp / 1000000000ULL),
            (int)((rec->timestamp / 1000000ULL) % 1000ULL), rec->pid, rec->tid,
            rec->data);
  }
  return filename;
}

namespace {

zx_status_t WriteVMO(crashpad::FileWriter* writer,
                     const fuchsia::mem::Buffer vmo) {
  // TODO(frousseau): make crashpad::FileWriter VMO-aware.
  std::unique_ptr<void, decltype(&free)> buffer(malloc(vmo.size), &free);
  zx_status_t status = vmo.vmo.read(buffer.get(), 0u, vmo.size);
  if (status != ZX_OK) {
    return ZX_ERR_INTERNAL;
  }
  writer->Write(buffer.get(), vmo.size);
  return ZX_OK;
}

zx_status_t AddAttachment(crashpad::CrashReportDatabase::NewReport* report,
                          const std::string& attachment_filename,
                          fuchsia::mem::Buffer content_buffer,
                          const std::string& debug_name) {
  crashpad::FileWriter* writer = report->AddAttachment(attachment_filename);
  if (!writer) {
    return ZX_ERR_INTERNAL;
  }
  if (zx_status_t status = WriteVMO(writer, std::move(content_buffer));
      status != ZX_OK) {
    FX_LOGS(ERROR) << "error writing " << debug_name
                   << " to file: " << zx_status_get_string(status);
    return ZX_ERR_INTERNAL;
  }
  return ZX_OK;
}

zx_status_t AddAttachment(crashpad::CrashReportDatabase::NewReport* report,
                          const std::string& attachment_filename,
                          const std::string& content_filepath) {
  crashpad::FileWriter* writer = report->AddAttachment(attachment_filename);
  if (!writer) {
    return ZX_ERR_INTERNAL;
  }
  std::string content;
  if (!files::ReadFileToString(content_filepath, &content)) {
    FX_LOGS(ERROR) << "Failed to read content from '" << content_filepath
                   << "'.";
    return ZX_ERR_INTERNAL;
  }
  content = fxl::TrimString(content, "\r\n").ToString();
  writer->Write(content.data(), content.size());
  return ZX_OK;
}

}  // namespace

zx_status_t AddManagedRuntimeExceptionAttachments(
    crashpad::CrashReportDatabase::NewReport* report,
    ManagedRuntimeLanguage language, fuchsia::mem::Buffer stack_trace) {
  const std::string& stack_trace_filename =
      (language == ManagedRuntimeLanguage::DART)
          ? "DartError"  // the crash server expects a specific name for Dart.
          : "stack_trace";
  AddAttachment(report, stack_trace_filename, std::move(stack_trace),
                "stack trace");
  AddAttachment(report, kAttachmentBuildInfoSnapshot,
                "/config/build-info/snapshot");
  // TODO(DX-581): attach syslog as well.
  // TODO(DX-748): attach kernel log as well.
  return ZX_OK;
}

zx_status_t AddKernelPanicAttachments(
    crashpad::CrashReportDatabase::NewReport* report,
    fuchsia::mem::Buffer crashlog) {
  AddAttachment(report, "log", std::move(crashlog), "kernel panic crashlog");
  AddAttachment(report, kAttachmentBuildInfoSnapshot,
                "/config/build-info/snapshot");
  return ZX_OK;
}

}  // namespace crash
}  // namespace fuchsia
