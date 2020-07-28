// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "safe_presenter.h"

#include <fuchsia/ui/gfx/cpp/fidl.h>
#include <fuchsia/ui/scenic/cpp/fidl.h>
#include <lib/syslog/cpp/macros.h>

namespace root_presenter {

SafePresenter::SafePresenter(scenic::Session* session) : session_(session) {
  FX_DCHECK(session_);

  session_->set_on_frame_presented_handler(
      [this](fuchsia::scenic::scheduling::FramePresentedInfo info) {
        size_t num_presents_handled = info.presentation_infos.size();

        presents_allowed_ = info.num_presents_allowed;
        FX_DCHECK(presents_allowed_ >= 0);

        // Fire the callbacks in order. We need to be careful in the case where QueuePresent() was
        // called when our |presents_allowed_| budget was 0. In this case, QueuePresent() callbacks
        // would be coalesced, and a single Present2 callback would trigger multiple
        // QueuePresentCallbacks.
        FX_DCHECK(present_callbacks_.size() >= num_presents_handled);
        for (size_t i = 0; i < num_presents_handled; ++i) {
          auto callbacks = std::move(present_callbacks_.front());

          for (auto& callback : callbacks) {
            callback();
          }

          present_callbacks_.pop();
        }

        if (presents_pending_ && presents_allowed_ > 0) {
          presents_allowed_--;
          presents_pending_ = false;
          session_->Present2(/*requested_presentation_time=*/0, /*requested_prediction_span=*/0,
                             [](auto) {});
        }
      });

  // The value of |presents_allowed_| is 0 until it is set in the RequestPresentationTimes()
  // callback. While Scenic ensures a session will have a Present2 budget of at least 1 to begin
  // with, there is no guarantee that Present2 was never called prior to SafePresenter being
  // initialized.
  session_->RequestPresentationTimes(
      /*requested_prediction_span=*/0,
      [this](fuchsia::scenic::scheduling::FuturePresentationTimes info) {
        presents_allowed_ = info.remaining_presents_in_flight_allowed;
        FX_DCHECK(presents_allowed_ >= 0);

        if (presents_pending_ && presents_allowed_ > 0) {
          presents_allowed_--;
          presents_pending_ = false;
          session_->Present2(/*requested_presentation_time=*/0, /*requested_prediction_span=*/0,
                             [](auto) {});
        }
      });
}

void SafePresenter::QueuePresent(QueuePresentCallback callback) {
  FX_DCHECK(session_);

  if (presents_allowed_ > 0) {
    presents_allowed_--;
    present_callbacks_.push({});
    present_callbacks_.back().push_back(std::move(callback));

    session_->Present2(/*requested_presentation_time=*/0, /*requested_prediction_span=*/0,
                       [](auto) {});
    return;
  }

  if (!presents_pending_) {
    presents_pending_ = true;
    present_callbacks_.push({});
  }

  present_callbacks_.back().push_back(std::move(callback));
}

}  // namespace root_presenter
