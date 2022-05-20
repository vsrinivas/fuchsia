// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/forensics/feedback_data/datastore.h"

#include <lib/fpromise/promise.h>
#include <lib/syslog/cpp/macros.h>

#include <utility>

#include "src/developer/forensics/feedback/redactor_factory.h"
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
                     RedactorBase* redactor, const AttachmentKeys& attachment_allowlist,
                     InspectDataBudget* inspect_data_budget)
    : dispatcher_(dispatcher),
      services_(services),
      cobalt_(cobalt),
      redactor_(redactor),
      attachment_allowlist_(attachment_allowlist),
      static_attachments_(feedback_data::GetStaticAttachments(attachment_allowlist_)),
      inspect_data_budget_(inspect_data_budget),
      system_log_(dispatcher_, services_, &clock_, redactor_, kActiveLoggingPeriod),
      inspect_(dispatcher_, services_,
               std::make_unique<backoff::ExponentialBackoff>(zx::min(1), 2u, zx::hour(1)),
               inspect_data_budget_->SizeInBytes()) {
  if (attachment_allowlist_.empty()) {
    FX_LOGS(WARNING)
        << "Attachment allowlist is empty, no platform attachments will be collected or returned";
  }
}

Datastore::Datastore(async_dispatcher_t* dispatcher,
                     std::shared_ptr<sys::ServiceDirectory> services,
                     const char* limit_data_flag_path)
    : dispatcher_(dispatcher),
      services_(services),
      // Somewhat risky, but the Cobalt's constructor sets up a bunch of stuff and this constructor
      // is intended for tests.
      cobalt_(nullptr),
      // Somewhat risky, but redaction isn't needed in tests that use this constructor.
      redactor_(nullptr),
      attachment_allowlist_({}),
      // Somewhat risky, but the AnnotationManager depends on a bunch of stuff and this constructor
      // is intended for tests.
      static_attachments_({}),
      system_log_(dispatcher_, services_, &clock_, redactor_, zx::sec(30)),
      inspect_(dispatcher_, services_, nullptr, std::nullopt) {}

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
                            MakeCobaltTimeout(cobalt::TimedOutData::kKernelLog, timeout),
                            redactor_);
  } else if (key == kAttachmentLogSystem) {
    return system_log_.Get(timeout).and_then([this](AttachmentValue& log) {
      if (log.HasError() && log.Error() == Error::kTimeout) {
        cobalt_->LogOccurrence(cobalt::TimedOutData::kSystemLog);
      }
      return ::fpromise::ok(std::move(log));
    });
  } else if (key == kAttachmentInspect) {
    return inspect_.Get(timeout).and_then([this](AttachmentValue& inspect) {
      if (inspect.HasError() && inspect.Error() == Error::kTimeout) {
        cobalt_->LogOccurrence(cobalt::TimedOutData::kInspect);
      }
      return ::fpromise::ok(std::move(inspect));
    });
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
