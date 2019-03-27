// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/feedback_agent/attachments.h"

#include <inttypes.h>

#include <string>
#include <vector>

#include <fuchsia/mem/cpp/fidl.h>
#include <lib/fsl/vmo/file.h>
#include <lib/fsl/vmo/sized_vmo.h>
#include <lib/fsl/vmo/strings.h>
#include <lib/fxl/strings/string_printf.h>
#include <lib/syslog/cpp/logger.h>
#include <lib/zx/debuglog.h>
#include <zircon/errors.h>
#include <zircon/status.h>
#include <zircon/syscalls/log.h>

namespace fuchsia {
namespace feedback {
namespace {

Attachment BuildAttachment(const std::string& key, fuchsia::mem::Buffer value) {
  Attachment attachment;
  attachment.key = key;
  attachment.value = std::move(value);
  return attachment;
}

std::optional<fuchsia::mem::Buffer> GetKernelLog() {
  zx::debuglog log;
  const zx_status_t create_status =
      zx::debuglog::create(zx::resource(), ZX_LOG_FLAG_READABLE, &log);
  if (create_status != ZX_OK) {
    FX_LOGS(ERROR) << "zx::debuglog::create failed: " << create_status << " ("
                   << zx_status_get_string(create_status) << ")";
    return std::nullopt;
  }

  std::string kernel_log;
  zx_log_record_t record;
  while (log.read(/*options=*/0, /*buffer=*/&record,
                  /*buffer_size=*/ZX_LOG_RECORD_MAX) > 0) {
    if (record.datalen && (record.data[record.datalen - 1] == '\n')) {
      record.datalen--;
    }
    record.data[record.datalen] = 0;

    kernel_log += fxl::StringPrintf(
        "[%05d.%03d] %05" PRIu64 ".%05" PRIu64 "> %s\n",
        static_cast<int>(record.timestamp / 1000000000ULL),
        static_cast<int>((record.timestamp / 1000000ULL) % 1000ULL), record.pid,
        record.tid, record.data);
  }

  fsl::SizedVmo vmo;
  if (!fsl::VmoFromString(kernel_log, &vmo)) {
    return std::nullopt;
  }
  return std::move(vmo).ToTransport();
}

std::optional<fuchsia::mem::Buffer> VmoFromFilename(
    const std::string& filename) {
  fsl::SizedVmo vmo;
  if (fsl::VmoFromFilename(filename, &vmo)) {
    return std::move(vmo).ToTransport();
  }
  return std::nullopt;
}

void PushBackIfValuePresent(const std::string& key,
                            std::optional<fuchsia::mem::Buffer> value,
                            std::vector<Attachment>* attachments) {
  if (value.has_value()) {
    attachments->push_back(BuildAttachment(key, std::move(value.value())));
  } else {
    FX_LOGS(WARNING) << "missing attachment " << key;
  }
}

}  // namespace

std::vector<Attachment> GetAttachments() {
  std::vector<Attachment> attachments;
  PushBackIfValuePresent("build.snapshot",
                         VmoFromFilename("/config/build-info/snapshot"),
                         &attachments);
  PushBackIfValuePresent("log.kernel", GetKernelLog(), &attachments);
  return attachments;
}

}  // namespace feedback
}  // namespace fuchsia
