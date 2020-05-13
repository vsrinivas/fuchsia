// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fdio/directory.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/zx/channel.h>

#include "garnet/bin/trace_manager/tests/trace_manager_test.h"

namespace tracing {
namespace test {

namespace provider = ::fuchsia::tracing::provider;

namespace {
const char kProviderRegistryPath[] = "svc/fuchsia.tracing.provider.Registry";
}  // namespace

// Trace providers use fdio so test it.
TEST_F(TraceManagerTest, RegisterProviderWithFdio) {
  zx::channel h1, h2;
  ASSERT_EQ(zx::channel::create(0, &h1, &h2), ZX_OK);
  ASSERT_EQ(fdio_service_connect_at(context_provider().outgoing_directory_ptr().channel().get(),
                                    kProviderRegistryPath, h2.release()),
            ZX_OK);
  fidl::InterfaceHandle<provider::Registry> registry{std::move(h1)};
  fidl::InterfacePtr<provider::Registry> registry_ptr = registry.Bind();

  zx::channel ph1a, ph1b;
  ASSERT_EQ(zx::channel::create(0, &ph1a, &ph1b), ZX_OK);
  fidl::InterfaceRequest<provider::Provider> provider1{std::move(ph1a)};
  fidl::InterfaceHandle<provider::Provider> provider1_h{std::move(ph1b)};
  registry_ptr->RegisterProvider(std::move(provider1_h), kProvider1Pid, kProvider1Name);

  zx::channel ph2a, ph2b;
  ASSERT_EQ(zx::channel::create(0, &ph2a, &ph2b), ZX_OK);
  fidl::InterfaceRequest<provider::Provider> provider2{std::move(ph2a)};
  fidl::InterfaceHandle<provider::Provider> provider2_h{std::move(ph2b)};
  registry_ptr->RegisterProvider(std::move(provider2_h), kProvider2Pid, kProvider2Name);

  // Provider registrations come in on a different channel than
  // |GetProviders()|. Make sure the providers are registered before we try
  // to fetch a list of them.
  RunLoopUntilIdle();

  FX_VLOGS(2) << "Providers registered";

  ConnectToControllerService();
  std::vector<controller::ProviderInfo> providers;
  controller()->GetProviders([&providers](std::vector<controller::ProviderInfo> in_providers) {
    providers = std::move(in_providers);
  });
  RunLoopUntilIdle();

  EXPECT_EQ(providers.size(), 2u);
  for (const auto& p : providers) {
    EXPECT_TRUE(p.has_id());
    EXPECT_TRUE(p.has_pid());
    EXPECT_TRUE(p.has_name());
    if (p.has_pid()) {
      switch (p.pid()) {
        case kProvider1Pid:
          EXPECT_STREQ(p.name().c_str(), kProvider1Name);
          break;
        case kProvider2Pid:
          EXPECT_STREQ(p.name().c_str(), kProvider2Name);
          break;
        default:
          EXPECT_TRUE(false) << "Unexpected provider id";
          break;
      }
    }
  }
}

TEST_F(TraceManagerTest, AddFakeProviders) {
  ConnectToControllerService();

  FakeProvider* provider1;
  ASSERT_TRUE(AddFakeProvider(kProvider1Pid, kProvider1Name, &provider1));
  EXPECT_EQ(fake_provider_bindings().size(), 1u);

  FakeProvider* provider2;
  ASSERT_TRUE(AddFakeProvider(kProvider2Pid, kProvider2Name, &provider2));
  EXPECT_EQ(fake_provider_bindings().size(), 2u);

  // Provider registrations come in on a different channel than
  // |GetProviders()|. Make sure the providers are registered before we try
  // to fetch a list of them.
  RunLoopUntilIdle();

  FX_VLOGS(2) << "Providers registered";

  std::vector<controller::ProviderInfo> providers;
  controller()->GetProviders([&providers](std::vector<controller::ProviderInfo> in_providers) {
    providers = std::move(in_providers);
  });
  RunLoopUntilIdle();

  EXPECT_EQ(providers.size(), 2u);
  for (const auto& p : providers) {
    EXPECT_TRUE(p.has_id());
    EXPECT_TRUE(p.has_pid());
    EXPECT_TRUE(p.has_name());
    if (p.has_pid()) {
      switch (p.pid()) {
        case kProvider1Pid:
          EXPECT_STREQ(p.name().c_str(), kProvider1Name);
          break;
        case kProvider2Pid:
          EXPECT_STREQ(p.name().c_str(), kProvider2Name);
          break;
        default:
          EXPECT_TRUE(false) << "Unexpected provider id";
          break;
      }
    }
  }
}

}  // namespace test
}  // namespace tracing
