// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/forensics/feedback_data/annotations/last_reboot_info_provider.h"

#include <lib/async/cpp/task.h>
#include <lib/async/default.h>
#include <lib/syslog/cpp/macros.h>
#include <zircon/errors.h>

#include <optional>

#include "src/developer/forensics/feedback_data/annotations/types.h"
#include "src/developer/forensics/feedback_data/annotations/utils.h"
#include "src/developer/forensics/feedback_data/constants.h"
#include "src/developer/forensics/utils/errors.h"
#include "src/developer/forensics/utils/fit/promise.h"
#include "src/developer/forensics/utils/time.h"
#include "src/lib/fxl/strings/join_strings.h"

namespace forensics {
namespace feedback_data {
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

std::string GetReason(const LastReboot& last_reboot) {
  if (last_reboot.has_reason()) {
    switch (last_reboot.reason()) {
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
      case RebootReason::USER_REQUEST:
        return "user request";
      case RebootReason::SYSTEM_UPDATE:
        return "system update";
      case RebootReason::HIGH_TEMPERATURE:
        return "device too hot";
      case RebootReason::SESSION_FAILURE:
        return "fatal session failure";
      case RebootReason::SYSTEM_FAILURE:
        return "fatal system failure";
      case RebootReason::FACTORY_DATA_RESET:
        return "factory data reset";
      default:
        if (!last_reboot.has_graceful()) {
          return "unknown";
        } else if (last_reboot.graceful()) {
          return "graceful";
        } else {
          return "ungraceful";
        }
    }
  }
  FX_CHECK(last_reboot.has_graceful());
  return (last_reboot.graceful()) ? "graceful" : "ungraceful";
}

}  // namespace

void LastRebootInfoProvider::GetLastReboot() {
  last_reboot_info_ptr_->Get([this](LastReboot last_reboot) {
    std::map<AnnotationKey, std::string> last_reboot_annotations;

    if (last_reboot.has_reason() || last_reboot.has_graceful()) {
      last_reboot_annotations.insert({kAnnotationSystemLastRebootReason, GetReason(last_reboot)});
    }

    if (last_reboot.has_uptime()) {
      const auto uptime = FormatDuration(zx::nsec(last_reboot.uptime()));
      if (uptime.has_value()) {
        last_reboot_annotations.insert({kAnnotationSystemLastRebootUptime, uptime.value()});
      }
    };

    last_reboot_info_ptr_.SetValue(std::move(last_reboot_annotations));
  });
}

}  // namespace feedback_data
}  // namespace forensics
