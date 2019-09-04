// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_PLAYBACK_MEDIAPLAYER_GRAPH_THREAD_PRIORITY_H_
#define SRC_MEDIA_PLAYBACK_MEDIAPLAYER_GRAPH_THREAD_PRIORITY_H_

#include <lib/zx/thread.h>

namespace media_player {

class ThreadPriority {
 public:
  // Sets the specified thread's priority to high. If |thread| is not supplied, the current thread's
  // priority is set.
  static zx_status_t SetToHigh(zx::thread* thread = nullptr);
};

}  // namespace media_player

#endif  // SRC_MEDIA_PLAYBACK_MEDIAPLAYER_GRAPH_THREAD_PRIORITY_H_
