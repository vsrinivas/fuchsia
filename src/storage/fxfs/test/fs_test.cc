// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include <fcntl.h>
#include <fuchsia/io/cpp/fidl.h>
#include <lib/fdio/cpp/caller.h>
#include <lib/zx/result.h>

#include <utility>

#include <fbl/unique_fd.h>

#include "gtest/gtest.h"
#include "src/lib/storage/block_client/cpp/remote_block_device.h"
#include "src/storage/fs_test/fs_test_fixture.h"
#include "storage/buffer/owned_vmoid.h"
#include "zircon/errors.h"

namespace fs_test {
namespace {

using DeviceTest = fs_test::FilesystemTest;

// Defaults to a filesize of 1 megabyte.
void CreateFxFile(const std::string& file_name, const off_t file_size = 1024 * 1024) {
  fbl::unique_fd fd(open(file_name.c_str(), O_CREAT | O_RDWR, 0666));
  ASSERT_TRUE(fd);
  ASSERT_EQ(ftruncate(fd.get(), file_size), 0);
}

TEST_P(DeviceTest, TestValidDiskFormat) {
  ASSERT_TRUE(fs().Unmount().is_ok());
  fbl::unique_fd device_fd(open(fs().DevicePath()->c_str(), O_RDWR));
  ASSERT_EQ(fs_management::DetectDiskFormat(device_fd.get()), fs_management::kDiskFormatFxfs);
}

TEST_P(DeviceTest, TestWriteThenRead) {
  const std::string kFilename = GetPath("block_device");
  const off_t kFileSize = 10 * 1024 * 1024;  // 10 megabytes
  CreateFxFile(kFilename, kFileSize);
  auto endpoints = fidl::CreateEndpoints<fuchsia_io::Node>();
  ASSERT_EQ(endpoints.status_value(), ZX_OK);
  auto [client, server] = *std::move(endpoints);

  // Re-open file as block device i.e. using MODE_TYPE_BLOCK_DEVICE
  fdio_cpp::FdioCaller caller(fs().GetRootFd());
  ASSERT_EQ(fidl::WireCall(caller.directory())
                ->Open(fuchsia_io::wire::OpenFlags::kRightReadable |
                           fuchsia_io::wire::OpenFlags::kRightWritable,
                       fuchsia_io::wire::kModeTypeBlockDevice, "block_device", std::move(server))
                .status(),
            ZX_OK);

  std::unique_ptr<block_client::RemoteBlockDevice> device;
  ASSERT_EQ(block_client::RemoteBlockDevice::Create(client.TakeChannel(), &device), ZX_OK);

  fuchsia_hardware_block_BlockInfo info = {};
  ASSERT_EQ(device->BlockGetInfo(&info), ZX_OK);
  ASSERT_EQ(info.block_count, static_cast<unsigned long>(kFileSize) / info.block_size);

  zx::vmo vmo;
  storage::OwnedVmoid vmoid;
  const size_t kVmoBlocks = 5;
  ASSERT_EQ(zx::vmo::create(kVmoBlocks * info.block_size, 0, &vmo), ZX_OK);
  ASSERT_NO_FATAL_FAILURE(device->BlockAttachVmo(vmo, &vmoid.GetReference(device.get())));

  const size_t kVmoWriteBlocks = 2;
  const size_t kVmoBlockOffset = 1;
  ASSERT_LE(kVmoBlockOffset + kVmoWriteBlocks, kVmoBlocks);
  char write_buf[kVmoWriteBlocks * info.block_size];
  memset(write_buf, 0xa3, sizeof(write_buf));
  ASSERT_EQ(vmo.write(write_buf, kVmoBlockOffset * info.block_size, sizeof(write_buf)), ZX_OK);

  block_fifo_request_t write_request;
  write_request.opcode = BLOCKIO_WRITE;
  write_request.vmoid = vmoid.get();
  write_request.length = kVmoWriteBlocks;
  write_request.vmo_offset = kVmoBlockOffset;
  write_request.dev_offset = 0;
  EXPECT_EQ(device->FifoTransaction(&write_request, 1), ZX_OK);

  // "Clear" vmo, so any data in the vmo after is solely dependent on the following BLOCKIO_READ
  char zero_buf[kVmoBlocks * info.block_size];
  memset(zero_buf, 0, sizeof(zero_buf));
  ASSERT_EQ(vmo.write(zero_buf, 0, sizeof(zero_buf)), ZX_OK);

  char read_buf[kVmoWriteBlocks * info.block_size];
  memset(read_buf, 0, sizeof(read_buf));
  block_fifo_request_t read_request;
  read_request.opcode = BLOCKIO_READ;
  read_request.vmoid = vmoid.get();
  read_request.length = kVmoWriteBlocks;
  read_request.vmo_offset = kVmoBlockOffset;
  read_request.dev_offset = 0;
  ASSERT_EQ(device->FifoTransaction(&read_request, 1), ZX_OK);
  ASSERT_EQ(vmo.read(read_buf, kVmoBlockOffset * info.block_size, sizeof(read_buf)), ZX_OK);
  EXPECT_EQ(memcmp(write_buf, read_buf, sizeof(write_buf)), 0);
}

// Tests multiple reads and writes in a group
TEST_P(DeviceTest, TestGroupWritesThenReads) {
  const std::string kFilename = GetPath("block_device");
  CreateFxFile(kFilename);
  auto endpoints = fidl::CreateEndpoints<fuchsia_io::Node>();
  ASSERT_EQ(endpoints.status_value(), ZX_OK);
  auto [client, server] = *std::move(endpoints);

  fdio_cpp::FdioCaller caller(fs().GetRootFd());
  ASSERT_EQ(fidl::WireCall(caller.directory())
                ->Open(fuchsia_io::wire::OpenFlags::kRightReadable |
                           fuchsia_io::wire::OpenFlags::kRightWritable,
                       fuchsia_io::wire::kModeTypeBlockDevice, "block_device", std::move(server))
                .status(),
            ZX_OK);

  std::unique_ptr<block_client::RemoteBlockDevice> device;
  ASSERT_EQ(block_client::RemoteBlockDevice::Create(client.TakeChannel(), &device), ZX_OK);

  const size_t kVmoBlocks = 6;
  zx::vmo vmo;
  storage::OwnedVmoid vmoid;
  fuchsia_hardware_block_BlockInfo info = {};
  ASSERT_EQ(device->BlockGetInfo(&info), ZX_OK);
  ASSERT_EQ(zx::vmo::create(kVmoBlocks * info.block_size, 0, &vmo), ZX_OK);
  ASSERT_NO_FATAL_FAILURE(device->BlockAttachVmo(vmo, &vmoid.GetReference(device.get())));

  // The first group of writes will send 2 write requests, with a buffer size of kVmoWriteBlocks *
  // block_size This test will write and read from vmo and device with an offset to test that read
  // and writes work with offsets
  const size_t kVmoWriteBlocks = 2;
  const size_t kOffsetBlocks = 1;
  ASSERT_LE(kOffsetBlocks + 2 * kVmoWriteBlocks, kVmoBlocks);

  // Write write_buf1 and write_buf2 to vmo with offset = kOffsetBlocks
  char write_buf1[kVmoWriteBlocks * info.block_size];
  memset(write_buf1, 0xa3, sizeof(write_buf1));
  ASSERT_EQ(vmo.write(write_buf1, kOffsetBlocks * info.block_size, sizeof(write_buf1)), ZX_OK);

  char write_buf2[kVmoWriteBlocks * info.block_size];
  memset(write_buf2, 0xf7, sizeof(write_buf2));
  ASSERT_EQ(vmo.write(write_buf2, (kOffsetBlocks + kVmoWriteBlocks) * info.block_size,
                      sizeof(write_buf2)),
            ZX_OK);

  block_fifo_request_t write_requests[2];
  write_requests[0].opcode = BLOCKIO_WRITE;
  write_requests[0].vmoid = vmoid.get();
  write_requests[0].length = kVmoWriteBlocks;
  write_requests[0].vmo_offset = kOffsetBlocks;
  write_requests[0].dev_offset = 0;

  write_requests[1].opcode = BLOCKIO_WRITE;
  write_requests[1].vmoid = vmoid.get();
  write_requests[1].length = kVmoWriteBlocks;
  write_requests[1].vmo_offset = kOffsetBlocks + kVmoWriteBlocks;
  write_requests[1].dev_offset = kVmoWriteBlocks;
  EXPECT_EQ(device->FifoTransaction(write_requests, std::size(write_requests)), ZX_OK);

  char read_buf[kVmoBlocks * info.block_size];
  memset(read_buf, 0, sizeof(read_buf));
  ASSERT_EQ(vmo.write(read_buf, 0, sizeof(read_buf)), ZX_OK);

  block_fifo_request_t read_requests[2];
  read_requests[0].opcode = BLOCKIO_READ;
  read_requests[0].vmoid = vmoid.get();
  read_requests[0].length = kVmoWriteBlocks;
  read_requests[0].vmo_offset = kOffsetBlocks;
  read_requests[0].dev_offset = 0;

  read_requests[1].opcode = BLOCKIO_READ;
  read_requests[1].vmoid = vmoid.get();
  read_requests[1].length = kVmoWriteBlocks;
  read_requests[1].vmo_offset = kOffsetBlocks + kVmoWriteBlocks;
  read_requests[1].dev_offset = kVmoWriteBlocks;
  ASSERT_EQ(device->FifoTransaction(read_requests, std::size(read_requests)), ZX_OK);
  ASSERT_EQ(vmo.read(read_buf, 0, sizeof(read_buf)), ZX_OK);
  EXPECT_EQ(memcmp(write_buf1, read_buf + (kOffsetBlocks * info.block_size), sizeof(write_buf1)),
            0);
  EXPECT_EQ(memcmp(write_buf2, read_buf + ((kOffsetBlocks + kVmoWriteBlocks) * info.block_size),
                   sizeof(write_buf2)),
            0);
}

TEST_P(DeviceTest, TestWriteThenFlushThenRead) {
  const std::string kFilename = GetPath("block_device");
  CreateFxFile(kFilename);
  auto endpoints = fidl::CreateEndpoints<fuchsia_io::Node>();
  ASSERT_EQ(endpoints.status_value(), ZX_OK);
  auto [client, server] = *std::move(endpoints);

  fdio_cpp::FdioCaller caller(fs().GetRootFd());
  ASSERT_EQ(fidl::WireCall(caller.directory())
                ->Open(fuchsia_io::wire::OpenFlags::kRightReadable |
                           fuchsia_io::wire::OpenFlags::kRightWritable,
                       fuchsia_io::wire::kModeTypeBlockDevice, "block_device", std::move(server))
                .status(),
            ZX_OK);

  std::unique_ptr<block_client::RemoteBlockDevice> device;
  ASSERT_EQ(block_client::RemoteBlockDevice::Create(client.TakeChannel(), &device), ZX_OK);

  const size_t kVmoBlocks = 2;
  zx::vmo vmo;
  storage::OwnedVmoid vmoid;
  fuchsia_hardware_block_BlockInfo info = {};
  ASSERT_EQ(device->BlockGetInfo(&info), ZX_OK);
  ASSERT_EQ(zx::vmo::create(kVmoBlocks * info.block_size, 0, &vmo), ZX_OK);
  ASSERT_NO_FATAL_FAILURE(device->BlockAttachVmo(vmo, &vmoid.GetReference(device.get())));

  char write_buf[kVmoBlocks * info.block_size];
  memset(write_buf, 0xa3, sizeof(write_buf));
  ASSERT_EQ(vmo.write(write_buf, 0, sizeof(write_buf)), ZX_OK);

  block_fifo_request_t requests[2];
  requests[0].opcode = BLOCKIO_WRITE;
  requests[0].vmoid = vmoid.get();
  requests[0].length = kVmoBlocks;
  requests[0].vmo_offset = 0;
  requests[0].dev_offset = 0;

  requests[1].opcode = BLOCKIO_FLUSH;
  requests[1].vmoid = vmoid.get();
  requests[1].length = 0;
  requests[1].vmo_offset = 0;
  requests[1].dev_offset = 0;
  EXPECT_EQ(device->FifoTransaction(requests, std::size(requests)), ZX_OK);

  char read_buf[kVmoBlocks * info.block_size];
  memset(read_buf, 0, sizeof(read_buf));
  ASSERT_EQ(vmo.write(read_buf, 0, sizeof(read_buf)), ZX_OK);

  block_fifo_request_t read_request;
  read_request.opcode = BLOCKIO_READ;
  read_request.vmoid = vmoid.get();
  read_request.length = kVmoBlocks;
  read_request.vmo_offset = 0;
  read_request.dev_offset = 0;
  ASSERT_EQ(device->FifoTransaction(&read_request, 1), ZX_OK);
  ASSERT_EQ(vmo.read(read_buf, 0, sizeof(read_buf)), ZX_OK);
  EXPECT_EQ(memcmp(write_buf, read_buf, sizeof(write_buf)), 0);
}

TEST_P(DeviceTest, TestInvalidGroupRequests) {
  const std::string kFilename = GetPath("block_device");
  CreateFxFile(kFilename);
  auto endpoints = fidl::CreateEndpoints<fuchsia_io::Node>();
  ASSERT_EQ(endpoints.status_value(), ZX_OK);
  auto [client, server] = *std::move(endpoints);

  fdio_cpp::FdioCaller caller(fs().GetRootFd());
  ASSERT_EQ(fidl::WireCall(caller.directory())
                ->Open(fuchsia_io::wire::OpenFlags::kRightReadable |
                           fuchsia_io::wire::OpenFlags::kRightWritable,
                       fuchsia_io::wire::kModeTypeBlockDevice, "block_device", std::move(server))
                .status(),
            ZX_OK);

  std::unique_ptr<block_client::RemoteBlockDevice> device;
  ASSERT_EQ(block_client::RemoteBlockDevice::Create(client.TakeChannel(), &device), ZX_OK);

  const size_t kVmoBlocks = 5;
  zx::vmo vmo;
  storage::OwnedVmoid vmoid;
  fuchsia_hardware_block_BlockInfo info = {};
  ASSERT_EQ(device->BlockGetInfo(&info), ZX_OK);
  ASSERT_EQ(zx::vmo::create(kVmoBlocks * info.block_size, 0, &vmo), ZX_OK);
  ASSERT_NO_FATAL_FAILURE(device->BlockAttachVmo(vmo, &vmoid.GetReference(device.get())));

  block_fifo_request_t requests[3];
  requests[0].opcode = BLOCKIO_FLUSH;
  requests[0].vmoid = vmoid.get();
  requests[0].length = 0;
  requests[0].vmo_offset = 0;
  requests[0].dev_offset = 0;

  // Not a valid request
  requests[1].opcode = BLOCKIO_CLOSE_VMO;
  requests[1].vmoid = 100;
  requests[1].length = 0;
  requests[1].vmo_offset = 0;
  requests[1].dev_offset = 0;

  requests[2].opcode = BLOCKIO_FLUSH;
  requests[2].vmoid = vmoid.get();
  requests[2].length = 0;
  requests[2].vmo_offset = 0;
  requests[2].dev_offset = 0;
  EXPECT_NE(device->FifoTransaction(requests, std::size(requests)), ZX_OK);
}

INSTANTIATE_TEST_SUITE_P(/*no prefix*/, DeviceTest, testing::ValuesIn(AllTestFilesystems()),
                         testing::PrintToStringParamName());
}  // namespace
}  // namespace fs_test
