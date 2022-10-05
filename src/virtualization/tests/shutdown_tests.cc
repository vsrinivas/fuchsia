// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/virtualization/tests/lib/guest_test.h"

namespace {

template <typename T>
class RestartableGuest : public T {
 public:
  explicit RestartableGuest(async::Loop& loop) : T(loop) {}

  zx_status_t BuildLaunchInfo(GuestLaunchInfo* launch_info) override {
    zx_status_t status = T::BuildLaunchInfo(launch_info);
    if (status != ZX_OK) {
      return status;
    }

    // TODO(fxbug.dev/106692): Remove once Debian flakes are resolved.
    if (std::is_same<T, DebianEnclosedGuest>::value) {
      launch_info->config.set_virtio_gpu(false);
    }

    return ZX_OK;
  }
};

// TODO(fxbug.dev/104989): Add Termina once its guest manager doesn't need to be destroyed.
using RestartableGuests =
    ::testing::Types<RestartableGuest<DebianEnclosedGuest>, RestartableGuest<ZirconEnclosedGuest>>;

template <class T>
using RestartableGuestTest = GuestTest<T>;
TYPED_TEST_SUITE(RestartableGuestTest, RestartableGuests, GuestTestNameGenerator);

TYPED_TEST(RestartableGuestTest, ForceRestartGuestInRealm) {
  GuestLaunchInfo guest_launch_info;
  ASSERT_EQ(this->GetEnclosedGuest().BuildLaunchInfo(&guest_launch_info), ZX_OK);

  // Restarting the guest without destroying the realm that the guest manager was launched into
  // validates that the out of process devices were correctly cleaned up upon guest termination.
  ASSERT_EQ(ZX_OK, this->GetEnclosedGuest().ForceRestart(guest_launch_info,
                                                         zx::deadline_after(zx::min(5))));
}

}  // namespace
