// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/feedback/feedback_data/annotations/board_info_provider.h"

#include <lib/async/cpp/task.h>
#include <lib/async/default.h>
#include <zircon/errors.h>

#include <optional>

#include "src/developer/feedback/feedback_data/annotations/aliases.h"
#include "src/developer/feedback/feedback_data/annotations/utils.h"
#include "src/developer/feedback/feedback_data/constants.h"
#include "src/developer/feedback/utils/fit/promise.h"
#include "src/lib/fxl/logging.h"
#include "src/lib/fxl/strings/join_strings.h"
#include "src/lib/syslog/cpp/logger.h"

namespace feedback {
namespace {

using fuchsia::hwinfo::BoardInfo;

const AnnotationKeys kSupportedAnnotations = {
    kAnnotationHardwareBoardName,
    kAnnotationHardwareBoardRevision,
};

}  // namespace

BoardInfoProvider::BoardInfoProvider(async_dispatcher_t* dispatcher,
                                     std::shared_ptr<sys::ServiceDirectory> services,
                                     zx::duration timeout, Cobalt* cobalt)
    : dispatcher_(dispatcher), services_(services), timeout_(timeout), cobalt_(cobalt) {}

::fit::promise<Annotations> BoardInfoProvider::GetAnnotations(const AnnotationKeys& allowlist) {
  const AnnotationKeys annotations_to_get = RestrictAllowlist(allowlist, kSupportedAnnotations);
  if (annotations_to_get.empty()) {
    return ::fit::make_result_promise<Annotations>(::fit::ok<Annotations>({}));
  }

  auto board_info_ptr = std::make_unique<internal::BoardInfoPtr>(dispatcher_, services_);

  auto board_info = board_info_ptr->GetBoardInfo(
      fit::Timeout(timeout_, /*action=*/[=] { cobalt_->LogOccurrence(TimedOutData::kBoardInfo); }));

  return fit::ExtendArgsLifetimeBeyondPromise(std::move(board_info),
                                              /*args=*/std::move(board_info_ptr))
      .and_then([=](const Annotations& board_info) {
        Annotations annotations;

        for (const auto& key : annotations_to_get) {
          if (board_info.find(key) == board_info.end()) {
            FX_LOGS(WARNING) << "Failed to build annotation " << key;
            continue;
          }
          annotations[key] = board_info.at(key);
        }

        return ::fit::ok(std::move(annotations));
      });
}

namespace internal {
BoardInfoPtr::BoardInfoPtr(async_dispatcher_t* dispatcher,
                           std::shared_ptr<sys::ServiceDirectory> services)
    : board_ptr_(dispatcher, services) {}

::fit::promise<Annotations> BoardInfoPtr::GetBoardInfo(fit::Timeout timeout) {
  board_ptr_->GetInfo([this](BoardInfo info) {
    if (board_ptr_.IsAlreadyDone()) {
      return;
    }

    Annotations board_info;

    if (info.has_name()) {
      board_info[kAnnotationHardwareBoardName] = info.name();
    }

    if (info.has_revision()) {
      board_info[kAnnotationHardwareBoardRevision] = info.revision();
    }

    board_ptr_.CompleteOk(std::move(board_info));
  });

  return board_ptr_.WaitForDone(std::move(timeout));
}

}  // namespace internal
}  // namespace feedback
