// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/zx/socket.h>
#include <lib/zxio/cpp/inception.h>
#include <lib/zxio/zxio.h>

#include <memory>

#include <zxtest/zxtest.h>

TEST(Pipe, Create) {
  zx::socket socket0, socket1;
  ASSERT_OK(zx::socket::create(0u, &socket0, &socket1));

  zxio_storage_t storage;
  ASSERT_OK(zxio_create(socket0.release(), &storage));
  zxio_t* io = &storage.io;

  ASSERT_OK(zxio_close(io));
}

TEST(Pipe, CreateWithAllocator) {
  zx::socket socket0, socket1;
  ASSERT_OK(zx::socket::create(0u, &socket0, &socket1));

  auto zxio_allocator = [](zxio_object_type_t type, zxio_storage_t** out_storage,
                           void** out_context) {
    EXPECT_EQ(type, ZXIO_OBJECT_TYPE_PIPE);
    *out_storage = new zxio_storage_t;
    *out_context = *out_storage;
    return ZX_OK;
  };
  void* context = nullptr;
  ASSERT_OK(zxio_create_with_allocator(std::move(socket0), zxio_allocator, &context));
  std::unique_ptr<zxio_storage_t> storage(static_cast<zxio_storage_t*>(context));
  zxio_t* io = &storage->io;

  ASSERT_OK(zxio_close(io));
}

TEST(Pipe, Basic) {
  zx::socket socket0, socket1;
  ASSERT_OK(zx::socket::create(0u, &socket0, &socket1));

  zxio_storage_t storage;
  ASSERT_OK(zxio_create(socket0.release(), &storage));
  zxio_t* io = &storage.io;

  const uint32_t data = 0x41424344;

  size_t actual = 0u;
  ASSERT_OK(socket1.write(0u, &data, sizeof(data), &actual));
  EXPECT_EQ(actual, sizeof(data));

  uint32_t buffer = 0u;
  ASSERT_OK(zxio_read(io, &buffer, sizeof(buffer), 0u, &actual));
  EXPECT_EQ(actual, sizeof(buffer));
  EXPECT_EQ(buffer, data);

  ASSERT_OK(zxio_close(io));
}

TEST(Pipe, GetReadBufferAvailable) {
  zx::socket socket0, socket1;
  ASSERT_OK(zx::socket::create(0u, &socket0, &socket1));

  zxio_storage_t storage;
  ASSERT_OK(zxio_create(socket0.release(), &storage));
  zxio_t* io = &storage.io;

  size_t available = 0;
  ASSERT_OK(zxio_get_read_buffer_available(io, &available));
  EXPECT_EQ(0u, available);

  const uint32_t data = 0x41424344;

  size_t actual = 0u;
  ASSERT_OK(socket1.write(0u, &data, sizeof(data), &actual));
  EXPECT_EQ(actual, sizeof(data));

  ASSERT_OK(zxio_get_read_buffer_available(io, &available));
  EXPECT_EQ(sizeof(data), available);

  uint32_t buffer = 0u;
  ASSERT_OK(zxio_read(io, &buffer, sizeof(buffer), 0u, &actual));
  EXPECT_EQ(actual, sizeof(buffer));

  ASSERT_OK(zxio_get_read_buffer_available(io, &available));
  EXPECT_EQ(0u, available);

  ASSERT_OK(zxio_close(io));
}

// Test that after shutting a pipe endpoint down for reading that reading from
// that endpoint and writing to the peer endpoint fail.
TEST(Pipe, ShutdownRead) {
  zx::socket socket0, socket1;
  ASSERT_OK(zx::socket::create(0u, &socket0, &socket1));

  zxio_storage_t storage;
  ASSERT_OK(zxio_create(socket0.release(), &storage));
  zxio_t* io = &storage.io;

  const uint32_t data = 0x41424344;

  // Write some data before shutting down reading on the peer. Should succeed.
  size_t actual = 0u;
  EXPECT_EQ(socket1.write(0u, &data, sizeof(data), &actual), ZX_OK);
  EXPECT_EQ(actual, 4u);
  actual = 0u;

  EXPECT_OK(zxio_shutdown(io, ZXIO_SHUTDOWN_OPTIONS_READ));

  // We shouldn't be able to write any more data into the peer.
  EXPECT_EQ(socket1.write(0u, &data, sizeof(data), &actual), ZX_ERR_BAD_STATE);
  EXPECT_EQ(actual, 0u);
  actual = 0u;

  char buf[4] = {};
  // We should be able to read data written into the pipe before reading was
  // disabled.
  EXPECT_EQ(zxio_read(io, buf, sizeof(buf), 0u, &actual), ZX_OK);
  EXPECT_EQ(actual, 4u);
  actual = 0u;

  // TODO(https://fxbug.dev/79778): Reading from an empty pipe object with
  // reading disabled returns ZX_OK, not ZX_ERR_BAD_STATE.
  EXPECT_EQ(zxio_read(io, buf, sizeof(buf), 0u, &actual), ZX_OK);
  EXPECT_EQ(actual, 0u);
  actual = 0u;

  ASSERT_OK(zxio_close(io));
}

// Test that after shutting a pipe endpoint down for writing that writing to
// that endpoint and reading from the peer endpoint fail.
TEST(Pipe, ShutdownWrite) {
  zx::socket socket0, socket1;
  ASSERT_OK(zx::socket::create(0u, &socket0, &socket1));

  zxio_storage_t storage;
  ASSERT_OK(zxio_create(socket0.release(), &storage));
  zxio_t* io = &storage.io;

  EXPECT_OK(zxio_shutdown(io, ZXIO_SHUTDOWN_OPTIONS_WRITE));

  size_t actual = 0u;

  char buf[4] = {};
  EXPECT_EQ(socket1.read(0u, &buf, sizeof(buf), &actual), ZX_ERR_BAD_STATE);
  EXPECT_EQ(actual, 0u);
  actual = 0u;

  const uint32_t data = 0x41424344;

  EXPECT_EQ(zxio_write(io, &data, sizeof(data), 0u, &actual), ZX_ERR_BAD_STATE);
  EXPECT_EQ(actual, 0u);

  ASSERT_OK(zxio_close(io));
}

// Test that after shutting a pipe endpoint down for reading and writing that
// reading or writing to either endpoint fails.
TEST(Pipe, ShutdownReadWrite) {
  zx::socket socket0, socket1;
  ASSERT_OK(zx::socket::create(0u, &socket0, &socket1));

  zxio_storage_t storage;
  ASSERT_OK(zxio_create(socket0.release(), &storage));
  zxio_t* io = &storage.io;

  const uint32_t data = 0x41424344;

  // Write some data before shutting down the peer. Should succeed.
  size_t actual = 0u;
  EXPECT_EQ(socket1.write(0u, &data, sizeof(data), &actual), ZX_OK);
  EXPECT_EQ(actual, 4u);
  actual = 0u;

  EXPECT_OK(zxio_shutdown(io, ZXIO_SHUTDOWN_OPTIONS_READ | ZXIO_SHUTDOWN_OPTIONS_WRITE));

  char buf[4] = {};
  EXPECT_EQ(socket1.read(0u, &buf, sizeof(buf), &actual), ZX_ERR_BAD_STATE);
  EXPECT_EQ(actual, 0u);
  actual = 0u;

  EXPECT_EQ(socket1.write(0u, &data, sizeof(data), &actual), ZX_ERR_BAD_STATE);
  EXPECT_EQ(actual, 0u);
  actual = 0u;

  // We should be able to read data written into the pipe before reading was
  // disabled.
  EXPECT_EQ(zxio_read(io, buf, sizeof(buf), 0u, &actual), ZX_OK);
  EXPECT_EQ(actual, 4u);
  actual = 0u;

  // TODO(https://fxbug.dev/79778): Reading from an empty pipe object with
  // reading disabled returns ZX_OK, not ZX_ERR_BAD_STATE.
  EXPECT_EQ(zxio_read(io, buf, sizeof(buf), 0u, &actual), ZX_OK);
  EXPECT_EQ(actual, 0u);
  actual = 0u;

  EXPECT_EQ(zxio_write(io, &data, sizeof(data), 0u, &actual), ZX_ERR_BAD_STATE);
  EXPECT_EQ(actual, 0u);

  ASSERT_OK(zxio_close(io));
}
