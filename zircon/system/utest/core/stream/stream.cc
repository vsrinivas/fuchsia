// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/zx/event.h>
#include <lib/zx/stream.h>
#include <lib/zx/time.h>
#include <lib/zx/vmo.h>
#include <limits.h>
#include <stdio.h>
#include <unistd.h>
#include <zircon/syscalls/object.h>
#include <zircon/system/public/zircon/syscalls.h>
#include <zircon/system/utest/core/pager/userpager.h>
#include <zircon/types.h>

#include <numeric>
#include <string>
#include <thread>
#include <vector>

#include <zxtest/zxtest.h>

extern "C" __WEAK zx_handle_t get_root_resource(void);

namespace {

// This value corresponds to `VmObject::LookupInfo::kMaxPages`
static constexpr uint64_t kMaxPagesBatch = 16;

void CheckRights(const zx::stream& stream, zx_rights_t expected_rights, const char* message) {
  zx_info_handle_basic_t info = {};
  EXPECT_OK(stream.get_info(ZX_INFO_HANDLE_BASIC, &info, sizeof(info), nullptr, nullptr));
  printf("CheckRights: %s\n", message);
  EXPECT_EQ(expected_rights, info.rights);
}

TEST(StreamTestCase, Create) {
  zx_handle_t raw_stream = ZX_HANDLE_INVALID;
  ASSERT_EQ(ZX_ERR_BAD_HANDLE, zx_stream_create(0, ZX_HANDLE_INVALID, 0, &raw_stream));

  zx::event event;
  ASSERT_OK(zx::event::create(0, &event));
  ASSERT_EQ(ZX_ERR_WRONG_TYPE, zx_stream_create(0, event.get(), 0, &raw_stream));

  zx::vmo vmo;
  ASSERT_OK(zx::vmo::create(zx_system_get_page_size() * 4, 0, &vmo));
  size_t content_size = 0u;
  ASSERT_OK(vmo.set_property(ZX_PROP_VMO_CONTENT_SIZE, &content_size, sizeof(content_size)));

  static_assert(!(ZX_DEFAULT_STREAM_RIGHTS & ZX_RIGHT_WRITE),
                "Streams are not writable by default");
  static_assert(!(ZX_DEFAULT_STREAM_RIGHTS & ZX_RIGHT_READ), "Streams are not readable by default");

  zx::stream stream;

  ASSERT_EQ(ZX_ERR_INVALID_ARGS, zx::stream::create(-42, vmo, 0, &stream));

  ASSERT_OK(zx::stream::create(0, vmo, 0, &stream));
  CheckRights(stream, ZX_DEFAULT_STREAM_RIGHTS, "Default");

  ASSERT_OK(zx::stream::create(ZX_STREAM_MODE_READ, vmo, 0, &stream));
  CheckRights(stream, ZX_DEFAULT_STREAM_RIGHTS | ZX_RIGHT_READ, "ZX_STREAM_MODE_READ");

  ASSERT_OK(zx::stream::create(ZX_STREAM_MODE_WRITE, vmo, 0, &stream));
  CheckRights(stream, ZX_DEFAULT_STREAM_RIGHTS | ZX_RIGHT_WRITE, "ZX_STREAM_MODE_WRITE");

  ASSERT_OK(zx::stream::create(ZX_STREAM_MODE_READ | ZX_STREAM_MODE_WRITE, vmo, 0, &stream));
  CheckRights(stream, ZX_DEFAULT_STREAM_RIGHTS | ZX_RIGHT_READ | ZX_RIGHT_WRITE,
              "ZX_STREAM_MODE_READ | ZX_STREAM_MODE_WRITE");

  ASSERT_OK(zx::stream::create(ZX_STREAM_MODE_APPEND, vmo, 0, &stream));
  CheckRights(stream, ZX_DEFAULT_STREAM_RIGHTS, "ZX_STREAM_MODE_APPEND");

  ASSERT_OK(zx::stream::create(ZX_STREAM_MODE_READ | ZX_STREAM_MODE_APPEND, vmo, 0, &stream));
  CheckRights(stream, ZX_DEFAULT_STREAM_RIGHTS | ZX_RIGHT_READ,
              "ZX_STREAM_MODE_READ | ZX_STREAM_MODE_APPEND");

  ASSERT_OK(zx::stream::create(ZX_STREAM_MODE_WRITE | ZX_STREAM_MODE_APPEND, vmo, 0, &stream));
  CheckRights(stream, ZX_DEFAULT_STREAM_RIGHTS | ZX_RIGHT_WRITE,
              "ZX_STREAM_MODE_WRITE | ZX_STREAM_MODE_APPEND");

  ASSERT_OK(zx::stream::create(ZX_STREAM_MODE_READ | ZX_STREAM_MODE_WRITE | ZX_STREAM_MODE_APPEND,
                               vmo, 0, &stream));
  CheckRights(stream, ZX_DEFAULT_STREAM_RIGHTS | ZX_RIGHT_READ | ZX_RIGHT_WRITE,
              "ZX_STREAM_MODE_READ | ZX_STREAM_MODE_WRITE | ZX_STREAM_MODE_APPEND");

  {
    zx::vmo read_only;
    vmo.duplicate(ZX_RIGHT_READ, &read_only);

    ASSERT_OK(zx::stream::create(0, read_only, 0, &stream));
    CheckRights(stream, ZX_DEFAULT_STREAM_RIGHTS, "read_only: Default");

    ASSERT_OK(zx::stream::create(ZX_STREAM_MODE_READ, read_only, 0, &stream));
    CheckRights(stream, ZX_DEFAULT_STREAM_RIGHTS | ZX_RIGHT_READ, "read_only: ZX_STREAM_MODE_READ");

    ASSERT_EQ(ZX_ERR_ACCESS_DENIED,
              zx::stream::create(ZX_STREAM_MODE_WRITE, read_only, 0, &stream));
    ASSERT_EQ(ZX_ERR_ACCESS_DENIED, zx::stream::create(ZX_STREAM_MODE_READ | ZX_STREAM_MODE_WRITE,
                                                       read_only, 0, &stream));
  }

  {
    zx::vmo write_only;
    vmo.duplicate(ZX_RIGHT_WRITE, &write_only);

    ASSERT_OK(zx::stream::create(0, write_only, 0, &stream));
    CheckRights(stream, ZX_DEFAULT_STREAM_RIGHTS, "write_only: Default");

    ASSERT_EQ(ZX_ERR_ACCESS_DENIED,
              zx::stream::create(ZX_STREAM_MODE_READ, write_only, 0, &stream));

    ASSERT_OK(zx::stream::create(ZX_STREAM_MODE_WRITE, write_only, 0, &stream));
    CheckRights(stream, ZX_DEFAULT_STREAM_RIGHTS | ZX_RIGHT_WRITE,
                "write_only: ZX_STREAM_MODE_WRITE");

    ASSERT_EQ(ZX_ERR_ACCESS_DENIED, zx::stream::create(ZX_STREAM_MODE_READ | ZX_STREAM_MODE_WRITE,
                                                       write_only, 0, &stream));
  }

  {
    zx::vmo none;
    vmo.duplicate(0, &none);

    ASSERT_OK(zx::stream::create(0, none, 0, &stream));
    CheckRights(stream, ZX_DEFAULT_STREAM_RIGHTS, "none: Default");

    ASSERT_EQ(ZX_ERR_ACCESS_DENIED, zx::stream::create(ZX_STREAM_MODE_READ, none, 0, &stream));
    ASSERT_EQ(ZX_ERR_ACCESS_DENIED, zx::stream::create(ZX_STREAM_MODE_WRITE, none, 0, &stream));
    ASSERT_EQ(ZX_ERR_ACCESS_DENIED,
              zx::stream::create(ZX_STREAM_MODE_READ | ZX_STREAM_MODE_WRITE, none, 0, &stream));
  }
}

TEST(StreamTestCase, Seek) {
  zx::vmo vmo;
  ASSERT_OK(zx::vmo::create(zx_system_get_page_size() * 4, 0, &vmo));
  size_t content_size = 42u;
  ASSERT_OK(vmo.set_property(ZX_PROP_VMO_CONTENT_SIZE, &content_size, sizeof(content_size)));

  zx::stream stream;
  zx_off_t result = 81u;

  ASSERT_OK(zx::stream::create(0, vmo, 0, &stream));
  ASSERT_EQ(ZX_ERR_ACCESS_DENIED, stream.seek(ZX_STREAM_SEEK_ORIGIN_START, 0, &result));

  ASSERT_OK(zx::stream::create(ZX_STREAM_MODE_READ, vmo, 9, &stream));
  ASSERT_OK(stream.seek(ZX_STREAM_SEEK_ORIGIN_CURRENT, 0, &result));
  EXPECT_EQ(9u, result);

  ASSERT_OK(zx::stream::create(ZX_STREAM_MODE_WRITE, vmo, 518, &stream));
  ASSERT_OK(stream.seek(ZX_STREAM_SEEK_ORIGIN_CURRENT, 0, &result));
  EXPECT_EQ(518u, result);

  ASSERT_OK(zx::stream::create(ZX_STREAM_MODE_READ | ZX_STREAM_MODE_WRITE, vmo, 0, &stream));

  ASSERT_EQ(ZX_ERR_INVALID_ARGS, stream.seek(34893, 12, &result));
  ASSERT_EQ(ZX_ERR_INVALID_ARGS, stream.seek(ZX_STREAM_SEEK_ORIGIN_START, -10, &result));
  ASSERT_OK(stream.seek(ZX_STREAM_SEEK_ORIGIN_START, 10, &result));
  EXPECT_EQ(10u, result);
  ASSERT_OK(stream.seek(ZX_STREAM_SEEK_ORIGIN_START, 12, &result));
  EXPECT_EQ(12u, result);

  ASSERT_EQ(ZX_ERR_INVALID_ARGS, stream.seek(ZX_STREAM_SEEK_ORIGIN_CURRENT, -21, &result));
  ASSERT_OK(stream.seek(ZX_STREAM_SEEK_ORIGIN_CURRENT, 3, &result));
  EXPECT_EQ(15u, result);
  ASSERT_OK(stream.seek(ZX_STREAM_SEEK_ORIGIN_CURRENT, -15, &result));
  EXPECT_EQ(0u, result);
  ASSERT_OK(stream.seek(ZX_STREAM_SEEK_ORIGIN_CURRENT, INT64_MAX, &result));
  EXPECT_EQ(static_cast<zx_off_t>(INT64_MAX), result);

  ASSERT_OK(stream.seek(ZX_STREAM_SEEK_ORIGIN_CURRENT, 1038, &result));
  EXPECT_EQ(static_cast<zx_off_t>(INT64_MAX) + 1038, result);
  ASSERT_EQ(ZX_ERR_INVALID_ARGS, stream.seek(ZX_STREAM_SEEK_ORIGIN_CURRENT, INT64_MAX, &result));

  ASSERT_OK(stream.seek(ZX_STREAM_SEEK_ORIGIN_END, 0, &result));
  EXPECT_EQ(content_size, result);
  ASSERT_OK(stream.seek(ZX_STREAM_SEEK_ORIGIN_END, -11, &result));
  EXPECT_EQ(31u, result);
  ASSERT_OK(stream.seek(ZX_STREAM_SEEK_ORIGIN_END, -13, &result));
  EXPECT_EQ(29u, result);
  ASSERT_OK(stream.seek(ZX_STREAM_SEEK_ORIGIN_END, -content_size, &result));
  EXPECT_EQ(0u, result);
  ASSERT_OK(stream.seek(ZX_STREAM_SEEK_ORIGIN_END, 24, &result));
  EXPECT_EQ(66u, result);
  ASSERT_EQ(ZX_ERR_INVALID_ARGS, stream.seek(ZX_STREAM_SEEK_ORIGIN_END, -1238, &result));

  content_size = UINT64_MAX;
  ASSERT_OK(vmo.set_property(ZX_PROP_VMO_CONTENT_SIZE, &content_size, sizeof(content_size)));
  ASSERT_OK(stream.seek(ZX_STREAM_SEEK_ORIGIN_END, -11, &result));
  EXPECT_EQ(UINT64_MAX - 11u, result);
  ASSERT_EQ(ZX_ERR_INVALID_ARGS, stream.seek(ZX_STREAM_SEEK_ORIGIN_END, 5, &result));

  ASSERT_OK(stream.seek(ZX_STREAM_SEEK_ORIGIN_START, 0, nullptr));
}

const char kAlphabet[] = "abcdefghijklmnopqrstuvwxyz";

TEST(StreamTestCase, ReadV) {
  zx::vmo vmo;
  ASSERT_OK(zx::vmo::create(zx_system_get_page_size(), 0, &vmo));
  ASSERT_OK(vmo.write(kAlphabet, 0u, strlen(kAlphabet)));
  size_t content_size = 26u;
  ASSERT_OK(vmo.set_property(ZX_PROP_VMO_CONTENT_SIZE, &content_size, sizeof(content_size)));

  zx::stream stream;
  char buffer[16] = {};
  zx_iovec_t vec = {
      .buffer = buffer,
      .capacity = sizeof(buffer),
  };
  size_t actual = 42u;

  ASSERT_OK(zx::stream::create(0, vmo, 0, &stream));
  ASSERT_EQ(ZX_ERR_ACCESS_DENIED, stream.readv(0, &vec, 1, &actual));

  ASSERT_OK(zx::stream::create(ZX_STREAM_MODE_READ, vmo, 0, &stream));
  vec.capacity = 7u;
  ASSERT_OK(stream.readv(0, &vec, 1, &actual));
  EXPECT_EQ(7u, actual);
  EXPECT_STREQ("abcdefg", buffer);
  memset(buffer, 0, sizeof(buffer));

  ASSERT_EQ(ZX_ERR_INVALID_ARGS, stream.readv(24098, &vec, 1, &actual));
  ASSERT_EQ(ZX_ERR_INVALID_ARGS, stream.readv(0, nullptr, 1, &actual));
  ASSERT_EQ(ZX_ERR_INVALID_ARGS, stream.readv(0, nullptr, 0, &actual));

  vec.capacity = 3u;
  ASSERT_OK(stream.readv(0, &vec, 1, nullptr));
  EXPECT_STREQ("hij", buffer);
  memset(buffer, 0, sizeof(buffer));

  vec.buffer = nullptr;
  vec.capacity = 7u;
  ASSERT_EQ(ZX_ERR_NOT_FOUND, stream.readv(0, &vec, 1, &actual));
  vec.buffer = buffer;

  const size_t kVectorCount = 7;
  zx_iovec_t multivec[kVectorCount] = {};
  for (size_t i = 0; i < kVectorCount; ++i) {
    multivec[i].buffer = buffer;
    multivec[i].capacity = INT64_MAX;
  }

  ASSERT_EQ(ZX_ERR_INVALID_ARGS, stream.readv(0, multivec, kVectorCount, &actual));

  vec.capacity = sizeof(buffer);
  ASSERT_OK(stream.readv(0, &vec, 1, &actual));
  memset(buffer, 0, sizeof(buffer));

  content_size = 6u;
  ASSERT_OK(vmo.set_property(ZX_PROP_VMO_CONTENT_SIZE, &content_size, sizeof(content_size)));

  vec.capacity = 3u;
  actual = 42u;
  ASSERT_OK(stream.readv(0, &vec, 1, &actual));
  EXPECT_EQ(0u, actual);
  memset(buffer, 0, sizeof(buffer));

  ASSERT_OK(stream.seek(ZX_STREAM_SEEK_ORIGIN_START, 0, nullptr));
  vec.capacity = 12u;
  actual = 42u;
  ASSERT_OK(stream.readv(0, &vec, 1, &actual));
  EXPECT_EQ(6u, actual);
  EXPECT_STREQ("abcdef", buffer);
  memset(buffer, 0, sizeof(buffer));

  content_size = 26u;
  ASSERT_OK(vmo.set_property(ZX_PROP_VMO_CONTENT_SIZE, &content_size, sizeof(content_size)));

  for (size_t i = 0; i < kVectorCount; ++i) {
    multivec[i].buffer = &buffer[i];
    multivec[i].capacity = 1;
  }

  ASSERT_OK(stream.seek(ZX_STREAM_SEEK_ORIGIN_START, 0, nullptr));
  ASSERT_OK(stream.readv(0, multivec, kVectorCount, &actual));
  EXPECT_EQ(kVectorCount, actual);
  EXPECT_STREQ("abcdef", buffer);
  memset(buffer, 0, sizeof(buffer));
}

std::string GetData(const zx::vmo& vmo) {
  std::vector<char> buffer(zx_system_get_page_size(), '\0');
  EXPECT_OK(vmo.read(buffer.data(), 0, buffer.size()));
  return std::string(buffer.data());
}

TEST(StreamTestCase, WriteV) {
  zx::vmo vmo;
  ASSERT_OK(zx::vmo::create(zx_system_get_page_size(), 0, &vmo));
  ASSERT_OK(vmo.write(kAlphabet, 0u, strlen(kAlphabet)));
  size_t content_size = 26u;
  ASSERT_OK(vmo.set_property(ZX_PROP_VMO_CONTENT_SIZE, &content_size, sizeof(content_size)));

  zx::stream stream;
  char buffer[17] = "0123456789ABCDEF";
  zx_iovec_t vec = {
      .buffer = buffer,
      .capacity = sizeof(buffer),
  };
  size_t actual = 42u;

  ASSERT_OK(zx::stream::create(0, vmo, 0, &stream));
  ASSERT_EQ(ZX_ERR_ACCESS_DENIED, stream.writev(0, &vec, 1, &actual));

  ASSERT_OK(zx::stream::create(ZX_STREAM_MODE_WRITE, vmo, 0, &stream));
  vec.capacity = 7u;
  ASSERT_OK(stream.writev(0, &vec, 1, &actual));
  EXPECT_EQ(7u, actual);
  EXPECT_STREQ("0123456hijklmnopqrstuvwxyz", GetData(vmo).c_str());
  ASSERT_OK(vmo.write(kAlphabet, 0u, strlen(kAlphabet)));

  ASSERT_EQ(ZX_ERR_INVALID_ARGS, stream.writev(24098, &vec, 1, &actual));
  ASSERT_EQ(ZX_ERR_INVALID_ARGS, stream.writev(0, nullptr, 1, &actual));
  ASSERT_EQ(ZX_ERR_INVALID_ARGS, stream.writev(0, nullptr, 0, &actual));

  vec.capacity = 3u;
  ASSERT_OK(stream.writev(0, &vec, 1, nullptr));
  EXPECT_STREQ("abcdefg012klmnopqrstuvwxyz", GetData(vmo).c_str());
  ASSERT_OK(vmo.write(kAlphabet, 0u, strlen(kAlphabet)));

  vec.buffer = nullptr;
  vec.capacity = 7u;
  ASSERT_EQ(ZX_ERR_NOT_FOUND, stream.writev(0, &vec, 1, &actual));
  vec.buffer = buffer;

  const size_t kVectorCount = 7;
  zx_iovec_t multivec[kVectorCount] = {};
  for (size_t i = 0; i < kVectorCount; ++i) {
    multivec[i].buffer = buffer;
    multivec[i].capacity = INT64_MAX;
  }

  ASSERT_EQ(ZX_ERR_INVALID_ARGS, stream.writev(0, multivec, kVectorCount, &actual));

  for (size_t i = 0; i < kVectorCount; ++i) {
    multivec[kVectorCount - i - 1].buffer = &buffer[i];
    multivec[kVectorCount - i - 1].capacity = 1;
  }

  ASSERT_OK(stream.seek(ZX_STREAM_SEEK_ORIGIN_START, 0, nullptr));
  ASSERT_OK(stream.writev(0, multivec, kVectorCount, &actual));
  EXPECT_EQ(kVectorCount, actual);
  EXPECT_STREQ("6543210hijklmnopqrstuvwxyz", GetData(vmo).c_str());
  ASSERT_OK(vmo.write(kAlphabet, 0u, strlen(kAlphabet)));
}

size_t GetContentSize(const zx::vmo& vmo) {
  size_t content_size = 45684651u;
  EXPECT_OK(vmo.get_property(ZX_PROP_VMO_CONTENT_SIZE, &content_size, sizeof(content_size)));
  return content_size;
}

TEST(StreamTestCase, WriteExtendsContentSize) {
  zx::vmo vmo;
  ASSERT_OK(zx::vmo::create(zx_system_get_page_size(), 0, &vmo));
  ASSERT_OK(vmo.write(kAlphabet, 0u, strlen(kAlphabet)));
  size_t content_size = 3u;
  ASSERT_OK(vmo.set_property(ZX_PROP_VMO_CONTENT_SIZE, &content_size, sizeof(content_size)));

  zx::stream stream;
  char buffer[17] = "0123456789ABCDEF";
  zx_iovec_t vec = {
      .buffer = buffer,
      .capacity = sizeof(buffer),
  };
  size_t actual = 42u;

  ASSERT_OK(zx::stream::create(ZX_STREAM_MODE_WRITE, vmo, 0, &stream));
  vec.capacity = 7u;
  ASSERT_OK(stream.writev(0, &vec, 1, &actual));
  EXPECT_EQ(7u, actual);
  EXPECT_STREQ("0123456", GetData(vmo).c_str());
  EXPECT_EQ(7u, GetContentSize(vmo));
  ASSERT_OK(vmo.write(kAlphabet, 0u, strlen(kAlphabet)));

  vec.capacity = 2u;
  ASSERT_OK(stream.writev(0, &vec, 1, &actual));
  EXPECT_EQ(2u, actual);
  EXPECT_STREQ("abcdefg01jklmnopqrstuvwxyz", GetData(vmo).c_str());
  EXPECT_EQ(9u, GetContentSize(vmo));
  ASSERT_OK(vmo.write(kAlphabet, 0u, strlen(kAlphabet)));

  ASSERT_OK(stream.seek(ZX_STREAM_SEEK_ORIGIN_START, 0, nullptr));

  vec.capacity = 10u;
  for (size_t i = 1; i * 10 < zx_system_get_page_size(); ++i) {
    ASSERT_OK(stream.writev(0, &vec, 1, &actual));
    EXPECT_EQ(10u, actual);
  }
  EXPECT_EQ(4090u, GetContentSize(vmo));

  actual = 9823u;
  EXPECT_EQ(ZX_OK, stream.writev(0, &vec, 1, &actual));
  EXPECT_EQ(6u, actual);
  EXPECT_EQ(4096u, GetContentSize(vmo));

  char scratch[17] = {};
  ASSERT_OK(vmo.read(scratch, 4090u, 6u));
  EXPECT_STREQ("012345", scratch);

  actual = 9823u;
  ASSERT_EQ(ZX_ERR_NO_SPACE, stream.writev(0, &vec, 1, &actual));
  EXPECT_EQ(4096u, GetContentSize(vmo));
}

TEST(StreamTestCase, WriteExtendsVMOSize) {
  zx::vmo vmo;
  ASSERT_OK(zx::vmo::create(zx_system_get_page_size(), ZX_VMO_RESIZABLE, &vmo));
  size_t content_size = 0u;
  ASSERT_OK(vmo.set_property(ZX_PROP_VMO_CONTENT_SIZE, &content_size, sizeof(content_size)));

  zx::stream stream;
  char buffer[17] = "0123456789ABCDEF";
  zx_iovec_t vec = {
      .buffer = buffer,
      .capacity = sizeof(buffer),
  };
  size_t actual = 42u;

  ASSERT_OK(zx::stream::create(ZX_STREAM_MODE_WRITE, vmo, 0, &stream));
  vec.capacity = 10u;
  for (size_t i = 1; i * 10 < zx_system_get_page_size(); ++i) {
    ASSERT_OK(stream.writev(0, &vec, 1, &actual));
    EXPECT_EQ(10u, actual);
  }
  EXPECT_EQ(4090u, GetContentSize(vmo));

  actual = 9823u;
  ASSERT_OK(stream.writev(0, &vec, 1, &actual));
  EXPECT_EQ(10u, actual);
  EXPECT_EQ(4100u, GetContentSize(vmo));

  uint64_t vmo_size = 839u;
  ASSERT_OK(vmo.get_size(&vmo_size));
  EXPECT_EQ(zx_system_get_page_size() * 2, vmo_size);

  vec.capacity = UINT64_MAX;
  actual = 5423u;
  ASSERT_EQ(ZX_ERR_FILE_BIG, stream.writev(0, &vec, 1, &actual));

  ASSERT_OK(vmo.get_size(&vmo_size));
  EXPECT_EQ(zx_system_get_page_size() * 2, vmo_size);
}

TEST(StreamTestCase, ReadVAt) {
  zx::vmo vmo;
  ASSERT_OK(zx::vmo::create(zx_system_get_page_size(), 0, &vmo));
  ASSERT_OK(vmo.write(kAlphabet, 0u, strlen(kAlphabet)));
  size_t content_size = 26u;
  ASSERT_OK(vmo.set_property(ZX_PROP_VMO_CONTENT_SIZE, &content_size, sizeof(content_size)));

  zx::stream stream;
  char buffer[16] = {};
  zx_iovec_t vec = {
      .buffer = buffer,
      .capacity = sizeof(buffer),
  };
  size_t actual = 42u;

  ASSERT_OK(zx::stream::create(ZX_STREAM_MODE_READ, vmo, 0, &stream));
  vec.capacity = 7u;
  ASSERT_OK(stream.readv_at(0, 24u, &vec, 1, &actual));
  EXPECT_EQ(2u, actual);
  EXPECT_STREQ("yz", buffer);
  memset(buffer, 0, sizeof(buffer));

  zx_off_t seek = 39u;
  ASSERT_OK(stream.seek(ZX_STREAM_SEEK_ORIGIN_CURRENT, 0, &seek));
  EXPECT_EQ(0u, seek);

  ASSERT_OK(stream.readv_at(0, 36u, &vec, 1, &actual));
  EXPECT_EQ(0u, actual);
  EXPECT_STREQ("", buffer);
  memset(buffer, 0, sizeof(buffer));

  ASSERT_OK(stream.readv_at(0, 3645651u, &vec, 1, &actual));
  EXPECT_EQ(0u, actual);
  EXPECT_STREQ("", buffer);
  memset(buffer, 0, sizeof(buffer));
}

TEST(StreamTestCase, WriteVAt) {
  zx::vmo vmo;
  ASSERT_OK(zx::vmo::create(zx_system_get_page_size(), 0, &vmo));
  ASSERT_OK(vmo.write(kAlphabet, 0u, strlen(kAlphabet)));
  size_t content_size = 26u;
  ASSERT_OK(vmo.set_property(ZX_PROP_VMO_CONTENT_SIZE, &content_size, sizeof(content_size)));

  zx::stream stream;
  char buffer[17] = "0123456789ABCDEF";
  zx_iovec_t vec = {
      .buffer = buffer,
      .capacity = sizeof(buffer),
  };
  size_t actual = 42u;

  ASSERT_OK(zx::stream::create(ZX_STREAM_MODE_WRITE, vmo, 0, &stream));
  vec.capacity = 3u;
  ASSERT_OK(stream.writev_at(0, 7, &vec, 1, &actual));
  EXPECT_EQ(3u, actual);
  EXPECT_STREQ("abcdefg012klmnopqrstuvwxyz", GetData(vmo).c_str());
  ASSERT_OK(vmo.write(kAlphabet, 0u, strlen(kAlphabet)));

  zx_off_t seek = 39u;
  ASSERT_OK(stream.seek(ZX_STREAM_SEEK_ORIGIN_CURRENT, 0, &seek));
  EXPECT_EQ(0u, seek);

  vec.capacity = 10u;
  actual = 9823u;
  ASSERT_EQ(ZX_ERR_NO_SPACE, stream.writev_at(0, 4100u, &vec, 1, &actual));

  ASSERT_OK(zx::vmo::create(zx_system_get_page_size(), ZX_VMO_RESIZABLE, &vmo));
  ASSERT_OK(zx::stream::create(ZX_STREAM_MODE_WRITE, vmo, 0, &stream));

  vec.capacity = 10u;
  actual = 9823u;
  ASSERT_OK(stream.writev_at(0, 4090, &vec, 1, &actual));
  EXPECT_EQ(10u, actual);
  EXPECT_EQ(4100u, GetContentSize(vmo));

  uint64_t vmo_size = 839u;
  ASSERT_OK(vmo.get_size(&vmo_size));
  EXPECT_EQ(zx_system_get_page_size() * 2, vmo_size);

  vec.capacity = UINT64_MAX;
  actual = 5423u;
  ASSERT_EQ(ZX_ERR_FILE_BIG, stream.writev_at(0, 5414u, &vec, 1, &actual));

  ASSERT_OK(vmo.get_size(&vmo_size));
  EXPECT_EQ(zx_system_get_page_size() * 2, vmo_size);
  EXPECT_EQ(4100u, GetContentSize(vmo));

  zx_iovec_t bad_vec = {
      .buffer = nullptr,
      .capacity = 42u,
  };

  actual = 5423u;
  ASSERT_NOT_OK(stream.writev_at(0, 5000u, &bad_vec, 1, &actual));
  ASSERT_EQ(4100u, GetContentSize(vmo));
}

TEST(StreamTestCase, ReadVectorAlias) {
  zx::vmo vmo;
  ASSERT_OK(zx::vmo::create(zx_system_get_page_size(), 0, &vmo));
  ASSERT_OK(vmo.write(kAlphabet, 0u, strlen(kAlphabet)));
  size_t content_size = 26u;
  ASSERT_OK(vmo.set_property(ZX_PROP_VMO_CONTENT_SIZE, &content_size, sizeof(content_size)));

  zx::stream stream;
  const size_t kVectorCount = 7;
  zx_iovec_t multivec[kVectorCount] = {};
  for (size_t i = 0; i < kVectorCount; ++i) {
    multivec[i].buffer = multivec;  // Notice the alias.
    multivec[i].capacity = sizeof(multivec);
  }

  ASSERT_OK(zx::stream::create(ZX_STREAM_MODE_READ, vmo, 0, &stream));
  size_t actual = 42u;
  EXPECT_OK(stream.readv(0, multivec, kVectorCount, &actual));
  ASSERT_EQ(26u, actual);
}

TEST(StreamTestCase, Append) {
  zx::vmo vmo;
  ASSERT_OK(zx::vmo::create(zx_system_get_page_size(), 0, &vmo));
  ASSERT_OK(vmo.write(kAlphabet, 0u, strlen(kAlphabet)));
  size_t content_size = 26u;
  ASSERT_OK(vmo.set_property(ZX_PROP_VMO_CONTENT_SIZE, &content_size, sizeof(content_size)));

  zx::stream stream;
  char buffer[17] = "0123456789ABCDEF";
  zx_iovec_t vec = {
      .buffer = buffer,
      .capacity = sizeof(buffer),
  };

  ASSERT_OK(zx::stream::create(ZX_STREAM_MODE_WRITE, vmo, 0, &stream));
  {
    zx_info_stream_t info{};
    ASSERT_OK(stream.get_info(ZX_INFO_STREAM, &info, sizeof(info), nullptr, nullptr));
    EXPECT_EQ(ZX_STREAM_MODE_WRITE, info.options);
    EXPECT_EQ(0u, info.seek);
    EXPECT_EQ(26u, info.content_size);
  }

  vec.capacity = 7u;
  size_t actual = 42u;
  ASSERT_OK(stream.writev(ZX_STREAM_APPEND, &vec, 1, &actual));
  EXPECT_EQ(7u, actual);
  EXPECT_STREQ("abcdefghijklmnopqrstuvwxyz0123456", GetData(vmo).c_str());

  {
    zx_info_stream_t info{};
    ASSERT_OK(stream.get_info(ZX_INFO_STREAM, &info, sizeof(info), nullptr, nullptr));
    EXPECT_EQ(ZX_STREAM_MODE_WRITE, info.options);
    EXPECT_EQ(33u, info.seek);
    EXPECT_EQ(33u, info.content_size);

    vec.capacity = 26u;
    for (size_t size = info.content_size; size + vec.capacity < zx_system_get_page_size();
         size += vec.capacity) {
      ASSERT_OK(stream.writev(ZX_STREAM_APPEND, &vec, 1, &actual));
      EXPECT_EQ(vec.capacity, actual);
    }
  }

  {
    zx_info_stream_t info{};
    ASSERT_OK(stream.get_info(ZX_INFO_STREAM, &info, sizeof(info), nullptr, nullptr));
    EXPECT_GT(zx_system_get_page_size(), info.content_size);

    EXPECT_OK(stream.writev(ZX_STREAM_APPEND, &vec, 1, &actual));
    EXPECT_EQ(zx_system_get_page_size() - info.content_size, actual);
  }

  ASSERT_EQ(ZX_ERR_NO_SPACE, stream.writev(ZX_STREAM_APPEND, &vec, 1, &actual));

  vec.capacity = UINT64_MAX;
  ASSERT_EQ(ZX_ERR_OUT_OF_RANGE, stream.writev(ZX_STREAM_APPEND, &vec, 1, &actual));
}

TEST(StreamTestCase, WriteVectorWithStreamInAppendMode) {
  zx::vmo vmo;
  ASSERT_OK(zx::vmo::create(zx_system_get_page_size(), 0, &vmo));
  ASSERT_OK(vmo.write(kAlphabet, 0u, strlen(kAlphabet)));
  size_t content_size = 26u;
  ASSERT_OK(vmo.set_property(ZX_PROP_VMO_CONTENT_SIZE, &content_size, sizeof(content_size)));

  zx::stream stream;
  char buffer[17] = "0123456789ABCDEF";
  zx_iovec_t vec = {
      .buffer = buffer,
      .capacity = sizeof(buffer),
  };

  ASSERT_OK(zx::stream::create(ZX_STREAM_MODE_WRITE | ZX_STREAM_MODE_APPEND, vmo, 0, &stream));
  {
    zx_info_stream_t info{};
    ASSERT_OK(stream.get_info(ZX_INFO_STREAM, &info, sizeof(info), nullptr, nullptr));
    EXPECT_EQ(ZX_STREAM_MODE_WRITE | ZX_STREAM_MODE_APPEND, info.options);
    EXPECT_EQ(0u, info.seek);
    EXPECT_EQ(26u, info.content_size);
  }

  vec.capacity = 7u;
  size_t actual = 42u;
  ASSERT_OK(stream.writev(0, &vec, 1, &actual));
  EXPECT_EQ(7u, actual);
  EXPECT_STREQ("abcdefghijklmnopqrstuvwxyz0123456", GetData(vmo).c_str());

  {
    zx_info_stream_t info{};
    ASSERT_OK(stream.get_info(ZX_INFO_STREAM, &info, sizeof(info), nullptr, nullptr));
    EXPECT_EQ(ZX_STREAM_MODE_WRITE | ZX_STREAM_MODE_APPEND, info.options);
    EXPECT_EQ(33u, info.seek);
    EXPECT_EQ(33u, info.content_size);

    vec.capacity = 26u;
    for (size_t size = info.content_size; size + vec.capacity < zx_system_get_page_size();
         size += vec.capacity) {
      ASSERT_OK(stream.writev(0, &vec, 1, &actual));
      EXPECT_EQ(vec.capacity, actual);
    }
  }

  {
    zx_info_stream_t info{};
    ASSERT_OK(stream.get_info(ZX_INFO_STREAM, &info, sizeof(info), nullptr, nullptr));
    EXPECT_GT(zx_system_get_page_size(), info.content_size);

    ASSERT_OK(stream.writev(0, &vec, 1, &actual));
    EXPECT_EQ(zx_system_get_page_size() - info.content_size, actual);
  }

  ASSERT_EQ(ZX_ERR_NO_SPACE, stream.writev(0, &vec, 1, nullptr));

  vec.capacity = UINT64_MAX;
  ASSERT_EQ(ZX_ERR_OUT_OF_RANGE, stream.writev(0, &vec, 1, nullptr));
}

TEST(StreamTestCase, PropertyModeAppend) {
  zx::vmo vmo;
  ASSERT_OK(zx::vmo::create(zx_system_get_page_size(), 0, &vmo));
  ASSERT_OK(vmo.set_prop_content_size(0));

  zx::stream stream;
  char buffer[] = "0123456789ABCDEF";
  zx_iovec_t vec = {
      .buffer = buffer,
      .capacity = 16,
  };

  // Create the stream not in append mode.
  ASSERT_OK(zx::stream::create(ZX_STREAM_MODE_WRITE, vmo, 0, &stream));
  ASSERT_OK(stream.writev(0, &vec, 1, nullptr));

  {
    zx_info_stream_t info{};
    ASSERT_OK(stream.get_info(ZX_INFO_STREAM, &info, sizeof(info), nullptr, nullptr));
    EXPECT_FALSE(info.options & ZX_STREAM_MODE_APPEND);
    EXPECT_EQ(16u, info.seek);
    EXPECT_EQ(16u, info.content_size);
    uint8_t mode_append;
    ASSERT_OK(stream.get_prop_mode_append(&mode_append));
    EXPECT_FALSE(mode_append);
  }

  // Switch the stream to append mode.
  ASSERT_OK(stream.set_prop_mode_append(true));
  {
    zx_info_stream_t info{};
    ASSERT_OK(stream.get_info(ZX_INFO_STREAM, &info, sizeof(info), nullptr, nullptr));
    EXPECT_TRUE(info.options & ZX_STREAM_MODE_APPEND);
    uint8_t mode_append;
    ASSERT_OK(stream.get_prop_mode_append(&mode_append));
    EXPECT_TRUE(mode_append);
  }
  ASSERT_OK(stream.seek(ZX_STREAM_SEEK_ORIGIN_START, 10, nullptr));
  ASSERT_OK(stream.writev(0, &vec, 1, nullptr));
  EXPECT_STREQ("0123456789ABCDEF0123456789ABCDEF", GetData(vmo).c_str());

  // Take the stream out of append mode.
  ASSERT_OK(stream.set_prop_mode_append(false));
  {
    zx_info_stream_t info{};
    ASSERT_OK(stream.get_info(ZX_INFO_STREAM, &info, sizeof(info), nullptr, nullptr));
    EXPECT_FALSE(info.options & ZX_STREAM_MODE_APPEND);
    // The previous write appended to the stream despite the seek offset not being at the end of the
    // stream.
    EXPECT_EQ(32u, info.seek);
    EXPECT_EQ(32u, info.content_size);
    uint8_t mode_append;
    ASSERT_OK(stream.get_prop_mode_append(&mode_append));
    EXPECT_FALSE(mode_append);
  }
  ASSERT_OK(stream.seek(ZX_STREAM_SEEK_ORIGIN_START, 10, nullptr));
  ASSERT_OK(stream.writev(0, &vec, 1, nullptr));
  EXPECT_STREQ("01234567890123456789ABCDEFABCDEF", GetData(vmo).c_str());
}

TEST(StreamTestCase, AppendWithMultipleThreads) {
  // kThreadCount threads collectively write the numbers 0 to kBufferSize-1 to the vmo.
  constexpr uint64_t kThreadCount = 4;
  constexpr uint64_t kBufferSize = 256;
  constexpr uint64_t kIterationCount = kBufferSize / kThreadCount;
  zx::vmo vmo;
  ASSERT_OK(zx::vmo::create(zx_system_get_page_size(), 0, &vmo));
  ASSERT_OK(vmo.set_prop_content_size(0));

  std::vector<uint8_t> buffer(kBufferSize, 0);
  std::iota(buffer.begin(), buffer.end(), 0);

  std::vector<std::thread> threads;
  threads.reserve(kThreadCount);
  for (uint64_t thread = 0; thread < kThreadCount; ++thread) {
    threads.emplace_back([&vmo, &buffer, thread]() {
      zx::stream stream;
      ASSERT_OK(zx::stream::create(ZX_STREAM_MODE_WRITE | ZX_STREAM_MODE_APPEND, vmo, 0, &stream));
      for (uint64_t i = 0; i < kIterationCount; ++i) {
        zx_iovec_t vec = {
            .buffer = &buffer[thread * kIterationCount + i],
            .capacity = 1,
        };
        ASSERT_OK(stream.writev(0, &vec, 1, nullptr));
      }
    });
  }
  for (auto& thread : threads) {
    thread.join();
  }

  // With several threads simultaneously appending, the data is likely out of order but none of the
  // appends should have overwritten each other.
  std::vector<uint8_t> vmo_data(kBufferSize, 0);
  ASSERT_OK(vmo.read(vmo_data.data(), 0, kBufferSize));
  std::sort(vmo_data.begin(), vmo_data.end());
  EXPECT_BYTES_EQ(buffer.data(), vmo_data.data(), kBufferSize);
}

TEST(StreamTestCase, ExtendFillsWithZeros) {
  const size_t kPageCount = 6;
  const size_t kVmoSize = zx_system_get_page_size() * kPageCount;
  zx::vmo vmo;
  ASSERT_OK(zx::vmo::create(kVmoSize, 0, &vmo));
  size_t content_size = 0u;
  ASSERT_OK(vmo.set_property(ZX_PROP_VMO_CONTENT_SIZE, &content_size, sizeof(content_size)));

  zx::stream stream;
  ASSERT_OK(zx::stream::create(ZX_STREAM_MODE_WRITE, vmo, 0, &stream));

  char scratch[zx_system_get_page_size()];
  memset(scratch, 'x', sizeof(scratch));

  for (size_t i = 0; i < kPageCount; ++i) {
    ASSERT_OK(vmo.write(scratch, zx_system_get_page_size() * i, sizeof(scratch)));
  }

  char buffer[17] = "0123456789ABCDEF";
  zx_iovec_t vec = {
      .buffer = buffer,
      .capacity = 4,
  };

  size_t actual = 0u;
  ASSERT_OK(stream.writev_at(0, zx_system_get_page_size() * 2 - 2, &vec, 1, &actual));
  ASSERT_EQ(4, actual);

  memset(scratch, 'a', sizeof(scratch));
  ASSERT_OK(vmo.read(scratch, 0, sizeof(scratch)));

  for (size_t i = 0; i < zx_system_get_page_size(); ++i) {
    ASSERT_EQ(0, scratch[i], "The %zu byte should be zero.", i);
  }

  memset(scratch, 'a', sizeof(scratch));
  ASSERT_OK(vmo.read(scratch, zx_system_get_page_size(), sizeof(scratch)));

  for (size_t i = 0; i < zx_system_get_page_size() - 2; ++i) {
    ASSERT_EQ(0, scratch[i], "The %zu byte of the second page should be zero.", i);
  }

  ASSERT_EQ('0', scratch[zx_system_get_page_size() - 2]);
  ASSERT_EQ('1', scratch[zx_system_get_page_size() - 1]);

  memset(scratch, 'a', sizeof(scratch));
  ASSERT_OK(vmo.read(scratch, zx_system_get_page_size() * 2, sizeof(scratch)));

  ASSERT_EQ('2', scratch[0]);
  ASSERT_EQ('3', scratch[1]);
  ASSERT_EQ('x', scratch[2]);
  ASSERT_EQ('x', scratch[3]);

  ASSERT_OK(stream.seek(ZX_STREAM_SEEK_ORIGIN_START, zx_system_get_page_size() * 5 - 2, nullptr));

  actual = 0;
  ASSERT_OK(stream.writev(0, &vec, 1, &actual));
  ASSERT_EQ(4, actual);

  memset(scratch, 'a', sizeof(scratch));
  ASSERT_OK(vmo.read(scratch, zx_system_get_page_size() * 2, sizeof(scratch)));

  ASSERT_EQ('2', scratch[0]);
  ASSERT_EQ('3', scratch[1]);
  ASSERT_EQ(0, scratch[2]);
  ASSERT_EQ(0, scratch[3]);

  memset(scratch, 'a', sizeof(scratch));
  ASSERT_OK(vmo.read(scratch, zx_system_get_page_size() * 3, sizeof(scratch)));

  for (size_t i = 0; i < zx_system_get_page_size(); ++i) {
    ASSERT_EQ(0, scratch[i], "The %zu byte of the third page should be zero.", i);
  }

  memset(scratch, 'a', sizeof(scratch));
  ASSERT_OK(vmo.read(scratch, zx_system_get_page_size() * 4, sizeof(scratch)));

  for (size_t i = 0; i < zx_system_get_page_size() - 2; ++i) {
    ASSERT_EQ(0, scratch[i], "The %zu byte of the fourth page should be zero.", i);
  }

  ASSERT_EQ('0', scratch[zx_system_get_page_size() - 2]);
  ASSERT_EQ('1', scratch[zx_system_get_page_size() - 1]);

  memset(scratch, 'a', sizeof(scratch));
  ASSERT_OK(vmo.read(scratch, zx_system_get_page_size() * 5, sizeof(scratch)));

  ASSERT_EQ('2', scratch[0]);
  ASSERT_EQ('3', scratch[1]);
  ASSERT_EQ('x', scratch[2]);
  ASSERT_EQ('x', scratch[3]);
}

TEST(StreamTestCase, ReadShrinkRace) {
  // This test is slow because of the `WaitForPageRead`. Be careful about the number of iterations.
  constexpr size_t kNumIterations = 10;

  constexpr size_t kInitialVmoSize = 80u;
  constexpr size_t kInitialVmoNumPages =
      fbl::round_up(kInitialVmoSize, ZX_PAGE_SIZE) / ZX_PAGE_SIZE;
  constexpr size_t kTruncateToSize = 0u;

  for (size_t i = 0; i < kNumIterations; ++i) {
    pager_tests::UserPager pager;
    ASSERT_TRUE(pager.Init());

    pager_tests::Vmo* vmo;
    ASSERT_TRUE(pager.CreateVmoWithOptions(kInitialVmoNumPages, ZX_VMO_RESIZABLE, &vmo));

    zx::stream stream;
    ASSERT_OK(zx::stream::create(ZX_STREAM_MODE_READ, vmo->vmo(), 0, &stream));

    // Create a read that intersects with the truncate.
    std::thread read_thread([&] {
      std::array<char, 16u> buffer = {};
      zx_iovec_t vec = {
          .buffer = buffer.data(),
          .capacity = buffer.size(),
      };

      size_t actual = 42u;
      ASSERT_OK(stream.readv(0, &vec, 1, &actual));

      // The read should have happened either before or after the set size, so either nothing or
      // everything should've been read.
      ASSERT_TRUE(actual == 0u || actual == buffer.size());
    });

    std::thread set_size_thread([&] { ASSERT_OK(vmo->vmo().set_size(kTruncateToSize)); });

    // Wait for and supply page read, in case |read_thread| wins. This is inherently a race we want
    // to test, so waiting is the best we can do.
    if (pager.WaitForPageRead(vmo, 0u, 1u, zx::deadline_after(zx::sec(5)).get())) {
      pager.SupplyPages(vmo, 0u, 1u);
    }

    set_size_thread.join();
    read_thread.join();

    // The set size must now be complete.
    uint64_t content_size = 42u;
    ASSERT_OK(vmo->vmo().get_prop_content_size(&content_size));
    EXPECT_EQ(kTruncateToSize, content_size);

    // Reads should be okay and return nothing.
    std::array<char, 16u> buffer = {};
    zx_iovec_t vec = {
        .buffer = buffer.data(),
        .capacity = buffer.size(),
    };
    size_t actual = 42u;
    ASSERT_OK(stream.readv(0, &vec, 1, &actual));
    EXPECT_EQ(0u, actual);
  }
}

TEST(StreamTestCase, WriteShrinkRace) {
  constexpr size_t kNumIterations = 50;
  const size_t kInitialVmoSize = static_cast<size_t>(zx_system_get_page_size()) + 8u;
  const size_t kInitialVmoNumPages =
      fbl::round_up(kInitialVmoSize, zx_system_get_page_size()) / zx_system_get_page_size();
  const size_t kTruncateToSize = static_cast<size_t>(zx_system_get_page_size());
  ASSERT_GT(kInitialVmoSize, kTruncateToSize);

  for (size_t i = 0; i < kNumIterations; ++i) {
    pager_tests::UserPager pager;
    ASSERT_TRUE(pager.Init());

    pager_tests::Vmo* vmo;
    ASSERT_TRUE(pager.CreateVmoWithOptions(kInitialVmoNumPages, ZX_VMO_RESIZABLE, &vmo));

    zx::stream stream;
    ASSERT_OK(zx::stream::create(ZX_STREAM_MODE_WRITE, vmo->vmo(), 0u, &stream));

    pager.SupplyPages(vmo, 0u, kInitialVmoNumPages);

    // Create a write that intersects with the truncate.
    std::thread boundary_write_thread([&] {
      std::array<char, 16u> buffer = {};
      ASSERT_LE(buffer.size(), kInitialVmoSize);
      zx_iovec_t vec = {
          .buffer = buffer.data(),
          .capacity = buffer.size(),
      };

      // Attempt to write the last |buffer.size()| bytes.
      const zx_off_t kOffset = kInitialVmoSize - buffer.size();
      size_t actual = 42u;
      ASSERT_OK(stream.writev_at(0, kOffset, &vec, 1u, &actual));
      ASSERT_EQ(actual, buffer.size());
    });

    // Create a write that should always complete, regardless of truncation.
    std::thread full_write_thread([&] {
      std::array<char, 16> buffer = {};
      ASSERT_LE(buffer.size(), kInitialVmoSize);
      zx_iovec_t vec = {
          .buffer = buffer.data(),
          .capacity = buffer.size(),
      };

      // Attempt to write the first |buffer.size()| bytes.
      size_t actual = 42u;
      ASSERT_OK(stream.writev_at(0, 0u, &vec, 1u, &actual));
      ASSERT_EQ(actual, buffer.size());
    });

    // Simultaneously try to truncate.
    std::thread truncate_thread([&] { ASSERT_OK(vmo->vmo().set_size(kTruncateToSize)); });

    boundary_write_thread.join();
    full_write_thread.join();
    truncate_thread.join();

    // The set size must now be complete.
    // The size will either be |kTruncateToSize| if the truncate happened last or |kInitialVmoSize|
    // if the write happened last.
    uint64_t content_size = 42u;
    ASSERT_OK(vmo->vmo().get_prop_content_size(&content_size));
    ASSERT_TRUE(content_size == kInitialVmoSize || content_size == kTruncateToSize);
  }
}

TEST(StreamTestCase, ReadWriteShrinkRace) {
  constexpr size_t kNumIterations = 500;
  constexpr size_t kInitialVmoSize = (ZX_PAGE_SIZE * 8) + 8u;
  constexpr size_t kInitialVmoNumPages =
      fbl::round_up(kInitialVmoSize, ZX_PAGE_SIZE) / ZX_PAGE_SIZE;
  constexpr size_t kTruncateToSize = ZX_PAGE_SIZE;
  ASSERT_GT(kInitialVmoSize, kTruncateToSize);

  for (size_t i = 0; i < kNumIterations; ++i) {
    pager_tests::UserPager pager;
    ASSERT_TRUE(pager.Init());

    pager_tests::Vmo* vmo;
    ASSERT_TRUE(pager.CreateVmoWithOptions(kInitialVmoNumPages, ZX_VMO_RESIZABLE, &vmo));

    zx::stream stream;
    ASSERT_OK(
        zx::stream::create(ZX_STREAM_MODE_READ | ZX_STREAM_MODE_WRITE, vmo->vmo(), 0u, &stream));

    pager.SupplyPages(vmo, 0u, kInitialVmoNumPages);

    // Create a write that intersects with the truncate.
    std::thread write_thread([&] {
      std::array<char, 16u> buffer = {};
      ASSERT_LE(buffer.size(), kInitialVmoSize);
      zx_iovec_t vec = {
          .buffer = buffer.data(),
          .capacity = buffer.size(),
      };

      // Attempt to write the last |buffer.size()| bytes.
      const zx_off_t kOffset = kInitialVmoSize - buffer.size();
      size_t actual = 42u;
      ASSERT_OK(stream.writev_at(0, kOffset, &vec, 1u, &actual));
      ASSERT_EQ(actual, buffer.size());
    });

    // Simultaneously try to truncate.
    std::thread truncate_thread([&] { ASSERT_OK(vmo->vmo().set_size(kTruncateToSize)); });

    // Create a read that intersects with the truncate.
    std::thread read_thread([&] {
      std::array<char, kInitialVmoSize> buffer = {};
      zx_iovec_t vec = {
          .buffer = buffer.data(),
          .capacity = buffer.size(),
      };

      size_t actual = 42u;
      ASSERT_OK(stream.readv_at(0, 0u, &vec, 1u, &actual));
      // If the write happens after the truncate, the read may see a content size in the range
      // [kTruncateToSize, kInitialVmoSize] because of a partial expanding write updating content
      // size as it progresses.
      ASSERT_TRUE(actual >= kTruncateToSize || actual <= kInitialVmoSize);
    });

    write_thread.join();
    truncate_thread.join();
    read_thread.join();

    // The set size must now be complete.
    // The size will either be |kTruncateToSize| if the truncate happened last or |kInitialVmoSize|
    // if the write happened last.
    uint64_t content_size = 42u;
    ASSERT_OK(vmo->vmo().get_prop_content_size(&content_size));
    ASSERT_TRUE(content_size == kInitialVmoSize || content_size == kTruncateToSize);
  }
}

// Regression test for fxbug.dev/94454. Writing to an offset that requires expansion should not
// result in an overflow when computing the new required VMO size.
TEST(StreamTestCase, ExpandOverflow) {
  zx::vmo vmo;
  ASSERT_OK(zx::vmo::create(zx_system_get_page_size(), ZX_VMO_RESIZABLE, &vmo));

  zx::stream stream;
  ASSERT_OK(zx::stream::create(ZX_STREAM_MODE_WRITE, vmo, 0, &stream));

  char buffer[] = "AAAA";
  zx_iovec_t vec = {
      .buffer = buffer,
      .capacity = 4,
  };

  size_t actual = 0u;
  // This write will require a content size of 0xfffffffffffffffc, which when rounded up to the page
  // boundary to compute the VMO size will overflow. So content expansion should fail.
  EXPECT_EQ(ZX_ERR_OUT_OF_RANGE, stream.writev_at(0, 0xfffffffffffffff8, &vec, 1, &actual));
  EXPECT_EQ(0, actual);

  // Verify the VMO and content sizes.
  uint64_t vmo_size;
  ASSERT_OK(vmo.get_size(&vmo_size));
  EXPECT_EQ(zx_system_get_page_size(), vmo_size);

  uint64_t content_size;
  ASSERT_OK(vmo.get_prop_content_size(&content_size));
  EXPECT_EQ(zx_system_get_page_size(), content_size);

  // Verify that a subsequent resize succeeds.
  EXPECT_OK(vmo.set_size(2 * zx_system_get_page_size()));
  ASSERT_OK(vmo.get_size(&vmo_size));
  EXPECT_EQ(2 * zx_system_get_page_size(), vmo_size);
  ASSERT_OK(vmo.get_prop_content_size(&content_size));
  EXPECT_EQ(2 * zx_system_get_page_size(), content_size);
}

// Tests that content size is updated as soon as bytes are committed to the VMO.
TEST(StreamTestCase, ContentSizeUpdatedOnPartialWrite) {
  constexpr uint64_t kNumPagesToWrite = kMaxPagesBatch * 3;

  pager_tests::UserPager pager;
  ASSERT_TRUE(pager.Init());

  pager_tests::Vmo* vmo;
  ASSERT_TRUE(pager.CreateVmoWithOptions(1, ZX_VMO_RESIZABLE | ZX_VMO_TRAP_DIRTY, &vmo));
  ASSERT_OK(vmo->vmo().set_prop_content_size(0));

  zx::stream stream;
  ASSERT_OK(
      zx::stream::create(ZX_STREAM_MODE_READ | ZX_STREAM_MODE_WRITE, vmo->vmo(), 0u, &stream));

  std::thread write_thread([&] {
    std::vector<char> buffer(kNumPagesToWrite * zx_system_get_page_size(), 'a');
    zx_iovec_t vec = {
        .buffer = buffer.data(),
        .capacity = buffer.size(),
    };
    size_t actual;
    ASSERT_OK(stream.writev(0, &vec, 1, &actual));

    ASSERT_EQ(actual, buffer.size());
  });

  for (uint64_t page_num = 0; page_num < kNumPagesToWrite; page_num += kMaxPagesBatch) {
    const uint64_t num_pages_to_dirty = std::min(kMaxPagesBatch, kNumPagesToWrite - page_num);

    pager.WaitForPageDirty(vmo, page_num, num_pages_to_dirty, ZX_TIME_INFINITE);
    ASSERT_EQ(GetContentSize(vmo->vmo()), page_num * zx_system_get_page_size());
    pager.DirtyPages(vmo, page_num, num_pages_to_dirty);
  }

  write_thread.join();
}

// Tests that resizing a `zx_iovec_t` capacity smaller while a read is using it does not fail.
TEST(StreamTestCase, RaceReadResizeVecSmaller) {
  constexpr size_t kNumIterations = 50;
  constexpr size_t kInitialVecSize = 26;
  constexpr size_t kResizeVecSize = 10;
  constexpr char kInitialBufferChar = '!';

  for (size_t i = 0; i < kNumIterations; ++i) {
    zx::vmo vmo;
    ASSERT_OK(zx::vmo::create(zx_system_get_page_size(), 0, &vmo));
    ASSERT_OK(vmo.write(kAlphabet, 0u, strlen(kAlphabet)));

    std::string buffer(kInitialVecSize, kInitialBufferChar);
    zx_iovec_t vec = {
        .buffer = buffer.data(),
        .capacity = buffer.size(),
    };

    zx::stream stream;
    ASSERT_OK(zx::stream::create(ZX_STREAM_MODE_READ, vmo, 0, &stream));

    std::thread read_thread([&] {
      size_t actual = 42u;
      ASSERT_OK(stream.readv(0, &vec, 1, &actual));

      ASSERT_TRUE(actual == buffer.size() || actual == kResizeVecSize);

      if (actual == kResizeVecSize) {
        std::string spliced = std::string(kAlphabet).substr(0, kResizeVecSize) +
                              std::string(kInitialVecSize - kResizeVecSize, kInitialBufferChar);

        EXPECT_STREQ(spliced.c_str(), buffer.c_str());
      } else {
        EXPECT_STREQ(kAlphabet, GetData(vmo).c_str());
      }
    });

    std::thread resize_thread([&] { vec.capacity = kResizeVecSize; });

    read_thread.join();
    resize_thread.join();
  }
}

// Tests that resizing a `zx_iovec_t` capacity smaller while a write is using it does not fail.
TEST(StreamTestCase, RaceWriteResizeVecSmaller) {
  constexpr size_t kNumIterations = 50;
  constexpr size_t kResizeVecSize = 10;

  for (size_t i = 0; i < kNumIterations; ++i) {
    zx::vmo vmo;
    ASSERT_OK(zx::vmo::create(zx_system_get_page_size(), 0, &vmo));
    ASSERT_OK(vmo.write(kAlphabet, 0u, strlen(kAlphabet)));

    std::string buffer = "ABCDEFGHIJKLMNOPQRSTUVWXYZ";
    zx_iovec_t vec = {
        .buffer = buffer.data(),
        .capacity = buffer.size(),
    };

    zx::stream stream;
    ASSERT_OK(zx::stream::create(ZX_STREAM_MODE_WRITE, vmo, 0, &stream));

    std::thread write_thread([&] {
      size_t actual = 42u;
      ASSERT_OK(stream.writev(0, &vec, 1, &actual));

      ASSERT_TRUE(actual == buffer.size() || actual == kResizeVecSize);

      if (actual == kResizeVecSize) {
        std::string spliced =
            buffer.substr(0, kResizeVecSize) + std::string(kAlphabet).substr(kResizeVecSize);

        EXPECT_STREQ(spliced.c_str(), GetData(vmo).c_str());
      } else {
        EXPECT_STREQ(buffer.c_str(), GetData(vmo).c_str());
      }
    });

    std::thread resize_thread([&] { vec.capacity = kResizeVecSize; });

    write_thread.join();
    resize_thread.join();
  }
}

// Tests that resizing a `zx_iovec_t` capacity larger while a read is using it does not fail.
TEST(StreamTestCase, RaceReadResizeVecLarger) {
  constexpr size_t kNumIterations = 50;
  constexpr size_t kInitialVecSize = 10;
  constexpr size_t kResizeVecSize = 26;
  constexpr char kInitialBufferChar = '!';

  for (size_t i = 0; i < kNumIterations; ++i) {
    zx::vmo vmo;
    ASSERT_OK(zx::vmo::create(zx_system_get_page_size(), 0, &vmo));
    ASSERT_OK(vmo.write(kAlphabet, 0u, strlen(kAlphabet)));

    std::string buffer(kResizeVecSize, kInitialBufferChar);
    zx_iovec_t vec = {
        .buffer = buffer.data(),
        .capacity = kInitialVecSize,
    };

    zx::stream stream;
    ASSERT_OK(zx::stream::create(ZX_STREAM_MODE_READ, vmo, 0, &stream));

    std::thread read_thread([&] {
      size_t actual = 42u;
      ASSERT_OK(stream.readv(0, &vec, 1, &actual));

      ASSERT_TRUE(actual == kInitialVecSize || actual == buffer.size());

      if (actual == kResizeVecSize) {
        EXPECT_STREQ(kAlphabet, buffer.c_str());
      } else {
        std::string spliced = std::string(kAlphabet).substr(0, kInitialVecSize) +
                              std::string(kResizeVecSize - kInitialVecSize, kInitialBufferChar);

        EXPECT_STREQ(spliced.c_str(), buffer.c_str());
      }
    });

    std::thread resize_thread([&] { vec.capacity = kResizeVecSize; });

    read_thread.join();
    resize_thread.join();
  }
}

// Tests that resizing a `zx_iovec_t` capacity larger while a write is using it does not fail.
TEST(StreamTestCase, RaceWriteResizeVecLarger) {
  constexpr size_t kNumIterations = 50;
  constexpr size_t kInitialVecSize = 10;

  for (size_t i = 0; i < kNumIterations; ++i) {
    zx::vmo vmo;
    ASSERT_OK(zx::vmo::create(zx_system_get_page_size(), 0, &vmo));
    ASSERT_OK(vmo.write(kAlphabet, 0u, strlen(kAlphabet)));

    std::string buffer = "ABCDEFGHIJKLMNOPQRSTUVWXYZ";
    zx_iovec_t vec = {
        .buffer = buffer.data(),
        .capacity = kInitialVecSize,
    };

    zx::stream stream;
    ASSERT_OK(zx::stream::create(ZX_STREAM_MODE_WRITE, vmo, 0, &stream));

    std::thread write_thread([&] {
      size_t actual = 42u;
      ASSERT_OK(stream.writev(0, &vec, 1, &actual));

      ASSERT_TRUE(actual == kInitialVecSize || actual == buffer.size());

      if (actual == kInitialVecSize) {
        std::string spliced =
            buffer.substr(0, kInitialVecSize) + std::string(kAlphabet).substr(kInitialVecSize);

        EXPECT_STREQ(spliced.c_str(), GetData(vmo).c_str());
      } else {
        EXPECT_STREQ(buffer.c_str(), GetData(vmo).c_str());
      }
    });

    std::thread resize_thread([&] { vec.capacity = buffer.size(); });

    write_thread.join();
    resize_thread.join();
  }
}

}  // namespace
