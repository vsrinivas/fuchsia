// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_VNEXT_LIB_STREAM_SINK_CLEAR_REQUEST_H_
#define SRC_MEDIA_VNEXT_LIB_STREAM_SINK_CLEAR_REQUEST_H_

#include <lib/syslog/cpp/macros.h>
#include <lib/zx/eventpair.h>

#include <memory>

namespace fmlib {

// A clear request for use with the FIDL stream transport.
class ClearRequest {
 public:
  // Constructs a |ClearRequest|. |completion_fence| is optional.
  ClearRequest(bool hold_last_frame, zx::eventpair completion_fence)
      : hold_last_frame_(hold_last_frame), completion_fence_(std::move(completion_fence)) {}

  ~ClearRequest() = default;

  // Moveable, not copyable.
  ClearRequest(ClearRequest&& other) = default;
  ClearRequest& operator=(ClearRequest&& other) = default;
  ClearRequest& operator=(const ClearRequest& other) = delete;

  // Indicates whether a video renderer, upon receiving this request, should hold the last-rendered
  // frame (true) or show black (false). Not used for audio.
  bool hold_last_frame() const { return hold_last_frame_; }

  // Returns a reference to the event pair used as a completion fence for this request.
  zx::eventpair& completion_fence() { return completion_fence_; }

  // Takes (moves) the |completion_fence| for this request, rendering this request invalid.
  zx::eventpair take_completion_fence() { return std::move(completion_fence_); }

  // Returns a duplicate |ClearRequest| or an error indicating why the completion fence couldn't
  // be duplicated.
  fpromise::result<ClearRequest, zx_status_t> Duplicate() {
    zx::eventpair completion_fence;
    if (completion_fence_) {
      zx_status_t status = completion_fence_.duplicate(ZX_RIGHT_SAME_RIGHTS, &completion_fence);
      if (status != ZX_OK) {
        // Return invalid result.
        return fpromise::error_result(status);
      }
    }

    return fpromise::ok_result(ClearRequest(hold_last_frame_, std::move(completion_fence)));
  }

 private:
  bool hold_last_frame_ = false;
  zx::eventpair completion_fence_;
};

}  // namespace fmlib

#endif  // SRC_MEDIA_VNEXT_LIB_STREAM_SINK_CLEAR_REQUEST_H_
