// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/suggestion_engine/rankers/ranker.h"

namespace modular {

Ranker::Ranker() = default;

Ranker::~Ranker() = default;

double Ranker::Rank(const RankedSuggestion& suggestion) {
  return Rank(fuchsia::modular::UserInput(), suggestion);
}

}  // namespace modular
