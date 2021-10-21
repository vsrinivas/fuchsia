// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/settings/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/sys/cpp/service_directory.h>

#include <zxtest/zxtest.h>

namespace {

class FactoryResetTest : public zxtest::Test {};

// Tests that Set() results in an update to factory reset settings.
TEST_F(FactoryResetTest, Set) {
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  ASSERT_EQ(loop.StartThread(), ZX_OK);

  fuchsia::settings::FactoryResetSyncPtr factory_reset;
  auto service = sys::ServiceDirectory::CreateFromNamespace();
  EXPECT_EQ(ZX_OK, service->Connect(factory_reset.NewRequest()));

  // Setup initial FactoryResetSettings.
  fuchsia::settings::FactoryResetSettings settings;
  fuchsia::settings::FactoryReset_Set_Result result;
  settings.set_is_local_reset_allowed(true);
  EXPECT_EQ(ZX_OK, factory_reset->Set(std::move(settings), &result));

  // Verify initial settings.
  fuchsia::settings::FactoryResetSettings got_settings;
  EXPECT_EQ(ZX_OK, factory_reset->Watch(&got_settings));
  EXPECT_TRUE(got_settings.is_local_reset_allowed());

  // Flip the settings.
  fuchsia::settings::FactoryResetSettings new_settings;
  new_settings.set_is_local_reset_allowed(false);
  EXPECT_EQ(ZX_OK, factory_reset->Set(std::move(new_settings), &result));

  // Verify the new settings.
  EXPECT_EQ(ZX_OK, factory_reset->Watch(&got_settings));
  EXPECT_FALSE(got_settings.is_local_reset_allowed());

  // Set back to the initial settings and verify.
  EXPECT_EQ(ZX_OK, factory_reset->Set(std::move(settings), &result));
  EXPECT_EQ(ZX_OK, factory_reset->Watch(&got_settings));
  EXPECT_TRUE(got_settings.is_local_reset_allowed());
}

}  // namespace
