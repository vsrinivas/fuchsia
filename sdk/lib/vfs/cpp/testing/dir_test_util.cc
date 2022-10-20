// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/io/cpp/fidl.h>
#include <lib/vfs/cpp/testing/dir_test_util.h>
#include <zircon/status.h>

namespace vfs_tests {

Dirent Dirent::DirentForDot() { return DirentForDirectory("."); }

Dirent Dirent::DirentForDirectory(const std::string& name) {
  return Dirent(fuchsia::io::INO_UNKNOWN, fuchsia::io::DirentType::DIRECTORY, name);
}

Dirent Dirent::DirentForFile(const std::string& name) {
  return Dirent(fuchsia::io::INO_UNKNOWN, fuchsia::io::DirentType::FILE, name);
}

Dirent Dirent::DirentForService(const std::string& name) {
  return Dirent(fuchsia::io::INO_UNKNOWN, fuchsia::io::DirentType::SERVICE, name);
}

std::string Dirent::String() {
  return "Dirent:\nino: " + std ::to_string(ino_) + "\ntype: " + std ::to_string(type_) +
         "\nsize: " + std ::to_string(size_) + "\nname: " + name_;
}

Dirent::Dirent(uint64_t ino, fuchsia::io::DirentType type, const std::string& name)
    : ino_(ino),
      type_(static_cast<uint8_t>(type)),
      size_(static_cast<uint8_t>(name.length())),
      name_(name),
      size_in_bytes_(sizeof(vdirent_t) + size_) {
  ZX_DEBUG_ASSERT(name.length() <= static_cast<uint64_t>(NAME_MAX));
}

void DirConnection::AssertOpen(async_dispatcher_t* dispatcher, fuchsia::io::OpenFlags flags,
                               zx_status_t expected_status, bool test_on_open_event) {
  fuchsia::io::NodePtr node_ptr;
  if (test_on_open_event) {
    flags |= fuchsia::io::OpenFlags::DESCRIBE;
  }
  EXPECT_EQ(expected_status,
            GetDirectoryNode()->Serve(flags, node_ptr.NewRequest().TakeChannel(), dispatcher));

  if (test_on_open_event) {
    bool on_open_called = false;
    node_ptr.events().OnOpen = [&](zx_status_t status,
                                   std::unique_ptr<fuchsia::io::NodeInfoDeprecated> info) {
      EXPECT_FALSE(on_open_called);  // should be called only once
      on_open_called = true;
      EXPECT_EQ(expected_status, status);
      if (expected_status == ZX_OK) {
        ASSERT_NE(info.get(), nullptr);
        EXPECT_TRUE(info->is_directory());
      } else {
        EXPECT_EQ(info.get(), nullptr);
      }
    };

    RunLoopUntil([&]() { return on_open_called; }, zx::msec(1));
  }
}

void DirConnection::AssertReadDirents(fuchsia::io::DirectorySyncPtr& ptr, uint64_t max_bytes,
                                      std::vector<Dirent>& expected_dirents,
                                      zx_status_t expected_status) {
  std::vector<uint8_t> out_dirents;
  zx_status_t status;
  ptr->ReadDirents(max_bytes, &status, &out_dirents);
  ASSERT_EQ(expected_status, status);
  if (status != ZX_OK) {
    return;
  }
  uint64_t expected_size = 0;
  for (auto& d : expected_dirents) {
    expected_size += d.size_in_bytes();
  }
  EXPECT_EQ(expected_size, out_dirents.size());
  uint64_t offset = 0;
  auto data_ptr = out_dirents.data();
  for (auto& d : expected_dirents) {
    SCOPED_TRACE(d.String());
    ASSERT_LE(sizeof(vdirent_t), out_dirents.size() - offset);
    vdirent_t* de = reinterpret_cast<vdirent_t*>(data_ptr + offset);

    uint64_t ino;
    memcpy(&ino, &de->ino, sizeof(ino));
    EXPECT_EQ(d.ino(), ino);

    uint8_t size, type;
    memcpy(&size, &de->size, sizeof(size));
    EXPECT_EQ(d.size(), size);

    memcpy(&type, &de->type, sizeof(type));
    EXPECT_EQ(d.type(), type);
    ASSERT_LE(d.size_in_bytes(), out_dirents.size() - offset);
    EXPECT_EQ(d.name(), std::string(de->name, size));

    offset += sizeof(vdirent_t) + size;
  }
}

void DirConnection::AssertRewind(fuchsia::io::DirectorySyncPtr& ptr, zx_status_t expected_status) {
  zx_status_t status;
  ptr->Rewind(&status);
  ASSERT_EQ(expected_status, status);
}

void DirConnection::AssertRead(fuchsia::io::FileSyncPtr& file, int count,
                               const std::string& expected_str, zx_status_t expected_status) {
  fuchsia::io::Readable_Read_Result result;
  file->Read(count, &result);
  if (expected_status == ZX_OK) {
    ASSERT_TRUE(result.is_response()) << zx_status_get_string(result.err());
    const std::vector<uint8_t>& data = result.response().data;
    ASSERT_EQ(expected_str,
              std::string_view(reinterpret_cast<const char*>(data.data()), data.size()));
  } else {
    ASSERT_TRUE(result.is_err());
    ASSERT_EQ(expected_status, result.err());
  }
}

}  // namespace vfs_tests
