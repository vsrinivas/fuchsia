// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/modular/src/story_runner/module_context_impl.h"

#include <string>

#include "apps/modular/lib/ledger/storage.h"
#include "apps/modular/src/story_runner/module_controller_impl.h"
#include "apps/modular/src/story_runner/story_impl.h"
#include "lib/fidl/cpp/bindings/interface_request.h"
#include "lib/ftl/strings/join_strings.h"

namespace modular {

ModuleContextImpl::ModuleContextImpl(
    const fidl::Array<fidl::String>& module_path,
    const ModuleContextInfo& info,
    const uint64_t id,
    const std::string& module_url,
    const LinkPathPtr& default_link_path,
    ModuleControllerImpl* const module_controller_impl,
    fidl::InterfaceRequest<ModuleContext> module_context)
    : module_path_(module_path.Clone()),
      id_(id),
      story_impl_(info.story_impl),
      module_url_(module_url),
      default_link_path_(default_link_path.Clone()),
      module_controller_impl_(module_controller_impl),
      component_context_impl_(
          info.component_context_info,
          EncodeModuleComponentNamespace(info.story_impl->GetStoryId()),
          EncodeModulePath(module_path_),
          module_url_),
      user_intelligence_provider_(info.user_intelligence_provider),
      binding_(this, std::move(module_context)) {}

ModuleContextImpl::~ModuleContextImpl() {}

void ModuleContextImpl::GetLink(const fidl::String& name,
                                fidl::InterfaceRequest<Link> link) {
  LinkPathPtr link_path;
  if (name) {
    link_path = LinkPath::New();
    link_path->module_path = module_path_.Clone();
    link_path->link_name = name;
  } else {
    link_path = default_link_path_.Clone();
  }
  story_impl_->GetLinkPath(std::move(link_path), std::move(link));
}

void ModuleContextImpl::StartModule(
    const fidl::String& name,
    const fidl::String& query,
    const fidl::String& link_name,
    fidl::InterfaceHandle<app::ServiceProvider> outgoing_services,
    fidl::InterfaceRequest<app::ServiceProvider> incoming_services,
    fidl::InterfaceRequest<ModuleController> module_controller,
    fidl::InterfaceRequest<mozart::ViewOwner> view_owner) {
  story_impl_->StartModule(
      module_path_, name, query, link_name, std::move(outgoing_services),
      std::move(incoming_services), std::move(module_controller),
      std::move(view_owner), [](uint32_t) {});
}

void ModuleContextImpl::StartModuleInShell(
    const fidl::String& name,
    const fidl::String& query,
    const fidl::String& link_name,
    fidl::InterfaceHandle<app::ServiceProvider> outgoing_services,
    fidl::InterfaceRequest<app::ServiceProvider> incoming_services,
    fidl::InterfaceRequest<ModuleController> module_controller,
    SurfaceRelationPtr surface_relation) {
  story_impl_->StartModuleInShell(
      module_path_, name, query, link_name, std::move(outgoing_services),
      std::move(incoming_services), std::move(module_controller), id_,
      std::move(surface_relation));
}

void ModuleContextImpl::GetComponentContext(
    fidl::InterfaceRequest<ComponentContext> context_request) {
  component_context_bindings_.AddBinding(&component_context_impl_,
                                         std::move(context_request));
}

void ModuleContextImpl::GetIntelligenceServices(
    fidl::InterfaceRequest<maxwell::IntelligenceServices> request) {
  auto module_scope = maxwell::ModuleScope::New();
  module_scope->url = module_url_;
  module_scope->story_id = story_impl_->GetStoryId();
  auto scope = maxwell::ComponentScope::New();
  scope->set_module_scope(std::move(module_scope));
  user_intelligence_provider_->GetComponentIntelligenceServices(
      std::move(scope), std::move(request));
}

void ModuleContextImpl::GetStoryId(const GetStoryIdCallback& callback) {
  callback(story_impl_->GetStoryId());
}

void ModuleContextImpl::Ready() {
  if (module_controller_impl_) {
    module_controller_impl_->SetState(ModuleState::RUNNING);
  }
}

void ModuleContextImpl::Done() {
  if (module_controller_impl_) {
    module_controller_impl_->SetState(ModuleState::DONE);
  }
}

}  // namespace modular
