// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "apps/maxwell/services/suggestion/proposal.fidl.h"

namespace maxwell {

// Should return true if the given Proposal should be included.
typedef std::function<bool(const Proposal&)> ProposalFilter;

}  // namespace maxwell
