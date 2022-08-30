// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_VIRTUALIZATION_LIB_GUEST_INTERACTION_TEST_INTEGRATION_TEST_LIB_H_
#define SRC_VIRTUALIZATION_LIB_GUEST_INTERACTION_TEST_INTEGRATION_TEST_LIB_H_

#include <fuchsia/virtualization/cpp/fidl.h>

#include <src/lib/testing/loop_fixture/real_loop_fixture.h>
#include <src/virtualization/tests/fake_netstack.h>

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

class GuestInteractionTest : public gtest::RealLoopFixture {
 protected:
  void SetUp() override;

  void GetHostVsockEndpoint(
      ::fidl::InterfaceRequest<::fuchsia::virtualization::HostVsockEndpoint> endpoint);

 private:
  std::unique_ptr<component_testing::RealmRoot> realm_root_;
  fuchsia::virtualization::DebianGuestManagerSyncPtr guest_manager_;
  fuchsia::virtualization::GuestPtr guest_;
  FakeNetstack fake_netstack_;
};

#endif  // SRC_VIRTUALIZATION_LIB_GUEST_INTERACTION_TEST_INTEGRATION_TEST_LIB_H_
