// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_PLAYBACK_MEDIAPLAYER_TMP_GRAPH_RESULT_H_
#define SRC_MEDIA_PLAYBACK_MEDIAPLAYER_TMP_GRAPH_RESULT_H_

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

#endif  // SRC_MEDIA_PLAYBACK_MEDIAPLAYER_TMP_GRAPH_RESULT_H_
