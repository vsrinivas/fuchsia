// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_MODULAR_SRC_STORY_RUNNER_STORY_CONNECTION_H_
#define APPS_MODULAR_SRC_STORY_RUNNER_STORY_CONNECTION_H_

#include <string>

#include "apps/maxwell/services/user/intelligence_services.fidl.h"
#include "apps/maxwell/services/user/user_intelligence_provider.fidl.h"
#include "apps/modular/services/module/module_context.fidl.h"
#include "apps/modular/services/module/module_data.fidl.h"
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
class ModuleContextImpl : ModuleContext {
 public:
  // |module_path| identifies this particular module instance using the path of
  // modules that have ended up starting this module. The last item in this list
  // is this module's name. |module_path| can be used to internally name
  // resources that belong to this module (message queues, Links).
  ModuleContextImpl(const fidl::Array<fidl::String>& module_path,
                    const ModuleContextInfo& info,
                    const uint64_t id,
                    const std::string& module_url,
                    const LinkPathPtr& default_link_path,
                    ModuleControllerImpl* module_controller_impl,
                    fidl::InterfaceRequest<ModuleContext> module_context);

  ~ModuleContextImpl() override;

 private:
  // |ModuleContext|
  void GetLink(const fidl::String& name,
               fidl::InterfaceRequest<Link> link) override;
  // |ModuleContext|
  void StartModule(
      const fidl::String& name,
      const fidl::String& query,
      const fidl::String& link,
      fidl::InterfaceHandle<app::ServiceProvider> outgoing_services,
      fidl::InterfaceRequest<app::ServiceProvider> incoming_services,
      fidl::InterfaceRequest<ModuleController> module_controller,
      fidl::InterfaceRequest<mozart::ViewOwner> view_owner) override;
  // |ModuleContext|
  void StartModuleInShell(
      const fidl::String& name,
      const fidl::String& query,
      const fidl::String& link,
      fidl::InterfaceHandle<app::ServiceProvider> outgoing_services,
      fidl::InterfaceRequest<app::ServiceProvider> incoming_services,
      fidl::InterfaceRequest<ModuleController> module_controller,
      SurfaceRelationPtr surface_relation) override;
  // |ModuleContext|
  void GetComponentContext(
      fidl::InterfaceRequest<ComponentContext> request) override;
  // |ModuleContext|
  void GetIntelligenceServices(
      fidl::InterfaceRequest<maxwell::IntelligenceServices> request) override;
  // |ModuleContext|
  void GetStoryId(const GetStoryIdCallback& callback) override;
  // |ModuleContext|
  void Ready() override;
  // |ModuleContext|
  void Done() override;

  // The path of the modules that have started this module (ie.,
  // module_path_[n-1] starts module_path_[n]). The last element represents the
  // name this module was given (as part of ModuleContext.StartModule() or
  // related).
  const fidl::Array<fidl::String> module_path_;

  // Used to identify the module instance within the story,
  // and everything directly associated with it,
  // specifically the view of the module instance to
  // the story shell.
  const uint64_t id_;

  // Not owned. The StoryImpl instance this ModuleContextImpl instance
  // connects to.
  StoryImpl* const story_impl_;

  // This ID is used to namespace a module's ledger.
  const std::string module_url_;

  // The link path this module was started with.
  LinkPathPtr default_link_path_;

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
