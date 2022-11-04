// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/audio_core/v1/pending_flush_token.h"

#include <lib/async/cpp/task.h>
#include <lib/trace/event.h>

namespace media::audio {

PendingFlushToken::~PendingFlushToken() {
  TRACE_DURATION("audio", "PendingFlushToken::~PendingFlushToken");
  if (callback_) {
    async::PostTask(dispatcher_, std::move(callback_));
  }
}

}  // namespace media::audio
