// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_USER_RUNNER_STORY_RUNNER_MODULE_CONTEXT_IMPL_H_
#define PERIDOT_BIN_USER_RUNNER_STORY_RUNNER_MODULE_CONTEXT_IMPL_H_

#include <string>

#include <fuchsia/modular/cpp/fidl.h>
#include <fuchsia/ui/viewsv1token/cpp/fidl.h>
#include <lib/component/cpp/service_provider_impl.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/fidl/cpp/interface_handle.h>
#include <lib/fidl/cpp/interface_request.h>
#include <lib/fxl/macros.h>

#include "peridot/bin/user_runner/component_context_impl.h"

namespace modular {

class StoryControllerImpl;

// The dependencies of ModuleContextImpl common to all instances.
struct ModuleContextInfo {
  const ComponentContextInfo component_context_info;
  StoryControllerImpl* const story_controller_impl;
  fuchsia::modular::UserIntelligenceProvider* const user_intelligence_provider;
};

// ModuleContextImpl keeps a single connection from a module instance in
// the story to a StoryControllerImpl. This way, requests that the module makes
// on its Story handle can be associated with the Module instance.
class ModuleContextImpl : fuchsia::modular::ModuleContext {
 public:
  // |module_data| identifies this particular module instance using the path of
  // modules that have ended up starting this module in the module_path
  // property. The last item in this list is this module's name. |module_path|
  // can be used to internally name resources that belong to this module
  // (message queues, Links).
  ModuleContextImpl(const ModuleContextInfo& info,
                    const fuchsia::modular::ModuleData* module_data,
                    fidl::InterfaceRequest<fuchsia::sys::ServiceProvider>
                        service_provider_request);

  ~ModuleContextImpl() override;

 private:
  // |fuchsia::modular::ModuleContext|
  void GetLink(fidl::StringPtr name,
               fidl::InterfaceRequest<fuchsia::modular::Link> request) override;

  // |fuchsia::modular::ModuleContext|
  void EmbedModule(
      fidl::StringPtr name, fuchsia::modular::Intent intent,
      fidl::InterfaceRequest<fuchsia::modular::ModuleController>
          module_controller,
      fidl::InterfaceRequest<fuchsia::ui::viewsv1token::ViewOwner> view_owner,
      EmbedModuleCallback callback) override;

  // |fuchsia::modular::ModuleContext|
  void StartModule(fidl::StringPtr name, fuchsia::modular::Intent intent,
                   fidl::InterfaceRequest<fuchsia::modular::ModuleController>
                       module_controller,
                   fuchsia::modular::SurfaceRelationPtr surface_relation,
                   StartModuleCallback callback) override;

  // |fuchsia::modular::ModuleContext|
  void StartContainerInShell(
      fidl::StringPtr name, fuchsia::modular::SurfaceRelation parent_relation,
      fidl::VectorPtr<fuchsia::modular::ContainerLayout> layout,
      fidl::VectorPtr<fuchsia::modular::ContainerRelationEntry> relationships,
      fidl::VectorPtr<fuchsia::modular::ContainerNode> nodes) override;

  // |fuchsia::modular::ModuleContext|
  void GetComponentContext(
      fidl::InterfaceRequest<fuchsia::modular::ComponentContext>
          context_request) override;

  // |fuchsia::modular::ModuleContext|
  void GetIntelligenceServices(
      fidl::InterfaceRequest<fuchsia::modular::IntelligenceServices> request)
      override;

  // |fuchsia::modular::ModuleContext|
  void GetStoryId(GetStoryIdCallback callback) override;

  // |fuchsia::modular::ModuleContext|
  void RequestFocus() override;

  // |fuchsia::modular::ModuleContext|
  void Active() override;

  // |fuchsia::modular::ModuleContext|
  void Done() override;

  // |fuchsia::modular::ModuleContext|
  void RequestStoryVisibilityState(
      fuchsia::modular::StoryVisibilityState visibility_state) override;

  // Identifies the module by its path, holds the URL of the running module, and
  // the link it was started with.
  const fuchsia::modular::ModuleData* const module_data_;

  // Not owned. The StoryControllerImpl for the Story in which this Module
  // lives.
  StoryControllerImpl* const story_controller_impl_;

  ComponentContextImpl component_context_impl_;

  fuchsia::modular::UserIntelligenceProvider* const
      user_intelligence_provider_;  // Not owned

  fidl::BindingSet<fuchsia::modular::ModuleContext> bindings_;

  // A service provider that represents the services to be added into an
  // application's namespace.
  component::ServiceProviderImpl service_provider_impl_;

  FXL_DISALLOW_COPY_AND_ASSIGN(ModuleContextImpl);
};

}  // namespace modular

#endif  // PERIDOT_BIN_USER_RUNNER_STORY_RUNNER_MODULE_CONTEXT_IMPL_H_
