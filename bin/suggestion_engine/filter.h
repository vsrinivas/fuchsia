// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_SUGGESTION_ENGINE_FILTER_H_
#define PERIDOT_BIN_SUGGESTION_ENGINE_FILTER_H_

#include "lib/suggestion/fidl/proposal.fidl.h"

namespace maxwell {

// Should return true if the given Proposal should be included.
typedef std::function<bool(const Proposal&)> ProposalFilter;

}  // namespace maxwell

#endif  // PERIDOT_BIN_SUGGESTION_ENGINE_FILTER_H_
