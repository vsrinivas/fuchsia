// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.scheduler/cpp/wire.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/fd.h>
#include <lib/fdio/fdio.h>
#include <lib/fdio/io.h>
#include <lib/zx/channel.h>
#include <lib/zx/object.h>
#include <lib/zx/profile.h>
#include <lib/zx/thread.h>
#include <threads.h>
#include <zircon/errors.h>
#include <zircon/syscalls.h>

#include <string>

#include <zxtest/zxtest.h>

namespace {

void GetProfileProvider(fidl::ClientEnd<fuchsia_scheduler::ProfileProvider>* provider) {
  // Connect to ProfileProvider.
  auto endpoints = fidl::CreateEndpoints<fuchsia_scheduler::ProfileProvider>();
  ASSERT_OK(endpoints.status_value());
  ASSERT_OK(fdio_service_connect_by_name(
                fidl::DiscoverableProtocolName<fuchsia_scheduler::ProfileProvider>,
                endpoints->server.channel().release()),
            "Could not connect to ProfileProvider.");
  *provider = std::move(endpoints->client);
}

void CheckBasicDetails(const zx::profile& profile) {
  // Ensure basic details are correct.
  zx_info_handle_basic_t info;
  ASSERT_OK(profile.get_info(ZX_INFO_HANDLE_BASIC, &info, sizeof(info), nullptr, nullptr),
            "object_get_info for profile");
  EXPECT_NE(info.koid, 0, "no koid for profile");
  EXPECT_EQ(info.type, ZX_OBJ_TYPE_PROFILE, "incorrect type for profile");
}

// Test getting a profile via the GetProfile FIDL method.
TEST(Profile, GetProfile) {
  fidl::ClientEnd<fuchsia_scheduler::ProfileProvider> provider;
  GetProfileProvider(&provider);
  ASSERT_FALSE(CURRENT_TEST_HAS_FAILURES());

  auto result = fidl::WireCall(provider)->GetProfile(/*priority=*/0u, "<test>");
  ASSERT_OK(result.status());
  ASSERT_OK(result.value().status);

  CheckBasicDetails(result.value().profile);
}

// Test getting a profile via the GetDeadlineProfile FIDL method.
TEST(Profile, GetDeadlineProfile) {
  fidl::ClientEnd<fuchsia_scheduler::ProfileProvider> provider;
  GetProfileProvider(&provider);
  ASSERT_FALSE(CURRENT_TEST_HAS_FAILURES());

  auto result = fidl::WireCall(provider)->GetDeadlineProfile(/*capacity=*/ZX_MSEC(2),
                                                             /*relative_deadline=*/ZX_MSEC(10),
                                                             /*period=*/ZX_MSEC(10), "<test>");
  ASSERT_OK(result.status());
  ASSERT_OK(result.value().status);

  CheckBasicDetails(result.value().profile);
}

// Test getting a profile via the GetCpuAffinityProfile FIDL method.
TEST(Profile, GetCpuAffinityProfile) {
  fidl::ClientEnd<fuchsia_scheduler::ProfileProvider> provider;
  GetProfileProvider(&provider);
  ASSERT_FALSE(CURRENT_TEST_HAS_FAILURES());

  fuchsia_scheduler::wire::CpuSet cpu_set = {};
  cpu_set.mask[0] = 1;
  auto result = fidl::WireCall(provider)->GetCpuAffinityProfile(cpu_set);
  ASSERT_OK(result.status());
  ASSERT_OK(result.value().status);

  CheckBasicDetails(result.value().profile);
}

// Test setting a profile via the SetProfileByRole FIDL method.
TEST(Profile, SetProfileByRole) {
  fidl::ClientEnd<fuchsia_scheduler::ProfileProvider> provider;
  GetProfileProvider(&provider);
  ASSERT_FALSE(CURRENT_TEST_HAS_FAILURES());

  const zx_rights_t kRights = ZX_RIGHT_TRANSFER | ZX_RIGHT_MANAGE_THREAD;
  zx::thread duplicate;

  {
    ASSERT_OK(zx::thread::self()->duplicate(kRights, &duplicate));
    auto result =
        fidl::WireCall(provider)->SetProfileByRole(std::move(duplicate), "fuchsia.test-role:ok");
    ASSERT_OK(result.status());
    ASSERT_OK(result.value().status);
  }

  {
    ASSERT_OK(zx::thread::self()->duplicate(kRights, &duplicate));
    auto result = fidl::WireCall(provider)->SetProfileByRole(std::move(duplicate),
                                                             "fuchsia.test-role:not-found");
    ASSERT_OK(result.status());
    ASSERT_EQ(ZX_ERR_NOT_FOUND, result.value().status);
  }
}

}  // namespace
