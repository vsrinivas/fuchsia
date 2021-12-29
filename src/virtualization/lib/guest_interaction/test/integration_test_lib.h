// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_VIRTUALIZATION_LIB_GUEST_INTERACTION_TEST_INTEGRATION_TEST_LIB_H_
#define SRC_VIRTUALIZATION_LIB_GUEST_INTERACTION_TEST_INTEGRATION_TEST_LIB_H_

#include <fuchsia/virtualization/cpp/fidl.h>
#include <lib/sys/cpp/testing/test_with_environment_fixture.h>

#include <src/virtualization/tests/fake_netstack.h>

static constexpr char kGuestLabel[] = "debian_guest";
static constexpr char kGuestDiscoveryUrl[] =
    "fuchsia-pkg://fuchsia.com/guest_discovery_service#meta/guest_discovery_service.cmx";

// The host will copy kTestScriptSource to kGuestScriptDestination on the
// guest.  The host will then ask the guest to exec kGuestScriptDestination
// and feed kTestScriptInput to the guest process's stdin.  The script will
// will echo kTestStdout to stdout, kTestStderr to stderr, and kTestScriptInput
// to  kGuestFileOutputLocation.  The host will
// download the file to kHostOuputCopyLocation.
static constexpr char kTestScriptSource[] = "/pkg/data/test_script.sh";
static constexpr char kGuestScriptDestination[] = "/root/input/test_script.sh";
static constexpr char kTestStdout[] = "stdout";
static constexpr char kTestStderr[] = "stderr";
static constexpr char kTestScriptInput[] = "hello world\n";
static constexpr char kGuestFileOutputLocation[] = "/root/output/script_output.txt";
static constexpr char kHostOuputCopyLocation[] = "/tmp/copy";

class GuestInteractionTest : public gtest::TestWithEnvironmentFixture {
 protected:
  GuestInteractionTest();
  void SetUp() override;

  uint32_t cid() const { return cid_.value(); }
  const fuchsia::virtualization::RealmPtr& realm() const { return realm_; }
  sys::testing::EnvironmentServices& services() { return *services_; }
  sys::testing::EnclosingEnvironment& env() { return *env_; }

 private:
  std::optional<uint32_t> cid_;
  fuchsia::virtualization::RealmPtr realm_;
  std::optional<zx_status_t> realm_error_;
  std::unique_ptr<sys::testing::EnvironmentServices> services_ = CreateServices();
  std::unique_ptr<sys::testing::EnclosingEnvironment> env_;
  FakeNetstack fake_netstack_;
};

#endif  // SRC_VIRTUALIZATION_LIB_GUEST_INTERACTION_TEST_INTEGRATION_TEST_LIB_H_
