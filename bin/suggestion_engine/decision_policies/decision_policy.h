// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_SUGGESTION_ENGINE_DECISION_POLICIES_DECISION_POLICY_H_
#define PERIDOT_BIN_SUGGESTION_ENGINE_DECISION_POLICIES_DECISION_POLICY_H_

#include "peridot/bin/suggestion_engine/ranked_suggestion.h"

namespace modular {

// Base class for performing a decision on some ranked suggestion.
class DecisionPolicy {
 public:
  DecisionPolicy();
  virtual ~DecisionPolicy();

  // Decides if a value is valid given some policy.
  virtual bool Accept(const RankedSuggestion& value) = 0;
};

}  // namespace modular

#endif  // PERIDOT_BIN_SUGGESTION_ENGINE_DECISION_POLICIES_DECISION_POLICY_H_
