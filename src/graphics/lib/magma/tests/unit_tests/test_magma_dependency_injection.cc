// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.gpu.magma/cpp/wire.h>
#include <fuchsia/memorypressure/cpp/fidl.h>
#include <lib/async-loop/default.h>
#include <lib/async-loop/loop.h>
#include <lib/async/cpp/task.h>
#include <lib/fake_ddk/fake_ddk.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/sync/completion.h>

#include <gtest/gtest.h>

#include "src/graphics/lib/magma/src/magma_util/platform/zircon/magma_dependency_injection_device.h"

namespace {
class TestOwner : public magma::MagmaDependencyInjectionDevice::Owner {
 public:
  // Will be called on an arbitrary thread.
  void SetMemoryPressureLevel(MagmaMemoryPressureLevel level) override {
    level_ = level;
    sync_completion_signal(&completion_);
  }
  MagmaMemoryPressureLevel level() const { return level_; }
  sync_completion_t& completion() { return completion_; }

 private:
  MagmaMemoryPressureLevel level_{MAGMA_MEMORY_PRESSURE_LEVEL_NORMAL};
  sync_completion_t completion_;
};

class Provider : public fuchsia::memorypressure::Provider {
 public:
  void RegisterWatcher(
      ::fidl::InterfaceHandle<::fuchsia::memorypressure::Watcher> watcher) override {
    fuchsia::memorypressure::WatcherSyncPtr watcher_sync;
    watcher_sync.Bind(std::move(watcher));
    watcher_sync->OnLevelChanged(fuchsia::memorypressure::Level::CRITICAL);
  }
  fidl::BindingSet<fuchsia::memorypressure::Provider>& binding_set() { return binding_set_; }

 private:
  fidl::BindingSet<fuchsia::memorypressure::Provider> binding_set_;
};

TEST(DependencyInjection, Load) {
  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  fake_ddk::Bind ddk;
  TestOwner owner;
  Provider provider;
  auto dependency_injection_device =
      std::make_unique<magma::MagmaDependencyInjectionDevice>(fake_ddk::kFakeParent, &owner);
  auto* device = dependency_injection_device.get();
  EXPECT_EQ(ZX_OK,
            magma::MagmaDependencyInjectionDevice::Bind(std::move(dependency_injection_device)));
  EXPECT_TRUE(device);
  auto client_end = ddk.FidlClient<fuchsia_gpu_magma::DependencyInjection>();
  EXPECT_EQ(ZX_OK, loop.StartThread("memory-pressure-thread"));

  auto client = fidl::BindSyncClient(std::move(client_end));

  fidl::InterfaceHandle<fuchsia::memorypressure::Provider> provider_handle;
  auto request = provider_handle.NewRequest();
  async::PostTask(loop.dispatcher(), [request = std::move(request), &provider]() mutable {
    provider.binding_set().AddBinding(&provider, std::move(request));
  });
  client.SetMemoryPressureProvider(
      fidl::ClientEnd<fuchsia_memorypressure::Provider>(provider_handle.TakeChannel()));

  sync_completion_wait(&owner.completion(), ZX_TIME_INFINITE);
  EXPECT_EQ(owner.level(), MAGMA_MEMORY_PRESSURE_LEVEL_CRITICAL);

  device->DdkAsyncRemove();
  EXPECT_TRUE(ddk.Ok());

  device->DdkRelease();

  // Ensure loop shutdown happens before |provider| is torn down.
  loop.Shutdown();
}
}  // namespace
