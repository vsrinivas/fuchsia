// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/forensics/feedback_data/attachment_manager.h"

#include <lib/fpromise/promise.h>
#include <lib/syslog/cpp/macros.h>

#include <utility>

#include "src/developer/forensics/feedback_data/attachments/static_attachments.h"
#include "src/developer/forensics/feedback_data/attachments/types.h"
#include "src/developer/forensics/feedback_data/constants.h"

namespace forensics {
namespace feedback_data {

AttachmentManager::AttachmentManager(async_dispatcher_t* dispatcher,
                                     std::shared_ptr<sys::ServiceDirectory> services,
                                     cobalt::Logger* cobalt, RedactorBase* redactor,
                                     const AttachmentKeys& allowlist,
                                     InspectDataBudget* inspect_data_budget)
    : allowlist_(allowlist),
      static_attachments_(feedback_data::GetStaticAttachments(allowlist_)),
      attachment_metrics_(cobalt),
      kernel_log_(dispatcher, services,
                  std::make_unique<backoff::ExponentialBackoff>(zx::min(1), 2u, zx::hour(1)),
                  redactor),
      system_log_(dispatcher, services, &clock_, redactor, kActiveLoggingPeriod),
      inspect_(dispatcher, services,
               std::make_unique<backoff::ExponentialBackoff>(zx::min(1), 2u, zx::hour(1)),
               inspect_data_budget->SizeInBytes()) {
  if (allowlist_.empty()) {
    FX_LOGS(WARNING)
        << "Attachment allowlist is empty, no platform attachments will be collected or returned";
  }
}

::fpromise::promise<Attachments> AttachmentManager::GetAttachments(const zx::duration timeout) {
  if (allowlist_.empty()) {
    return ::fpromise::make_result_promise<Attachments>(::fpromise::error());
  }

  std::vector<::fpromise::promise<Attachment>> attachments;
  for (const auto& key : allowlist_) {
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

        attachment_metrics_.LogMetrics(ok_attachments);

        return ::fpromise::ok(ok_attachments);
      });
}

::fpromise::promise<Attachment> AttachmentManager::BuildAttachment(const AttachmentKey& key,
                                                                   const zx::duration timeout) {
  return BuildAttachmentValue(key, timeout)
      .and_then([key](AttachmentValue& value) -> ::fpromise::result<Attachment> {
        return ::fpromise::ok(Attachment(key, value));
      });
}

::fpromise::promise<AttachmentValue> AttachmentManager::BuildAttachmentValue(
    const AttachmentKey& key, const zx::duration timeout) {
  if (key == kAttachmentLogKernel) {
    return kernel_log_.Get(timeout);
  } else if (key == kAttachmentLogSystem) {
    return system_log_.Get(timeout);
  } else if (key == kAttachmentInspect) {
    return inspect_.Get(timeout);
  }

  // There are static attachments in the allowlist that we just skip here.
  return ::fpromise::make_result_promise<AttachmentValue>(::fpromise::error());
}

void AttachmentManager::DropStaticAttachment(const AttachmentKey& key, const Error error) {
  if (static_attachments_.find(key) == static_attachments_.end()) {
    return;
  }

  static_attachments_.insert_or_assign(key, AttachmentValue(error));
}

}  // namespace feedback_data
}  // namespace forensics
