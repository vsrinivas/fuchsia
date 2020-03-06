// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/scheduler/c/fidl.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/fd.h>
#include <lib/fdio/fdio.h>
#include <lib/fdio/io.h>
#include <lib/zx/channel.h>
#include <lib/zx/object.h>
#include <lib/zx/profile.h>
#include <zircon/syscalls.h>

#include <zxtest/zxtest.h>

namespace {

zx_status_t CreateProfile(const zx::channel& profile_provider, uint32_t priority,
                          const std::string& name, zx_status_t* server_status,
                          zx::profile* profile) {
  zx_handle_t raw_profile_handle;
  zx_status_t result = fuchsia_scheduler_ProfileProviderGetProfile(
      profile_provider.get(), priority, name.c_str(), name.length(), server_status,
      &raw_profile_handle);
  if (result != ZX_OK) {
    return ZX_OK;
  }
  *profile = zx::profile(raw_profile_handle);
  return ZX_OK;
}

zx_status_t CreateDeadlineProfile(const zx::channel& profile_provider, zx_duration_t capacity,
                                  zx_duration_t relative_deadline, zx_duration_t period,
                                  const std::string& name, zx_status_t* server_status,
                                  zx::profile* profile) {
  zx_handle_t raw_profile_handle;
  zx_status_t result = fuchsia_scheduler_ProfileProviderGetDeadlineProfile(
      profile_provider.get(), capacity, relative_deadline, period, name.c_str(), name.length(),
      server_status, &raw_profile_handle);
  if (result != ZX_OK) {
    return ZX_OK;
  }
  *profile = zx::profile(raw_profile_handle);
  return ZX_OK;
}

TEST(Profile, CreateDestroy) {
  // Connect to ProfileProvider.
  zx::channel channel1, channel2;
  ASSERT_OK(zx::channel::create(/*flags=*/0u, &channel1, &channel2));
  ASSERT_OK(fdio_service_connect("/svc/" fuchsia_scheduler_ProfileProvider_Name, channel1.release()),
            "Could not connect to ProfileProvider.");
  ASSERT_EQ(channel1.get(), ZX_HANDLE_INVALID);

  // Create available profile types.
  zx::profile profile;
  zx_status_t status;
  ASSERT_OK(CreateProfile(channel2, /*priority=*/0u, "<test>", &status, &profile),
            "Error creating profile.");

  zx::profile deadline_profile;
  ASSERT_OK(
      CreateDeadlineProfile(channel2, /*capacity=*/ZX_MSEC(2), /*relative_deadline=*/ZX_MSEC(10),
                            /*period=*/ZX_MSEC(10), "<test>", &status, &deadline_profile),
      "Error creating deadline profile.");

  // Ensure basic details are correct.
  zx_info_handle_basic_t info;
  ASSERT_OK(profile.get_info(ZX_INFO_HANDLE_BASIC, &info, sizeof(info), nullptr, nullptr),
            "object_get_info for profile");
  EXPECT_NE(info.koid, 0, "no koid for profile");
  EXPECT_EQ(info.type, ZX_OBJ_TYPE_PROFILE, "incorrect type for profile");

  ASSERT_OK(deadline_profile.get_info(ZX_INFO_HANDLE_BASIC, &info, sizeof(info), nullptr, nullptr),
            "object_get_info for deadline profile");
  EXPECT_NE(info.koid, 0, "no koid for deadline profile");
  EXPECT_EQ(info.type, ZX_OBJ_TYPE_PROFILE, "incorrect type for deadline profile");
}

}  // namespace
