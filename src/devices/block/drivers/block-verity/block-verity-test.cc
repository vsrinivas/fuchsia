// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/device/llcpp/fidl.h>
#include <fuchsia/hardware/block/verified/llcpp/fidl.h>
#include <fuchsia/io/llcpp/fidl.h>
#include <lib/devmgr-integration-test/fixture.h>
#include <lib/devmgr-launcher/launch.h>
#include <lib/driver-integration-test/fixture.h>
#include <lib/fdio/directory.h>
#include <lib/zx/channel.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <cstdint>
#include <utility>

#include <fvm/test/device-ref.h>
#include <ramdevice-client/ramdisk.h>
#include <zxtest/zxtest.h>

namespace {

constexpr uint64_t kBlockSize = 4096;
constexpr uint64_t kBlockCount = 8192;

using driver_integration_test::IsolatedDevmgr;

const char* kDriverLib = "/pkg/driver/block-verity.so";

class BlockVerityTest : public zxtest::Test {
 public:
  BlockVerityTest() {}
  void SetUp() override {
    IsolatedDevmgr::Args args;
    zx::channel pkg_client, pkg_server;
    // Export /pkg to the isolated devmgr as /pkg
    ASSERT_OK(zx::channel::create(0, &pkg_client, &pkg_server));
    ASSERT_OK(fdio_open(
        "/pkg", llcpp::fuchsia::io::OPEN_RIGHT_READABLE | llcpp::fuchsia::io::OPEN_RIGHT_EXECUTABLE,
        pkg_server.release()));
    args.flat_namespace.push_back(std::make_pair("/pkg", std::move(pkg_client)));
    args.driver_search_paths.push_back("/pkg/driver");
    args.driver_search_paths.push_back("/boot/driver");
    args.disable_block_watcher = true;
    args.disable_netsvc = true;
    ASSERT_OK(driver_integration_test::IsolatedDevmgr::Create(&args, &devmgr_));
    // Create ramdisk.
    ramdisk_ = fvm::RamdiskRef::Create(devmgr_.devfs_root(), kBlockSize, kBlockCount);
    ASSERT_TRUE(ramdisk_);
  }

 protected:
  IsolatedDevmgr devmgr_;
  std::unique_ptr<fvm::RamdiskRef> ramdisk_;

 private:
};

// Bind the block verity driver to the ramdisk.
zx_status_t BindVerityDriver(zx::unowned_channel ramdisk_chan) {
  zx_status_t rc;
  auto resp = ::llcpp::fuchsia::device::Controller::Call::Bind(
      std::move(ramdisk_chan), ::fidl::unowned_str(kDriverLib, strlen(kDriverLib)));
  rc = resp.status();
  if (rc == ZX_OK) {
    if (resp->result.is_err()) {
      rc = resp->result.err();
    }
  }
  return rc;
}

class BlockVerityMutableTest : public BlockVerityTest {
 public:
  BlockVerityMutableTest() {}
  void SetUp() override {
    // Set up the IsolatedDevmgr and ramdisk as before.
    BlockVerityTest::SetUp();

    // bind the driver to the ramdisk
    ASSERT_OK(BindVerityDriver(zx::unowned_channel(ramdisk_->channel())));

    // wait for block-verity device to appear and open it
    std::string verity_path = std::string(ramdisk_->path()) + "/verity";
    std::string mutable_path = verity_path + "/mutable";
    std::string mutable_block_path = mutable_path + "/block";
    ASSERT_OK(devmgr_integration_test::RecursiveWaitForFile(devmgr_.devfs_root(),
                                                            verity_path.c_str(), &verity_fd_));

    // claim channel from fd
    ASSERT_OK(fdio_get_service_handle(verity_fd_.release(), verity_chan_.reset_and_get_address()));

    // make FIDL call to open in authoring mode
    fidl::aligned<::llcpp::fuchsia::hardware::block::verified::HashFunction> hash_function =
        ::llcpp::fuchsia::hardware::block::verified::HashFunction::SHA256;
    fidl::aligned<::llcpp::fuchsia::hardware::block::verified::BlockSize> block_size =
        ::llcpp::fuchsia::hardware::block::verified::BlockSize::SIZE_4096;
    auto config =
        ::llcpp::fuchsia::hardware::block::verified::Config::Builder(
            std::make_unique<::llcpp::fuchsia::hardware::block::verified::Config::Frame>())
            .set_hash_function(fidl::unowned_ptr(&hash_function))
            .set_block_size(fidl::unowned_ptr(&block_size))
            .build();

    // Request the device be opened for writes
    auto open_resp = ::llcpp::fuchsia::hardware::block::verified::DeviceManager::Call::OpenForWrite(
        zx::unowned_channel(verity_chan_), std::move(config));
    ASSERT_OK(open_resp.status());
    ASSERT_FALSE(open_resp->result.is_err());

    // Wait for the `mutable` device to appear
    ASSERT_OK(devmgr_integration_test::RecursiveWaitForFile(devmgr_.devfs_root(),
                                                            mutable_path.c_str(), &mutable_fd_));

    // And then wait for the `block` driver to bind to that device too
    ASSERT_OK(devmgr_integration_test::RecursiveWaitForFile(
        devmgr_.devfs_root(), mutable_block_path.c_str(), &mutable_block_fd_));
  }

  void TearDown() override {
    // Close the device cleanly
    auto close_resp = ::llcpp::fuchsia::hardware::block::verified::DeviceManager::Call::Close(
        zx::unowned_channel(verity_chan_));
    ASSERT_OK(close_resp.status());
    ASSERT_FALSE(close_resp->result.is_err());
  }

 protected:
  fbl::unique_fd verity_fd_;
  zx::channel verity_chan_;
  fbl::unique_fd mutable_fd_;
  fbl::unique_fd mutable_block_fd_;
};

TEST_F(BlockVerityTest, Bind) { ASSERT_OK(BindVerityDriver(ramdisk_->channel())); }

TEST_F(BlockVerityMutableTest, OpenForWriting) {
  // Zero out the underlying ramdisk.
  fbl::Array<uint8_t> write_buf(new uint8_t[kBlockSize], kBlockSize);
  memset(write_buf.get(), 0, write_buf.size());
  ASSERT_EQ(lseek(ramdisk_->fd(), 0, SEEK_SET), 0);
  for (uint64_t block = 0; block < kBlockCount; block++) {
    ASSERT_EQ(write(ramdisk_->fd(), write_buf.get(), write_buf.size()), kBlockSize);
  }

  // Examine the size of the child device.  Expect it to be 8126 blocks, because
  // we've reserved 1 superblock and 65 integrity blocks of our 8192-block device.
  struct stat st;
  ASSERT_EQ(fstat(mutable_block_fd_.get(), &st), 0);
  ASSERT_EQ(st.st_size, 8126 * kBlockSize);
  uint64_t inner_block_count = st.st_size / kBlockSize;

  // Read the entire inner block device.  Expect to see all zeroes.
  fbl::Array<uint8_t> zero_buf(new uint8_t[kBlockSize], kBlockSize);
  memset(zero_buf.get(), 0, zero_buf.size());
  fbl::Array<uint8_t> read_buf(new uint8_t[kBlockSize], kBlockSize);
  memset(read_buf.get(), 0, read_buf.size());
  for (uint64_t block = 0; block < inner_block_count; block++) {
    // Seek to start of block
    off_t offset = block * kBlockSize;
    ASSERT_EQ(lseek(mutable_block_fd_.get(), offset, SEEK_SET), offset);
    // Verify read succeeds
    ASSERT_EQ(read(mutable_block_fd_.get(), read_buf.get(), read_buf.size()), kBlockSize);
    // Expect to read all zeroes.
    ASSERT_EQ(memcmp(zero_buf.get(), read_buf.get(), zero_buf.size()), 0);
  }

  // Make a pattern in the write buffer.
  for (size_t i = 0; i < kBlockSize; i++) {
    write_buf[i] = static_cast<uint8_t>(i % 256);
  }

  // Write the first block on the mutable device with that pattern.
  ASSERT_EQ(lseek(mutable_block_fd_.get(), 0, SEEK_SET), 0);
  ASSERT_EQ(write(mutable_block_fd_.get(), write_buf.get(), write_buf.size()), kBlockSize);

  // Read it back.
  ASSERT_EQ(lseek(mutable_block_fd_.get(), 0, SEEK_SET), 0);
  ASSERT_EQ(read(mutable_block_fd_.get(), read_buf.get(), read_buf.size()), kBlockSize);
  ASSERT_EQ(memcmp(write_buf.get(), read_buf.get(), read_buf.size()), 0);

  // Find a block that matches from the underlying device.
  bool found = false;
  for (uint64_t block = 0; block < kBlockCount; block++) {
    // Seek to start of block
    off_t offset = block * kBlockSize;
    ASSERT_EQ(lseek(ramdisk_->fd(), offset, SEEK_SET), offset);
    ASSERT_EQ(read(ramdisk_->fd(), read_buf.get(), read_buf.size()), kBlockSize);
    if (memcmp(read_buf.get(), write_buf.get(), read_buf.size()) == 0) {
      found = true;
      // Expect to find the block at block 66 (after one superblock & 65 integrity blocks)
      ASSERT_EQ(block, 66);
      break;
    }
  }
  ASSERT_TRUE(found);
}

}  // namespace
