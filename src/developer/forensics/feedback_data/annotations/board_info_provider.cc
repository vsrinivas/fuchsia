// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/forensics/feedback_data/annotations/board_info_provider.h"

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
#include "src/lib/fxl/strings/join_strings.h"

namespace forensics {
namespace feedback_data {
namespace {

using fuchsia::hwinfo::BoardInfo;

const AnnotationKeys kSupportedAnnotations = {
    kAnnotationHardwareBoardName,
    kAnnotationHardwareBoardRevision,
};

}  // namespace

BoardInfoProvider::BoardInfoProvider(async_dispatcher_t* dispatcher,
                                     std::shared_ptr<sys::ServiceDirectory> services,
                                     cobalt::Logger* cobalt)
    : dispatcher_(dispatcher),
      services_(services),
      cobalt_(cobalt),
      board_ptr_(dispatcher_, services_, [this] { GetInfo(); }) {}

::fpromise::promise<Annotations> BoardInfoProvider::GetAnnotations(
    zx::duration timeout, const AnnotationKeys& allowlist) {
  const AnnotationKeys to_get = RestrictAllowlist(allowlist, kSupportedAnnotations);
  if (to_get.empty()) {
    return ::fpromise::make_result_promise<Annotations>(::fpromise::ok<Annotations>({}));
  }

  auto on_timeout = [this] { cobalt_->LogOccurrence(cobalt::TimedOutData::kBoardInfo); };
  return board_ptr_.GetValue(fit::Timeout(timeout, std::move(on_timeout)))
      .then([=](const ::fpromise::result<Annotations, Error>& result) {
        Annotations annotations = (result.is_error()) ? WithError(to_get, result.error())
                                                      : ExtractAllowlisted(to_get, result.value());
        return ::fpromise::ok(std::move(annotations));
      });
}

void BoardInfoProvider::GetInfo() {
  board_ptr_->GetInfo([this](BoardInfo info) {
    Annotations annotations({
        {kAnnotationHardwareBoardName, Error::kMissingValue},
        {kAnnotationHardwareBoardRevision, Error::kMissingValue},
    });

    if (info.has_name()) {
      annotations.insert_or_assign(kAnnotationHardwareBoardName, info.name());
    }

    if (info.has_revision()) {
      annotations.insert_or_assign(kAnnotationHardwareBoardRevision, info.revision());
    }

    board_ptr_.SetValue(std::move(annotations));
  });
}

}  // namespace feedback_data
}  // namespace forensics
