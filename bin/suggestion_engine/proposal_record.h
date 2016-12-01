// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "apps/maxwell/services/suggestion/proposal.fidl.h"
#include "lib/ftl/time/time_point.h"

namespace maxwell {

class SuggestionAgentClientImpl;

struct ProposalRecord {
  ProposalRecord(SuggestionAgentClientImpl* source, ProposalPtr proposal)
      : source(source),
        timestamp(ftl::TimePoint::Now()),
        proposal(std::move(proposal)) {}

  SuggestionAgentClientImpl* source;
  ftl::TimePoint timestamp;
  ProposalPtr proposal;
};

typedef std::unique_ptr<ProposalRecord> ProposalRecordPtr;
// first: suggestion ID
// second: proposal record ptr
typedef std::pair<const std::string, ProposalRecordPtr> SuggestionPrototype;

}  // maxwell
