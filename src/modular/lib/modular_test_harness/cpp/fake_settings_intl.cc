// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/modular/lib/modular_test_harness/cpp/fake_settings_intl.h"

#include <fuchsia/modular/cpp/fidl.h>

namespace modular_testing {

FakeSettingsIntl::FakeSettingsIntl(FakeComponent::Args args) : FakeComponent(std::move(args)) {}
FakeSettingsIntl::~FakeSettingsIntl() = default;

// static
std::unique_ptr<FakeSettingsIntl> FakeSettingsIntl::CreateWithDefaultOptions() {
  return std::make_unique<FakeSettingsIntl>(modular_testing::FakeComponent::Args{
      .url = modular_testing::TestHarnessBuilder::GenerateFakeUrl(), .sandbox_services = {}});
}

fidl::InterfaceRequestHandler<fuchsia::settings::Intl> FakeSettingsIntl::GetHandler() {
  return bindings_.GetHandler(this);
}

void FakeSettingsIntl::OnCreate(fuchsia::sys::StartupInfo startup_info) {
  component_context()->outgoing()->AddPublicService(bindings_.GetHandler(this));
}

void FakeSettingsIntl::OnDestroy() {}

// |fuchsia::settings::Intl|
void FakeSettingsIntl::Watch(WatchCallback callback) { watch_callback_ = std::move(callback); }

// |fuchsia::settings::Intl|
void FakeSettingsIntl::Set(fuchsia::settings::IntlSettings settings, SetCallback callback) {
  settings_ = std::make_unique<fuchsia::settings::IntlSettings>();
  zx_status_t status = settings.Clone(settings_.get());

  if (status == ZX_OK) {
    callback(fuchsia::settings::Intl_Set_Result{fpromise::ok()});
  } else {
    callback(fuchsia::settings::Intl_Set_Result{fpromise::error(fuchsia::settings::Error::FAILED)});
  }

  if (watch_callback_) {
    watch_callback_(std::move(settings));
  }
}

}  // namespace modular_testing
