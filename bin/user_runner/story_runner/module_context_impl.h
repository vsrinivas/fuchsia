// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_USER_RUNNER_STORY_RUNNER_MODULE_CONTEXT_IMPL_H_
#define PERIDOT_BIN_USER_RUNNER_STORY_RUNNER_MODULE_CONTEXT_IMPL_H_

#include <string>

#include <fuchsia/modular/cpp/fidl.h>
#include <fuchsia/ui/views_v1_token/cpp/fidl.h>

#include "lib/app/cpp/service_provider_impl.h"
#include "lib/fidl/cpp/binding.h"
#include "lib/fidl/cpp/binding_set.h"
#include "lib/fidl/cpp/interface_handle.h"
#include "lib/fidl/cpp/interface_request.h"
#include "lib/fxl/macros.h"
#include "peridot/bin/user_runner/component_context_impl.h"

namespace fuchsia {
namespace modular {

class StoryControllerImpl;

// The dependencies of ModuleContextImpl common to all instances.
struct ModuleContextInfo {
  const ComponentContextInfo component_context_info;
  StoryControllerImpl* const story_controller_impl;
  modular::UserIntelligenceProvider* const user_intelligence_provider;
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
  ModuleContextImpl(const ModuleContextInfo& info,
                    const ModuleData* module_data,
                    fidl::InterfaceRequest<fuchsia::sys::ServiceProvider>
                        service_provider_request);

  ~ModuleContextImpl() override;

 private:
  // |ModuleContext|
  void GetLink(fidl::StringPtr name,
               fidl::InterfaceRequest<Link> request) override;
  // |ModuleContext|
  void EmbedModule(
      fidl::StringPtr name, Intent intent,
      fidl::InterfaceRequest<ModuleController> module_controller,
      fidl::InterfaceRequest<fuchsia::ui::views_v1_token::ViewOwner> view_owner,
      EmbedModuleCallback callback) override;
  // |ModuleContext|
  void StartModule(fidl::StringPtr name, Intent intent,
                   fidl::InterfaceRequest<ModuleController> module_controller,
                   SurfaceRelationPtr surface_relation,
                   StartModuleCallback callback) override;
  // |ModuleContext|
  void StartContainerInShell(
      fidl::StringPtr name, SurfaceRelation parent_relation,
      fidl::VectorPtr<ContainerLayout> layout,
      fidl::VectorPtr<ContainerRelationEntry> relationships,
      fidl::VectorPtr<ContainerNode> nodes) override;
  // |ModuleContext|
  void GetComponentContext(
      fidl::InterfaceRequest<ComponentContext> context_request) override;
  // |ModuleContext|
  void GetIntelligenceServices(
      fidl::InterfaceRequest<fuchsia::modular::IntelligenceServices> request)
      override;
  // |ModuleContext|
  void GetStoryId(GetStoryIdCallback callback) override;
  // |ModuleContext|
  void RequestFocus() override;
  // |ModuleContext|
  void Active() override;

  // Identifies the module by its path, holds the URL of the running module, and
  // the link it was started with.
  const ModuleData* const module_data_;

  // Not owned. The StoryControllerImpl for the Story in which this Module
  // lives.
  StoryControllerImpl* const story_controller_impl_;

  ComponentContextImpl component_context_impl_;

  fuchsia::modular::UserIntelligenceProvider* const
      user_intelligence_provider_;  // Not owned

  fidl::BindingSet<ModuleContext> bindings_;

  // A service provider that represents the services to be added into an
  // application's namespace.
  fuchsia::sys::ServiceProviderImpl service_provider_impl_;

  FXL_DISALLOW_COPY_AND_ASSIGN(ModuleContextImpl);
};

}  // namespace modular
}  // namespace fuchsia

#endif  // PERIDOT_BIN_USER_RUNNER_STORY_RUNNER_MODULE_CONTEXT_IMPL_H_
