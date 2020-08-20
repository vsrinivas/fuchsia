// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/forensics/feedback_data/datastore.h"

#include <lib/fit/promise.h>
#include <lib/syslog/cpp/macros.h>

#include <utility>

#include "src/developer/forensics/feedback_data/annotations/annotation_provider_factory.h"
#include "src/developer/forensics/feedback_data/annotations/static_annotations.h"
#include "src/developer/forensics/feedback_data/annotations/types.h"
#include "src/developer/forensics/feedback_data/attachments/inspect_ptr.h"
#include "src/developer/forensics/feedback_data/attachments/kernel_log_ptr.h"
#include "src/developer/forensics/feedback_data/attachments/static_attachments.h"
#include "src/developer/forensics/feedback_data/attachments/system_log_ptr.h"
#include "src/developer/forensics/feedback_data/attachments/types.h"
#include "src/developer/forensics/feedback_data/constants.h"
#include "src/developer/forensics/utils/cobalt/metrics.h"
#include "src/lib/fxl/strings/string_printf.h"

namespace forensics {
namespace feedback_data {

Datastore::Datastore(async_dispatcher_t* dispatcher,
                     std::shared_ptr<sys::ServiceDirectory> services, cobalt::Logger* cobalt,
                     const AnnotationKeys& annotation_allowlist,
                     const AttachmentKeys& attachment_allowlist,
                     DeviceIdProvider* device_id_provider)
    : dispatcher_(dispatcher),
      services_(services),
      cobalt_(cobalt),
      annotation_allowlist_(annotation_allowlist),
      attachment_allowlist_(attachment_allowlist),
      static_annotations_(
          feedback_data::GetStaticAnnotations(annotation_allowlist_, device_id_provider)),
      static_attachments_(feedback_data::GetStaticAttachments(attachment_allowlist_, cobalt)),
      reusable_annotation_providers_(GetReusableProviders(dispatcher_, services_, cobalt_)) {
  FX_CHECK(annotation_allowlist_.size() <= kMaxNumPlatformAnnotations)
      << "Requesting more platform annotations than the maximum number of platform annotations "
         "allowed";

  if (annotation_allowlist_.empty()) {
    FX_LOGS(WARNING)
        << "Annotation allowlist is empty, no platform annotations will be collected or returned";
  }
  if (attachment_allowlist_.empty()) {
    FX_LOGS(WARNING)
        << "Attachment allowlist is empty, no platform attachments will be collected or returned";
  }
}

Datastore::Datastore(async_dispatcher_t* dispatcher,
                     std::shared_ptr<sys::ServiceDirectory> services)
    : dispatcher_(dispatcher),
      services_(services),
      // Somewhat risky, but the Cobalt's constructor sets up a bunch of stuff and this constructor
      // is intended for tests.
      cobalt_(nullptr),
      annotation_allowlist_({}),
      attachment_allowlist_({}),
      static_annotations_({}),
      static_attachments_({}),
      reusable_annotation_providers_(GetReusableProviders(dispatcher_, services_, cobalt_)) {}

::fit::promise<Annotations> Datastore::GetAnnotations(const zx::duration timeout) {
  if (annotation_allowlist_.empty() && non_platform_annotations_.empty()) {
    return ::fit::make_result_promise<Annotations>(::fit::error());
  }

  std::vector<::fit::promise<Annotations>> annotations;
  for (auto& provider : reusable_annotation_providers_) {
    annotations.push_back(provider->GetAnnotations(timeout, annotation_allowlist_));
  }

  for (auto& provider : GetSingleUseProviders(dispatcher_, services_, cobalt_)) {
    annotations.push_back(provider->GetAnnotations(timeout, annotation_allowlist_));
  }

  return ::fit::join_promise_vector(std::move(annotations))
      .and_then([this](std::vector<::fit::result<Annotations>>& annotations)
                    -> ::fit::result<Annotations> {
        // We seed the returned annotations with the static platform annotations.
        Annotations ok_annotations(static_annotations_.begin(), static_annotations_.end());

        // We then augment the returned annotations with the dynamic platform annotations.
        for (auto& result : annotations) {
          if (result.is_ok()) {
            for (const auto& [key, value] : result.take_value()) {
              ok_annotations.insert({key, value});
            }
          }
        }

        // We then augment the returned annotations with the non-platform component annotations.
        // We are guaranteed to have enough space left in the returned annotations to do this as
        // we cap the number of platform annotations and cap the number of non-platform annotations
        // to sum to the max number of annotations we can return.
        for (const auto& [key, value] : non_platform_annotations_) {
          ok_annotations.insert({key, value});
        }

        for (const auto& key : annotation_allowlist_) {
          if (ok_annotations.find(key) == ok_annotations.end()) {
            FX_LOGS(ERROR) << "No provider collected annotation " << key;
            ok_annotations.insert({key, AnnotationOr(Error::kMissingValue)});
          }
        }

        return ::fit::ok(ok_annotations);
      });
}

::fit::promise<Attachments> Datastore::GetAttachments(const zx::duration timeout) {
  if (attachment_allowlist_.empty()) {
    return ::fit::make_result_promise<Attachments>(::fit::error());
  }

  std::vector<::fit::promise<Attachment>> attachments;
  for (const auto& key : attachment_allowlist_) {
    attachments.push_back(BuildAttachment(key, timeout));
  }

  return ::fit::join_promise_vector(std::move(attachments))
      .and_then([this](std::vector<::fit::result<Attachment>>& attachments)
                    -> ::fit::result<Attachments> {
        // We seed the returned attachments with the static ones.
        Attachments ok_attachments(static_attachments_.begin(), static_attachments_.end());

        // We then augment them with the dynamic ones.
        for (auto& result : attachments) {
          if (result.is_ok()) {
            Attachment attachment = result.take_value();
            ok_attachments.insert({attachment.first, attachment.second});
          }
        }

        if (ok_attachments.empty()) {
          return ::fit::error();
        }

        // Make sure all attachments are correctly categorized. Any complete or partial attachments
        // that have empty values should be categorized as missing to not be included in the final
        // snapshot and marked as such in the integrity manifest.
        for (auto& [_, attachment] : ok_attachments) {
          if (attachment.HasValue() && attachment.Value().empty()) {
            // In case there is an error and a value, i.e. a partial attachment, preserve the error.
            if (attachment.HasError()) {
              attachment = AttachmentValue(attachment.Error());
            } else {
              attachment = AttachmentValue(Error::kMissingValue);
            }
          }
        }

        return ::fit::ok(ok_attachments);
      });
}

::fit::promise<Attachment> Datastore::BuildAttachment(const AttachmentKey& key,
                                                      const zx::duration timeout) {
  return BuildAttachmentValue(key, timeout)
      .and_then([key](AttachmentValue& value) -> ::fit::result<Attachment> {
        return ::fit::ok(Attachment(key, value));
      });
}

::fit::promise<AttachmentValue> Datastore::BuildAttachmentValue(const AttachmentKey& key,
                                                                const zx::duration timeout) {
  if (key == kAttachmentLogKernel) {
    return CollectKernelLog(dispatcher_, services_,
                            MakeCobaltTimeout(cobalt::TimedOutData::kKernelLog, timeout));
  } else if (key == kAttachmentLogSystem) {
    return CollectSystemLog(dispatcher_, services_,
                            MakeCobaltTimeout(cobalt::TimedOutData::kSystemLog, timeout));
  } else if (key == kAttachmentInspect) {
    return CollectInspectData(dispatcher_, services_,
                              MakeCobaltTimeout(cobalt::TimedOutData::kInspect, timeout));
  }
  // There are static attachments in the allowlist that we just skip here.
  return ::fit::make_result_promise<AttachmentValue>(::fit::error());
}

bool Datastore::TrySetNonPlatformAnnotations(const Annotations& non_platform_annotations) {
  if (non_platform_annotations.size() <= kMaxNumNonPlatformAnnotations) {
    is_missing_non_platform_annotations_ = false;
    non_platform_annotations_ = non_platform_annotations;
    return true;
  } else {
    is_missing_non_platform_annotations_ = true;
    FX_LOGS(WARNING) << fxl::StringPrintf(
        "Ignoring all %lu new non-platform annotations as only %u non-platform annotations are "
        "allowed",
        non_platform_annotations.size(), kMaxNumNonPlatformAnnotations);
    return false;
  }
}

fit::Timeout Datastore::MakeCobaltTimeout(cobalt::TimedOutData data, const zx::duration timeout) {
  return fit::Timeout(timeout,
                      /*action=*/[cobalt = cobalt_, data] { cobalt->LogOccurrence(data); });
}

}  // namespace feedback_data
}  // namespace forensics
