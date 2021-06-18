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
        present_in_flight_ = false;

        size_t num_presents_handled = info.presentation_infos.size();
        FX_DCHECK(present_callbacks_.size() >= num_presents_handled);

        // Fire the callbacks in order. We need to be careful in the case where QueuePresent() was
        // called when our |presents_allowed_| budget was 0. In this case, QueuePresent() callbacks
        // would be coalesced, and a single Present2 callback would trigger multiple
        // QueuePresentCallbacks.
        for (size_t i = 0; i < num_presents_handled; ++i) {
          auto callbacks = std::move(present_callbacks_.front());

          for (auto& callback : callbacks) {
            callback();
          }

          present_callbacks_.pop();
        }

        // It is possible that in between QueuePresent() returning and Present2 being handled on the
        // Scenic side, an OnFramePresented() event can fire, leading to this value being out of
        // sync. However, given that SafePresenter only has at most one Present2 in flight, this
        // does not affect the following calculation. If SafePresenter allows multiple Present2s in
        // flight later, then the following line should be something like:
        // presents_allowed_ = info.num_presents_allowed - num_unhandled_presents.
        presents_allowed_ = info.num_presents_allowed > 0;

        // Since we only have one Present2() call in progress at once, this must be true.
        FX_DCHECK(presents_allowed_);

        if (!present_queue_empty_ && presents_allowed_) {
          QueuePresentHelper();
        }
      });

  // The value of |presents_allowed_| is 0 until it is set in the RequestPresentationTimes()
  // callback. While Scenic ensures a session will have a Present2 budget of at least 1 to begin
  // with, there is no guarantee that Present2 was never called prior to SafePresenter being
  // initialized.
  session_->RequestPresentationTimes(
      /*requested_prediction_span=*/0,
      [this](fuchsia::scenic::scheduling::FuturePresentationTimes info) {
        presents_allowed_ = info.remaining_presents_in_flight_allowed > 0;
        // Since we only have one Present2() call in progress at once, this must be true.
        FX_DCHECK(presents_allowed_);
        FX_DCHECK(!present_in_flight_);

        if (!present_queue_empty_ && presents_allowed_) {
          QueuePresentHelper();
        }
      });
}

void SafePresenter::QueuePresent(QueuePresentCallback callback) {
  FX_DCHECK(session_);

  // Present to Scenic immediately, if we can.
  if (presents_allowed_ && !present_in_flight_) {
    present_callbacks_.push({});
    present_callbacks_.back().push_back(std::move(callback));

    QueuePresentHelper();
    return;
  }

  // We cannot present immediately, so add the callback to the backlog to be presenter later.
  if (present_queue_empty_) {
    present_queue_empty_ = false;
    present_callbacks_.push({});
  }
  present_callbacks_.back().push_back(std::move(callback));
}

void SafePresenter::QueuePresentHelper() {
  FX_DCHECK(presents_allowed_);
  FX_DCHECK(!present_in_flight_);

  presents_allowed_ = false;
  present_in_flight_ = true;
  present_queue_empty_ = true;
  session_->Present2(/*requested_presentation_time=*/0, /*requested_prediction_span=*/0,
                     [](auto) {});
}

}  // namespace root_presenter
