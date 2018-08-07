// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/media/audio_core/pending_flush_token.h"

#include "garnet/bin/media/audio_core/audio_core_impl.h"
#include "lib/fxl/logging.h"

namespace media {
namespace audio {

PendingFlushToken::~PendingFlushToken() { FXL_DCHECK(was_recycled_); }

void PendingFlushToken::fbl_recycle() {
  if (!was_recycled_) {
    was_recycled_ = true;
    FXL_DCHECK(service_);
    service_->ScheduleFlushCleanup(fbl::unique_ptr<PendingFlushToken>(this));
  } else {
    delete this;
  }
}

}  // namespace audio
}  // namespace media
