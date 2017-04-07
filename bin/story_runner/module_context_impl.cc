// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/modular/src/story_runner/module_context_impl.h"

#include <string>

#include "lib/ftl/strings/join_strings.h"
#include "apps/modular/lib/ledger/storage.h"
#include "apps/modular/src/story_runner/module_controller_impl.h"
#include "apps/modular/src/story_runner/story_impl.h"
#include "lib/fidl/cpp/bindings/interface_request.h"

namespace modular {

ModuleContextImpl::ModuleContextImpl(
    fidl::Array<fidl::String> module_path, const ModuleContextInfo& info,
    const uint64_t id, const std::string& module_url,
    ModuleControllerImpl* const module_controller_impl,
    fidl::InterfaceRequest<ModuleContext> module_context)
    : module_path_(std::move(module_path)),
      id_(id),
      story_impl_(info.story_impl),
      module_url_(module_url),
      module_controller_impl_(module_controller_impl),
      component_context_impl_(
          info.component_context_info,
          EncodeModuleComponentNamespace(info.story_impl->GetStoryId()),
          EncodeModulePath(module_path_)),
      user_intelligence_provider_(info.user_intelligence_provider),
      binding_(this, std::move(module_context)) {}

ModuleContextImpl::~ModuleContextImpl() {}

void ModuleContextImpl::CreateLink(const fidl::String& name,
                                   fidl::InterfaceRequest<Link> link) {
  story_impl_->CreateLink(module_path_, name, std::move(link));
}

void ModuleContextImpl::StartModule(
    const fidl::String& name,
    const fidl::String& query,
    fidl::InterfaceHandle<Link> link,
    fidl::InterfaceHandle<app::ServiceProvider> outgoing_services,
    fidl::InterfaceRequest<app::ServiceProvider> incoming_services,
    fidl::InterfaceRequest<ModuleController> module_controller,
    fidl::InterfaceRequest<mozart::ViewOwner> view_owner) {
  story_impl_->StartModule(module_path_, name, query, std::move(link),
                           std::move(outgoing_services),
                           std::move(incoming_services),
                           std::move(module_controller), std::move(view_owner));
}

void ModuleContextImpl::StartModuleInShell(
    const fidl::String& name,
    const fidl::String& query,
    fidl::InterfaceHandle<Link> link,
    fidl::InterfaceHandle<app::ServiceProvider> outgoing_services,
    fidl::InterfaceRequest<app::ServiceProvider> incoming_services,
    fidl::InterfaceRequest<ModuleController> module_controller,
    const fidl::String& view_type) {
  story_impl_->StartModuleInShell(module_path_, name, query, std::move(link),
                                  std::move(outgoing_services),
                                  std::move(incoming_services),
                                  std::move(module_controller), id_, view_type);
}

void ModuleContextImpl::GetComponentContext(
    fidl::InterfaceRequest<ComponentContext> context_request) {
  component_context_bindings_.AddBinding(&component_context_impl_,
                                         std::move(context_request));
}

void ModuleContextImpl::GetIntelligenceServices(
    fidl::InterfaceRequest<maxwell::IntelligenceServices> request) {
  user_intelligence_provider_->GetComponentIntelligenceServices(
      story_impl_->GetStoryId(), module_url_, std::move(request));
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
