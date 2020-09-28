// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_SCENIC_LIB_GFX_ENGINE_HIT_ACCUMULATOR_H_
#define SRC_UI_SCENIC_LIB_GFX_ENGINE_HIT_ACCUMULATOR_H_

#include <lib/fit/function.h>

#include <optional>
#include <unordered_map>
#include <vector>

#include "src/ui/scenic/lib/gfx/engine/hit.h"

namespace scenic_impl {
namespace gfx {

// Represents a data structure that accumulates and optionally reduces hits during hit testing.
template <typename H>
class HitAccumulator {
 public:
  virtual ~HitAccumulator() = default;

  // Adds a hit to this accumulator.
  virtual void Add(const H& hit) = 0;

  // Called by |LayerStack| to prepare the accumulator for the next layer. Returns true if hit
  // testing should continue, or false if it should be short circuited.
  virtual bool EndLayer() = 0;
};

// Wraps another hit accumulator in a mapping function.
template <typename U, typename V>
class MappingAccumulator : public HitAccumulator<U> {
 public:
  MappingAccumulator(HitAccumulator<V>* base, fit::function<std::optional<V>(const U&)> mapping)
      : base_(base), mapping_(std::move(mapping)) {}

  // |HitAccumulator<U>|
  void Add(const U& hit) override {
    auto v = mapping_(hit);
    if (v) {
      base_->Add(*v);
    }
  }

  // |HitAccumulator<U>|
  bool EndLayer() override { return base_->EndLayer(); }

 private:
  HitAccumulator<V>* const base_;
  fit::function<std::optional<V>(const U&)> const mapping_;
};

// Accumulates one hit per view per layer, on the top view in each, sorted by depth per layer.
//
// We specifically want sort-first-by-layer-then-by-depth ordering.
//
// TODO(fxbug.dev/24152): Return full set of hits to each client.
class ViewHitAccumulator : public HitAccumulator<ViewHit> {
 public:
  const std::vector<ViewHit>& hits() const { return hits_; }

  // |HitAccumulator<ViewHit>|
  void Add(const ViewHit& hit) override;

  // |HitAccumulator<ViewHit>|
  // This implementation sorts hits for the layer by distance and resets view deduplication for
  // the next layer and returns true.
  bool EndLayer() override;

 private:
  std::vector<ViewHit> hits_;
  // Used to accumulate the topmost hit in each view.
  std::map</*view_ref_koid*/ zx_koid_t, ViewHit> views_;
};

// Accumulates one hit overall, on the top view by depth. Hits are in the coordinate space of the
// view.
class TopHitAccumulator : public HitAccumulator<ViewHit> {
 public:
  const std::optional<ViewHit>& hit() const { return hit_; }

  // |HitAccumulator<ViewHit>|
  void Add(const ViewHit& hit) override;

  // |HitAccumulator<ViewHit>|
  // This implementation continues only until a hit is found.
  bool EndLayer() override;

 private:
  std::optional<ViewHit> hit_;
};

}  // namespace gfx
}  // namespace scenic_impl

#endif  // SRC_UI_SCENIC_LIB_GFX_ENGINE_HIT_ACCUMULATOR_H_
