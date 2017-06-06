// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_MOZART_SRC_VIEW_MANAGER_INTERNAL_RESOLVED_HITS_H_
#define APPS_MOZART_SRC_VIEW_MANAGER_INTERNAL_RESOLVED_HITS_H_

#include <iosfwd>
#include <memory>
#include <unordered_map>

#include "apps/mozart/services/composition/hit_tests.fidl.h"
#include "apps/mozart/services/views/view_token.fidl.h"
#include "lib/ftl/macros.h"

namespace view_manager {

using SceneTokenValueToViewTokenMap =
    std::unordered_map<uint32_t, mozart::ViewTokenPtr>;

// A hit test result combined with a map explaining how scenes are mapped
// to views.
class ResolvedHits {
 public:
  ResolvedHits(mozart::HitTestResultPtr result);
  ~ResolvedHits();

  // The hit test result, not null unless |TakeResult| was called.
  const mozart::HitTestResult* result() const { return result_.get(); }
  mozart::HitTestResultPtr TakeResult() { return std::move(result_); }

  // A map from scene token value to view token containing all scenes which
  // could be resolved.
  const SceneTokenValueToViewTokenMap& map() const { return map_; }

  // Adds a mapping for the specified scene token value to a view token.
  void AddMapping(uint32_t scene_token_value, mozart::ViewTokenPtr view_token);

 private:
  mozart::HitTestResultPtr result_;
  SceneTokenValueToViewTokenMap map_;

  FTL_DISALLOW_COPY_AND_ASSIGN(ResolvedHits);
};

// Provides a resolved description of the hit test results, or null if the
// hit test could not be performed at all.
// TODO(jeffbrown): Would prefer to use |std::unique_ptr| here but it doesn't
// play nice with base::Callback right now.
using ResolvedHitsCallback = std::function<void(std::unique_ptr<ResolvedHits>)>;

std::ostream& operator<<(std::ostream& os, const ResolvedHits& value);
std::ostream& operator<<(std::ostream& os, const ResolvedHits* value);

}  // namespace view_manager

#endif  // APPS_MOZART_SRC_VIEW_MANAGER_INTERNAL_RESOLVED_HITS_H_
