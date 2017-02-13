// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_MODULAR_SRC_STORY_RUNNER_STORY_CONNECTION_H_
#define APPS_MODULAR_SRC_STORY_RUNNER_STORY_CONNECTION_H_

#include <string>

#include "apps/ledger/services/public/ledger.fidl.h"
#include "apps/modular/services/story/story.fidl.h"
#include "apps/modular/src/component/component_context_impl.h"
#include "apps/mozart/services/views/view_token.fidl.h"
#include "lib/fidl/cpp/bindings/binding.h"
#include "lib/fidl/cpp/bindings/binding_set.h"
#include "lib/fidl/cpp/bindings/interface_handle.h"
#include "lib/fidl/cpp/bindings/interface_request.h"
#include "lib/ftl/macros.h"

namespace modular {

class ModuleControllerImpl;
class StoryImpl;

// StoryConnection keeps a single connection from a module instance in
// the story to a StoryImpl. This way, requests that the module makes
// on its Story handle can be associated with the Module instance.
class StoryConnection : public Story {
 public:
  StoryConnection(StoryImpl* story_impl,
                  const std::string& module_url,
                  ModuleControllerImpl* module_controller_impl,
                  AgentRunner* agent_runner,
                  fidl::InterfaceRequest<Story> story);

  ~StoryConnection() override;

 private:
  // |Story|
  void CreateLink(const fidl::String& name,
                  fidl::InterfaceRequest<Link> link) override;
  void StartModule(
      const fidl::String& query,
      fidl::InterfaceHandle<Link> link,
      fidl::InterfaceHandle<app::ServiceProvider> outgoing_services,
      fidl::InterfaceRequest<app::ServiceProvider> incoming_services,
      fidl::InterfaceRequest<ModuleController> module_controller,
      fidl::InterfaceRequest<mozart::ViewOwner> view_owner) override;
  void GetLedger(fidl::InterfaceRequest<ledger::Ledger> request,
                 const GetLedgerCallback& result) override;
  void GetComponentContext(
      fidl::InterfaceRequest<ComponentContext> context_request) override;
  void Ready() override;
  void Done() override;

  // Not owned. The StoryImpl instance this StoryConnection instance
  // connects to.
  StoryImpl* const story_impl_;

  // This ID is used to namespace a module's ledger.
  std::string module_url_;

  // Not owned. Used to notify module watchers and request tear down.
  ModuleControllerImpl* const module_controller_impl_;

  ComponentContextImpl component_context_impl_;
  fidl::BindingSet<ComponentContext> component_context_bindings_;

  // The one connection to the StoryImpl instance that this
  // StoryConnection instance represents.
  fidl::Binding<Story> binding_;

  FTL_DISALLOW_COPY_AND_ASSIGN(StoryConnection);
};

}  // namespace modular

#endif  // APPS_MODULAR_SRC_STORY_RUNNER_STORY_CONNECTION_H_
