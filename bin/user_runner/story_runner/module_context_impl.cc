// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/user_runner/story_runner/module_context_impl.h"

#include <string>

#include <lib/fidl/cpp/interface_request.h>
#include <lib/fxl/functional/make_copyable.h>
#include <lib/fxl/strings/join_strings.h>

#include "peridot/bin/user_runner/storage/constants_and_utils.h"
#include "peridot/bin/user_runner/story_runner/story_controller_impl.h"
#include "peridot/lib/fidl/clone.h"

namespace modular {

ModuleContextImpl::ModuleContextImpl(
    const ModuleContextInfo& info,
    const fuchsia::modular::ModuleData* const module_data,
    fidl::InterfaceRequest<fuchsia::sys::ServiceProvider>
        service_provider_request)
    : module_data_(module_data),
      story_controller_impl_(info.story_controller_impl),
      component_context_impl_(info.component_context_info,
                              EncodeModuleComponentNamespace(
                                  info.story_controller_impl->GetStoryId()),
                              EncodeModulePath(module_data_->module_path),
                              module_data_->module_url),
      user_intelligence_provider_(info.user_intelligence_provider) {
  service_provider_impl_.AddService<fuchsia::modular::ModuleContext>(
      [this](fidl::InterfaceRequest<fuchsia::modular::ModuleContext> request) {
        bindings_.AddBinding(this, std::move(request));
      });
  service_provider_impl_.AddBinding(std::move(service_provider_request));
}

ModuleContextImpl::~ModuleContextImpl() {}

void ModuleContextImpl::GetLink(
    fidl::StringPtr name,
    fidl::InterfaceRequest<fuchsia::modular::Link> request) {
  fuchsia::modular::LinkPathPtr link_path;
  // See if there's a parameter mapping for this link.
  link_path = story_controller_impl_->GetLinkPathForParameterName(
      module_data_->module_path, name);
  story_controller_impl_->ConnectLinkPath(std::move(link_path),
                                          std::move(request));
}

void ModuleContextImpl::EmbedModule(
    fidl::StringPtr name, fuchsia::modular::Intent intent,
    fidl::InterfaceRequest<fuchsia::modular::ModuleController>
        module_controller,
    fidl::InterfaceRequest<fuchsia::ui::viewsv1token::ViewOwner> view_owner,
    EmbedModuleCallback callback) {
  story_controller_impl_->EmbedModule(
      module_data_->module_path, name, fidl::MakeOptional(std::move(intent)),
      std::move(module_controller), std::move(view_owner),
      fuchsia::modular::ModuleSource::INTERNAL, callback);
}

void ModuleContextImpl::StartModule(
    fidl::StringPtr name, fuchsia::modular::Intent intent,
    fidl::InterfaceRequest<fuchsia::modular::ModuleController>
        module_controller,
    fuchsia::modular::SurfaceRelationPtr surface_relation,
    StartModuleCallback callback) {
  story_controller_impl_->StartModule(
      module_data_->module_path, name, fidl::MakeOptional(std::move(intent)),
      std::move(module_controller), std::move(surface_relation),
      fuchsia::modular::ModuleSource::INTERNAL, callback);
}

void ModuleContextImpl::StartContainerInShell(
    fidl::StringPtr name, fuchsia::modular::SurfaceRelation parent_relation,
    fidl::VectorPtr<fuchsia::modular::ContainerLayout> layout,
    fidl::VectorPtr<fuchsia::modular::ContainerRelationEntry> relationships,
    fidl::VectorPtr<fuchsia::modular::ContainerNode> nodes) {
  fidl::VectorPtr<fuchsia::modular::ContainerNodePtr> node_ptrs;
  node_ptrs->reserve(nodes->size());
  for (auto& i : *nodes) {
    node_ptrs.push_back(fidl::MakeOptional(std::move(i)));
  }
  story_controller_impl_->StartContainerInShell(
      module_data_->module_path, name,
      fidl::MakeOptional(std::move(parent_relation)), std::move(layout),
      std::move(relationships), std::move(node_ptrs));
}

void ModuleContextImpl::GetComponentContext(
    fidl::InterfaceRequest<fuchsia::modular::ComponentContext>
        context_request) {
  component_context_impl_.Connect(std::move(context_request));
}

void ModuleContextImpl::GetIntelligenceServices(
    fidl::InterfaceRequest<fuchsia::modular::IntelligenceServices> request) {
  auto module_scope = fuchsia::modular::ModuleScope::New();
  module_scope->module_path = module_data_->module_path.Clone();
  module_scope->url = module_data_->module_url;
  module_scope->story_id = story_controller_impl_->GetStoryId();

  auto scope = fuchsia::modular::ComponentScope::New();
  scope->set_module_scope(std::move(*module_scope));
  user_intelligence_provider_->GetComponentIntelligenceServices(
      std::move(*scope), std::move(request));
}

void ModuleContextImpl::GetStoryId(GetStoryIdCallback callback) {
  callback(story_controller_impl_->GetStoryId());
}

void ModuleContextImpl::RequestFocus() {
  story_controller_impl_->FocusModule(module_data_->module_path);
  story_controller_impl_->RequestStoryFocus();
}

void ModuleContextImpl::Active() { story_controller_impl_->Active(); }

void ModuleContextImpl::Done() {
  story_controller_impl_->HandleModuleDone(module_data_->module_path);
}

}  // namespace modular
