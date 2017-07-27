// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/media/src/framework/stages/stage.h"

#include "apps/media/src/framework/engine.h"
#include "lib/ftl/logging.h"

namespace media {

Stage::Stage(Engine* engine) : engine_(engine), update_counter_(0) {}

Stage::~Stage() {}

void Stage::UnprepareInput(size_t index) {}

void Stage::UnprepareOutput(size_t index, const UpstreamCallback& callback) {}

void Stage::NeedsUpdate() {
  FTL_DCHECK(engine_);

  if (++update_counter_ == 1) {
    // This stage was not in the update backlog. Add it.
    engine_->StageNeedsUpdate(this);
  } else {
    // This stage was already in the update backlog. Set the counter to 2 so
    // it will never go out of range. We don't set it to 1, because, if we're
    // in |UpdateUntilDone|, that would indicate we no longer need to update.
    update_counter_ = 2;
  }
}

void Stage::UpdateUntilDone() {
  FTL_DCHECK(engine_);

  while (true) {
    // Set the counter to 1. If it's still 1 after we updated, we're done.
    // Otherwise, we need to update more.
    update_counter_ = 1;

    Update();

    // Quit if the counter is still at 1, otherwise update again.
    uint32_t expected = 1;
    if (update_counter_.compare_exchange_strong(expected, 0)) {
      break;
    }
  }
}

}  // namespace media
