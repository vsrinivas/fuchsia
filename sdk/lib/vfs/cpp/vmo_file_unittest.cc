// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/fd.h>
#include <lib/fdio/fdio.h>
#include <lib/fdio/limits.h>
#include <lib/vfs/cpp/vmo_file.h>
#include <unistd.h>
#include <zircon/processargs.h>
#include <zircon/status.h>

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

namespace {

void FillBuffer(char* buf, size_t size) {
  for (size_t i = 0; i < size; i++) {
    buf[i] = i % 256;
  }
}

zx::vmo MakeTestVmo() {
  zx::vmo ret;
  EXPECT_EQ(ZX_OK, zx::vmo::create(4096, 0, &ret));

  char buf[4096];
  FillBuffer(buf, 4096);
  EXPECT_EQ(ZX_OK, ret.write(buf, 0, 4096));
  return ret;
}

fuchsia::io::FileSyncPtr OpenAsFile(vfs::internal::Node* node, async_dispatcher_t* dispatcher,
                                    bool writable = false) {
  zx::channel local, remote;
  EXPECT_EQ(ZX_OK, zx::channel::create(0, &local, &remote));
  fuchsia::io::OpenFlags flags = fuchsia::io::OpenFlags::RIGHT_READABLE;
  if (writable) {
    flags |= fuchsia::io::OpenFlags::RIGHT_WRITABLE;
  }
  EXPECT_EQ(ZX_OK, node->Serve(flags, std::move(remote), dispatcher));
  fuchsia::io::FileSyncPtr ret;
  ret.Bind(std::move(local));
  return ret;
}

std::vector<uint8_t> ReadVmo(const zx::vmo& vmo, size_t offset, size_t length) {
  std::vector<uint8_t> ret;
  ret.resize(length);
  EXPECT_EQ(ZX_OK, vmo.read(ret.data(), offset, length));
  return ret;
}

TEST(VmoFile, ConstructTransferOwnership) {
  vfs::VmoFile file(MakeTestVmo(), 1000);
  std::vector<uint8_t> output;
  EXPECT_EQ(ZX_OK, file.ReadAt(1000, 0, &output));
  EXPECT_EQ(1000u, output.size());
}

TEST(VmoFile, Reading) {
  // Create a VmoFile wrapping 1000 bytes of the VMO.
  zx::vmo test_vmo = MakeTestVmo();
  zx::vmo dup;
  ASSERT_EQ(ZX_OK, test_vmo.duplicate(ZX_RIGHT_SAME_RIGHTS, &dup));
  vfs::VmoFile file(std::move(dup), 1000, vfs::VmoFile::WriteOption::READ_ONLY,
                    vfs::VmoFile::Sharing::NONE);

  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  loop.StartThread("vfs test thread");

  auto file_ptr = OpenAsFile(&file, loop.dispatcher());
  ASSERT_TRUE(file_ptr.is_bound());

  // Reading the VMO should match reading the file.
  {
    fuchsia::io::File2_Read_Result result;
    EXPECT_EQ(ZX_OK, file_ptr->Read(500, &result));
    ASSERT_TRUE(result.is_response()) << zx_status_get_string(result.err());
    EXPECT_EQ(ReadVmo(test_vmo, 0, 500), result.response().data);
  }
  {
    fuchsia::io::File2_Read_Result result;
    EXPECT_EQ(ZX_OK, file_ptr->Read(500, &result));
    ASSERT_TRUE(result.is_response()) << zx_status_get_string(result.err());
    EXPECT_EQ(ReadVmo(test_vmo, 500, 500), result.response().data);
  }
}

TEST(VmoFile, GetAttrReadOnly) {
  // Create a VmoFile wrapping 1000 bytes of the VMO.
  zx::vmo test_vmo = MakeTestVmo();
  zx::vmo dup;
  ASSERT_EQ(ZX_OK, test_vmo.duplicate(ZX_RIGHT_SAME_RIGHTS, &dup));
  vfs::VmoFile file(std::move(dup), 1000, vfs::VmoFile::WriteOption::READ_ONLY,
                    vfs::VmoFile::Sharing::NONE);

  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  loop.StartThread("vfs test thread");

  auto file_ptr = OpenAsFile(&file, loop.dispatcher());
  ASSERT_TRUE(file_ptr.is_bound());

  fuchsia::io::NodeAttributes attr;
  zx_status_t status;
  EXPECT_EQ(ZX_OK, file_ptr->GetAttr(&status, &attr));
  EXPECT_EQ(ZX_OK, status);
  EXPECT_EQ(1000u, attr.content_size);
  EXPECT_EQ(1000u, attr.storage_size);
  EXPECT_EQ(
      fuchsia::io::MODE_TYPE_FILE | static_cast<uint32_t>(fuchsia::io::OpenFlags::RIGHT_READABLE),
      attr.mode);
}

TEST(VmoFile, GetAttrWritable) {
  // Create a VmoFile wrapping 1000 bytes of the VMO.
  zx::vmo test_vmo = MakeTestVmo();
  zx::vmo dup;
  ASSERT_EQ(ZX_OK, test_vmo.duplicate(ZX_RIGHT_SAME_RIGHTS, &dup));
  vfs::VmoFile file(std::move(dup), 1000, vfs::VmoFile::WriteOption::WRITABLE,
                    vfs::VmoFile::Sharing::NONE);

  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  loop.StartThread("vfs test thread");

  auto file_ptr = OpenAsFile(&file, loop.dispatcher());
  ASSERT_TRUE(file_ptr.is_bound());

  fuchsia::io::NodeAttributes attr;
  zx_status_t status;
  EXPECT_EQ(ZX_OK, file_ptr->GetAttr(&status, &attr));
  EXPECT_EQ(ZX_OK, status);
  EXPECT_EQ(1000u, attr.content_size);
  EXPECT_EQ(1000u, attr.storage_size);
  EXPECT_EQ(
      fuchsia::io::MODE_TYPE_FILE | static_cast<uint32_t>(fuchsia::io::OpenFlags::RIGHT_READABLE |
                                                          fuchsia::io::OpenFlags::RIGHT_WRITABLE),
      attr.mode);
}

TEST(VmoFile, ReadOnlyNoSharing) {
  // Create a VmoFile wrapping 1000 bytes of the VMO.
  zx::vmo test_vmo = MakeTestVmo();
  zx::vmo dup;
  ASSERT_EQ(ZX_OK, test_vmo.duplicate(ZX_RIGHT_SAME_RIGHTS, &dup));
  vfs::VmoFile file(std::move(dup), 1000, vfs::VmoFile::WriteOption::READ_ONLY,
                    vfs::VmoFile::Sharing::NONE);

  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  loop.StartThread("vfs test thread");

  auto file_ptr = OpenAsFile(&file, loop.dispatcher());
  ASSERT_TRUE(file_ptr.is_bound());

  // Writes should fail, since the VMO is read-only.
  {
    std::vector<uint8_t> value{'a', 'b', 'c', 'd'};
    fuchsia::io::File2_WriteAt_Result result;
    EXPECT_EQ(ZX_OK, file_ptr->WriteAt(value, 0, &result));
    EXPECT_TRUE(result.is_err());
    EXPECT_EQ(ZX_ERR_BAD_HANDLE, result.err());
  }

  // Reading the VMO should match reading the file.
  {
    std::vector<uint8_t> vmo_result = ReadVmo(test_vmo, 24, 500);
    fuchsia::io::File2_ReadAt_Result result;
    EXPECT_EQ(ZX_OK, file_ptr->ReadAt(500, 24, &result));
    EXPECT_TRUE(result.is_response()) << zx_status_get_string(result.err());
    EXPECT_EQ(vmo_result, result.response().data);
  }

  // The file should appear as a regular file, the fact that a VMO is backing it
  // is hidden.
  std::vector<uint8_t> protocol;
  zx_status_t status = file_ptr->Query(&protocol);
  ASSERT_EQ(status, ZX_OK) << zx_status_get_string(status);
  ASSERT_EQ(std::string(protocol.begin(), protocol.end()), fuchsia::io::FILE_PROTOCOL_NAME);
}

TEST(VmoFile, WritableNoSharing) {
  // Create a VmoFile wrapping 1000 bytes of the VMO.
  zx::vmo test_vmo = MakeTestVmo();
  zx::vmo dup;
  ASSERT_EQ(ZX_OK, test_vmo.duplicate(ZX_RIGHT_SAME_RIGHTS, &dup));
  vfs::VmoFile file(std::move(dup), 1000, vfs::VmoFile::WriteOption::WRITABLE,
                    vfs::VmoFile::Sharing::NONE);

  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  loop.StartThread("vfs test thread");

  auto file_ptr = OpenAsFile(&file, loop.dispatcher(), /*writable=*/true);
  ASSERT_TRUE(file_ptr.is_bound());

  // Writes should succeed.
  {
    std::vector<uint8_t> value{'a', 'b', 'c', 'd'};
    fuchsia::io::File2_WriteAt_Result result;
    EXPECT_EQ(ZX_OK, file_ptr->WriteAt(value, 0, &result));
    EXPECT_TRUE(result.is_response()) << zx_status_get_string(result.err());
    EXPECT_EQ(value.size(), result.response().actual_count);
  }

  // Reading the VMO should match reading the file.
  {
    std::vector<uint8_t> vmo_result = ReadVmo(test_vmo, 0, 500);
    fuchsia::io::File2_ReadAt_Result result;
    EXPECT_EQ(ZX_OK, file_ptr->ReadAt(500, 0, &result));
    EXPECT_TRUE(result.is_response()) << zx_status_get_string(result.err());
    EXPECT_EQ(vmo_result, result.response().data);
    EXPECT_EQ('a', result.response().data[0]);
  }

  // The file should appear as a regular file, the fact that a VMO is backing it
  // is hidden.
  std::vector<uint8_t> protocol;
  zx_status_t status = file_ptr->Query(&protocol);
  ASSERT_EQ(status, ZX_OK) << zx_status_get_string(status);
  ASSERT_EQ(std::string(protocol.begin(), protocol.end()), fuchsia::io::FILE_PROTOCOL_NAME);
}

TEST(VmoFile, ReadOnlyDuplicate) {
  // Create a VmoFile wrapping 1000 bytes of the VMO.
  zx::vmo test_vmo = MakeTestVmo();
  zx::vmo dup;
  ASSERT_EQ(ZX_OK, test_vmo.duplicate(ZX_RIGHT_SAME_RIGHTS, &dup));
  vfs::VmoFile file(std::move(dup), 1000);

  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  loop.StartThread("vfs test thread");

  auto file_ptr = OpenAsFile(&file, loop.dispatcher());
  ASSERT_TRUE(file_ptr.is_bound());

  // Writes should fail, since the VMO is read-only.
  {
    std::vector<uint8_t> value{'a', 'b', 'c', 'd'};
    fuchsia::io::File2_WriteAt_Result result;
    EXPECT_EQ(ZX_OK, file_ptr->WriteAt(value, 0, &result));
    EXPECT_TRUE(result.is_err());
    EXPECT_EQ(ZX_ERR_BAD_HANDLE, result.err());
  }

  // Reading the VMO should match reading the file.
  {
    std::vector<uint8_t> vmo_result = ReadVmo(test_vmo, 24, 500);
    fuchsia::io::File2_ReadAt_Result result;
    EXPECT_EQ(ZX_OK, file_ptr->ReadAt(500, 24, &result));
    EXPECT_TRUE(result.is_response()) << zx_status_get_string(result.err());
    EXPECT_EQ(vmo_result, result.response().data);
  }

  // GetBackingMemory duplicates the handle, and we can access the entire VMO.
  fuchsia::io::File2_GetBackingMemory_Result result;
  EXPECT_EQ(ZX_OK, file_ptr->GetBackingMemory(fuchsia::io::VmoFlags::READ, &result));
  ASSERT_TRUE(result.is_response()) << zx_status_get_string(result.err());
  const fuchsia::io::File2_GetBackingMemory_Response& response = result.response();
  const zx::vmo& vmo = response.vmo;
  EXPECT_EQ(ReadVmo(test_vmo, 0, 4096), ReadVmo(vmo, 0, 4096));

  // Writing should fail on the new VMO.
  EXPECT_NE(ZX_OK, vmo.write("test", 0, 4));
}

TEST(VmoFile, WritableDuplicate) {
  // Create a VmoFile wrapping 1000 bytes of the VMO.
  zx::vmo test_vmo = MakeTestVmo();
  zx::vmo dup;
  ASSERT_EQ(ZX_OK, test_vmo.duplicate(ZX_RIGHT_SAME_RIGHTS, &dup));
  vfs::VmoFile file(std::move(dup), 1000, vfs::VmoFile::WriteOption::WRITABLE);

  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  loop.StartThread("vfs test thread");

  auto file_ptr = OpenAsFile(&file, loop.dispatcher(), /*writable=*/true);
  ASSERT_TRUE(file_ptr.is_bound());

  // Writes should succeed.
  {
    std::vector<uint8_t> value{'a', 'b', 'c', 'd'};
    fuchsia::io::File2_WriteAt_Result result;
    EXPECT_EQ(ZX_OK, file_ptr->WriteAt(value, 0, &result));
    EXPECT_TRUE(result.is_response()) << zx_status_get_string(result.err());
    EXPECT_EQ(value.size(), result.response().actual_count);
  }

  // Reading the VMO should match reading the file.
  {
    std::vector<uint8_t> vmo_result = ReadVmo(test_vmo, 0, 500);
    fuchsia::io::File2_ReadAt_Result result;
    EXPECT_EQ(ZX_OK, file_ptr->ReadAt(500, 0, &result));
    EXPECT_TRUE(result.is_response()) << zx_status_get_string(result.err());
    EXPECT_EQ(vmo_result, result.response().data);
    EXPECT_EQ('a', result.response().data[0]);
  }

  // GetBackingMemory duplicates the handle, and we can access the entire VMO.
  fuchsia::io::File2_GetBackingMemory_Result result;
  EXPECT_EQ(ZX_OK, file_ptr->GetBackingMemory(fuchsia::io::VmoFlags::READ, &result));
  ASSERT_TRUE(result.is_response()) << zx_status_get_string(result.err());
  const fuchsia::io::File2_GetBackingMemory_Response& response = result.response();
  const zx::vmo& vmo = response.vmo;
  EXPECT_EQ(ReadVmo(test_vmo, 0, 4096), ReadVmo(vmo, 0, 4096));
}

TEST(VmoFile, ReadOnlyCopyOnWrite) {
  // Create a VmoFile wrapping the VMO.
  zx::vmo test_vmo = MakeTestVmo();
  zx::vmo dup;
  ASSERT_EQ(ZX_OK, test_vmo.duplicate(ZX_RIGHT_SAME_RIGHTS, &dup));
  vfs::VmoFile file(std::move(dup), 4096, vfs::VmoFile::WriteOption::READ_ONLY,
                    vfs::VmoFile::Sharing::CLONE_COW);

  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  loop.StartThread("vfs test thread");

  auto file_ptr = OpenAsFile(&file, loop.dispatcher());
  ASSERT_TRUE(file_ptr.is_bound());

  // Writes should fail, since the VMO is read-only.
  {
    std::vector<uint8_t> value{'a', 'b', 'c', 'd'};
    fuchsia::io::File2_WriteAt_Result result;
    EXPECT_EQ(ZX_OK, file_ptr->WriteAt(value, 0, &result));
    EXPECT_TRUE(result.is_err());
    EXPECT_EQ(ZX_ERR_BAD_HANDLE, result.err());
  }

  // Reading the VMO should match reading the file.
  {
    std::vector<uint8_t> vmo_result = ReadVmo(test_vmo, 0, 4096);
    fuchsia::io::File2_ReadAt_Result result;
    EXPECT_EQ(ZX_OK, file_ptr->ReadAt(4096, 0, &result));
    EXPECT_TRUE(result.is_response()) << zx_status_get_string(result.err());
    EXPECT_EQ(vmo_result, result.response().data);
  }

  // GetBackingMemory duplicates the handle, and we can access the entire VMO.
  fuchsia::io::File2_GetBackingMemory_Result result;
  EXPECT_EQ(ZX_OK, file_ptr->GetBackingMemory({}, &result));
  ASSERT_TRUE(result.is_response()) << zx_status_get_string(result.err());
  const fuchsia::io::File2_GetBackingMemory_Response& response = result.response();
  const zx::vmo& vmo = response.vmo;
  EXPECT_EQ(ReadVmo(test_vmo, 0, 4096), ReadVmo(vmo, 0, 4096));

  // Writing should succeed on the new VMO, due to copy on write.
  EXPECT_EQ(ZX_OK, vmo.write("test", 0, 4));
  EXPECT_NE(ReadVmo(test_vmo, 0, 4096), ReadVmo(vmo, 0, 4096));
}

TEST(VmoFile, WritableCopyOnWrite) {
  // Create a VmoFile wrapping the VMO.
  zx::vmo test_vmo = MakeTestVmo();
  zx::vmo dup;
  ASSERT_EQ(ZX_OK, test_vmo.duplicate(ZX_RIGHT_SAME_RIGHTS, &dup));
  vfs::VmoFile file(std::move(dup), 4096, vfs::VmoFile::WriteOption::WRITABLE,
                    vfs::VmoFile::Sharing::CLONE_COW);

  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  loop.StartThread("vfs test thread");

  auto file_ptr = OpenAsFile(&file, loop.dispatcher(), /*writable=*/true);
  ASSERT_TRUE(file_ptr.is_bound());

  // Writes should succeed.
  {
    std::vector<uint8_t> value{'a', 'b', 'c', 'd'};
    fuchsia::io::File2_WriteAt_Result result;
    EXPECT_EQ(ZX_OK, file_ptr->WriteAt(value, 0, &result));
    EXPECT_TRUE(result.is_response()) << zx_status_get_string(result.err());
    EXPECT_EQ(value.size(), result.response().actual_count);
  }

  // Reading the VMO should match reading the file.
  {
    std::vector<uint8_t> vmo_result = ReadVmo(test_vmo, 0, 4096);
    fuchsia::io::File2_ReadAt_Result result;
    EXPECT_EQ(ZX_OK, file_ptr->ReadAt(4096, 0, &result));
    EXPECT_TRUE(result.is_response()) << zx_status_get_string(result.err());
    EXPECT_EQ(vmo_result, result.response().data);
    EXPECT_EQ('a', result.response().data[0]);
  }

  // GetBackingMemory duplicates the handle, and we can access the entire VMO.
  fuchsia::io::File2_GetBackingMemory_Result result;
  EXPECT_EQ(ZX_OK, file_ptr->GetBackingMemory({}, &result));
  ASSERT_TRUE(result.is_response()) << zx_status_get_string(result.err());
  const fuchsia::io::File2_GetBackingMemory_Response& response = result.response();
  const zx::vmo& vmo = response.vmo;
  EXPECT_EQ(ReadVmo(test_vmo, 0, 4096), ReadVmo(vmo, 0, 4096));

  // Writing should succeed on the new VMO, due to copy on write.
  EXPECT_EQ(ZX_OK, vmo.write("test", 0, 4));
  EXPECT_NE(ReadVmo(test_vmo, 0, 4096), ReadVmo(vmo, 0, 4096));
}

TEST(VmoFile, VmoWithNoRights) {
  // Create a VmoFile wrapping 1000 bytes of the VMO.
  // The vmo we use has no rights, so reading, writing, and duplication will
  // fail.
  zx::vmo test_vmo = MakeTestVmo();
  zx::vmo bad_vmo;
  ASSERT_EQ(ZX_OK, test_vmo.duplicate(0, &bad_vmo));
  vfs::VmoFile file(std::move(bad_vmo), 1000, vfs::VmoFile::WriteOption::WRITABLE);

  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  loop.StartThread("vfs test thread");

  auto file_ptr = OpenAsFile(&file, loop.dispatcher(), /*writable=*/true);
  ASSERT_TRUE(file_ptr.is_bound());

  // Writes should fail.
  {
    std::vector<uint8_t> value{'a', 'b', 'c', 'd'};
    fuchsia::io::File2_WriteAt_Result result;
    EXPECT_EQ(ZX_OK, file_ptr->WriteAt(value, 0, &result));
    EXPECT_TRUE(result.is_err());
    EXPECT_EQ(ZX_ERR_ACCESS_DENIED, result.err());
  }

  // Reading should fail.
  {
    fuchsia::io::File2_ReadAt_Result result;
    EXPECT_EQ(ZX_OK, file_ptr->ReadAt(1000, 0, &result));
    EXPECT_TRUE(result.is_err());
    EXPECT_EQ(ZX_ERR_ACCESS_DENIED, result.err());
  }
}

}  // namespace
