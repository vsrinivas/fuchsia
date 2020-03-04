// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/feedback/feedback_agent/annotations/board_info_provider.h"

#include <lib/async/cpp/task.h>
#include <lib/async/default.h>
#include <zircon/errors.h>

#include <optional>

#include "src/developer/feedback/feedback_agent/annotations/aliases.h"
#include "src/developer/feedback/feedback_agent/constants.h"
#include "src/developer/feedback/utils/promise.h"
#include "src/lib/fxl/logging.h"
#include "src/lib/fxl/strings/join_strings.h"
#include "src/lib/syslog/cpp/logger.h"

namespace feedback {
namespace {

using fuchsia::hwinfo::BoardInfo;

}  // namespace

BoardInfoProvider::BoardInfoProvider(const AnnotationKeys& annotations_to_get,
                                     async_dispatcher_t* dispatcher,
                                     std::shared_ptr<sys::ServiceDirectory> services,
                                     zx::duration timeout, Cobalt* cobalt)
    : annotations_to_get_(annotations_to_get),
      dispatcher_(dispatcher),
      services_(services),
      timeout_(timeout),
      cobalt_(cobalt) {
  const auto supported_annotations = GetSupportedAnnotations();
  AnnotationKeys not_supported;
  for (const auto& annotation : annotations_to_get_) {
    if (supported_annotations.find(annotation) == supported_annotations.end()) {
      FX_LOGS(WARNING) << "annotation " << annotation << " not supported by BoardInfoProvider";
      not_supported.insert(annotation);
    }
  }

  for (auto annotation : not_supported) {
    annotations_to_get_.erase(annotation);
  }
}

AnnotationKeys BoardInfoProvider::GetSupportedAnnotations() {
  return {
      kAnnotationHardwareBoardName,
      kAnnotationHardwareBoardRevision,
  };
}

fit::promise<Annotations> BoardInfoProvider::GetAnnotations() {
  auto board_info_ptr = std::make_unique<internal::BoardInfoPtr>(dispatcher_, services_, cobalt_);

  auto board_info = board_info_ptr->GetBoardInfo(timeout_);

  return ExtendArgsLifetimeBeyondPromise(std::move(board_info), /*args=*/std::move(board_info_ptr))
      .and_then([annotations_to_get = annotations_to_get_](const Annotations& board_info) {
        Annotations annotations;

        for (const auto& key : annotations_to_get) {
          if (board_info.find(key) == board_info.end()) {
            FX_LOGS(WARNING) << "Failed to build annotation " << key;
            continue;
          }
          annotations[key] = board_info.at(key);
        }

        return fit::ok(std::move(annotations));
      });
}

namespace internal {
BoardInfoPtr::BoardInfoPtr(async_dispatcher_t* dispatcher,
                           std::shared_ptr<sys::ServiceDirectory> services, Cobalt* cobalt)
    : dispatcher_(dispatcher), services_(services), cobalt_(cobalt) {}

fit::promise<Annotations> BoardInfoPtr::GetBoardInfo(zx::duration timeout) {
  FXL_CHECK(!has_called_get_board_info_) << "GetBoardInfo() is not intended to be called twice";
  has_called_get_board_info_ = true;

  // fit::promise does not have the notion of a timeout. So we post a delayed task that will call
  // the completer after the timeout and return an error.
  //
  // We wrap the delayed task in a CancelableClosure so we can cancel it when the fit::bridge is
  // completed another way.
  //
  // It is safe to pass "this" to the fit::function as the callback won't be callable when the
  // CancelableClosure goes out of scope, which is before "this".
  done_after_timeout_.Reset([this] {
    if (!done_.completer) {
      return;
    }

    FX_LOGS(ERROR) << "Hardware board info retrieval timed out";
    cobalt_->LogOccurrence(TimedOutData::kBoardInfo);
    done_.completer.complete_error();
  });

  board_ptr_ = services_->Connect<fuchsia::hwinfo::Board>();

  const zx_status_t post_status = async::PostDelayedTask(
      dispatcher_, [cb = done_after_timeout_.callback()] { cb(); }, timeout);
  if (post_status != ZX_OK) {
    FX_PLOGS(ERROR, post_status) << "Failed to post delayed task";
    FX_LOGS(ERROR) << "Skipping hardware board info retrieval as it is not safe without a timeout";
    return fit::make_result_promise<Annotations>(fit::error());
  }

  board_ptr_.set_error_handler([this](zx_status_t status) {
    if (!done_.completer) {
      return;
    }

    FX_PLOGS(ERROR, status) << "Lost connection to fuchsia.hwinfo.Board";

    done_.completer.complete_error();
  });

  board_ptr_->GetInfo([this](BoardInfo info) {
    Annotations board_info;

    if (info.has_name()) {
      board_info[kAnnotationHardwareBoardName] = info.name();
    }

    if (info.has_revision()) {
      board_info[kAnnotationHardwareBoardRevision] = info.revision();
    }

    done_.completer.complete_ok(std::move(board_info));
  });

  return done_.consumer.promise_or(fit::error()).then([this](fit::result<Annotations>& result) {
    done_after_timeout_.Cancel();
    return std::move(result);
  });
}

}  // namespace internal
}  // namespace feedback
