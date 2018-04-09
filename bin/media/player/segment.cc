// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/media/player/segment.h"

#include <lib/async/dispatcher.h>

#include "lib/fxl/logging.h"

namespace media_player {

Segment::Segment() {}

Segment::~Segment() {}

void Segment::Provision(Graph* graph,
                        async_t* async,
                        fxl::Closure update_callback) {
  graph_ = graph;
  async_ = async;
  update_callback_ = update_callback;
  DidProvision();
}

void Segment::Deprovision() {
  WillDeprovision();
  graph_ = nullptr;
  async_ = nullptr;
  update_callback_ = nullptr;
}

void Segment::NotifyUpdate() {
  if (update_callback_) {
    update_callback_();
  }
}

void Segment::ReportProblem(const std::string& type,
                            const std::string& details) {
  if (problem_ && problem_->type == type && problem_->details == details) {
    // No change.
    return;
  }

  problem_ = Problem::New();
  problem_->type = type;
  problem_->details = details;
  NotifyUpdate();
}

void Segment::ReportNoProblem() {
  if (!problem_) {
    // No change.
    return;
  }

  problem_ = nullptr;
  NotifyUpdate();
}

}  // namespace media_player
