// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_MODULAR_SRC_STORY_RUNNER_STORY_CONNECTION_H_
#define APPS_MODULAR_SRC_STORY_RUNNER_STORY_CONNECTION_H_

#include <string>

#include "apps/maxwell/services/user/intelligence_services.fidl.h"
#include "apps/maxwell/services/user/user_intelligence_provider.fidl.h"
#include "apps/modular/services/module/module_context.fidl.h"
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

// The parameters of module context that do not vary by instance.
struct ModuleContextInfo {
  const ComponentContextInfo component_context_info;
  StoryImpl* const story_impl;
  maxwell::UserIntelligenceProvider* const user_intelligence_provider;
};

// ModuleContextImpl keeps a single connection from a module instance in
// the story to a StoryImpl. This way, requests that the module makes
// on its Story handle can be associated with the Module instance.
class ModuleContextImpl : public ModuleContext {
 public:
  ModuleContextImpl(
      const ModuleContextInfo& info,
      const uint64_t id,
      const std::string& module_url,
      ModuleControllerImpl* module_controller_impl,
      fidl::InterfaceRequest<ModuleContext> module_context);

  ~ModuleContextImpl() override;

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
  void StartModuleInShell(
      const fidl::String& query,
      fidl::InterfaceHandle<Link> link,
      fidl::InterfaceHandle<app::ServiceProvider> outgoing_services,
      fidl::InterfaceRequest<app::ServiceProvider> incoming_services,
      fidl::InterfaceRequest<ModuleController> module_controller,
      const fidl::String& view_type) override;
  void GetComponentContext(
      fidl::InterfaceRequest<ComponentContext> request) override;
  void GetIntelligenceServices(
      fidl::InterfaceRequest<maxwell::IntelligenceServices> request) override;
  void GetStoryId(const GetStoryIdCallback& callback) override;
  void Ready() override;
  void Done() override;

  // Used to identify the module instance within the story,
  // and everything directly associated with it,
  // specifically the view of the module instance to
  // the story shell.
  const uint64_t id_;

  // Not owned. The StoryImpl instance this ModuleContextImpl instance
  // connects to.
  StoryImpl* const story_impl_;

  // This ID is used to namespace a module's ledger.
  std::string module_url_;

  // Not owned. Used to notify module watchers and request tear down.
  ModuleControllerImpl* const module_controller_impl_;

  ComponentContextImpl component_context_impl_;
  fidl::BindingSet<ComponentContext> component_context_bindings_;

  maxwell::UserIntelligenceProvider* const
      user_intelligence_provider_;  // Not owned

  // The one connection to the StoryImpl instance that this
  // ModuleContextImpl instance represents.
  fidl::Binding<ModuleContext> binding_;

  FTL_DISALLOW_COPY_AND_ASSIGN(ModuleContextImpl);
};

}  // namespace modular

#endif  // APPS_MODULAR_SRC_STORY_RUNNER_STORY_CONNECTION_H_
