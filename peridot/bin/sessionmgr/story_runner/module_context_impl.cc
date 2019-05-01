// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/sessionmgr/story_runner/module_context_impl.h"

#include <lib/fidl/cpp/interface_request.h>
#include <lib/ui/scenic/cpp/view_token_pair.h>
#include <src/lib/fxl/strings/join_strings.h>

#include <string>

#include "peridot/bin/sessionmgr/storage/constants_and_utils.h"
#include "peridot/bin/sessionmgr/story/systems/story_visibility_system.h"
#include "peridot/bin/sessionmgr/story_runner/story_controller_impl.h"
#include "peridot/lib/fidl/clone.h"

namespace modular {

ModuleContextImpl::ModuleContextImpl(
    const ModuleContextInfo& info,
    const fuchsia::modular::ModuleData* const module_data,
    fidl::InterfaceRequest<fuchsia::sys::ServiceProvider>
        service_provider_request)
    : module_data_(module_data),
      story_controller_impl_(info.story_controller_impl),
      story_visibility_system_(info.story_visibility_system),
      component_context_impl_(info.component_context_info,
                              EncodeModuleComponentNamespace(
                                  info.story_controller_impl->GetStoryId()),
                              EncodeModulePath(module_data_->module_path),
                              module_data_->module_url),
      user_intelligence_provider_(info.user_intelligence_provider) {
  service_provider_impl_.AddService<fuchsia::modular::ComponentContext>(
      [this](
          fidl::InterfaceRequest<fuchsia::modular::ComponentContext> request) {
        component_context_impl_.Connect(std::move(request));
      });
  service_provider_impl_.AddService<fuchsia::modular::ModuleContext>(
      [this](fidl::InterfaceRequest<fuchsia::modular::ModuleContext> request) {
        bindings_.AddBinding(this, std::move(request));
      });
  service_provider_impl_.AddService<fuchsia::modular::IntelligenceServices>(
      [this](fidl::InterfaceRequest<fuchsia::modular::IntelligenceServices>
                 request) {
        auto module_scope = fuchsia::modular::ModuleScope::New();
        module_scope->module_path = module_data_->module_path;
        module_scope->url = module_data_->module_url;
        module_scope->story_id = story_controller_impl_->GetStoryId();

        auto scope = fuchsia::modular::ComponentScope::New();
        scope->set_module_scope(std::move(*module_scope));
        user_intelligence_provider_->GetComponentIntelligenceServices(
            std::move(*scope), std::move(request));
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
    std::string name, fuchsia::modular::Intent intent,
    fidl::InterfaceRequest<fuchsia::modular::ModuleController>
        module_controller,
    fidl::InterfaceRequest<fuchsia::ui::viewsv1token::ViewOwner> view_owner,
    EmbedModuleCallback callback) {
  EmbedModule2(
      std::move(name), std::move(intent), std::move(module_controller),
      scenic::ToViewToken(zx::eventpair(view_owner.TakeChannel().release())),
      std::move(callback));
}

void ModuleContextImpl::EmbedModule2(
    std::string name, fuchsia::modular::Intent intent,
    fidl::InterfaceRequest<fuchsia::modular::ModuleController>
        module_controller,
    fuchsia::ui::views::ViewToken view_token, EmbedModule2Callback callback) {
  AddModParams params;
  params.parent_mod_path = module_data_->module_path;
  params.mod_name = name;
  params.intent = std::move(intent);
  params.module_source = fuchsia::modular::ModuleSource::INTERNAL;
  params.surface_relation = nullptr;
  params.is_embedded = true;
  story_controller_impl_->EmbedModule(
      std::move(params), std::move(module_controller), std::move(view_token),
      std::move(callback));
}

void ModuleContextImpl::AddModuleToStory(
    std::string name, fuchsia::modular::Intent intent,
    fidl::InterfaceRequest<fuchsia::modular::ModuleController>
        module_controller,
    fuchsia::modular::SurfaceRelationPtr surface_relation,
    AddModuleToStoryCallback callback) {
  AddModParams params;
  params.parent_mod_path = module_data_->module_path;
  params.mod_name = name;
  params.intent = std::move(intent);
  params.module_source = fuchsia::modular::ModuleSource::INTERNAL;
  params.surface_relation = std::move(surface_relation);
  params.is_embedded = false;
  story_controller_impl_->AddModuleToStory(
      std::move(params), std::move(module_controller), std::move(callback));
}

void ModuleContextImpl::StartContainerInShell(
    std::string name, fuchsia::modular::SurfaceRelation parent_relation,
    std::vector<fuchsia::modular::ContainerLayout> layout,
    std::vector<fuchsia::modular::ContainerRelationEntry> relationships,
    std::vector<fuchsia::modular::ContainerNode> nodes) {
  FXL_LOG(ERROR) << "ModuleContext.StartContainerInShell() not implemented.";
}

void ModuleContextImpl::GetComponentContext(
    fidl::InterfaceRequest<fuchsia::modular::ComponentContext>
        context_request) {
  component_context_impl_.Connect(std::move(context_request));
}

void ModuleContextImpl::GetStoryId(GetStoryIdCallback callback) {
  callback(story_controller_impl_->GetStoryId());
}

void ModuleContextImpl::RequestFocus() {
  story_controller_impl_->FocusModule(module_data_->module_path);
  story_controller_impl_->RequestStoryFocus();
}

void ModuleContextImpl::Active() {}

void ModuleContextImpl::RemoveSelfFromStory() {
  story_controller_impl_->RemoveModuleFromStory(module_data_->module_path);
}

void ModuleContextImpl::RequestStoryVisibilityState(
    fuchsia::modular::StoryVisibilityState visibility_state) {
  story_visibility_system_->RequestStoryVisibilityStateChange(visibility_state);
}

void ModuleContextImpl::StartOngoingActivity(
    fuchsia::modular::OngoingActivityType ongoing_activity_type,
    fidl::InterfaceRequest<fuchsia::modular::OngoingActivity> request) {
  story_controller_impl_->StartOngoingActivity(ongoing_activity_type,
                                               std::move(request));
}

void ModuleContextImpl::CreateEntity(
    std::string type, fuchsia::mem::Buffer data,
    fidl::InterfaceRequest<fuchsia::modular::Entity> entity_request,
    CreateEntityCallback callback) {
  story_controller_impl_->CreateEntity(
      type, std::move(data), std::move(entity_request), std::move(callback));
}

}  // namespace modular
