// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/modular/src/story_runner/module_context_impl.h"

#include <string>

#include "apps/modular/src/story_runner/module_controller_impl.h"
#include "apps/modular/src/story_runner/story_impl.h"
#include "lib/fidl/cpp/bindings/interface_request.h"

namespace modular {

ModuleContextImpl::ModuleContextImpl(
    StoryImpl* const story_impl,
    const std::string& module_url,
    ModuleControllerImpl* const module_controller_impl,
    const ComponentContextInfo& component_context_info,
    fidl::InterfaceRequest<ModuleContext> module_context)
    : story_impl_(story_impl),
      module_url_(module_url),
      module_controller_impl_(module_controller_impl),
      component_context_impl_(component_context_info, module_url),
      binding_(this, std::move(module_context)) {}

ModuleContextImpl::~ModuleContextImpl() {}

void ModuleContextImpl::CreateLink(const fidl::String& name,
                                   fidl::InterfaceRequest<Link> link) {
  story_impl_->CreateLink(name, std::move(link));
}

void ModuleContextImpl::StartModule(
    const fidl::String& query,
    fidl::InterfaceHandle<Link> link,
    fidl::InterfaceHandle<app::ServiceProvider> outgoing_services,
    fidl::InterfaceRequest<app::ServiceProvider> incoming_services,
    fidl::InterfaceRequest<ModuleController> module_controller,
    fidl::InterfaceRequest<mozart::ViewOwner> view_owner) {
  story_impl_->StartModule(query, std::move(link), std::move(outgoing_services),
                           std::move(incoming_services),
                           std::move(module_controller), std::move(view_owner));
}

void ModuleContextImpl::StartModuleInShell(
    const fidl::String& query,
    fidl::InterfaceHandle<Link> link,
    fidl::InterfaceHandle<app::ServiceProvider> outgoing_services,
    fidl::InterfaceRequest<app::ServiceProvider> incoming_services,
    fidl::InterfaceRequest<ModuleController> module_controller) {
  story_impl_->StartModuleInShell(
      query, std::move(link), std::move(outgoing_services),
      std::move(incoming_services), std::move(module_controller));
}

void ModuleContextImpl::GetComponentContext(
    fidl::InterfaceRequest<ComponentContext> context_request) {
  component_context_bindings_.AddBinding(&component_context_impl_,
                                         std::move(context_request));
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
