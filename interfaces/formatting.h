// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "apps/maxwell/interfaces/suggestion_manager.mojom.h"
#include "mojo/public/cpp/bindings/formatting.h"

namespace maxwell {
namespace suggestion_engine {

std::ostream& operator<<(std::ostream& os,
                         const SuggestionDisplayProperties& o) {
  return os << "{ icon: " << o.icon << ", headline: " << o.headline
            << ", subtext: " << o.subtext << ", details: " << o.details << "}";
}

std::ostream& operator<<(std::ostream& os, const Suggestion& o) {
  return os << "{ uuid: " << o.uuid << ", rank: " << o.rank
            << ", display_properties: " << o.display_properties << "}";
}

}  // namespace suggestion_engine
}  // namespace maxwell
