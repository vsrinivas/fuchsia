// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "apps/maxwell/src/suggestion_engine/suggestion_prototype.h"

namespace maxwell {

struct RankedSuggestion {
  const SuggestionPrototype* prototype;
  float rank;
};

}  // maxwell
