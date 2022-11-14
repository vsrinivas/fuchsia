// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_VIRTUALIZATION_TESTS_LIB_FAKE_MEMORY_PRESSURE_PROVIDER_H_
#define SRC_VIRTUALIZATION_TESTS_LIB_FAKE_MEMORY_PRESSURE_PROVIDER_H_

#include <fuchsia/memorypressure/cpp/fidl.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/sys/component/cpp/testing/realm_builder.h>

class FakeMemoryPressureProvider : public fuchsia::memorypressure::Provider,
                                   public component_testing::LocalComponent {
 public:
  explicit FakeMemoryPressureProvider(async_dispatcher_t* dispatcher) : dispatcher_(dispatcher) {}

  void RegisterWatcher(
      ::fidl::InterfaceHandle<::fuchsia::memorypressure::Watcher> watcher) override;

  void Start(std::unique_ptr<component_testing::LocalComponentHandles> handles) override;

  void OnLevelChanged(::fuchsia::memorypressure::Level level);

 private:
  async_dispatcher_t* dispatcher_;
  std::vector<fuchsia::memorypressure::WatcherPtr> watchers_;
  fidl::BindingSet<fuchsia::memorypressure::Provider> bindings_;
  std::unique_ptr<component_testing::LocalComponentHandles> handles_;
};

#endif  // SRC_VIRTUALIZATION_TESTS_LIB_FAKE_MEMORY_PRESSURE_PROVIDER_H_
