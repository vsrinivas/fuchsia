// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/volume_image/fvm/fvm_sparse_image_reader.h"

#include <fcntl.h>
#include <fuchsia/device/llcpp/fidl.h>
#include <lib/fdio/fdio.h>
#include <lib/fzl/resizeable-vmo-mapper.h>
#include <zircon/hw/gpt.h>

#include <block-client/cpp/remote-block-device.h>
#include <fbl/unique_fd.h>
#include <fs-management/admin.h>
#include <fs-management/fvm.h>
#include <fvm/format.h>
#include <fvm/fvm-sparse.h>
#include <fvm/fvm.h>
#include <gtest/gtest.h>

#include "src/lib/isolated_devmgr/v2_component/ram_disk.h"
#include "src/storage/volume_image/ftl/ftl_image.h"
#include "src/storage/volume_image/ftl/options.h"
#include "src/storage/volume_image/utils/fd_reader.h"
#include "src/storage/volume_image/utils/writer.h"

namespace storage::volume_image {
namespace {

constexpr std::string_view sparse_image_path = "/pkg/data/test_fvm.sparse.blk";

static fidl::StringView FvmDriverLib() { return fidl::StringView("/pkg/bin/driver/fvm.so"); }

zx::status<std::string> AttachFvm(const std::string& device_path) {
  fbl::unique_fd fd(open(device_path.c_str(), O_RDWR));
  if (!fd) {
    return zx::error(ZX_ERR_BAD_STATE);
  }
  zx::channel fvm_channel;
  auto status =
      zx::make_status(fdio_get_service_handle(fd.get(), fvm_channel.reset_and_get_address()));
  if (status.is_error()) {
    return status.take_error();
  }
  auto resp = llcpp::fuchsia::device::Controller::Call::Bind(zx::unowned_channel(fvm_channel.get()),
                                                             FvmDriverLib());
  status = zx::make_status(resp.status());
  if (status.is_ok()) {
    if (resp->result.is_err()) {
      status = zx::make_status(resp->result.err());
    }
  }
  if (status.is_error()) {
    return status.take_error();
  }
  std::string fvm_disk_path = device_path + "/fvm";
  status = zx::make_status(wait_for_device(fvm_disk_path.c_str(), zx::sec(3).get()));
  if (status.is_error()) {
    return status.take_error();
  }

  return zx::ok(fvm_disk_path);
}

// Create a ram-disk and copy the output directly into the ram-disk, and then see if FVM can read
// it and minfs Fsck passes.
TEST(FvmSparseImageReaderTest, ImageWithMinfsPassesFsck) {
  auto base_reader_or = FdReader::Create(sparse_image_path);
  ASSERT_TRUE(base_reader_or.is_ok()) << base_reader_or.error();
  auto sparse_image_or = OpenSparseImage(base_reader_or.value(), std::nullopt);
  ASSERT_TRUE(sparse_image_or.is_ok()) << sparse_image_or.error();

  // Create a ram-disk.
  constexpr int kDeviceBlockSize = 8192;
  const uint64_t disk_size = sparse_image_or.value().volume().size;
  auto ram_disk_or =
      isolated_devmgr::RamDisk::Create(kDeviceBlockSize, disk_size / kDeviceBlockSize);
  ASSERT_TRUE(ram_disk_or.is_ok()) << ram_disk_or.status_string();

  // Open the ram disk
  fbl::unique_fd fd(open(ram_disk_or.value().path().c_str(), O_RDWR));
  ASSERT_TRUE(fd);

  zx::channel device;
  zx_status_t status = fdio_get_service_handle(fd.release(), device.reset_and_get_address());
  ASSERT_EQ(status, ZX_OK);

  std::unique_ptr<block_client::RemoteBlockDevice> client;
  status = block_client::RemoteBlockDevice::Create(std::move(device), &client);
  ASSERT_EQ(status, ZX_OK);

  constexpr uint64_t kInitialVmoSize = 1048576;
  auto vmo = fzl::ResizeableVmoMapper::Create(kInitialVmoSize, "test");
  ASSERT_TRUE(vmo);

  storage::Vmoid vmoid;
  status = client->BlockAttachVmo(vmo->vmo(), &vmoid);
  // This is a test, so we don't need to worry about cleaning it up.
  vmoid_t vmo_id = vmoid.TakeId();
  ASSERT_EQ(status, ZX_OK);

  memset(vmo->start(), 0xaf, kInitialVmoSize);

  // Initialize the entire ramdisk with a filler (that isn't zero).
  for (uint64_t offset = 0; offset < disk_size; offset += kInitialVmoSize) {
    block_fifo_request_t request = {
        .opcode = BLOCKIO_WRITE,
        .vmoid = vmo_id,
        .length =
            static_cast<uint32_t>(std::min(disk_size - offset, kInitialVmoSize) / kDeviceBlockSize),
        .dev_offset = offset / kDeviceBlockSize};
    status = client->FifoTransaction(&request, 1);
    ASSERT_EQ(status, ZX_OK);
  }

  for (const AddressMap& map : sparse_image_or.value().address().mappings) {
    ASSERT_EQ(map.count % kDeviceBlockSize, 0u);
    ASSERT_EQ(map.target % kDeviceBlockSize, 0u);
    ASSERT_LT(map.target, disk_size);
    ASSERT_LE(map.target + map.count, disk_size);
    EXPECT_TRUE(map.options.empty());

    if (vmo->size() < map.count) {
      status = vmo->Grow(map.count);
      ASSERT_EQ(status, ZX_OK);
    }
    auto result = sparse_image_or.value().reader()->Read(
        map.source, fbl::Span<uint8_t>(reinterpret_cast<uint8_t*>(vmo->start()), map.count));
    ASSERT_TRUE(result.is_ok()) << result.error();

    // Write the mapping to the ram disk.
    block_fifo_request_t request = {.opcode = BLOCKIO_WRITE,
                                    .vmoid = vmo_id,
                                    .length = static_cast<uint32_t>(map.count / kDeviceBlockSize),
                                    .dev_offset = map.target / kDeviceBlockSize};

    status = client->FifoTransaction(&request, 1);
    ASSERT_EQ(status, ZX_OK) << "length=" << request.length
                             << ", dev_offset=" << request.dev_offset;
  }

  fd.reset();
  client.reset();

  // Now try and attach FVM.
  auto result = AttachFvm(ram_disk_or.value().path());
  ASSERT_TRUE(result.is_ok());

  uint8_t minfs_guid[] = GUID_DATA_VALUE;
  char path[PATH_MAX];
  fd.reset(open_partition(nullptr, minfs_guid, zx::duration::infinite().get(), path));
  ASSERT_TRUE(fd);
  fd.reset();

  // And finally run fsck on the volume.
  fsck_options_t options{
      .verbose = false,
      .never_modify = true,
      .always_modify = false,
      .force = true,
      .apply_journal = false,
  };
  ASSERT_EQ(fsck(path, DISK_FORMAT_MINFS, &options, launch_stdio_sync), 0);
}

class NullWriter : public Writer {
 public:
  fit::result<void, std::string> Write(uint64_t offset, fbl::Span<const uint8_t> buffer) {
    return fit::ok();
  }
};

TEST(FvmSparseImageReaderTest, ImageWithMaxSizeALlocatesEnoughMetadata) {
  auto base_reader_or = FdReader::Create(sparse_image_path);
  ASSERT_TRUE(base_reader_or.is_ok()) << base_reader_or.error();

  fvm::SparseImage image = {};
  auto image_stream = fbl::Span<uint8_t>(reinterpret_cast<uint8_t*>(&image), sizeof(image));
  auto read_result = base_reader_or.value().Read(0, image_stream);
  ASSERT_TRUE(read_result.is_ok()) << read_result.error();
  ASSERT_EQ(image.magic, fvm::kSparseFormatMagic);

  auto sparse_image_or = OpenSparseImage(base_reader_or.value(), 300 << 20);
  ASSERT_TRUE(sparse_image_or.is_ok()) << sparse_image_or.error();
  auto sparse_image = sparse_image_or.take_value();

  auto expected_format_info = fvm::FormatInfo::FromDiskSize(300 << 20, image.slice_size);

  fvm::Header header = {};
  auto header_stream = fbl::Span<uint8_t>(reinterpret_cast<uint8_t*>(&header), sizeof(header));
  read_result = sparse_image.reader()->Read(sparse_image.reader()->GetMaximumOffset() -
                                                2 * expected_format_info.metadata_allocated_size(),
                                            header_stream);
  ASSERT_TRUE(read_result.is_ok()) << read_result.error();
  ASSERT_EQ(header.magic, fvm::kMagic);

  auto actual_format_info = fvm::FormatInfo(header);
  EXPECT_EQ(expected_format_info.metadata_allocated_size(),
            actual_format_info.metadata_allocated_size());
}

// This doesn't test that the resulting image is valid, but it least tests that FtlImageWrite can
// consume the sparse image without complaining.
TEST(FvmSparseImageReaderTest, WriteFtlImageSucceeds) {
  auto base_reader_or = FdReader::Create(sparse_image_path);
  ASSERT_TRUE(base_reader_or.is_ok()) << base_reader_or.error();
  auto sparse_image_or = OpenSparseImage(base_reader_or.value(), std::nullopt);
  ASSERT_TRUE(sparse_image_or.is_ok()) << sparse_image_or.error();

  constexpr int kFtlPageSize = 8192;
  NullWriter writer;
  auto result = FtlImageWrite(
      RawNandOptions{
          .page_size = kFtlPageSize,
          .page_count = static_cast<uint32_t>(sparse_image_or.value().volume().size / kFtlPageSize),
          .pages_per_block = 32,
          .oob_bytes_size = 16,
      },
      sparse_image_or.value(), &writer);
  EXPECT_TRUE(result.is_ok()) << result.error();
}

}  // namespace
}  // namespace storage::volume_image
