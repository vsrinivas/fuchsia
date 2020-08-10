// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_BIN_ROOT_PRESENTER_SAFE_PRESENTER_H_
#define SRC_UI_BIN_ROOT_PRESENTER_SAFE_PRESENTER_H_

#include <lib/ui/scenic/cpp/session.h>

#include <queue>
namespace root_presenter {

using QueuePresentCallback = fit::function<void()>;

// This class allows users to call Present2 without exceeding the budget of Present2s allowed
// by the fuchsia.ui.scenic.Session protocol. By limiting the number of Present2 calls,
// SafePresenter ensures that the Session will not be shut down, thus, users of SafePresenter should
// not call Present2 on their own.
//
// More information can be found in the fuchsia.scenic.scheduling FIDL library, in the
// prediction_info.fidl file.
class SafePresenter {
 public:
  SafePresenter(scenic::Session* session);

  // If possible, QueuePresent() immediately presents to the underlying Session. If the maximum
  // amount of pending Present2()s has been reached, SafePresenter presents at the next earliest
  // possible time. QueuePresent() ensures that callbacks get processed in FIFO order.
  void QueuePresent(QueuePresentCallback callback);

 private:
  void QueuePresentHelper();

  scenic::Session* session_ = nullptr;
  std::queue<std::vector<QueuePresentCallback>> present_callbacks_;
  // |presents_allowed_| is true if Scenic allows at least one more Present2() call.
  bool presents_allowed_ = 0;

  // |present_in_flight_| is true if there is an unhandled Present2() call.
  bool present_in_flight_ = false;

  // |present_queue_empty_| is true if there are no unhandled QueuePresent() calls.
  bool present_queue_empty_ = true;
};

}  // namespace root_presenter

#endif  // SRC_UI_BIN_ROOT_PRESENTER_SAFE_PRESENTER_H_
