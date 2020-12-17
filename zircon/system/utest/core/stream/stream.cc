// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/zx/event.h>
#include <lib/zx/stream.h>
#include <lib/zx/vmo.h>
#include <limits.h>
#include <stdio.h>
#include <zircon/syscalls/object.h>

#include <zxtest/zxtest.h>

extern "C" __WEAK zx_handle_t get_root_resource(void);

namespace {

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
  ASSERT_OK(zx::vmo::create(PAGE_SIZE * 4, 0, &vmo));

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
  ASSERT_OK(zx::vmo::create(PAGE_SIZE * 4, 0, &vmo));
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
  ASSERT_OK(zx::vmo::create(PAGE_SIZE, 0, &vmo));
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
  EXPECT_STR_EQ("abcdefg", buffer);
  memset(buffer, 0, sizeof(buffer));

  ASSERT_EQ(ZX_ERR_INVALID_ARGS, stream.readv(24098, &vec, 1, &actual));
  ASSERT_EQ(ZX_ERR_INVALID_ARGS, stream.readv(0, nullptr, 1, &actual));
  ASSERT_EQ(ZX_ERR_INVALID_ARGS, stream.readv(0, nullptr, 0, &actual));

  vec.capacity = 3u;
  ASSERT_OK(stream.readv(0, &vec, 1, nullptr));
  EXPECT_STR_EQ("hij", buffer);
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
  EXPECT_STR_EQ("abcdef", buffer);
  memset(buffer, 0, sizeof(buffer));

  content_size = 26u;
  ASSERT_OK(vmo.set_property(ZX_PROP_VMO_CONTENT_SIZE, &content_size, sizeof(content_size)));

  for (size_t i = 0; i < kVectorCount; ++i) {
    multivec[kVectorCount - i - 1].buffer = &buffer[i];
    multivec[kVectorCount - i - 1].capacity = 1;
  }

  ASSERT_OK(stream.seek(ZX_STREAM_SEEK_ORIGIN_START, 0, nullptr));
  ASSERT_OK(stream.readv(0, multivec, kVectorCount, &actual));
  EXPECT_EQ(kVectorCount, actual);
  EXPECT_STR_EQ("gfedcba", buffer);
  memset(buffer, 0, sizeof(buffer));
}

std::string GetData(const zx::vmo& vmo) {
  char buffer[PAGE_SIZE] = {};
  EXPECT_OK(vmo.read(buffer, 0, sizeof(buffer)));
  return std::string(buffer);
}

TEST(StreamTestCase, WriteV) {
  zx::vmo vmo;
  ASSERT_OK(zx::vmo::create(PAGE_SIZE, 0, &vmo));
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
  EXPECT_STR_EQ("0123456hijklmnopqrstuvwxyz", GetData(vmo).c_str());
  ASSERT_OK(vmo.write(kAlphabet, 0u, strlen(kAlphabet)));

  ASSERT_EQ(ZX_ERR_INVALID_ARGS, stream.writev(24098, &vec, 1, &actual));
  ASSERT_EQ(ZX_ERR_INVALID_ARGS, stream.writev(0, nullptr, 1, &actual));
  ASSERT_EQ(ZX_ERR_INVALID_ARGS, stream.writev(0, nullptr, 0, &actual));

  vec.capacity = 3u;
  ASSERT_OK(stream.writev(0, &vec, 1, nullptr));
  EXPECT_STR_EQ("abcdefg012klmnopqrstuvwxyz", GetData(vmo).c_str());
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
  EXPECT_STR_EQ("6543210hijklmnopqrstuvwxyz", GetData(vmo).c_str());
  ASSERT_OK(vmo.write(kAlphabet, 0u, strlen(kAlphabet)));
}

size_t GetContentSize(const zx::vmo& vmo) {
  size_t content_size = 45684651u;
  EXPECT_OK(vmo.get_property(ZX_PROP_VMO_CONTENT_SIZE, &content_size, sizeof(content_size)));
  return content_size;
}

TEST(StreamTestCase, WriteExtendsContentSize) {
  zx::vmo vmo;
  ASSERT_OK(zx::vmo::create(PAGE_SIZE, 0, &vmo));
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
  EXPECT_STR_EQ("0123456hijklmnopqrstuvwxyz", GetData(vmo).c_str());
  EXPECT_EQ(7u, GetContentSize(vmo));
  ASSERT_OK(vmo.write(kAlphabet, 0u, strlen(kAlphabet)));

  vec.capacity = 2u;
  ASSERT_OK(stream.writev(0, &vec, 1, &actual));
  EXPECT_EQ(2u, actual);
  EXPECT_STR_EQ("abcdefg01jklmnopqrstuvwxyz", GetData(vmo).c_str());
  EXPECT_EQ(9u, GetContentSize(vmo));
  ASSERT_OK(vmo.write(kAlphabet, 0u, strlen(kAlphabet)));

  ASSERT_OK(stream.seek(ZX_STREAM_SEEK_ORIGIN_START, 0, nullptr));

  vec.capacity = 10u;
  for (size_t i = 1; i * 10 < PAGE_SIZE; ++i) {
    ASSERT_OK(stream.writev(0, &vec, 1, &actual));
    EXPECT_EQ(10u, actual);
  }
  EXPECT_EQ(4090u, GetContentSize(vmo));

  actual = 9823u;
  ASSERT_OK(stream.writev(0, &vec, 1, &actual));
  EXPECT_EQ(6u, actual);
  EXPECT_EQ(4096u, GetContentSize(vmo));

  char scratch[17] = {};
  ASSERT_OK(vmo.read(scratch, 4090u, 6u));
  EXPECT_STR_EQ("012345", scratch);

  actual = 9823u;
  ASSERT_EQ(ZX_ERR_NO_SPACE, stream.writev(0, &vec, 1, &actual));
  EXPECT_EQ(9823u, actual);
  EXPECT_EQ(4096u, GetContentSize(vmo));
}

TEST(StreamTestCase, WriteExtendsVMOSize) {
  zx::vmo vmo;
  ASSERT_OK(zx::vmo::create(PAGE_SIZE, ZX_VMO_RESIZABLE, &vmo));

  zx::stream stream;
  char buffer[17] = "0123456789ABCDEF";
  zx_iovec_t vec = {
      .buffer = buffer,
      .capacity = sizeof(buffer),
  };
  size_t actual = 42u;

  ASSERT_OK(zx::stream::create(ZX_STREAM_MODE_WRITE, vmo, 0, &stream));
  vec.capacity = 10u;
  for (size_t i = 1; i * 10 < PAGE_SIZE; ++i) {
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
  EXPECT_EQ(PAGE_SIZE * 2, vmo_size);

  vec.capacity = UINT64_MAX;
  actual = 5423u;
  ASSERT_EQ(ZX_ERR_FILE_BIG, stream.writev(0, &vec, 1, &actual));
  EXPECT_EQ(5423u, actual);

  ASSERT_OK(vmo.get_size(&vmo_size));
  EXPECT_EQ(PAGE_SIZE * 2, vmo_size);
}

TEST(StreamTestCase, ReadVAt) {
  zx::vmo vmo;
  ASSERT_OK(zx::vmo::create(PAGE_SIZE, 0, &vmo));
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
  EXPECT_STR_EQ("yz", buffer);
  memset(buffer, 0, sizeof(buffer));

  zx_off_t seek = 39u;
  ASSERT_OK(stream.seek(ZX_STREAM_SEEK_ORIGIN_CURRENT, 0, &seek));
  EXPECT_EQ(0u, seek);

  ASSERT_OK(stream.readv_at(0, 36u, &vec, 1, &actual));
  EXPECT_EQ(0u, actual);
  EXPECT_STR_EQ("", buffer);
  memset(buffer, 0, sizeof(buffer));

  ASSERT_OK(stream.readv_at(0, 3645651u, &vec, 1, &actual));
  EXPECT_EQ(0u, actual);
  EXPECT_STR_EQ("", buffer);
  memset(buffer, 0, sizeof(buffer));
}

TEST(StreamTestCase, WriteVAt) {
  zx::vmo vmo;
  ASSERT_OK(zx::vmo::create(PAGE_SIZE, 0, &vmo));
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
  EXPECT_STR_EQ("abcdefg012klmnopqrstuvwxyz", GetData(vmo).c_str());
  ASSERT_OK(vmo.write(kAlphabet, 0u, strlen(kAlphabet)));

  zx_off_t seek = 39u;
  ASSERT_OK(stream.seek(ZX_STREAM_SEEK_ORIGIN_CURRENT, 0, &seek));
  EXPECT_EQ(0u, seek);

  vec.capacity = 10u;
  actual = 9823u;
  ASSERT_EQ(ZX_ERR_NO_SPACE, stream.writev_at(0, 4100u, &vec, 1, &actual));
  EXPECT_EQ(9823u, actual);

  ASSERT_OK(zx::vmo::create(PAGE_SIZE, ZX_VMO_RESIZABLE, &vmo));
  ASSERT_OK(zx::stream::create(ZX_STREAM_MODE_WRITE, vmo, 0, &stream));

  vec.capacity = 10u;
  actual = 9823u;
  ASSERT_OK(stream.writev_at(0, 4090, &vec, 1, &actual));
  EXPECT_EQ(10u, actual);
  EXPECT_EQ(4100u, GetContentSize(vmo));

  uint64_t vmo_size = 839u;
  ASSERT_OK(vmo.get_size(&vmo_size));
  EXPECT_EQ(PAGE_SIZE * 2, vmo_size);

  vec.capacity = UINT64_MAX;
  actual = 5423u;
  ASSERT_EQ(ZX_ERR_FILE_BIG, stream.writev_at(0, 5414u, &vec, 1, &actual));
  EXPECT_EQ(5423u, actual);

  ASSERT_OK(vmo.get_size(&vmo_size));
  EXPECT_EQ(PAGE_SIZE * 2, vmo_size);
}

TEST(StreamTestCase, ReadVectorAlias) {
  zx::vmo vmo;
  ASSERT_OK(zx::vmo::create(PAGE_SIZE, 0, &vmo));
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
  ASSERT_OK(stream.readv(0, multivec, kVectorCount, nullptr));
}

TEST(StreamTestCase, Append) {
  zx::vmo vmo;
  ASSERT_OK(zx::vmo::create(PAGE_SIZE, 0, &vmo));
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
  zx_info_stream_t info = {};
  ASSERT_OK(stream.get_info(ZX_INFO_STREAM, &info, sizeof(info), nullptr, nullptr));
  EXPECT_EQ(ZX_STREAM_MODE_WRITE, info.options);
  EXPECT_EQ(0u, info.seek);
  EXPECT_EQ(26u, info.content_size);

  vec.capacity = 7u;
  size_t actual = 42u;
  ASSERT_OK(stream.writev(ZX_STREAM_APPEND, &vec, 1, &actual));
  EXPECT_EQ(7u, actual);
  EXPECT_STR_EQ("abcdefghijklmnopqrstuvwxyz0123456", GetData(vmo).c_str());

  memset(&info, 0, sizeof(info));
  ASSERT_OK(stream.get_info(ZX_INFO_STREAM, &info, sizeof(info), nullptr, nullptr));
  EXPECT_EQ(ZX_STREAM_MODE_WRITE, info.options);
  EXPECT_EQ(33u, info.seek);
  EXPECT_EQ(33u, info.content_size);

  vec.capacity = 26u;
  for (size_t size = info.content_size; size + vec.capacity < PAGE_SIZE; size += vec.capacity) {
    ASSERT_OK(stream.writev(ZX_STREAM_APPEND, &vec, 1, &actual));
    EXPECT_EQ(vec.capacity, actual);
  }

  ASSERT_OK(stream.get_info(ZX_INFO_STREAM, &info, sizeof(info), nullptr, nullptr));
  EXPECT_GT(PAGE_SIZE, info.content_size);

  ASSERT_OK(stream.writev(ZX_STREAM_APPEND, &vec, 1, &actual));
  EXPECT_EQ(PAGE_SIZE - info.content_size, actual);

  ASSERT_EQ(ZX_ERR_NO_SPACE, stream.writev(ZX_STREAM_APPEND, &vec, 1, &actual));

  vec.capacity = UINT64_MAX;
  ASSERT_EQ(ZX_ERR_FILE_BIG, stream.writev(ZX_STREAM_APPEND, &vec, 1, &actual));
}

TEST(StreamTestCase, ExtendFillsWithZeros) {
  const size_t kPageCount = 6;
  const size_t kVmoSize = PAGE_SIZE * kPageCount;
  zx::vmo vmo;
  ASSERT_OK(zx::vmo::create(kVmoSize, 0, &vmo));

  zx::stream stream;
  ASSERT_OK(zx::stream::create(ZX_STREAM_MODE_WRITE, vmo, 0, &stream));

  char scratch[PAGE_SIZE];
  memset(scratch, 'x', sizeof(scratch));

  for (size_t i = 0; i < kPageCount; ++i) {
    ASSERT_OK(vmo.write(scratch, PAGE_SIZE * i, sizeof(scratch)));
  }

  char buffer[17] = "0123456789ABCDEF";
  zx_iovec_t vec = {
      .buffer = buffer,
      .capacity = 4,
  };

  size_t actual = 0u;
  ASSERT_OK(stream.writev_at(0, PAGE_SIZE * 2 - 2, &vec, 1, &actual));
  ASSERT_EQ(4, actual);

  memset(scratch, 'a', sizeof(scratch));
  ASSERT_OK(vmo.read(scratch, 0, sizeof(scratch)));

  for (size_t i = 0; i < PAGE_SIZE; ++i) {
    ASSERT_EQ(0, scratch[i], "The %zu byte should be zero.", i);
  }

  memset(scratch, 'a', sizeof(scratch));
  ASSERT_OK(vmo.read(scratch, PAGE_SIZE, sizeof(scratch)));

  for (size_t i = 0; i < PAGE_SIZE - 2; ++i) {
    ASSERT_EQ(0, scratch[i], "The %zu byte of the second page should be zero.", i);
  }

  ASSERT_EQ('0', scratch[PAGE_SIZE - 2]);
  ASSERT_EQ('1', scratch[PAGE_SIZE - 1]);

  memset(scratch, 'a', sizeof(scratch));
  ASSERT_OK(vmo.read(scratch, PAGE_SIZE * 2, sizeof(scratch)));

  ASSERT_EQ('2', scratch[0]);
  ASSERT_EQ('3', scratch[1]);
  ASSERT_EQ('x', scratch[2]);
  ASSERT_EQ('x', scratch[3]);

  ASSERT_OK(stream.seek(ZX_STREAM_SEEK_ORIGIN_START, PAGE_SIZE * 5 - 2, nullptr));

  actual = 0;
  ASSERT_OK(stream.writev(0, &vec, 1, &actual));
  ASSERT_EQ(4, actual);

  memset(scratch, 'a', sizeof(scratch));
  ASSERT_OK(vmo.read(scratch, PAGE_SIZE * 2, sizeof(scratch)));

  ASSERT_EQ('2', scratch[0]);
  ASSERT_EQ('3', scratch[1]);
  ASSERT_EQ(0, scratch[2]);
  ASSERT_EQ(0, scratch[3]);

  memset(scratch, 'a', sizeof(scratch));
  ASSERT_OK(vmo.read(scratch, PAGE_SIZE * 3, sizeof(scratch)));

  for (size_t i = 0; i < PAGE_SIZE; ++i) {
    ASSERT_EQ(0, scratch[i], "The %zu byte of the third page should be zero.", i);
  }

  memset(scratch, 'a', sizeof(scratch));
  ASSERT_OK(vmo.read(scratch, PAGE_SIZE * 4, sizeof(scratch)));

  for (size_t i = 0; i < PAGE_SIZE - 2; ++i) {
    ASSERT_EQ(0, scratch[i], "The %zu byte of the fourth page should be zero.", i);
  }

  ASSERT_EQ('0', scratch[PAGE_SIZE - 2]);
  ASSERT_EQ('1', scratch[PAGE_SIZE - 1]);

  memset(scratch, 'a', sizeof(scratch));
  ASSERT_OK(vmo.read(scratch, PAGE_SIZE * 5, sizeof(scratch)));

  ASSERT_EQ('2', scratch[0]);
  ASSERT_EQ('3', scratch[1]);
  ASSERT_EQ('x', scratch[2]);
  ASSERT_EQ('x', scratch[3]);
}

}  // namespace
