// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/gfx/engine/hit_accumulator.h"

#include <lib/syslog/cpp/macros.h>

#include <algorithm>

#include "src/ui/scenic/lib/gfx/resources/nodes/shape_node.h"

namespace scenic_impl {
namespace gfx {

void SessionHitAccumulator::Add(const ViewHit& hit) {
  ViewHit& incumbent = sessions_.emplace(hit.view->session_id(), hit).first->second;

  if (hit.distance < incumbent.distance) {
    incumbent = hit;
  }
}

bool SessionHitAccumulator::EndLayer() {
  size_t layer_start = hits_.size();
  hits_.reserve(hits_.size() + sessions_.size());

  for (auto& [_, hit] : sessions_) {
    hits_.push_back(std::move(hit));
  }

  // Sort by distance within layer.
  std::sort(hits_.begin() + layer_start, hits_.end(),
            [](const ViewHit& a, const ViewHit& b) { return a.distance < b.distance; });

  sessions_.clear();

  return true;
}

void TopHitAccumulator::Add(const ViewHit& hit) {
  if (!hit_ || hit.distance < hit_->distance) {
    hit_ = hit;
  }
}

bool TopHitAccumulator::EndLayer() { return !hit_; }

std::vector<std::vector<GlobalId>> CollisionAccumulator::Report() const {
  std::vector<std::vector<GlobalId>> report;

  for (const auto& [_, ids] : ids_by_depth_) {
    if (ids.size() > 1) {
      // Potential savings: restrict this to one call per layer and move instead of copy.
      report.push_back(ids);
    }
  }

  return report;
}

void CollisionAccumulator::Add(const NodeHit& hit) {
  ids_by_depth_[hit.distance].push_back(hit.node->global_id());
}

bool CollisionAccumulator::EndLayer() {
  ids_by_depth_.clear();
  return true;
}

}  // namespace gfx
}  // namespace scenic_impl
