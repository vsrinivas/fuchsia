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
}

}  // namespace
