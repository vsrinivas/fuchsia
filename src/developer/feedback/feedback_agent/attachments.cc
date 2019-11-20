// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/feedback/feedback_agent/attachments.h"

#include <fuchsia/mem/cpp/fidl.h>
#include <lib/fit/promise.h>
#include <zircon/errors.h>
#include <zircon/status.h>

#include <cinttypes>
#include <string>
#include <vector>

#include "src/developer/feedback/feedback_agent/attachments/inspect_ptr.h"
#include "src/developer/feedback/feedback_agent/attachments/kernel_log_ptr.h"
#include "src/developer/feedback/feedback_agent/attachments/system_log_ptr.h"
#include "src/developer/feedback/feedback_agent/constants.h"
#include "src/developer/feedback/utils/archive.h"
#include "src/lib/fsl/vmo/file.h"
#include "src/lib/fsl/vmo/sized_vmo.h"
#include "src/lib/fsl/vmo/strings.h"
#include "src/lib/syslog/cpp/logger.h"
#include "third_party/rapidjson/include/rapidjson/document.h"
#include "third_party/rapidjson/include/rapidjson/prettywriter.h"

namespace feedback {
namespace {

using fuchsia::feedback::Annotation;
using fuchsia::feedback::Attachment;

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
                                              std::shared_ptr<sys::ServiceDirectory> services,
                                              const zx::duration timeout,
                                              async::Executor* inspect_executor) {
  if (key == kAttachmentBuildSnapshot) {
    return VmoFromFilename("/config/build-info/snapshot");
  } else if (key == kAttachmentLogKernel) {
    return CollectKernelLog(dispatcher, services, timeout);
  } else if (key == kAttachmentLogSystem) {
    return CollectSystemLog(dispatcher, services, timeout);
  } else if (key == kAttachmentInspect) {
    return CollectInspectData(dispatcher, timeout, inspect_executor);
  } else {
    FX_LOGS(WARNING) << "Unknown attachment " << key;
    return fit::make_result_promise<fuchsia::mem::Buffer>(fit::error());
  }
}

fit::promise<Attachment> BuildAttachment(const std::string& key, async_dispatcher_t* dispatcher,
                                         std::shared_ptr<sys::ServiceDirectory> services,
                                         const zx::duration timeout,
                                         async::Executor* inspect_executor) {
  return BuildValue(key, dispatcher, services, timeout, inspect_executor)
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
    async_dispatcher_t* dispatcher, std::shared_ptr<sys::ServiceDirectory> services,
    const std::set<std::string>& allowlist, const zx::duration timeout,
    async::Executor* inspect_executor) {
  if (allowlist.empty()) {
    FX_LOGS(WARNING) << "Attachment allowlist is empty, nothing to retrieve";
    return {};
  }

  std::vector<fit::promise<Attachment>> attachments;
  for (const auto& key : allowlist) {
    attachments.push_back(BuildAttachment(key, dispatcher, services, timeout, inspect_executor));
  }
  return attachments;
}

void AddAnnotationsAsExtraAttachment(const std::vector<Annotation>& annotations,
                                     std::vector<Attachment>* attachments) {
  rapidjson::Document json;
  json.SetObject();
  for (const auto& annotation : annotations) {
    json.AddMember(rapidjson::StringRef(annotation.key), annotation.value, json.GetAllocator());
  }
  rapidjson::StringBuffer buffer;
  rapidjson::PrettyWriter<rapidjson::StringBuffer> writer(buffer);
  json.Accept(writer);
  if (!writer.IsComplete()) {
    FX_LOGS(WARNING) << "Failed to write annotations as a JSON";
    return;
  }
  std::string json_str(buffer.GetString(), buffer.GetSize());

  Attachment extra_attachment;
  extra_attachment.key = kAttachmentAnnotations;
  if (!fsl::VmoFromString(json_str, &extra_attachment.value)) {
    FX_LOGS(WARNING) << "Failed to write annotations as an extra attachment";
    return;
  }
  attachments->push_back(std::move(extra_attachment));
}

bool BundleAttachments(const std::vector<Attachment>& attachments, Attachment* bundle) {
  if (!::feedback::Archive(attachments, &(bundle->value))) {
    FX_LOGS(ERROR) << "failed to archive attachments into one bundle";
    return false;
  }
  bundle->key = kAttachmentBundle;
  return true;
}

}  // namespace feedback
