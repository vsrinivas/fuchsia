// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/forensics/feedback_data/datastore.h"

#include <lib/fpromise/promise.h>
#include <lib/syslog/cpp/macros.h>

#include <utility>

#include "src/developer/forensics/feedback/device_id_provider.h"
#include "src/developer/forensics/feedback_data/annotations/annotation_provider_factory.h"
#include "src/developer/forensics/feedback_data/annotations/static_annotations.h"
#include "src/developer/forensics/feedback_data/annotations/types.h"
#include "src/developer/forensics/feedback_data/attachments/inspect.h"
#include "src/developer/forensics/feedback_data/attachments/kernel_log_ptr.h"
#include "src/developer/forensics/feedback_data/attachments/static_attachments.h"
#include "src/developer/forensics/feedback_data/attachments/system_log.h"
#include "src/developer/forensics/feedback_data/attachments/types.h"
#include "src/developer/forensics/feedback_data/constants.h"
#include "src/developer/forensics/utils/cobalt/metrics.h"
#include "src/developer/forensics/utils/previous_boot_file.h"
#include "src/lib/fxl/strings/string_printf.h"

namespace forensics {
namespace feedback_data {

Datastore::Datastore(async_dispatcher_t* dispatcher,
                     std::shared_ptr<sys::ServiceDirectory> services, cobalt::Logger* cobalt,
                     const AnnotationKeys& annotation_allowlist,
                     const AttachmentKeys& attachment_allowlist,
                     feedback::AnnotationManager* annotation_manager,
                     feedback::DeviceIdProvider* device_id_provider,
                     InspectDataBudget* inspect_data_budget)
    : dispatcher_(dispatcher),
      services_(services),
      cobalt_(cobalt),
      annotation_allowlist_(annotation_allowlist),
      attachment_allowlist_(attachment_allowlist),
      annotation_manager_(annotation_manager),
      static_attachments_(feedback_data::GetStaticAttachments(attachment_allowlist_)),
      reusable_annotation_providers_(
          GetReusableProviders(dispatcher_, services_, device_id_provider, cobalt_)),
      inspect_data_budget_(inspect_data_budget) {
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
                     std::shared_ptr<sys::ServiceDirectory> services,
                     feedback::DeviceIdProvider* device_id_provider,
                     const char* limit_data_flag_path)
    : dispatcher_(dispatcher),
      services_(services),
      // Somewhat risky, but the Cobalt's constructor sets up a bunch of stuff and this constructor
      // is intended for tests.
      cobalt_(nullptr),
      annotation_allowlist_({}),
      attachment_allowlist_({}),
      // Somewhat risky, but the AnnotationManager depends on a bunch of stuff and this constructor
      // is intended for tests.
      annotation_manager_(nullptr),
      static_attachments_({}),
      reusable_annotation_providers_(
          GetReusableProviders(dispatcher_, services_, device_id_provider, cobalt_)) {}

::fpromise::promise<Annotations> Datastore::GetAnnotations(const zx::duration timeout) {
  if (annotation_allowlist_.empty()) {
    return ::fpromise::make_result_promise<Annotations>(::fpromise::error());
  }

  std::vector<::fpromise::promise<Annotations>> annotations;
  for (auto& provider : reusable_annotation_providers_) {
    annotations.push_back(provider->GetAnnotations(timeout, annotation_allowlist_));
  }

  for (auto& provider : GetSingleUseProviders(dispatcher_, services_, cobalt_)) {
    annotations.push_back(provider->GetAnnotations(timeout, annotation_allowlist_));
  }

  return ::fpromise::join_promise_vector(std::move(annotations))
      .and_then([this](std::vector<::fpromise::result<Annotations>>& annotations)
                    -> ::fpromise::result<Annotations> {
        // We seed the returned annotations with the annotations that are available immediately.
        //
        // The number of platform and non-platform annotations are capped so this is safe to do.
        Annotations ok_annotations(annotation_manager_->ImmediatelyAvailable());

        // We then augment the returned annotations with the dynamic platform annotations.
        for (auto& result : annotations) {
          if (result.is_ok()) {
            for (const auto& [key, value] : result.take_value()) {
              ok_annotations.insert({key, value});
            }
          }
        }

        for (const auto& key : annotation_allowlist_) {
          if (ok_annotations.find(key) == ok_annotations.end()) {
            FX_LOGS(ERROR) << "No provider collected annotation " << key;
            ok_annotations.insert({key, Error::kMissingValue});
          }
        }

        return ::fpromise::ok(ok_annotations);
      });
}

::fpromise::promise<Attachments> Datastore::GetAttachments(const zx::duration timeout) {
  if (attachment_allowlist_.empty()) {
    return ::fpromise::make_result_promise<Attachments>(::fpromise::error());
  }

  std::vector<::fpromise::promise<Attachment>> attachments;
  for (const auto& key : attachment_allowlist_) {
    attachments.push_back(BuildAttachment(key, timeout));
  }

  return ::fpromise::join_promise_vector(std::move(attachments))
      .and_then([this](std::vector<::fpromise::result<Attachment>>& attachments)
                    -> ::fpromise::result<Attachments> {
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
          return ::fpromise::error();
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

        return ::fpromise::ok(ok_attachments);
      });
}

::fpromise::promise<Attachment> Datastore::BuildAttachment(const AttachmentKey& key,
                                                           const zx::duration timeout) {
  return BuildAttachmentValue(key, timeout)
      .and_then([key](AttachmentValue& value) -> ::fpromise::result<Attachment> {
        return ::fpromise::ok(Attachment(key, value));
      });
}

::fpromise::promise<AttachmentValue> Datastore::BuildAttachmentValue(const AttachmentKey& key,
                                                                     const zx::duration timeout) {
  if (key == kAttachmentLogKernel) {
    return CollectKernelLog(dispatcher_, services_,
                            MakeCobaltTimeout(cobalt::TimedOutData::kKernelLog, timeout));
  } else if (key == kAttachmentLogSystem) {
    return CollectSystemLog(dispatcher_, services_,
                            MakeCobaltTimeout(cobalt::TimedOutData::kSystemLog, timeout));
  } else if (key == kAttachmentInspect) {
    return CollectInspectData(dispatcher_, services_,
                              MakeCobaltTimeout(cobalt::TimedOutData::kInspect, timeout),
                              inspect_data_budget_->SizeInBytes());
  }
  // There are static attachments in the allowlist that we just skip here.
  return ::fpromise::make_result_promise<AttachmentValue>(::fpromise::error());
}

void Datastore::DropStaticAttachment(const AttachmentKey& key, const Error error) {
  if (static_attachments_.find(key) == static_attachments_.end()) {
    return;
  }

  static_attachments_.insert_or_assign(key, AttachmentValue(error));
}

fit::Timeout Datastore::MakeCobaltTimeout(cobalt::TimedOutData data, const zx::duration timeout) {
  return fit::Timeout(timeout,
                      /*action=*/[cobalt = cobalt_, data] { cobalt->LogOccurrence(data); });
}

}  // namespace feedback_data
}  // namespace forensics
