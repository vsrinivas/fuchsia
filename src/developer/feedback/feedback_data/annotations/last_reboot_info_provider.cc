// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/feedback/feedback_data/annotations/last_reboot_info_provider.h"

#include <lib/async/cpp/task.h>
#include <lib/async/default.h>
#include <zircon/errors.h>

#include <optional>

#include "src/developer/feedback/feedback_data/annotations/types.h"
#include "src/developer/feedback/feedback_data/annotations/utils.h"
#include "src/developer/feedback/feedback_data/constants.h"
#include "src/developer/feedback/utils/errors.h"
#include "src/developer/feedback/utils/fit/promise.h"
#include "src/developer/feedback/utils/time.h"
#include "src/lib/fxl/logging.h"
#include "src/lib/fxl/strings/join_strings.h"

namespace feedback {
namespace {

using fuchsia::feedback::LastReboot;
using fuchsia::feedback::RebootReason;

const AnnotationKeys kSupportedAnnotations = {
    kAnnotationSystemLastRebootReason,
    kAnnotationSystemLastRebootUptime,
};

}  // namespace

LastRebootInfoProvider::LastRebootInfoProvider(async_dispatcher_t* dispatcher,
                                               std::shared_ptr<sys::ServiceDirectory> services,
                                               cobalt::Logger* cobalt)
    : dispatcher_(dispatcher),
      services_(services),
      cobalt_(cobalt),
      last_reboot_info_ptr_(dispatcher_, services_, [this] { GetLastReboot(); }) {}

::fit::promise<Annotations> LastRebootInfoProvider::GetAnnotations(
    zx::duration timeout, const AnnotationKeys& allowlist) {
  const AnnotationKeys annotations_to_get = RestrictAllowlist(allowlist, kSupportedAnnotations);
  if (annotations_to_get.empty()) {
    return ::fit::make_result_promise<Annotations>(::fit::ok<Annotations>({}));
  }

  return last_reboot_info_ptr_
      .GetValue(fit::Timeout(
          timeout, [=] { cobalt_->LogOccurrence(cobalt::TimedOutData::kLastRebootInfo); }))
      .then([=](const ::fit::result<std::map<AnnotationKey, std::string>, Error>& result) {
        Annotations annotations;

        if (result.is_error()) {
          for (const auto& key : annotations_to_get) {
            annotations.insert({key, AnnotationOr(result.error())});
          }
        } else {
          for (const auto& key : annotations_to_get) {
            const auto& last_reboot = result.value();
            if (last_reboot.find(key) == last_reboot.end()) {
              annotations.insert({key, AnnotationOr(Error::kMissingValue)});
            } else {
              annotations.insert({key, last_reboot.at(key)});
            }
          }
        }
        return ::fit::ok(std::move(annotations));
      });
}

namespace {

std::string ToString(RebootReason reboot_reason) {
  switch (reboot_reason) {
    case RebootReason::GENERIC_GRACEFUL:
      return "generic graceful";
    case RebootReason::COLD:
      return "cold";
    case RebootReason::BRIEF_POWER_LOSS:
      return "brief loss of power";
    case RebootReason::BROWNOUT:
      return "brownout";
    case RebootReason::KERNEL_PANIC:
      return "kernel panic";
    case RebootReason::SYSTEM_OUT_OF_MEMORY:
      return "system out of memory";
    case RebootReason::HARDWARE_WATCHDOG_TIMEOUT:
      return "hardware watchdog timeout";
    case RebootReason::SOFTWARE_WATCHDOG_TIMEOUT:
      return "software watchdog timeout";
  }
}

}  // namespace

void LastRebootInfoProvider::GetLastReboot() {
  last_reboot_info_ptr_->Get([this](LastReboot last_reboot) {
    std::map<AnnotationKey, std::string> last_reboot_annotations;

    if (last_reboot.has_reason()) {
      last_reboot_annotations.insert(
          {kAnnotationSystemLastRebootReason, ToString(last_reboot.reason())});
    };

    if (last_reboot.has_uptime()) {
      const auto uptime = FormatDuration(zx::nsec(last_reboot.uptime()));
      if (uptime.has_value()) {
        last_reboot_annotations.insert({kAnnotationSystemLastRebootUptime, uptime.value()});
      }
    };

    last_reboot_info_ptr_.SetValue(std::move(last_reboot_annotations));
  });
}

}  // namespace feedback
