// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "mojo/ui/associates/resolved_hits.h"

#include "base/bind.h"
#include "base/logging.h"
#include "mojo/services/gfx/composition/cpp/formatting.h"
#include "mojo/services/ui/views/cpp/formatting.h"

namespace mojo {
namespace ui {

ResolvedHits::ResolvedHits(mojo::gfx::composition::HitTestResultPtr result)
    : result_(result.Pass()) {
  DCHECK(result_);
}

ResolvedHits::~ResolvedHits() {}

void ResolvedHits::AddMapping(uint32_t scene_token_value,
                              mojo::ui::ViewTokenPtr view_token) {
  DCHECK(scene_token_value);
  DCHECK(view_token);

  auto pair = map_.emplace(scene_token_value, view_token.Pass());
  DCHECK(pair.second);
}

std::ostream& operator<<(std::ostream& os,
                         const mojo::ui::ResolvedHits& value) {
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

std::ostream& operator<<(std::ostream& os,
                         const mojo::ui::ResolvedHits* value) {
  return value ? os << *value : os << "null";
}

}  // namespace ui
}  // namespace mojo
