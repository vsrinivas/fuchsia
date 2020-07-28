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

const char* kDriverLib = "/boot/driver/block-verity.so";

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

class BlockVerityTest : public zxtest::Test {
 public:
  BlockVerityTest() {}
  void SetUp() override {
    IsolatedDevmgr::Args args;
    args.driver_search_paths.push_back("/boot/driver");
    args.path_prefix = "/pkg/";
    args.disable_block_watcher = true;
    args.disable_netsvc = true;
    ASSERT_OK(driver_integration_test::IsolatedDevmgr::Create(&args, &devmgr_));
    // Create ramdisk.
    ramdisk_ = fvm::RamdiskRef::Create(devmgr_.devfs_root(), kBlockSize, kBlockCount);
    ASSERT_TRUE(ramdisk_);
  }

 protected:
  void OpenForAuthoring(IsolatedDevmgr& devmgr, std::unique_ptr<fvm::RamdiskRef>& ramdisk,
                        fbl::unique_fd& verity_fd, zx::channel& verity_chan,
                        fbl::unique_fd& mutable_fd, fbl::unique_fd& mutable_block_fd) {
    // bind the driver to the ramdisk
    ASSERT_OK(BindVerityDriver(zx::unowned_channel(ramdisk->channel())));

    // wait for block-verity device to appear and open it
    std::string verity_path = std::string(ramdisk->path()) + "/verity";
    std::string mutable_path = verity_path + "/mutable";
    std::string mutable_block_path = mutable_path + "/block";
    ASSERT_OK(devmgr_integration_test::RecursiveWaitForFile(devmgr.devfs_root(),
                                                            verity_path.c_str(), &verity_fd));

    // claim channel from fd
    ASSERT_OK(fdio_get_service_handle(verity_fd.release(), verity_chan.reset_and_get_address()));

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
        zx::unowned_channel(verity_chan), std::move(config));
    ASSERT_OK(open_resp.status());
    ASSERT_FALSE(open_resp->result.is_err());

    // Wait for the `mutable` device to appear
    ASSERT_OK(devmgr_integration_test::RecursiveWaitForFile(devmgr.devfs_root(),
                                                            mutable_path.c_str(), &mutable_fd));

    // And then wait for the `block` driver to bind to that device too
    ASSERT_OK(devmgr_integration_test::RecursiveWaitForFile(
        devmgr.devfs_root(), mutable_block_path.c_str(), &mutable_block_fd));
  }

  void CloseAndGenerateSeal(
      zx::channel& verity_chan,
      ::llcpp::fuchsia::hardware::block::verified::DeviceManager_CloseAndGenerateSeal_Result* out) {
    // We use the caller-provided buffer FIDL call style because we need to keep
    // the response object alive so that the test code can interact with it
    // after this function returns.
    auto seal_resp =
        ::llcpp::fuchsia::hardware::block::verified::DeviceManager::Call::CloseAndGenerateSeal(
            zx::unowned_channel(verity_chan), seal_response_buffer_.view());
    ASSERT_OK(seal_resp.status());
    ASSERT_FALSE(seal_resp->result.is_err());
    *out = std::move(seal_resp->result);
  }

  void Close(zx::channel& verity_chan) {
    // Close the device cleanly
    auto close_resp = ::llcpp::fuchsia::hardware::block::verified::DeviceManager::Call::Close(
        zx::unowned_channel(verity_chan));
    ASSERT_OK(close_resp.status());
    ASSERT_FALSE(close_resp->result.is_err());
  }

  IsolatedDevmgr devmgr_;
  std::unique_ptr<fvm::RamdiskRef> ramdisk_;
  fidl::Buffer<
      llcpp::fuchsia::hardware::block::verified::DeviceManager::CloseAndGenerateSealResponse>
      seal_response_buffer_;

 private:
};

class BlockVerityMutableTest : public BlockVerityTest {
 protected:
  fbl::unique_fd verity_fd_;
  zx::channel verity_chan_;
  fbl::unique_fd mutable_fd_;
  fbl::unique_fd mutable_block_fd_;
};

TEST_F(BlockVerityTest, Bind) { ASSERT_OK(BindVerityDriver(ramdisk_->channel())); }

TEST_F(BlockVerityMutableTest, BasicWrites) {
  OpenForAuthoring(devmgr_, ramdisk_, verity_fd_, verity_chan_, mutable_fd_, mutable_block_fd_);
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

  // Close the device cleanly
  Close(verity_chan_);
}

TEST_F(BlockVerityMutableTest, BasicSeal) {
  // Zero out the underlying ramdisk.
  fbl::Array<uint8_t> write_buf(new uint8_t[kBlockSize], kBlockSize);
  memset(write_buf.get(), 0, write_buf.size());
  ASSERT_EQ(lseek(ramdisk_->fd(), 0, SEEK_SET), 0);
  for (uint64_t block = 0; block < kBlockCount; block++) {
    ASSERT_EQ(write(ramdisk_->fd(), write_buf.get(), write_buf.size()), kBlockSize);
  }

  // Open the device.
  OpenForAuthoring(devmgr_, ramdisk_, verity_fd_, verity_chan_, mutable_fd_, mutable_block_fd_);

  // Close and generate a seal over the all-zeroes data section.
  ::llcpp::fuchsia::hardware::block::verified::DeviceManager_CloseAndGenerateSeal_Result result;
  CloseAndGenerateSeal(verity_chan_, &result);
  ASSERT_TRUE(result.is_response());

  // Verify contents of the integrity section.  For our 8126 data blocks of all-zeros,
  // we expect to find:
  // * 63 blocks of <128 * hash of all-zeroes block>
  //   head -c 4096 /dev/zero | sha256sum -
  //   ad7facb2586fc6e966c004d7d1d16b024f5805ff7cb47c7a85dabd8b48892ca7  -
  //   So this block is 128 of those concatenated, which happens to hash to
  //   b24a5dfc7087b09c7378bb9100b5ea913f283da2c8ca05297f39457cbdd651d4.
  // * 1 block of <(8126 - (128*63)) = 62 copies of (hash of the empty block) + 2112 zero bytes of
  //   padding.  This happens to hash to:
  //   b3f0b10c454da8c746faf2a6f1dc89f29385ac56aed6e4b6ffb8fa3e9cee79ec
  // * 1 block with 63 hashes of the first type of block, 1 hash of the second
  //   type of block, and zeroes to pad
  //   As noted above, SHA256(The first type of block) =
  //     b24a5dfc7087b09c7378bb9100b5ea913f283da2c8ca05297f39457cbdd651d4
  //   As noted above, SHA256(The second type of block) =
  //     b3f0b10c454da8c746faf2a6f1dc89f29385ac56aed6e4b6ffb8fa3e9cee79ec
  //
  fbl::Array<uint8_t> zero_block(new uint8_t[kBlockSize], kBlockSize);
  memset(zero_block.get(), 0, kBlockSize);
  uint8_t zero_block_hash[32] = {0xad, 0x7f, 0xac, 0xb2, 0x58, 0x6f, 0xc6, 0xe9, 0x66, 0xc0, 0x04,
                                 0xd7, 0xd1, 0xd1, 0x6b, 0x02, 0x4f, 0x58, 0x05, 0xff, 0x7c, 0xb4,
                                 0x7c, 0x7a, 0x85, 0xda, 0xbd, 0x8b, 0x48, 0x89, 0x2c, 0xa7};

  fbl::Array<uint8_t> expected_early_tier_0_integrity_block(new uint8_t[kBlockSize], kBlockSize);
  for (size_t i = 0; i < 128; i++) {
    memcpy(expected_early_tier_0_integrity_block.get() + (32 * i), zero_block_hash, 32);
  }

  fbl::Array<uint8_t> expected_final_tier_0_integrity_block(new uint8_t[kBlockSize], kBlockSize);
  for (size_t i = 0; i < 62; i++) {
    memcpy(expected_final_tier_0_integrity_block.get() + (32 * i), zero_block_hash, 32);
  }
  memset(expected_final_tier_0_integrity_block.get() + (62 * 32), 0, kBlockSize - (62 * 32));

  uint8_t early_tier_0_integrity_block_hash[32] = {
      // Computed as follows:
      // python2
      // >>> import hashlib
      // >>> h = hashlib.sha256()
      // >>> hzero =
      // "ad7facb2586fc6e966c004d7d1d16b024f5805ff7cb47c7a85dabd8b48892ca7".decode("hex")
      // >>> h.update(hzero * 128)
      // >>> h.hexdigest()
      // "b24a5dfc7087b09c7378bb9100b5ea913f283da2c8ca05297f39457cbdd651d4"
      0xb2, 0x4a, 0x5d, 0xfc, 0x70, 0x87, 0xb0, 0x9c, 0x73, 0x78, 0xbb,
      0x91, 0x00, 0xb5, 0xea, 0x91, 0x3f, 0x28, 0x3d, 0xa2, 0xc8, 0xca,
      0x05, 0x29, 0x7f, 0x39, 0x45, 0x7c, 0xbd, 0xd6, 0x51, 0xd4};
  uint8_t final_tier_0_integrity_block_hash[32] = {
      // >>> import hashlib
      // >>> h = hashlib.sha256()
      // >>> hzero =
      // "ad7facb2586fc6e966c004d7d1d16b024f5805ff7cb47c7a85dabd8b48892ca7".decode("hex")
      // >>> h.update((hzero * 62) + ('\0' * 2112))
      // >>> h.hexdigest()
      // "b3f0b10c454da8c746faf2a6f1dc89f29385ac56aed6e4b6ffb8fa3e9cee79ec"
      0xb3, 0xf0, 0xb1, 0x0c, 0x45, 0x4d, 0xa8, 0xc7, 0x46, 0xfa, 0xf2,
      0xa6, 0xf1, 0xdc, 0x89, 0xf2, 0x93, 0x85, 0xac, 0x56, 0xae, 0xd6,
      0xe4, 0xb6, 0xff, 0xb8, 0xfa, 0x3e, 0x9c, 0xee, 0x79, 0xec};

  fbl::Array<uint8_t> expected_root_integrity_block(new uint8_t[kBlockSize], kBlockSize);
  for (size_t i = 0; i < 63; i++) {
    memcpy(expected_root_integrity_block.get() + (32 * i), early_tier_0_integrity_block_hash, 32);
  }
  memcpy(expected_root_integrity_block.get() + (32 * 63), final_tier_0_integrity_block_hash, 32);
  memset(expected_root_integrity_block.get() + (32 * 64), 0, kBlockSize - (32 * 64));

  fbl::Array<uint8_t> read_buf(new uint8_t[kBlockSize], kBlockSize);
  for (size_t integrity_block_index = 0; integrity_block_index < 65; integrity_block_index++) {
    size_t absolute_block_index = integrity_block_index + 1;
    off_t offset = absolute_block_index * kBlockSize;
    ASSERT_EQ(lseek(ramdisk_->fd(), offset, SEEK_SET), offset);
    ASSERT_EQ(read(ramdisk_->fd(), read_buf.get(), read_buf.size()), kBlockSize);
    uint8_t* expected_block;
    if (integrity_block_index < 63) {
      expected_block = expected_early_tier_0_integrity_block.get();
    } else if (integrity_block_index == 63) {
      expected_block = expected_final_tier_0_integrity_block.get();
    } else {
      expected_block = expected_root_integrity_block.get();
    }
    EXPECT_EQ(memcmp(expected_block, read_buf.get(), kBlockSize), 0,
              "integrity block %lu did not contain expected contents", integrity_block_index);
  }

  // Verify contents of the superblock.
  // Root integrity block hash is:
  // python2
  // >>> import hashlib
  // >>> early_t0_hash =
  // "b24a5dfc7087b09c7378bb9100b5ea913f283da2c8ca05297f39457cbdd651d4".decode('hex')
  // >>> late_t0_hash =
  // "b3f0b10c454da8c746faf2a6f1dc89f29385ac56aed6e4b6ffb8fa3e9cee79ec".decode('hex')
  // >>> root_integrity_block = early_t0_hash * 63 + late_t0_hash + ('\0' * 2048)
  // >>> h = hashlib.sha256()
  // >>> h.update(root_integrity_block)
  // >>> h.hexdigest()
  // "5b7ecbf17daa9832c2484342f924e5480157c3582fcfaedc63c83e20875800f2"
  uint8_t expected_superblock[kBlockSize] = {
      // Recall: the superblock format is:

      // * 16 bytes magic
      0x62, 0x6c, 0x6f, 0x63, 0x6b, 0x2d, 0x76, 0x65, 0x72, 0x69, 0x74, 0x79, 0x2d, 0x76, 0x31,
      0x00,

      // * 8 bytes block count (little-endian)
      0x00, 0x20, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,

      // * 4 bytes block size (little-endian)
      0x00, 0x10, 0x00, 0x00,

      // * 4 bytes hash function tag (little-endian)
      0x01, 0x00, 0x00, 0x00,

      // * 32 bytes integrity root hash (see computation above)
      0x5b, 0x7e, 0xcb, 0xf1, 0x7d, 0xaa, 0x98, 0x32, 0xc2, 0x48, 0x43, 0x42, 0xf9, 0x24, 0xe5,
      0x48, 0x01, 0x57, 0xc3, 0x58, 0x2f, 0xcf, 0xae, 0xdc, 0x63, 0xc8, 0x3e, 0x20, 0x87, 0x58,
      0x00, 0xf2,
      // * 4032 zero bytes padding the rest of the block, autofilled by compiler.
  };

  ASSERT_EQ(lseek(ramdisk_->fd(), 0, SEEK_SET), 0);
  ASSERT_EQ(read(ramdisk_->fd(), read_buf.get(), read_buf.size()), kBlockSize);
  EXPECT_EQ(memcmp(expected_superblock, read_buf.get(), kBlockSize), 0,
            "superblock did not contain expected contents");

  // Verify that the root hash in Seal is the hash of the superblock.
  uint8_t expected_seal[32] = {0x79, 0x66, 0xa2, 0x81, 0x27, 0x55, 0xbc, 0x70, 0xba, 0x70, 0x58,
                               0xbe, 0x1f, 0xbb, 0xf1, 0xc4, 0xd8, 0x06, 0xf1, 0xd4, 0x0b, 0x16,
                               0x00, 0xaa, 0xc2, 0x96, 0x33, 0x32, 0xbf, 0x78, 0x1e, 0x28};
  auto& actual_seal = result.response().seal;
  ASSERT_FALSE(actual_seal.has_invalid_tag());
  ASSERT_TRUE(actual_seal.is_sha256());
  auto actual_seal_data = actual_seal.sha256().superblock_hash.data();
  ASSERT_EQ(memcmp(expected_seal, actual_seal_data, 32), 0,
            "Seal did not contain expected contents");
}

}  // namespace
