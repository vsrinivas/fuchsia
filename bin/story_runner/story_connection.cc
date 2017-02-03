// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/modular/src/story_runner/story_connection.h"

#include <string>

#include "apps/ledger/services/public/ledger.fidl.h"
#include "apps/modular/src/story_runner/module_controller_impl.h"
#include "apps/modular/src/story_runner/story_impl.h"
#include "lib/fidl/cpp/bindings/interface_request.h"

namespace modular {

StoryConnection::StoryConnection(
    StoryImpl* const story_impl,
    const std::string& module_url,
    ModuleControllerImpl* const module_controller_impl,
    fidl::InterfaceRequest<Story> story)
    : story_impl_(story_impl),
      module_url_(module_url),
      module_controller_impl_(module_controller_impl),
      binding_(this, std::move(story)) {}

StoryConnection::~StoryConnection() {}

void StoryConnection::CreateLink(const fidl::String& name,
                                 fidl::InterfaceRequest<Link> link) {
  story_impl_->CreateLink(name, std::move(link));
}

void StoryConnection::StartModule(
    const fidl::String& query,
    fidl::InterfaceHandle<Link> link,
    fidl::InterfaceHandle<ServiceProvider> outgoing_services,
    fidl::InterfaceRequest<ServiceProvider> incoming_services,
    fidl::InterfaceRequest<ModuleController> module_controller,
    fidl::InterfaceRequest<mozart::ViewOwner> view_owner) {
  story_impl_->StartModule(query, std::move(link), std::move(outgoing_services),
                           std::move(incoming_services),
                           std::move(module_controller), std::move(view_owner));
}

void StoryConnection::GetLedger(fidl::InterfaceRequest<ledger::Ledger> request,
                                const GetLedgerCallback& result) {
  if (!module_url_.empty()) {
    story_impl_->GetLedger(module_url_, std::move(request), result);
  } else {
    result(ledger::Status::UNKNOWN_ERROR);
  }
}

void StoryConnection::GetComponentContext(
      fidl::InterfaceRequest<ComponentContext> context_request) {
  component_context_bindings_.AddBinding(&component_context_impl_,
                                         std::move(context_request));
}

void StoryConnection::Ready() {
  if (module_controller_impl_) {
    module_controller_impl_->SetState(ModuleState::RUNNING);
  }
}

void StoryConnection::Done() {
  if (module_controller_impl_) {
    module_controller_impl_->SetState(ModuleState::DONE);
  }
}

}  // namespace modular
