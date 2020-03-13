// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/feedback/feedback_agent/datastore.h"

#include <lib/fit/promise.h>
#include <lib/zx/time.h>

#include <utility>

#include "src/developer/feedback/feedback_agent/annotations/aliases.h"
#include "src/developer/feedback/feedback_agent/annotations/annotation_provider_factory.h"
#include "src/developer/feedback/feedback_agent/annotations/static_annotations.h"
#include "src/developer/feedback/feedback_agent/attachments/aliases.h"
#include "src/developer/feedback/feedback_agent/attachments/inspect_ptr.h"
#include "src/developer/feedback/feedback_agent/attachments/kernel_log_ptr.h"
#include "src/developer/feedback/feedback_agent/attachments/static_attachments.h"
#include "src/developer/feedback/feedback_agent/attachments/system_log_ptr.h"
#include "src/developer/feedback/feedback_agent/constants.h"
#include "src/lib/syslog/cpp/logger.h"

namespace feedback {
namespace {

// Timeout for a single asynchronous piece of data, e.g., syslog collection.
const zx::duration kTimeout = zx::sec(30);

}  // namespace

Datastore::Datastore(async_dispatcher_t* dispatcher,
                     std::shared_ptr<sys::ServiceDirectory> services, Cobalt* cobalt,
                     const AnnotationKeys& annotation_allowlist,
                     const AttachmentKeys& attachment_allowlist)
    : dispatcher_(dispatcher),
      services_(services),
      cobalt_(cobalt),
      annotation_allowlist_(annotation_allowlist),
      attachment_allowlist_(attachment_allowlist),
      static_annotations_(feedback::GetStaticAnnotations(annotation_allowlist_)),
      static_attachments_(feedback::GetStaticAttachments(attachment_allowlist_)) {
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
       GetProviders(annotation_allowlist_, dispatcher_, services_, kTimeout, cobalt_)) {
    annotations.push_back(provider->GetAnnotations());
  }

  return fit::join_promise_vector(std::move(annotations))
      .and_then(
          [this](std::vector<fit::result<Annotations>>& annotations) -> fit::result<Annotations> {
            // We seed the returned annotations with the static ones.
            Annotations ok_annotations(static_annotations_.begin(), static_annotations_.end());

            // We then augment them with the dynamic ones.
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
      .and_then(
          [this](std::vector<fit::result<Attachment>>& attachments) -> fit::result<Attachments> {
            // We seed the returned attachments with the static ones.
            Attachments ok_attachments(static_attachments_.begin(), static_attachments_.end());

            // We then augment them with the dynamic ones.
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
  return BuildAttachmentValue(key).and_then(
      [key](AttachmentValue& value) -> fit::result<Attachment> {
        return fit::ok(Attachment(key, value));
      });
}

fit::promise<AttachmentValue> Datastore::BuildAttachmentValue(const AttachmentKey& key) {
  if (key == kAttachmentLogKernel) {
    return CollectKernelLog(dispatcher_, services_, kTimeout, cobalt_);
  } else if (key == kAttachmentLogSystem) {
    return CollectSystemLog(dispatcher_, services_, kTimeout, cobalt_);
  } else if (key == kAttachmentInspect) {
    return CollectInspectData(dispatcher_, services_, kTimeout, cobalt_);
  }
  // There are static attachments in the allowlist that we just skip here.
  return fit::make_result_promise<AnnotationValue>(fit::error());
}

}  // namespace feedback
