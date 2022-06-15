// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/a11y/lib/view/flatland_connection.h"

#include <lib/syslog/cpp/macros.h>

namespace a11y {

FlatlandConnection::FlatlandConnection(sys::ComponentContext* context,
                                       const std::string& debug_name)
    : context_(context) {
  context_->svc()->Connect(flatland_.NewRequest());
  flatland_->SetDebugName(debug_name);
  flatland_.events().OnError = fit::bind_member(this, &FlatlandConnection::OnError);
  flatland_.events().OnFramePresented =
      fit::bind_member(this, &FlatlandConnection::OnFramePresented);
  flatland_.events().OnNextFrameBegin =
      fit::bind_member(this, &FlatlandConnection::OnNextFrameBegin);
}

FlatlandConnection::~FlatlandConnection() = default;

void FlatlandConnection::Present() {
  fuchsia::ui::composition::PresentArgs present_args;
  present_args.set_requested_presentation_time(0);
  present_args.set_acquire_fences({});
  present_args.set_release_fences({});
  present_args.set_unsquashable(false);
  Present(std::move(present_args), [](auto) {});
}

void FlatlandConnection::Present(fuchsia::ui::composition::PresentArgs present_args,
                                 OnFramePresentedCallback callback) {
  if (present_credits_ == 0) {
    pending_presents_.emplace(std::move(present_args), std::move(callback));
    FX_DCHECK(pending_presents_.size() <= 3u) << "Too many pending presents.";
    return;
  }
  --present_credits_;

  // In Flatland, release fences apply to the content of the previous present.
  // Keeping track of the previous frame's release fences and swapping ensure we
  // set the correct ones.
  present_args.mutable_release_fences()->swap(previous_present_release_fences_);

  flatland_->Present(std::move(present_args));
  presented_callbacks_.push(std::move(callback));
}

void FlatlandConnection::OnError(fuchsia::ui::composition::FlatlandError error) {
  FX_LOGS(ERROR) << "Flatland error: " << static_cast<int>(error);
}

void FlatlandConnection::OnNextFrameBegin(fuchsia::ui::composition::OnNextFrameBeginValues values) {
  present_credits_ += values.additional_present_credits();
  if (present_credits_ && !pending_presents_.empty()) {
    // Only iterate over the elements once, because they may be added back to
    // the queue.
    while (present_credits_ && !pending_presents_.empty()) {
      PendingPresent present = std::move(pending_presents_.front());
      pending_presents_.pop();
      Present(std::move(present.present_args), std::move(present.callback));
    }
  }
}

void FlatlandConnection::OnFramePresented(fuchsia::scenic::scheduling::FramePresentedInfo info) {
  for (size_t i = 0; i < info.presentation_infos.size(); ++i) {
    presented_callbacks_.front()(info.actual_presentation_time);
    presented_callbacks_.pop();
  }
}

FlatlandConnection::PendingPresent::PendingPresent(
    fuchsia::ui::composition::PresentArgs present_args, OnFramePresentedCallback callback)
    : present_args(std::move(present_args)), callback(std::move(callback)) {}
FlatlandConnection::PendingPresent::~PendingPresent() = default;

FlatlandConnection::PendingPresent::PendingPresent(PendingPresent&& other) = default;
FlatlandConnection::PendingPresent& FlatlandConnection::PendingPresent::operator=(
    PendingPresent&&) = default;

}  // namespace a11y
