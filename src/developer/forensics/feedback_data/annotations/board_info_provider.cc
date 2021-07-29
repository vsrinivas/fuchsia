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
  const AnnotationKeys annotations_to_get = RestrictAllowlist(allowlist, kSupportedAnnotations);
  if (annotations_to_get.empty()) {
    return ::fpromise::make_result_promise<Annotations>(::fpromise::ok<Annotations>({}));
  }

  return board_ptr_
      .GetValue(fit::Timeout(
          timeout,
          /*action*/ [=] { cobalt_->LogOccurrence(cobalt::TimedOutData::kBoardInfo); }))
      .then([=](const ::fpromise::result<std::map<AnnotationKey, std::string>, Error>& result) {
        Annotations annotations;

        if (result.is_error()) {
          for (const auto& key : annotations_to_get) {
            annotations.insert({key, result.error()});
          }
        } else {
          for (const auto& key : annotations_to_get) {
            const auto& board_info = result.value();
            if (board_info.find(key) == board_info.end()) {
              annotations.insert({key, Error::kMissingValue});
            } else {
              annotations.insert({key, board_info.at(key)});
            }
          }
        }
        return ::fpromise::ok(std::move(annotations));
      });
}

void BoardInfoProvider::GetInfo() {
  board_ptr_->GetInfo([this](BoardInfo info) {
    std::map<AnnotationKey, std::string> board_info;

    if (info.has_name()) {
      board_info[kAnnotationHardwareBoardName] = info.name();
    }

    if (info.has_revision()) {
      board_info[kAnnotationHardwareBoardRevision] = info.revision();
    }

    board_ptr_.SetValue(board_info);
  });
}

}  // namespace feedback_data
}  // namespace forensics
