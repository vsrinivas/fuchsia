// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "apps/maxwell/src/suggestion_engine/proposal_record.h"

namespace maxwell {
namespace suggestion {

struct RankedSuggestion {
  SuggestionPrototype* prototype;
  float rank;
};

}  // suggestion
}  // maxwell
