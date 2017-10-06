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
                    ModuleDataPtr module_data,
                    ModuleControllerImpl* module_controller_impl,
                    fidl::InterfaceRequest<app::ServiceProvider> service_provider_request);

  ~ModuleContextImpl() override;

  const ModuleData& module_data() const { return *module_data_; }

  const fidl::Array<fidl::String>& module_path() const {
    return module_data_->module_path;
  }

  const std::string& module_url() const { return module_data_->module_url; }

  const LinkPath& link_path() const { return *module_data_->link_path; }

 private:
  // |ModuleContext|
  void GetLink(const fidl::String& name,
               fidl::InterfaceRequest<Link> request) override;
  // |ModuleContext|
  void StartModule(
      const fidl::String& name,
      const fidl::String& query,
      const fidl::String& link_name,
      fidl::InterfaceHandle<app::ServiceProvider> outgoing_services,
      fidl::InterfaceRequest<app::ServiceProvider> incoming_services,
      fidl::InterfaceRequest<ModuleController> module_controller,
      fidl::InterfaceRequest<mozart::ViewOwner> view_owner) override;
  // |ModuleContext|
  void StartModuleInShell(
      const fidl::String& name,
      const fidl::String& query,
      const fidl::String& link_name,
      fidl::InterfaceHandle<app::ServiceProvider> outgoing_services,
      fidl::InterfaceRequest<app::ServiceProvider> incoming_services,
      fidl::InterfaceRequest<ModuleController> module_controller,
      SurfaceRelationPtr surface_relation,
      bool focus) override;
  // |ModuleContext|
  void GetComponentContext(
      fidl::InterfaceRequest<ComponentContext> context_request) override;
  // |ModuleContext|
  void GetIntelligenceServices(
      fidl::InterfaceRequest<maxwell::IntelligenceServices> request) override;
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
  const ModuleDataPtr module_data_;

  // Not owned. The StoryControllerImpl instance this ModuleContextImpl instance
  // connects to.
  StoryControllerImpl* const story_controller_impl_;

  // Not owned. Used to notify module watchers and request tear down.
  ModuleControllerImpl* const module_controller_impl_;

  ComponentContextImpl component_context_impl_;
  fidl::BindingSet<ComponentContext> component_context_bindings_;

  maxwell::UserIntelligenceProvider* const
      user_intelligence_provider_;  // Not owned

  fidl::BindingSet<ModuleContext> bindings_;

  // A service provider that represents the services to be added into an
  // application's namespace.
  app::ServiceProviderImpl service_provider_impl_;

  FXL_DISALLOW_COPY_AND_ASSIGN(ModuleContextImpl);
};

}  // namespace modular

#endif  // PERIDOT_BIN_STORY_RUNNER_MODULE_CONTEXT_IMPL_H_
