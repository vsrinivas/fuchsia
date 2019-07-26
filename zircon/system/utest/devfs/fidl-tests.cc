// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fbl/algorithm.h>
#include <fs/connection.h>
#include <fuchsia/io/c/fidl.h>
#include <lib/fdio/namespace.h>
#include <lib/fdio/fd.h>
#include <lib/fdio/fdio.h>
#include <lib/fdio/directory.h>
#include <lib/zx/channel.h>
#include <zircon/device/vfs.h>
#include <zircon/syscalls.h>
#include <zxtest/zxtest.h>

#include <utility>

namespace {

void OpenHelper(const zx::channel& directory, const char* path, zx::channel* response_channel) {
  // Open the requested path from the provded directory, and wait for the open
  // response on the accompanying channel.
  zx::channel client, server;
  ASSERT_OK(zx::channel::create(0, &client, &server));
  ASSERT_EQ(fuchsia_io_DirectoryOpen(directory.get(), ZX_FS_RIGHT_READABLE | ZX_FS_FLAG_DESCRIBE, 0,
                                     path, strlen(path), server.release()),
            ZX_OK);
  zx_signals_t pending;
  ASSERT_EQ(
      client.wait_one(ZX_CHANNEL_PEER_CLOSED | ZX_CHANNEL_READABLE, zx::time::infinite(), &pending),
      ZX_OK);
  ASSERT_EQ(pending & ZX_CHANNEL_READABLE, ZX_CHANNEL_READABLE);
  *response_channel = std::move(client);
}

// Validate some size information and expected fields without fully decoding the
// FIDL message, for opening a path from a directory where we expect to open successfully.
void FidlOpenValidator(const zx::channel& directory, const char* path,
                       fidl_union_tag_t expected_tag, size_t expected_handles) {
  zx::channel client;
  ASSERT_NO_FAILURES(OpenHelper(directory, path, &client));

  char buf[8192];
  zx_handle_t handles[4];
  uint32_t actual_bytes;
  uint32_t actual_handles;
  ASSERT_EQ(client.read(0, buf, handles, sizeof(buf), fbl::count_of(handles), &actual_bytes,
                        &actual_handles),
            ZX_OK);
  ASSERT_EQ(actual_bytes, sizeof(fs::OnOpenMsg));
  ASSERT_EQ(actual_handles, expected_handles);
  auto response = reinterpret_cast<fs::OnOpenMsg*>(buf);
  ASSERT_EQ(response->primary.hdr.ordinal, fuchsia_io_NodeOnOpenOrdinal);
  ASSERT_OK(response->primary.s);
  ASSERT_EQ(response->extra.tag, expected_tag);
  zx_handle_close_many(handles, actual_handles);
}

// Validate some size information and expected fields without fully decoding the
// FIDL message, for opening a path from a directory where we expect to fail.
void FidlOpenErrorValidator(const zx::channel& directory, const char* path) {
  zx::channel client;
  ASSERT_NO_FAILURES(OpenHelper(directory, path, &client));

  char buf[8192];
  zx_handle_t handles[4];
  uint32_t actual_bytes;
  uint32_t actual_handles;
  ASSERT_EQ(client.read(0, buf, handles, sizeof(buf), fbl::count_of(handles), &actual_bytes,
                        &actual_handles),
            ZX_OK);
  ASSERT_EQ(actual_bytes, sizeof(fuchsia_io_NodeOnOpenEvent));
  ASSERT_EQ(actual_handles, 0);
  auto response = reinterpret_cast<fuchsia_io_NodeOnOpenEvent*>(buf);
  ASSERT_EQ(response->hdr.ordinal, fuchsia_io_NodeOnOpenOrdinal);
  ASSERT_EQ(response->s, ZX_ERR_NOT_FOUND);
}

// Ensure that our hand-rolled FIDL messages within devfs and memfs are acting correctly
// for open event messages (on both success and error).
TEST(FidlTestCase, Open) {
  {
    zx::channel dev_client, dev_server;
    ASSERT_OK(zx::channel::create(0, &dev_client, &dev_server));
    fdio_ns_t* ns;
    ASSERT_OK(fdio_ns_get_installed(&ns));
    ASSERT_OK(fdio_ns_connect(ns, "/dev", ZX_FS_RIGHT_READABLE, dev_server.release()));
    ASSERT_NO_FAILURES(FidlOpenValidator(dev_client, "zero", fuchsia_io_NodeInfoTag_device, 1));
    ASSERT_NO_FAILURES(
        FidlOpenValidator(dev_client, "class/platform-bus/000", fuchsia_io_NodeInfoTag_device, 1));
    ASSERT_NO_FAILURES(FidlOpenErrorValidator(dev_client, "this-path-better-not-actually-exist"));
    ASSERT_NO_FAILURES(
        FidlOpenErrorValidator(dev_client, "zero/this-path-better-not-actually-exist"));
  }

  {
    zx::channel dev_client, dev_server;
    ASSERT_OK(zx::channel::create(0, &dev_client, &dev_server));
    fdio_ns_t* ns;
    ASSERT_OK(fdio_ns_get_installed(&ns));
    ASSERT_OK(fdio_ns_connect(ns, "/boot", ZX_FS_RIGHT_READABLE, dev_server.release()));
    ASSERT_NO_FAILURES(FidlOpenValidator(dev_client, "lib", fuchsia_io_NodeInfoTag_directory, 0));
    ASSERT_NO_FAILURES(FidlOpenErrorValidator(dev_client, "this-path-better-not-actually-exist"));
  }
}

TEST(FidlTestCase, Basic) {
  fuchsia_io_NodeInfo info = {};
  {
    zx::channel client, server;
    ASSERT_OK(zx::channel::create(0, &client, &server));
    ASSERT_OK(fdio_service_connect("/dev/class", server.release()));
    memset(&info, 0, sizeof(info));
    ASSERT_OK(fuchsia_io_FileDescribe(client.get(), &info));
    ASSERT_EQ(info.tag, fuchsia_io_NodeInfoTag_directory);
  }

  {
    zx::channel client, server;
    ASSERT_OK(zx::channel::create(0, &client, &server));
    ASSERT_OK(fdio_service_connect("/dev/zero", server.release()));
    memset(&info, 0, sizeof(info));
    ASSERT_OK(fuchsia_io_FileDescribe(client.get(), &info));
    ASSERT_EQ(info.tag, fuchsia_io_NodeInfoTag_device);
    ASSERT_NE(info.device.event, ZX_HANDLE_INVALID);
    zx_handle_close(info.device.event);
  }
}

typedef struct {
  // Buffer containing cached messages
  uint8_t buf[fuchsia_io_MAX_BUF];
  uint8_t name_buf[fuchsia_io_MAX_FILENAME + 1];
  // Offset into 'buf' of next message
  uint8_t* ptr;
  // Maximum size of buffer
  size_t size;
} watch_buffer_t;

void CheckLocalEvent(watch_buffer_t* wb, const char** name, uint8_t* event) {
  ASSERT_NOT_NULL(wb->ptr);

  // Used a cached event
  *event = wb->ptr[0];
  ASSERT_LT(static_cast<size_t>(wb->ptr[1]), sizeof(wb->name_buf));
  memcpy(wb->name_buf, wb->ptr + 2, wb->ptr[1]);
  wb->name_buf[wb->ptr[1]] = 0;
  *name = reinterpret_cast<const char*>(wb->name_buf);
  wb->ptr += wb->ptr[1] + 2;
  ASSERT_LE((uintptr_t)wb->ptr, (uintptr_t)wb->buf + wb->size);
  if ((uintptr_t)wb->ptr == (uintptr_t)wb->buf + wb->size) {
    wb->ptr = nullptr;
  }
}

// Read the next event off the channel.  Storage for |*name| will be reused
// between calls.
void ReadEvent(watch_buffer_t* wb, const zx::channel& c, const char** name, uint8_t* event) {
  if (wb->ptr == nullptr) {
    zx_signals_t observed;
    ASSERT_OK(c.wait_one(ZX_CHANNEL_READABLE, zx::time::infinite(), &observed));
    ASSERT_EQ(observed & ZX_CHANNEL_READABLE, ZX_CHANNEL_READABLE);
    uint32_t actual;
    ASSERT_OK(c.read(0, wb->buf, nullptr, sizeof(wb->buf), 0, &actual, nullptr));
    wb->size = actual;
    wb->ptr = wb->buf;
  }
  ASSERT_NO_FAILURES(CheckLocalEvent(wb, name, event));
}

TEST(FidlTestCase, DirectoryWatcherExisting) {
  // Channel pair for fuchsia.io.Directory interface
  zx::channel h, request;
  // Channel pair for directory watch events
  zx::channel watcher, remote_watcher;

  ASSERT_OK(zx::channel::create(0, &h, &request));
  ASSERT_OK(zx::channel::create(0, &watcher, &remote_watcher));
  ASSERT_OK(fdio_service_connect("/dev/class", request.release()));

  zx_status_t status;
  ASSERT_EQ(fuchsia_io_DirectoryWatch(h.get(), fuchsia_io_WATCH_MASK_ALL, 0,
                                      remote_watcher.release(), &status),
            ZX_OK);
  ASSERT_OK(status);

  watch_buffer_t wb = {};
  // We should see nothing but EXISTING events until we see an IDLE event
  while (1) {
    const char* name = nullptr;
    uint8_t event = 0;
    ASSERT_NO_FAILURES(ReadEvent(&wb, watcher, &name, &event));
    if (event == fuchsia_io_WATCH_EVENT_IDLE) {
      ASSERT_STR_EQ(name, "");
      break;
    }
    ASSERT_EQ(event, fuchsia_io_WATCH_EVENT_EXISTING);
    ASSERT_STR_NE(name, "");
  }
}

TEST(FidlTestCase, DirectoryWatcherWithClosedHalf) {
  // Channel pair for fuchsia.io.Directory interface
  zx::channel h, request;
  // Channel pair for directory watch events
  zx::channel watcher, remote_watcher;

  ASSERT_OK(zx::channel::create(0, &h, &request));
  ASSERT_OK(zx::channel::create(0, &watcher, &remote_watcher));
  ASSERT_OK(fdio_service_connect("/dev/class", request.release()));

  // Close our half of the watcher before devmgr gets its half.
  watcher.reset();

  zx_status_t status;
  ASSERT_EQ(fuchsia_io_DirectoryWatch(h.get(), fuchsia_io_WATCH_MASK_ALL, 0,
                                      remote_watcher.release(), &status),
            ZX_OK);
  ASSERT_OK(status);
  // If we're here and usermode didn't crash, we didn't hit the bug.

  // Create a new watcher, and see if it's functional at all
  ASSERT_OK(zx::channel::create(0, &watcher, &remote_watcher));
  ASSERT_EQ(fuchsia_io_DirectoryWatch(h.get(), fuchsia_io_WATCH_MASK_ALL, 0,
                                      remote_watcher.release(), &status),
            ZX_OK);
  ASSERT_OK(status);

  watch_buffer_t wb = {};
  const char* name = nullptr;
  uint8_t event = 0;
  ASSERT_NO_FAILURES(ReadEvent(&wb, watcher, &name, &event));
  ASSERT_EQ(event, fuchsia_io_WATCH_EVENT_EXISTING);
}

}  // namespace
