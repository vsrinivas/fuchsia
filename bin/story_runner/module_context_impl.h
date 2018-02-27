// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_STORY_RUNNER_MODULE_CONTEXT_IMPL_H_
#define PERIDOT_BIN_STORY_RUNNER_MODULE_CONTEXT_IMPL_H_

#include <string>

#include "lib/app/cpp/service_provider_impl.h"
#include "lib/fidl/cpp/bindings/binding.h"
#include "lib/fidl/cpp/bindings/binding_set.h"
#include "lib/fidl/cpp/bindings/interface_handle.h"
#include "lib/fidl/cpp/bindings/interface_request.h"
#include "lib/fxl/macros.h"
#include "lib/module/fidl/module_context.fidl.h"
#include "lib/module/fidl/module_data.fidl.h"
#include "lib/module_resolver/fidl/module_resolver.fidl.h"
#include "lib/surface/fidl/surface.fidl.h"
#include "lib/ui/views/fidl/view_token.fidl.h"
#include "lib/user_intelligence/fidl/intelligence_services.fidl.h"
#include "lib/user_intelligence/fidl/user_intelligence_provider.fidl.h"
#include "peridot/bin/component/component_context_impl.h"

namespace modular {

class ModuleControllerImpl;
class StoryControllerImpl;

// The parameters of module context that do not vary by instance.
struct ModuleContextInfo {
  const ComponentContextInfo component_context_info;
  StoryControllerImpl* const story_controller_impl;
  maxwell::UserIntelligenceProvider* const user_intelligence_provider;
  ModuleResolver* const module_resolver;
};

// ModuleContextImpl keeps a single connection from a module instance in
// the story to a StoryControllerImpl. This way, requests that the module makes
// on its Story handle can be associated with the Module instance.
class ModuleContextImpl : ModuleContext {
 public:
  // |module_data| identifies this particular module instance using the path of
  // modules that have ended up starting this module in the module_path
  // property. The last item in this list is this module's name. |module_path|
  // can be used to internally name resources that belong to this module
  // (message queues, Links).
  ModuleContextImpl(
      const ModuleContextInfo& info,
      const ModuleData* module_data,
      ModuleControllerImpl* module_controller_impl,
      f1dl::InterfaceRequest<app::ServiceProvider> service_provider_request);

  ~ModuleContextImpl() override;

 private:
  // |ModuleContext|
  void GetLink(const f1dl::String& name,
               f1dl::InterfaceRequest<Link> request) override;
  // |ModuleContext|
  void StartModule(
      const f1dl::String& name,
      const f1dl::String& query,
      const f1dl::String& link_name,
      f1dl::InterfaceRequest<app::ServiceProvider> incoming_services,
      f1dl::InterfaceRequest<ModuleController> module_controller,
      f1dl::InterfaceRequest<mozart::ViewOwner> view_owner) override;
  // |ModuleContext|
  void EmbedDaisy(
      const f1dl::String& name,
      DaisyPtr daisy,
      f1dl::InterfaceRequest<app::ServiceProvider> incoming_services,
      f1dl::InterfaceRequest<ModuleController> module_controller,
      f1dl::InterfaceRequest<mozart::ViewOwner> view_owner,
      const EmbedDaisyCallback& callback) override;
  // |ModuleContext|
  void StartModuleInShell(
      const f1dl::String& name,
      const f1dl::String& query,
      const f1dl::String& link_name,
      f1dl::InterfaceRequest<app::ServiceProvider> incoming_services,
      f1dl::InterfaceRequest<ModuleController> module_controller,
      SurfaceRelationPtr surface_relation,
      bool focus) override;
  // |ModuleContext|
  void StartDaisy(
      const f1dl::String& name,
      DaisyPtr daisy,
      f1dl::InterfaceRequest<app::ServiceProvider> incoming_services,
      f1dl::InterfaceRequest<ModuleController> module_controller,
      SurfaceRelationPtr surface_relation,
      const StartDaisyCallback& callback) override;
  // |ModuleContext|
  void StartContainerInShell(
      const f1dl::String& name,
      f1dl::Array<ContainerLayoutPtr> layout,
      f1dl::Array<ContainerRelationEntryPtr> relationships,
      f1dl::Array<ContainerNodePtr> nodes) override;
  // |ModuleContext|
  void EmbedModule(
      const f1dl::String& name,
      const f1dl::String& query,
      const f1dl::String& link_name,
      f1dl::InterfaceRequest<app::ServiceProvider> incoming_services,
      f1dl::InterfaceRequest<ModuleController> module_controller,
      f1dl::InterfaceHandle<EmbedModuleWatcher> embed_module_watcher,
      f1dl::InterfaceRequest<mozart::ViewOwner> view_owner) override;
  // |ModuleContext|
  void GetComponentContext(
      f1dl::InterfaceRequest<ComponentContext> context_request) override;
  // |ModuleContext|
  void GetIntelligenceServices(
      f1dl::InterfaceRequest<maxwell::IntelligenceServices> request) override;
  // |ModuleContext|
  void GetStoryId(const GetStoryIdCallback& callback) override;
  // |ModuleContext|
  void RequestFocus() override;
  // |ModuleContext|
  void Ready() override;
  // |ModuleContext|
  void Done() override;

  // Identifies the module by its path, holds the URL of the running module, and
  // the link it was started with.
  const ModuleData* const module_data_;

  // Not owned. The StoryControllerImpl for the Story in which this Module
  // lives.
  StoryControllerImpl* const story_controller_impl_;

  // Not owned. Used to notify module watchers and request tear down.
  ModuleControllerImpl* const module_controller_impl_;

  ComponentContextImpl component_context_impl_;

  maxwell::UserIntelligenceProvider* const
      user_intelligence_provider_;  // Not owned

  f1dl::BindingSet<ModuleContext> bindings_;

  // A service provider that represents the services to be added into an
  // application's namespace.
  app::ServiceProviderImpl service_provider_impl_;

  FXL_DISALLOW_COPY_AND_ASSIGN(ModuleContextImpl);
};

}  // namespace modular

#endif  // PERIDOT_BIN_STORY_RUNNER_MODULE_CONTEXT_IMPL_H_
