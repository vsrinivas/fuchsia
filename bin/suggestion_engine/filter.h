// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "apps/maxwell/src/suggestion_engine/proposal_record.h"

namespace maxwell {

// Should return false if the given Proposal should be filtered.
typedef std::function<bool(const Proposal&)> ProposalFilter;

}  // namespace maxwell
