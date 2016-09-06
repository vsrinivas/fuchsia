// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/media/services/framework/stages/input.h"

#include "apps/media/services/framework/engine.h"
#include "apps/media/services/framework/stages/stage.h"

namespace mojo {
namespace media {

Input::Input() : prepared_(false) {}

Input::~Input() {}

void Input::Connect(const OutputRef& output) {
  FTL_DCHECK(output.valid());
  FTL_DCHECK(!mate_);
  mate_ = output;
}

Output& Input::actual_mate() const {
  FTL_DCHECK(mate_.valid());
  return mate_.actual();
}

void Input::SetDemand(Demand demand, Engine* engine) const {
  FTL_DCHECK(engine);
  FTL_DCHECK(connected());

  if (actual_mate().UpdateDemandFromInput(demand)) {
    engine->PushToDemandBacklog(mate().stage_);
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
}  // namespace mojo
