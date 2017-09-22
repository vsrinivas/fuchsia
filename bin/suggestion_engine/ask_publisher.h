//// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "apps/maxwell/services/suggestion/ask_handler.fidl.h"

#include "apps/maxwell/src/suggestion_engine/proposal_publisher_impl.h"
#include "lib/fxl/memory/weak_ptr.h"

namespace maxwell {

class ProposalPublisherImpl;

// This struct allows proper ownership and lifecycle management of proposals
// produced during Ask so that they are namespaced by publisher like Next
// proposals.
struct AskPublisher {
  AskHandlerPtr handler;
  fxl::WeakPtr<ProposalPublisherImpl> const publisher;

  AskPublisher(AskHandlerPtr handler,
               fxl::WeakPtr<ProposalPublisherImpl> publisher)
      : handler(std::move(handler)), publisher(publisher) {}

  static AskHandlerPtr* GetHandler(std::unique_ptr<AskPublisher>* ask) {
    return &(*ask)->handler;
  }
};

}  // namespace maxwell
