// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/io/llcpp/fidl.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/fd.h>
#include <lib/fdio/fdio.h>
#include <lib/fdio/namespace.h>
#include <lib/zx/channel.h>
#include <zircon/device/vfs.h>
#include <zircon/errors.h>
#include <zircon/syscalls.h>
#include <zircon/types.h>

#include <utility>

#include <fbl/algorithm.h>
#include <zxtest/zxtest.h>

namespace {

namespace fio = fuchsia_io;

void OpenHelper(const zx::channel& directory, const char* path, zx::channel* response_channel) {
  // Open the requested path from the provided directory, and wait for the open
  // response on the accompanying channel.
  zx::channel client, server;
  ASSERT_OK(zx::channel::create(0, &client, &server));
  auto result =
      fio::Directory::Call::Open(zx::unowned_channel(directory),
                                 fio::wire::OPEN_RIGHT_READABLE | fio::wire::OPEN_FLAG_DESCRIBE, 0,
                                 fidl::unowned_str(path, strlen(path)), std::move(server));
  ASSERT_EQ(result.status(), ZX_OK);
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
                       fio::wire::NodeInfo::Tag expected_tag, size_t expected_handles) {
  zx::channel client;
  ASSERT_NO_FAILURES(OpenHelper(directory, path, &client));

  class EventHandler : public fio::Node::SyncEventHandler {
   public:
    explicit EventHandler(fio::wire::NodeInfo::Tag expected_tag) : expected_tag_(expected_tag) {}

    bool event_tag_ok() const { return event_tag_ok_; }
    bool status_ok() const { return status_ok_; }
    bool node_info_ok() const { return node_info_ok_; }

    void OnOpen(fio::Node::OnOpenResponse* event) override {
      event_tag_ok_ = true;
      status_ok_ = event->s == ZX_OK;
      node_info_ok_ = event->info.which() == expected_tag_;
    }

    zx_status_t Unknown() override {
      EXPECT_TRUE(false);
      return ZX_OK;
    }

   private:
    const fio::wire::NodeInfo::Tag expected_tag_;
    bool event_tag_ok_ = false;
    bool status_ok_ = false;
    bool node_info_ok_ = false;
  };

  EventHandler event_handler(expected_tag);
  ASSERT_OK(event_handler.HandleOneEvent(zx::unowned_channel(client)));
  ASSERT_TRUE(event_handler.event_tag_ok());
  ASSERT_TRUE(event_handler.status_ok());
  ASSERT_TRUE(event_handler.node_info_ok());
}

// Validate some size information and expected fields without fully decoding the
// FIDL message, for opening a path from a directory where we expect to fail.
void FidlOpenErrorValidator(const zx::channel& directory, const char* path) {
  zx::channel client;
  ASSERT_NO_FAILURES(OpenHelper(directory, path, &client));

  class EventHandler : public fio::Node::SyncEventHandler {
   public:
    EventHandler() = default;

    bool event_tag_ok() const { return event_tag_ok_; }
    bool status_ok() const { return status_ok_; }
    bool node_info_ok() const { return node_info_ok_; }

    void OnOpen(fio::Node::OnOpenResponse* event) override {
      event_tag_ok_ = true;
      status_ok_ = static_cast<int>(event->s) == ZX_ERR_NOT_FOUND;
      node_info_ok_ = event->info.has_invalid_tag();
    }

    zx_status_t Unknown() override {
      EXPECT_TRUE(false);
      return ZX_OK;
    }

   private:
    bool event_tag_ok_ = false;
    bool status_ok_ = false;
    bool node_info_ok_ = false;
  };

  EventHandler event_handler;
  ASSERT_OK(event_handler.HandleOneEvent(zx::unowned_channel(client)));
  ASSERT_TRUE(event_handler.event_tag_ok());
  ASSERT_TRUE(event_handler.status_ok());
  ASSERT_TRUE(event_handler.node_info_ok());
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
    ASSERT_NO_FAILURES(FidlOpenValidator(dev_client, "zero", fio::wire::NodeInfo::Tag::kDevice, 1));
    ASSERT_NO_FAILURES(FidlOpenValidator(dev_client, "class/platform-bus/000",
                                         fio::wire::NodeInfo::Tag::kDevice, 1));
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
    ASSERT_NO_FAILURES(
        FidlOpenValidator(dev_client, "lib", fio::wire::NodeInfo::Tag::kDirectory, 0));
    ASSERT_NO_FAILURES(FidlOpenErrorValidator(dev_client, "this-path-better-not-actually-exist"));
  }
}

TEST(FidlTestCase, Basic) {
  {
    zx::channel client, server;
    ASSERT_OK(zx::channel::create(0, &client, &server));
    ASSERT_OK(fdio_service_connect("/dev/class", server.release()));
    auto result = fio::File::Call::Describe(zx::unowned_channel(client));
    ASSERT_OK(result.status());
    ASSERT_TRUE(result->info.is_directory());
  }

  {
    zx::channel client, server;
    ASSERT_OK(zx::channel::create(0, &client, &server));
    ASSERT_OK(fdio_service_connect("/dev/zero", server.release()));
    auto result = fio::File::Call::Describe(zx::unowned_channel(client));
    auto info = std::move(result->info);
    ASSERT_TRUE(info.is_device());
    ASSERT_NE(info.device().event, ZX_HANDLE_INVALID);
    zx_handle_close(info.mutable_device().event.release());
  }
}

typedef struct {
  // Buffer containing cached messages
  uint8_t buf[fio::wire::MAX_BUF];
  uint8_t name_buf[fio::wire::MAX_FILENAME + 1];
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

  auto result = fio::Directory::Call::Watch(zx::unowned_channel(h), fio::wire::WATCH_MASK_ALL, 0,
                                            std::move(remote_watcher));
  ASSERT_EQ(result.status(), ZX_OK);
  ASSERT_OK(result->s);

  watch_buffer_t wb = {};
  // We should see nothing but EXISTING events until we see an IDLE event
  while (1) {
    const char* name = nullptr;
    uint8_t event = 0;
    ASSERT_NO_FAILURES(ReadEvent(&wb, watcher, &name, &event));
    if (event == fio::wire::WATCH_EVENT_IDLE) {
      ASSERT_STR_EQ(name, "");
      break;
    }
    ASSERT_EQ(event, fio::wire::WATCH_EVENT_EXISTING);
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

  {
    auto result = fio::Directory::Call::Watch(zx::unowned_channel(h), fio::wire::WATCH_MASK_ALL, 0,
                                              std::move(remote_watcher));
    ASSERT_EQ(result.status(), ZX_OK);
    ASSERT_OK(result->s);
    // If we're here and usermode didn't crash, we didn't hit the bug.
  }

  {
    // Create a new watcher, and see if it's functional at all
    ASSERT_OK(zx::channel::create(0, &watcher, &remote_watcher));
    auto result = fio::Directory::Call::Watch(zx::unowned_channel(h), fio::wire::WATCH_MASK_ALL, 0,
                                              std::move(remote_watcher));
    ASSERT_EQ(result.status(), ZX_OK);
    ASSERT_OK(result->s);
  }

  watch_buffer_t wb = {};
  const char* name = nullptr;
  uint8_t event = 0;
  ASSERT_NO_FAILURES(ReadEvent(&wb, watcher, &name, &event));
  ASSERT_EQ(event, fio::wire::WATCH_EVENT_EXISTING);
}

}  // namespace
