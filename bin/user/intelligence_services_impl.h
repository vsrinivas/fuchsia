// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_USER_INTELLIGENCE_SERVICES_IMPL_H_
#define PERIDOT_BIN_USER_INTELLIGENCE_SERVICES_IMPL_H_

#include <fuchsia/cpp/modular.h>

namespace maxwell {

class IntelligenceServicesImpl : public modular::IntelligenceServices {
 public:
  // |context_engine| and |suggestion_engine| are not owned and must outlive
  // this instance.
  IntelligenceServicesImpl(modular::ComponentScope scope,
                           modular::ContextEngine* context_engine,
                           modular::SuggestionEngine* suggestion_engine,
                           modular::UserActionLog* user_action_log);

  void GetContextReader(
      fidl::InterfaceRequest<modular::ContextReader> request) override;
  void GetContextWriter(
      fidl::InterfaceRequest<modular::ContextWriter> request) override;

  void GetProposalPublisher(
      fidl::InterfaceRequest<modular::ProposalPublisher> request) override;

  void GetActionLog(
      fidl::InterfaceRequest<modular::ComponentActionLog> request) override;

  void RegisterQueryHandler(
      fidl::InterfaceHandle<modular::QueryHandler> query_handler) override;

 private:
  modular::ComponentScope CloneScope();

  modular::ComponentScope scope_;
  modular::ContextEngine* const context_engine_;        // Not owned.
  modular::SuggestionEngine* const suggestion_engine_;  // Not owned.
  modular::UserActionLog* const user_action_log_;       // Not owned.
};

}  // namespace maxwell

#endif  // PERIDOT_BIN_USER_INTELLIGENCE_SERVICES_IMPL_H_
