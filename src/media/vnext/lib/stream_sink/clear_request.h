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
  // Constructs an invalid |ClearRequest|.
  ClearRequest() = default;

  // Constructs a valid |ClearRequest|.
  ClearRequest(bool hold_last_frame, zx::eventpair completion_fence)
      : hold_last_frame_(hold_last_frame),
        completion_fence_(std::move(completion_fence)) {
    FX_CHECK(completion_fence_);
  }

  ~ClearRequest() = default;

  ClearRequest(ClearRequest&& other) = default;
  ClearRequest& operator=(ClearRequest&& other) = default;
  ClearRequest& operator=(const ClearRequest& other) = delete;

  // Indicates whether this |ClearRequest| is valid.
  bool is_valid() const { return completion_fence_.is_valid(); }
  explicit operator bool() const { return is_valid(); }

  // Indicates whether a video renderer, upon receiving this request, should hold the last-rendered
  // frame (true) or show black (false). May not be called on an invalid |ClearRequest|.
  bool hold_last_frame() const {
    FX_CHECK(is_valid());
    return hold_last_frame_;
  }

  // Returns a reference to the event pair used as a completion fence for this request. May not be
  // called on an invalid |ClearRequest|.
  zx::eventpair& completion_fence() {
    FX_CHECK(is_valid());
    return completion_fence_;
  }

  // Takes (moves) the |completion_fence| for this request, rendering this request invalid. May not
  // be called on an invalid |ClearRequest|.
  zx::eventpair take_completion_fence() {
    FX_CHECK(is_valid());
    return std::move(completion_fence_);
  }

  // Returns a duplicate |ClearRequest|. If the completion fence cannot be duplicated, an invalid
  // |ClearRequest| is returned. May not be called on an invalid |ClearRequest|.
  fpromise::result<ClearRequest, zx_status_t> Duplicate() {
    FX_CHECK(is_valid());

    zx::eventpair completion_fence;
    zx_status_t status = completion_fence_.duplicate(ZX_RIGHT_SAME_RIGHTS, &completion_fence);
    if (status != ZX_OK) {
      // Return invalid result.
      return fpromise::error_result(status);
    }

    return fpromise::ok_result(ClearRequest(hold_last_frame_, std::move(completion_fence)));
  }

 private:
  bool hold_last_frame_ = false;
  zx::eventpair completion_fence_;
};

}  // namespace fmlib

#endif  // SRC_MEDIA_VNEXT_LIB_STREAM_SINK_CLEAR_REQUEST_H_
