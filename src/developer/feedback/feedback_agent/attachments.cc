// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#include "src/developer/feedback/feedback_agent/attachments.h"

#include <fuchsia/mem/cpp/fidl.h>
#include <inttypes.h>
#include <lib/fit/promise.h>
#include <lib/fsl/vmo/file.h>
#include <lib/fsl/vmo/sized_vmo.h>
#include <lib/fsl/vmo/strings.h>
#include <lib/syslog/cpp/logger.h>
#include <lib/zx/debuglog.h>
#include <zircon/errors.h>
#include <zircon/status.h>
#include <zircon/syscalls/log.h>

#include <string>
#include <vector>

#include "src/developer/feedback/feedback_agent/inspect_ptr.h"
#include "src/developer/feedback/feedback_agent/log_listener_ptr.h"
#include "src/lib/fxl/strings/string_printf.h"

namespace fuchsia {
namespace feedback {
namespace {

// This is actually synchronous, but we return a fit::promise to match other attachment providers
// that are asynchronous.
fit::promise<fuchsia::mem::Buffer> GetKernelLog() {
  zx::debuglog log;
  const zx_status_t create_status =
      zx::debuglog::create(zx::resource(), ZX_LOG_FLAG_READABLE, &log);
  if (create_status != ZX_OK) {
    FX_PLOGS(ERROR, create_status) << "zx::debuglog::create failed";
    return fit::make_result_promise<fuchsia::mem::Buffer>(fit::error());
  }

  // zx_log_record_t has a flexible array member, so we need to allocate the buffer explicitly.
  char buf[ZX_LOG_RECORD_MAX + 1];
  zx_log_record_t* record = reinterpret_cast<zx_log_record_t*>(buf);
  std::string kernel_log;
  while (log.read(/*options=*/0, /*buffer=*/record,
                  /*buffer_size=*/ZX_LOG_RECORD_MAX) > 0) {
    if (record->datalen && (record->data[record->datalen - 1] == '\n')) {
      record->datalen--;
    }
    record->data[record->datalen] = 0;

    kernel_log += fxl::StringPrintf("[%05d.%03d] %05" PRIu64 ".%05" PRIu64 "> %s\n",
                                    static_cast<int>(record->timestamp / 1000000000ULL),
                                    static_cast<int>((record->timestamp / 1000000ULL) % 1000ULL),
                                    record->pid, record->tid, record->data);
  }

  fsl::SizedVmo vmo;
  if (!fsl::VmoFromString(kernel_log, &vmo)) {
    FX_LOGS(ERROR) << "Failed to convert kernel log string to vmo";
    return fit::make_result_promise<fuchsia::mem::Buffer>(fit::error());
  }
  return fit::make_ok_promise(std::move(vmo).ToTransport());
}

// This is actually synchronous, but we return a fit::promise to match other attachment providers
// that are asynchronous.
fit::promise<fuchsia::mem::Buffer> VmoFromFilename(const std::string& filename) {
  fsl::SizedVmo vmo;
  if (!fsl::VmoFromFilename(filename, &vmo)) {
    FX_LOGS(ERROR) << "Failed to read VMO from file " << filename;
    return fit::make_result_promise<fuchsia::mem::Buffer>(fit::error());
  }
  return fit::make_ok_promise(std::move(vmo).ToTransport());
}

fit::promise<fuchsia::mem::Buffer> BuildValue(const std::string& key,
                                              async_dispatcher_t* dispatcher,
                                              std::shared_ptr<::sys::ServiceDirectory> services,
                                              const zx::duration timeout) {
  if (key == "build.snapshot.xml") {
    return VmoFromFilename("/config/build-info/snapshot");
  } else if (key == "log.kernel.txt") {
    return GetKernelLog();
  } else if (key == "log.system.txt") {
    return CollectSystemLog(dispatcher, services, timeout);
  } else if (key == "inspect.json") {
    return CollectInspectData(dispatcher, timeout);
  } else {
    FX_LOGS(WARNING) << "Unknown attachment " << key;
    return fit::make_result_promise<fuchsia::mem::Buffer>(fit::error());
  }
}

fit::promise<Attachment> BuildAttachment(const std::string& key, async_dispatcher_t* dispatcher,
                                         std::shared_ptr<::sys::ServiceDirectory> services,
                                         const zx::duration timeout) {
  return BuildValue(key, dispatcher, services, timeout)
      .and_then([key](fuchsia::mem::Buffer& vmo) -> fit::result<Attachment> {
        Attachment attachment;
        attachment.key = key;
        attachment.value = std::move(vmo);
        return fit::ok(std::move(attachment));
      })
      .or_else([key]() {
        FX_LOGS(WARNING) << "Failed to build attachment " << key;
        return fit::error();
      });
}

}  // namespace

std::vector<fit::promise<Attachment>> GetAttachments(
    async_dispatcher_t* dispatcher, std::shared_ptr<::sys::ServiceDirectory> services,
    const std::set<std::string>& allowlist, const zx::duration timeout) {
  if (allowlist.empty()) {
    FX_LOGS(WARNING) << "Attachment allowlist is empty, nothing to retrieve";
    return {};
  }

  std::vector<fit::promise<Attachment>> attachments;
  for (const auto& key : allowlist) {
    attachments.push_back(BuildAttachment(key, dispatcher, services, timeout));
  }
  return attachments;
}

}  // namespace feedback
}  // namespace fuchsia
