// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "apps/maxwell/services/user/intelligence_services.fidl.h"

namespace maxwell {

class ContextEngine;
class SuggestionEngine;

class IntelligenceServicesImpl : public IntelligenceServices {
 public:
  // |context_engine| and |suggestion_engine| are not owned and must outlive
  // this instance.
  IntelligenceServicesImpl(const std::string& story_id,
                           const std::string& component_id,
                           ContextEngine* context_engine,
                           SuggestionEngine* suggestion_engine);

  void GetContextProvider(
      fidl::InterfaceRequest<ContextSubscriber> request) override;

  void GetContextPublisher(
      fidl::InterfaceRequest<ContextPublisher> request) override;

  void GetProposalPublisher(
      fidl::InterfaceRequest<ProposalPublisher> request) override;

 private:
  const std::string story_id_;
  const std::string component_id_;
  ContextEngine* const context_engine_;        // Not owned.
  SuggestionEngine* const suggestion_engine_;  // Not owned.
};

}  // namespace maxwell
