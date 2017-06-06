// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/mozart/src/view_manager/internal/resolved_hits.h"

#include "apps/mozart/services/composition/cpp/formatting.h"
#include "apps/mozart/services/views/cpp/formatting.h"
#include "lib/ftl/logging.h"

namespace view_manager {

ResolvedHits::ResolvedHits(mozart::HitTestResultPtr result)
    : result_(std::move(result)) {
  FTL_DCHECK(result_);
}

ResolvedHits::~ResolvedHits() {}

void ResolvedHits::AddMapping(uint32_t scene_token_value,
                              mozart::ViewTokenPtr view_token) {
  FTL_DCHECK(scene_token_value);
  FTL_DCHECK(view_token);

  auto pair = map_.emplace(scene_token_value, std::move(view_token));
  FTL_DCHECK(pair.second);
}

std::ostream& operator<<(std::ostream& os, const ResolvedHits& value) {
  os << "{result=";
  if (value.result())
    os << *value.result();
  else
    os << "null";
  os << ", map={";
  bool first = true;
  for (const auto& pair : value.map()) {
    if (first)
      first = false;
    else
      os << ", ";
    os << pair.first << ": " << pair.second;
  }
  return os << "}}";
}

std::ostream& operator<<(std::ostream& os, const ResolvedHits* value) {
  return value ? os << *value : os << "null";
}

}  // namespace view_manager
