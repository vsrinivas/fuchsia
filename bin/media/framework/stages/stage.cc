// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/media/src/framework/stages/stage.h"

#include "lib/ftl/logging.h"

namespace mojo {
namespace media {

Stage::Stage() : in_supply_backlog_(false), in_demand_backlog_(false) {}

Stage::~Stage() {}

void Stage::UnprepareInput(size_t index) {}

void Stage::UnprepareOutput(size_t index, const UpstreamCallback& callback) {}

}  // namespace media
}  // namespace mojo
