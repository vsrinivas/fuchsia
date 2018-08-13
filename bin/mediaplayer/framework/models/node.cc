// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/mediaplayer/framework/models/node.h"

#include "garnet/bin/mediaplayer/framework/formatting.h"

namespace media_player {

const char* GenericNode::label() const { return "<not labelled>"; }

void GenericNode::Dump(std::ostream& os) const {
  os << label();
  generic_stage_.load()->Dump(os);
}

void GenericNode::PostTask(fit::closure task) {
  Stage* generic_stage = generic_stage_;
  if (generic_stage) {
    generic_stage->PostTask(std::move(task));
  }
}

}  // namespace media_player
