// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/modular_test_harness/cpp/test_harness_fixture.h>

namespace modular {
namespace testing {
namespace {

const char kTestHarnessUrl[] =
    "fuchsia-pkg://fuchsia.com/modular_test_harness#meta/"
    "modular_test_harness.cmx";

}  // namespace

TestHarnessFixture::TestHarnessFixture() {
  fuchsia::sys::LaunchInfo launch_info;
  launch_info.url = kTestHarnessUrl;
  svc_ =
      sys::ServiceDirectory::CreateWithRequest(&launch_info.directory_request);
  launcher_ptr()->CreateComponent(std::move(launch_info),
                                  test_harness_ctrl_.NewRequest());

  test_harness_ = svc_->Connect<fuchsia::modular::testing::TestHarness>();
}

TestHarnessFixture::~TestHarnessFixture() = default;

}  // namespace testing
}  // namespace modular
