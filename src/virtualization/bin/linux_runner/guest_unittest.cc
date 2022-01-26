// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/virtualization/bin/linux_runner/guest.h"

#include <errno.h>
#include <fcntl.h>
#include <fuchsia/virtualization/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fdio/namespace.h>
#include <lib/gtest/test_loop_fixture.h>
#include <lib/sync/completion.h>
#include <lib/sys/cpp/testing/component_context_provider.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/virtualization/testing/fake_manager.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <unordered_map>

#include "src/storage/memfs/scoped_memfs.h"
#include "src/virtualization/bin/linux_runner/ports.h"

namespace linux_runner {
namespace {

// Use a small image here since we won't actually put any data on it; we just
// want to verify we can correctly create the image.
static constexpr off_t kStatefulImageSizeForTest = 10ul * 1024 * 1024;
static constexpr const char* kStatefulImagePath = "/data/stateful.img";
static constexpr std::string_view kEnvironmentLabelForTest("test");

// Disabled due to flakes, see: https://bugs.fuchsia.dev/p/fuchsia/issues/detail?id=69299
class DISABLED_LinuxRunnerGuestTest : public gtest::TestLoopFixture {
 public:
  void SetUp() override {
    TestLoopFixture::SetUp();

    // Install memfs on a different async loop thread to resolve some deadlock when doing blocking
    // file operations on our test loop.
    memfs_loop_.StartThread();
    zx::status<ScopedMemfs> memfs = ScopedMemfs::CreateMountedAt(memfs_loop_.dispatcher(), "/data");
    FX_CHECK(memfs.is_ok());
    data_ = std::make_unique<ScopedMemfs>(std::move(*memfs));

    // Add a fake guest Manager to the components context.
    provider_.service_directory_provider()->AddService(fake_guest_manager_.GetHandler());
  }

  void TearDown() override {
    data_.reset();
    memfs_loop_.Shutdown();

    TestLoopFixture::TearDown();
  }

 protected:
  void StartGuest() {
    GuestConfig config = {
        .env_label = kEnvironmentLabelForTest,
        .stateful_image_size = kStatefulImageSizeForTest,
    };
    Guest::CreateAndStart(
        provider_.context(), config, [](GuestInfo) {}, &guest_);
    RunLoopUntilIdle();
  }

  guest::testing::FakeManager* guest_manager() { return &fake_guest_manager_; }

 private:
  guest::testing::FakeManager fake_guest_manager_;
  std::unique_ptr<Guest> guest_;
  sys::testing::ComponentContextProvider provider_;
  async::Loop memfs_loop_{&kAsyncLoopConfigNoAttachToCurrentThread};
  std::unique_ptr<ScopedMemfs> data_;
};

TEST_F(DISABLED_LinuxRunnerGuestTest, ConnectToStartupListener) {
  StartGuest();
  zx::handle handle;
  zx_status_t status = guest_manager()->GuestVsock()->ConnectToHost(
      kStartupListenerPort, [&handle](zx::handle h) { handle = std::move(h); });
  ASSERT_EQ(ZX_OK, status) << "linux_runner is not listening on StartupListener port";
  RunLoopUntilIdle();

  // We've estabished a VSOCK connection to the host. This is how the guest
  // signals boot completed.
  ASSERT_TRUE(handle) << "Unable to connect to StartupListener";
}

// If a stateful image partition does not exist on device; one shall be created
// as part of the guest creation.
TEST_F(DISABLED_LinuxRunnerGuestTest, CreateEmptyStatefulPartition) {
  // Verify no image exists.
  struct stat st = {};
  ASSERT_EQ(-1, stat(kStatefulImagePath, &st)) << "Stateful image already exists";
  ASSERT_EQ(ENOENT, errno);

  StartGuest();

  // Verfify an image file has been created with the expected size:
  ASSERT_EQ(0, stat(kStatefulImagePath, &st)) << "Stateful was not created";
  ASSERT_TRUE(S_ISREG(st.st_mode));
  ASSERT_EQ(st.st_size, kStatefulImageSizeForTest);
}

// TODO(fxbug.dev/40751): With ShadowCallStack enabled and SafeStack disabled, we can
// trigger a segfault in `ReuseExistingStatefulParition` all the way in
// pthread_mutex_lock. We believe the underlying cause of this is some race
// condition internal to gRPC. The segfault seems nondeterministic in that there
// are many ways to hide the segfault, including:
// - Disabling at least one of the other tests
// - Avoid reading the `handle` at the end of `ConnectToStartupListener`
// - Adding a log right after declaring the handle in
//   `ConnectToStartupListener`
// - Moving the SetUp and TearDown logic to the start and end of each test
//   function
// - Probably others to be discovered...
TEST_F(DISABLED_LinuxRunnerGuestTest, ReuseExistingStatefulParition) {
  // Use a different size here to verify we don't go though the partition create
  // logic, which will create a full-size image.
  static constexpr auto image_size = 1024;
  int fd = open(kStatefulImagePath, O_RDWR | O_CREAT);
  ASSERT_GE(fd, 0);

  // Write some data do the disk image;
  uint8_t expected[image_size];
  for (size_t i = 0; i < image_size; ++i) {
    expected[i] = static_cast<uint8_t>(i & UINT8_MAX);
  }
  ASSERT_EQ(write(fd, expected, sizeof(expected)), static_cast<int>(sizeof(expected)))
      << "Failed to write test data to disk image";
  close(fd);

  StartGuest();

  // Read disk back out and verify it has not been changed.
  fd = open(kStatefulImagePath, O_RDONLY);
  ASSERT_GE(fd, 0) << "Stateful has been deleted";

  uint8_t actual[image_size];
  static_assert(sizeof(actual) == sizeof(expected));
  ASSERT_EQ(read(fd, actual, sizeof(actual)), static_cast<int>(sizeof(actual)))
      << "Failed to read back disk image";
  ASSERT_EQ(0, memcmp(actual, expected, sizeof(actual))) << "Disk image has changed";
}

}  // namespace
}  // namespace linux_runner
