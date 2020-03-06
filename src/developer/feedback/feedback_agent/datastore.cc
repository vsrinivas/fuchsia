// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/feedback/feedback_agent/datastore.h"

#include <lib/fit/promise.h>

#include <utility>

#include "src/developer/feedback/feedback_agent/annotations/aliases.h"
#include "src/developer/feedback/feedback_agent/annotations/annotation_provider_factory.h"
#include "src/developer/feedback/feedback_agent/attachments/aliases.h"
#include "src/developer/feedback/feedback_agent/attachments/inspect_ptr.h"
#include "src/developer/feedback/feedback_agent/attachments/kernel_log_ptr.h"
#include "src/developer/feedback/feedback_agent/attachments/previous_system_log_ptr.h"
#include "src/developer/feedback/feedback_agent/attachments/system_log_ptr.h"
#include "src/developer/feedback/feedback_agent/constants.h"
#include "src/lib/files/file.h"
#include "src/lib/syslog/cpp/logger.h"

namespace feedback {

Datastore::Datastore(async_dispatcher_t* dispatcher,
                     std::shared_ptr<sys::ServiceDirectory> services, Cobalt* cobalt,
                     const zx::duration timeout, const AnnotationKeys& annotation_allowlist,
                     const AttachmentKeys& attachment_allowlist)
    : dispatcher_(dispatcher),
      services_(services),
      cobalt_(cobalt),
      timeout_(timeout),
      annotation_allowlist_(annotation_allowlist),
      attachment_allowlist_(attachment_allowlist) {
  if (annotation_allowlist_.empty()) {
    FX_LOGS(WARNING)
        << "Annotation allowlist is empty, no annotations will be collected or returned";
  }
  if (attachment_allowlist_.empty()) {
    FX_LOGS(WARNING)
        << "Attachment allowlist is empty, no attachments will be collected or returned";
  }
}

fit::promise<Annotations> Datastore::GetAnnotations() {
  if (annotation_allowlist_.empty()) {
    return fit::make_result_promise<Annotations>(fit::error());
  }

  std::vector<fit::promise<Annotations>> annotations;
  for (auto& provider :
       GetProviders(annotation_allowlist_, dispatcher_, services_, timeout_, cobalt_)) {
    annotations.push_back(provider->GetAnnotations());
  }

  return fit::join_promise_vector(std::move(annotations))
      .and_then([](std::vector<fit::result<Annotations>>& annotations) -> fit::result<Annotations> {
        Annotations ok_annotations;
        for (auto& result : annotations) {
          if (result.is_ok()) {
            for (const auto& [key, value] : result.take_value()) {
              ok_annotations[key] = value;
            }
          }
        }

        if (ok_annotations.empty()) {
          return fit::error();
        }

        return fit::ok(ok_annotations);
      });
}

fit::promise<Attachments> Datastore::GetAttachments() {
  if (attachment_allowlist_.empty()) {
    return fit::make_result_promise<Attachments>(fit::error());
  }

  std::vector<fit::promise<Attachment>> attachments;
  for (const auto& key : attachment_allowlist_) {
    attachments.push_back(BuildAttachment(key));
  }

  return fit::join_promise_vector(std::move(attachments))
      .and_then([](std::vector<fit::result<Attachment>>& attachments) -> fit::result<Attachments> {
        Attachments ok_attachments;
        for (auto& result : attachments) {
          if (result.is_ok()) {
            Attachment attachment = result.take_value();
            ok_attachments[attachment.first] = attachment.second;
          }
        }

        if (ok_attachments.empty()) {
          return fit::error();
        }

        return fit::ok(ok_attachments);
      });
}

fit::promise<Attachment> Datastore::BuildAttachment(const AttachmentKey& key) {
  return BuildAttachmentValue(key)
      .and_then([key](AttachmentValue& value) -> fit::result<Attachment> {
        return fit::ok(Attachment(key, value));
      })
      .or_else([key]() {
        FX_LOGS(WARNING) << "Failed to build attachment " << key;
        return fit::error();
      });
}

namespace {

fit::promise<std::string> StringFromFilepath(const std::string& filepath) {
  std::string content;
  if (!files::ReadFileToString(filepath, &content)) {
    FX_LOGS(ERROR) << "Failed to read attachment from file " << filepath;
    return fit::make_result_promise<AttachmentValue>(fit::error());
  }

  return fit::make_result_promise<AttachmentValue>(fit::ok(content));
}

}  // namespace

fit::promise<AttachmentValue> Datastore::BuildAttachmentValue(const AttachmentKey& key) {
  if (key == kAttachmentBuildSnapshot) {
    return StringFromFilepath("/config/build-info/snapshot");
  } else if (key == kAttachmentLogKernel) {
    return CollectKernelLog(dispatcher_, services_, timeout_, cobalt_);
  } else if (key == kAttachmentLogSystemPrevious) {
    return CollectPreviousSystemLog();
  } else if (key == kAttachmentLogSystem) {
    return CollectSystemLog(dispatcher_, services_, timeout_, cobalt_);
  } else if (key == kAttachmentInspect) {
    return CollectInspectData(dispatcher_, services_, timeout_, cobalt_);
  } else {
    FX_LOGS(WARNING) << "Unknown attachment " << key;
    return fit::make_result_promise<AnnotationValue>(fit::error());
  }
}

}  // namespace feedback
