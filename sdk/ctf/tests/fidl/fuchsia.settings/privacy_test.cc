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

class PrivacyTest : public zxtest::Test {
 protected:
  void SetUp() override { EXPECT_EQ(ZX_OK, service->Connect(privacy.NewRequest())); }

  void TearDown() override {
    fuchsia::settings::PrivacySettings settings;
    settings.set_user_data_sharing_consent(GetInitValue());
    fuchsia::settings::Privacy_Set_Result result;
    fuchsia::settings::PrivacySettings got_settings;
    // Set back to the initial settings and verify.
    EXPECT_EQ(ZX_OK, privacy->Set(std::move(settings), &result));
    EXPECT_EQ(ZX_OK, privacy->Watch(&got_settings));
    EXPECT_TRUE(got_settings.user_data_sharing_consent());
  }

  bool GetInitValue() const { return this->init_user_data_sharing_consent; }

  fuchsia::settings::PrivacySyncPtr privacy;

 private:
  std::shared_ptr<sys::ServiceDirectory> service = sys::ServiceDirectory::CreateFromNamespace();
  bool init_user_data_sharing_consent = true;
};

// Tests that Set() results in an update to privacy settings.
TEST_F(PrivacyTest, Set) {
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  ASSERT_EQ(loop.StartThread(), ZX_OK);

  // Setup initial PrivacySettings.
  fuchsia::settings::PrivacySettings settings;
  fuchsia::settings::Privacy_Set_Result result;
  settings.set_user_data_sharing_consent(GetInitValue());
  EXPECT_EQ(ZX_OK, privacy->Set(std::move(settings), &result));

  // Verify initial settings.
  fuchsia::settings::PrivacySettings got_settings;
  EXPECT_EQ(ZX_OK, privacy->Watch(&got_settings));
  EXPECT_TRUE(got_settings.user_data_sharing_consent());

  // Flip the settings.
  fuchsia::settings::PrivacySettings new_settings;
  new_settings.set_user_data_sharing_consent(false);
  EXPECT_EQ(ZX_OK, privacy->Set(std::move(new_settings), &result));

  // Verify the new settings.
  EXPECT_EQ(ZX_OK, privacy->Watch(&got_settings));
  EXPECT_FALSE(got_settings.user_data_sharing_consent());
}

}  // namespace
