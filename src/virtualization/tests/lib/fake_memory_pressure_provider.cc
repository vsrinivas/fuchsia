// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "fake_memory_pressure_provider.h"

#include <gtest/gtest.h>

void FakeMemoryPressureProvider::RegisterWatcher(
    ::fidl::InterfaceHandle<::fuchsia::memorypressure::Watcher> watcher) {
  fuchsia::memorypressure::WatcherPtr watcher_proxy = watcher.Bind();
  watcher_proxy->OnLevelChanged(::fuchsia::memorypressure::Level::NORMAL, []() {});
  watchers_.push_back(std::move(watcher_proxy));
}

void FakeMemoryPressureProvider::Start(
    std::unique_ptr<component_testing::LocalComponentHandles> handles) {
  handles_ = std::move(handles);

  ASSERT_EQ(handles_->outgoing()->AddPublicService(bindings_.GetHandler(this, dispatcher_)), ZX_OK);
}

void FakeMemoryPressureProvider::OnLevelChanged(::fuchsia::memorypressure::Level level) {
  for (auto& watcher : watchers_) {
    watcher->OnLevelChanged(level, []() {});
  }
}
