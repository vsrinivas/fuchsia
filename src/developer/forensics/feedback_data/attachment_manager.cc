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
#include "src/lib/timekeeper/clock.h"

namespace forensics {
namespace feedback_data {
namespace {

template <typename T>
void EraseNotAllowlisted(std::map<std::string, T>& c, const std::set<std::string>& allowlist) {
  for (auto it = c.begin(); it != c.end();) {
    if (allowlist.count(it->first) == 0) {
      FX_LOGS(INFO) << "Attachment \"" << it->first << "\" not allowlisted, dropping";
      c.erase(it++);
    } else {
      ++it;
    }
  }
}

}  // namespace

AttachmentManager::AttachmentManager(async_dispatcher_t* dispatcher,
                                     std::shared_ptr<sys::ServiceDirectory> services,
                                     timekeeper::Clock* clock, cobalt::Logger* cobalt,
                                     RedactorBase* redactor, const AttachmentKeys& allowlist,
                                     InspectDataBudget* inspect_data_budget)
    : allowlist_(allowlist),
      static_attachments_(feedback_data::GetStaticAttachments(allowlist_)),
      attachment_metrics_(cobalt),
      kernel_log_(dispatcher, services,
                  std::make_unique<backoff::ExponentialBackoff>(zx::min(1), 2u, zx::hour(1)),
                  redactor),
      system_log_(dispatcher, services, clock, redactor, kActiveLoggingPeriod),
      inspect_(dispatcher, services,
               std::make_unique<backoff::ExponentialBackoff>(zx::min(1), 2u, zx::hour(1)),
               inspect_data_budget),

      providers_({
          {kAttachmentLogKernel, &kernel_log_},
          {kAttachmentLogSystem, &system_log_},
          {kAttachmentInspect, &inspect_},
      }) {
  if (allowlist_.empty()) {
    FX_LOGS(WARNING)
        << "Attachment allowlist is empty, no platform attachments will be collected or returned";
  }

  // Remove any static attachments or providers that return attachments not in |allowlist_|.
  EraseNotAllowlisted(static_attachments_, allowlist);
  EraseNotAllowlisted(providers_, allowlist);

  for (const auto& k : allowlist_) {
    const auto num_providers = static_attachments_.count(k) + providers_.count(k);

    FX_CHECK(num_providers == 1) << "Attachment \"" << k << "\" collected by " << num_providers
                                 << " providers";
  }
}

::fpromise::promise<Attachments> AttachmentManager::GetAttachments(const zx::duration timeout) {
  std::vector<std::string> keys;
  std::vector<::fpromise::promise<AttachmentValue>> promises;

  for (auto& [k, p] : providers_) {
    keys.push_back(k);
    promises.push_back(p->Get(timeout));
  }

  auto join = ::fpromise::join_promise_vector(std::move(promises));
  using result_t = decltype(join)::value_type;

  // Start with the static attachments and the add the dynamically collected values to them.
  return join.and_then([this, keys, attachments = static_attachments_](result_t& results) mutable {
    for (size_t i = 0; i < results.size(); ++i) {
      attachments.insert({keys[i], results[i].take_value()});

      // Consider any attachments without content as missing attachments.
      if (auto& attachment = attachments.at(keys[i]);
          attachment.HasValue() && attachment.Value().empty()) {
        attachment = attachment.HasError() ? attachment.Error() : Error::kMissingValue;
      }
    }

    attachment_metrics_.LogMetrics(attachments);

    return ::fpromise::ok(std::move(attachments));
  });
}

void AttachmentManager::DropStaticAttachment(const AttachmentKey& key, const Error error) {
  if (static_attachments_.find(key) == static_attachments_.end()) {
    return;
  }

  static_attachments_.insert_or_assign(key, AttachmentValue(error));
}

}  // namespace feedback_data
}  // namespace forensics
