// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <vector>

#include "apps/maxwell/src/suggestion_engine/suggestion_prototype.h"

namespace maxwell {

struct RankedSuggestion {
  SuggestionPrototype* prototype;
  float rank;
};

typedef std::vector<std::unique_ptr<RankedSuggestion>> RankedSuggestions;

}  // namespace maxwell
