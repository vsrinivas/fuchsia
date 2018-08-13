// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_MEDIAPLAYER_FRAMEWORK_RESULT_H_
#define GARNET_BIN_MEDIAPLAYER_FRAMEWORK_RESULT_H_

#include <cstdint>

namespace media_player {

// Possible result values indicating success or type of failure.
enum class Result {
  kOk,
  kUnknownError,
  kInternalError,
  kUnsupportedOperation,
  kInvalidArgument,
  kNotFound,
  kPeerClosed,
  kCancelled
};

}  // namespace media_player

#endif  // GARNET_BIN_MEDIAPLAYER_FRAMEWORK_RESULT_H_
