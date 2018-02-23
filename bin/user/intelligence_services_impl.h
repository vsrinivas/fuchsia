// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_USER_INTELLIGENCE_SERVICES_IMPL_H_
#define PERIDOT_BIN_USER_INTELLIGENCE_SERVICES_IMPL_H_

#include "lib/action_log/fidl/user.fidl.h"
#include "lib/user_intelligence/fidl/intelligence_services.fidl.h"
#include "lib/user_intelligence/fidl/scope.fidl.h"

namespace maxwell {

class ContextEngine;
class SuggestionEngine;

class IntelligenceServicesImpl : public IntelligenceServices {
 public:
  // |context_engine| and |suggestion_engine| are not owned and must outlive
  // this instance.
  IntelligenceServicesImpl(ComponentScopePtr scope,
                           ContextEngine* context_engine,
                           SuggestionEngine* suggestion_engine,
                           UserActionLog* user_action_log);

  void GetContextReader(f1dl::InterfaceRequest<ContextReader> request) override;
  void GetContextWriter(f1dl::InterfaceRequest<ContextWriter> request) override;

  void GetProposalPublisher(
      f1dl::InterfaceRequest<ProposalPublisher> request) override;

  void GetActionLog(
      f1dl::InterfaceRequest<ComponentActionLog> request) override;

  void RegisterQueryHandler(
      f1dl::InterfaceHandle<QueryHandler> query_handler) override;

 private:
  ComponentScopePtr scope_;
  ContextEngine* const context_engine_;        // Not owned.
  SuggestionEngine* const suggestion_engine_;  // Not owned.
  UserActionLog* const user_action_log_;       // Not owned.
};

}  // namespace maxwell

#endif  // PERIDOT_BIN_USER_INTELLIGENCE_SERVICES_IMPL_H_
