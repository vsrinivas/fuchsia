// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/syslog/cpp/macros.h>

#include <src/ui/bin/root_presenter/tests/fakes/fake_injector_registry.h>

namespace root_presenter {
namespace testing {

FakeInjectorRegistry::FakeInjectorRegistry(
    sys::testing::ComponentContextProvider& context_provider) {
  context_provider.service_directory_provider()->AddService<fuchsia::ui::pointerinjector::Registry>(
      registry_.GetHandler(this));
}

void FakeInjectorRegistry::Register(
    fuchsia::ui::pointerinjector::Config config,
    fidl::InterfaceRequest<fuchsia::ui::pointerinjector::Device> injector,
    RegisterCallback callback) {
  const uint32_t id = next_id_++;
  auto [it, success] = bindings_.try_emplace(id, this, std::move(injector));
  FX_CHECK(success);
  it->second.set_error_handler([this, id](zx_status_t status) { bindings_.erase(id); });
  callback();
}

void FakeInjectorRegistry::Inject(std::vector<fuchsia::ui::pointerinjector::Event> events,
                                  InjectCallback callback) {
  num_events_received_ += events.size();
  pending_callbacks_.emplace_back(std::move(callback));
}

void FakeInjectorRegistry::FirePendingCallbacks() {
  for (auto& callback : pending_callbacks_) {
    callback();
  }
  pending_callbacks_.clear();
}

void FakeInjectorRegistry::KillAllBindings() { bindings_.clear(); }

}  // namespace testing
}  // namespace root_presenter
