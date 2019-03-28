// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/a11y/a11y_manager/app.h"

namespace a11y_manager {

App::App()
    : startup_context_(sys::ComponentContext::Create()),
      semantic_tree_(std::make_unique<SemanticTree>()),
      a11y_manager_(std::make_unique<ManagerImpl>()),
      toggler_impl_(std::make_unique<TogglerImpl>()),
      settings_manager_impl_(std::make_unique<SettingsManagerImpl>()),
      semantics_manager_impl_(std::make_unique<SemanticsManagerImpl>()) {
  startup_context_->outgoing()
      ->AddPublicService<fuchsia::accessibility::Manager>(
          [this](
              fidl::InterfaceRequest<fuchsia::accessibility::Manager> request) {
            a11y_manager_->AddBinding(std::move(request));
          });

  startup_context_->outgoing()
      ->AddPublicService<fuchsia::accessibility::SemanticsRoot>(
          [this](fidl::InterfaceRequest<fuchsia::accessibility::SemanticsRoot>
                     request) {
            semantic_tree_->AddBinding(std::move(request));
          });

  startup_context_->outgoing()
      ->AddPublicService<fuchsia::accessibility::Toggler>(
          [this](
              fidl::InterfaceRequest<fuchsia::accessibility::Toggler> request) {
            toggler_impl_->AddTogglerBinding(std::move(request));
          });
  startup_context_->outgoing()
      ->AddPublicService<fuchsia::accessibility::ToggleBroadcaster>(
          [this](
              fidl::InterfaceRequest<fuchsia::accessibility::ToggleBroadcaster>
                  request) {
            toggler_impl_->AddToggleBroadcasterBinding(std::move(request));
          });

  startup_context_->outgoing()
      ->AddPublicService<fuchsia::accessibility::SettingsManager>(
          [this](fidl::InterfaceRequest<fuchsia::accessibility::SettingsManager>
                     request) {
            settings_manager_impl_->AddBinding(std::move(request));
          });

  startup_context_->outgoing()
      ->AddPublicService<fuchsia::accessibility::semantics::SemanticsManager>(
          [this](fidl::InterfaceRequest<
                 fuchsia::accessibility::semantics::SemanticsManager>
                     request) {
            semantics_manager_impl_->AddBinding(std::move(request));
          });
}

}  // namespace a11y_manager
