// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/media/src/framework/stages/stage.h"

#include "apps/media/src/framework/engine.h"
#include "lib/ftl/logging.h"

namespace media {

Stage::Stage(Engine* engine)
    : engine_(engine), in_supply_backlog_(false), in_demand_backlog_(false) {
  FTL_DCHECK(engine_);
}

Stage::~Stage() {}

void Stage::UnprepareInput(size_t index) {}

void Stage::UnprepareOutput(size_t index, const UpstreamCallback& callback) {}

}  // namespace media
