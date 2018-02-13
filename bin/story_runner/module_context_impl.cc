// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/story_runner/module_context_impl.h"

#include <string>

#include "lib/fidl/cpp/bindings/interface_request.h"
#include "lib/fxl/functional/make_copyable.h"
#include "lib/fxl/strings/join_strings.h"
#include "peridot/bin/story_runner/module_controller_impl.h"
#include "peridot/bin/story_runner/story_controller_impl.h"
#include "peridot/lib/ledger_client/storage.h"

namespace modular {

ModuleContextImpl::ModuleContextImpl(
    const ModuleContextInfo& info,
    const ModuleData* const module_data,
    ModuleControllerImpl* const module_controller_impl,
    f1dl::InterfaceRequest<app::ServiceProvider> service_provider_request)
    : module_data_(module_data),
      story_controller_impl_(info.story_controller_impl),
      module_controller_impl_(module_controller_impl),
      component_context_impl_(info.component_context_info,
                              EncodeModuleComponentNamespace(
                                  info.story_controller_impl->GetStoryId()),
                              EncodeModulePath(module_data_->module_path),
                              module_data_->module_url),
      user_intelligence_provider_(info.user_intelligence_provider) {
  service_provider_impl_.AddService<ModuleContext>(
      [this](f1dl::InterfaceRequest<ModuleContext> request) {
        bindings_.AddBinding(this, std::move(request));
      });
  service_provider_impl_.AddBinding(std::move(service_provider_request));
}

ModuleContextImpl::~ModuleContextImpl() {}

void ModuleContextImpl::GetLink(const f1dl::String& name,
                                f1dl::InterfaceRequest<Link> request) {
  LinkPathPtr link_path;
  LinkImpl::ConnectionType connection_type{LinkImpl::ConnectionType::Secondary};
  if (name) {
    // See if there's a chain mapping for this module.
    link_path = story_controller_impl_->GetLinkPathForChainKey(
        module_data_->module_path, name);
    if (!link_path) {
      link_path = LinkPath::New();
      link_path->module_path = module_data_->module_path.Clone();
      link_path->link_name = name;
      connection_type = LinkImpl::ConnectionType::Primary;
    }
  } else {
    link_path = module_data_->link_path.Clone();
  }
  story_controller_impl_->ConnectLinkPath(std::move(link_path), connection_type,
                                          std::move(request));
}

void ModuleContextImpl::StartModule(
    const f1dl::String& name,
    const f1dl::String& query,
    const f1dl::String& link_name,
    f1dl::InterfaceRequest<app::ServiceProvider> incoming_services,
    f1dl::InterfaceRequest<ModuleController> module_controller,
    f1dl::InterfaceRequest<mozart::ViewOwner> view_owner) {
  story_controller_impl_->StartModule(
      module_data_->module_path, name, query, link_name,
      nullptr /* create_chain_info */, std::move(incoming_services),
      std::move(module_controller), std::move(view_owner),
      ModuleSource::INTERNAL);
}

void ModuleContextImpl::StartDaisy(
    const f1dl::String& name,
    DaisyPtr daisy,
    const f1dl::String& link_name,
    f1dl::InterfaceRequest<app::ServiceProvider> incoming_services,
    f1dl::InterfaceRequest<ModuleController> module_controller,
    f1dl::InterfaceRequest<mozart::ViewOwner> view_owner,
    const StartDaisyCallback& callback) {
  story_controller_impl_->StartDaisy(
      module_data_->module_path, name, std::move(daisy),
      std::move(incoming_services), std::move(module_controller),
      std::move(view_owner), ModuleSource::INTERNAL, callback);
}

void ModuleContextImpl::StartModuleInShell(
    const f1dl::String& name,
    const f1dl::String& query,
    const f1dl::String& link_name,
    f1dl::InterfaceRequest<app::ServiceProvider> incoming_services,
    f1dl::InterfaceRequest<ModuleController> module_controller,
    SurfaceRelationPtr surface_relation,
    const bool focus) {
  story_controller_impl_->StartModuleInShell(
      module_data_->module_path, name, query, link_name,
      nullptr /* create_chain_info */, std::move(incoming_services),
      std::move(module_controller), std::move(surface_relation), focus,
      ModuleSource::INTERNAL);
}

void ModuleContextImpl::StartDaisyInShell(
    const f1dl::String& name,
    DaisyPtr daisy,
    const f1dl::String& link_name,
    f1dl::InterfaceRequest<app::ServiceProvider> incoming_services,
    f1dl::InterfaceRequest<ModuleController> module_controller,
    SurfaceRelationPtr surface_relation,
    const StartDaisyInShellCallback& callback) {
  story_controller_impl_->StartDaisyInShell(
      module_data_->module_path, name, std::move(daisy),
      std::move(incoming_services), std::move(module_controller),
      std::move(surface_relation), ModuleSource::INTERNAL, callback);
}

void ModuleContextImpl::StartContainerInShell(
    const f1dl::String& name,
    f1dl::Array<ContainerLayoutPtr> layout,
    f1dl::Array<ContainerRelationEntryPtr> relationships,
    f1dl::Array<ContainerNodePtr> nodes) {
  story_controller_impl_->StartContainerInShell(
      module_data_->module_path,
      name, std::move(layout), std::move(relationships), std::move(nodes));
}

void ModuleContextImpl::EmbedModule(
    const f1dl::String& name,
    const f1dl::String& query,
    const f1dl::String& link_name,
    f1dl::InterfaceRequest<app::ServiceProvider> incoming_services,
    f1dl::InterfaceRequest<ModuleController> module_controller,
    f1dl::InterfaceHandle<EmbedModuleWatcher> embed_module_watcher,
    f1dl::InterfaceRequest<mozart::ViewOwner> view_owner) {
  story_controller_impl_->EmbedModule(
      module_data_->module_path, name, query, link_name,
      nullptr /* create_chain_info */, std::move(incoming_services),
      std::move(module_controller), std::move(embed_module_watcher),
      std::move(view_owner));
}

void ModuleContextImpl::GetComponentContext(
    f1dl::InterfaceRequest<ComponentContext> context_request) {
  component_context_impl_.Connect(std::move(context_request));
}

void ModuleContextImpl::GetIntelligenceServices(
    f1dl::InterfaceRequest<maxwell::IntelligenceServices> request) {
  auto module_scope = maxwell::ModuleScope::New();
  module_scope->module_path = module_data_->module_path.Clone();
  module_scope->url = module_data_->module_url;
  module_scope->story_id = story_controller_impl_->GetStoryId();

  auto scope = maxwell::ComponentScope::New();
  scope->set_module_scope(std::move(module_scope));
  user_intelligence_provider_->GetComponentIntelligenceServices(
      std::move(scope), std::move(request));
}

void ModuleContextImpl::GetStoryId(const GetStoryIdCallback& callback) {
  callback(story_controller_impl_->GetStoryId());
}

void ModuleContextImpl::RequestFocus() {
  // TODO(zbowling): we should be asking the module_controller_impl_ if it's ok.
  // For now, we are not going to "request" anything. Just do it.
  story_controller_impl_->FocusModule(module_data_->module_path);
  story_controller_impl_->RequestStoryFocus();
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
