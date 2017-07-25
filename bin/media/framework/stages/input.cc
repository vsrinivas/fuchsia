// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/media/src/framework/stages/input.h"

#include "apps/media/src/framework/engine.h"
#include "apps/media/src/framework/stages/stage.h"

namespace media {

Input::Input(Stage* stage, size_t index) : stage_(stage), index_(index) {
  FTL_DCHECK(stage_);
}

Input::~Input() {}

void Input::Connect(Output* output) {
  FTL_DCHECK(output);
  FTL_DCHECK(!mate_);
  mate_ = output;
}

void Input::SetDemand(Demand demand) const {
  FTL_DCHECK(mate_);

  if (mate_->UpdateDemandFromInput(demand)) {
    stage_->engine()->PushToDemandBacklog(mate_->stage());
  }
}

bool Input::SupplyPacketFromOutput(PacketPtr packet) {
  FTL_DCHECK(packet);
  FTL_DCHECK(!packet_from_upstream_);
  packet_from_upstream_ = std::move(packet);
  return true;
}

void Input::Flush() {
  packet_from_upstream_.reset(nullptr);
}

}  // namespace media
