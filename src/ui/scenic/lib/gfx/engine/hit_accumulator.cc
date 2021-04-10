// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/gfx/engine/hit_accumulator.h"

#include <lib/syslog/cpp/macros.h>

#include <algorithm>

namespace scenic_impl {
namespace gfx {

void ViewHitAccumulator::Add(const ViewHit& hit) {
  ViewHit& incumbent = views_.emplace(hit.view_ref_koid, hit).first->second;

  if (hit.distance < incumbent.distance) {
    incumbent = hit;
  }
}

bool ViewHitAccumulator::EndLayer() {
  size_t layer_start = hits_.size();
  hits_.reserve(hits_.size() + views_.size());

  for (auto& [_, hit] : views_) {
    hits_.push_back(std::move(hit));
  }

  // Sort by distance within layer.
  std::sort(hits_.begin() + layer_start, hits_.end(),
            [](const ViewHit& a, const ViewHit& b) { return a.distance < b.distance; });

  views_.clear();

  return true;
}

}  // namespace gfx
}  // namespace scenic_impl
