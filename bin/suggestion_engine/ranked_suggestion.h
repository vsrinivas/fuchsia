// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "peridot/bin/suggestion_engine/suggestion_prototype.h"

namespace maxwell {

struct RankedSuggestion {
  SuggestionPrototype* prototype;
  float rank;
};

}  // namespace maxwell
