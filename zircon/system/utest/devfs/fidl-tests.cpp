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
#include <unittest/unittest.h>
#include <zircon/device/vfs.h>
#include <zircon/syscalls.h>

#include <utility>

namespace {

bool OpenHelper(const zx::channel& directory, const char* path, zx::channel* response_channel) {
    BEGIN_HELPER;

    // Open the requested path from the provded directory, and wait for the open
    // response on the accompanying channel.
    zx::channel client, server;
    ASSERT_EQ(zx::channel::create(0, &client, &server), ZX_OK);
    ASSERT_EQ(fuchsia_io_DirectoryOpen(directory.get(), ZX_FS_RIGHT_READABLE | ZX_FS_FLAG_DESCRIBE,
                                       0, path, strlen(path), server.release()), ZX_OK);
    zx_signals_t pending;
    ASSERT_EQ(client.wait_one(ZX_CHANNEL_PEER_CLOSED | ZX_CHANNEL_READABLE,
                              zx::deadline_after(zx::sec(1)), &pending), ZX_OK);
    ASSERT_EQ(pending & ZX_CHANNEL_READABLE, ZX_CHANNEL_READABLE);
    *response_channel = std::move(client);

    END_HELPER;
}

// Validate some size information and expected fields without fully decoding the
// FIDL message, for opening a path from a directory where we expect to open successfully.
bool FidlOpenValidator(const zx::channel& directory, const char* path,
                       fidl_union_tag_t expected_tag, size_t expected_handles) {
    BEGIN_HELPER;

    zx::channel client;
    ASSERT_TRUE(OpenHelper(directory, path, &client));

    char buf[8192];
    zx_handle_t handles[4];
    uint32_t actual_bytes;
    uint32_t actual_handles;
    ASSERT_EQ(client.read(0, buf, handles, sizeof(buf), fbl::count_of(handles),
                          &actual_bytes, &actual_handles), ZX_OK);
    ASSERT_EQ(actual_bytes, sizeof(fs::OnOpenMsg));
    ASSERT_EQ(actual_handles, expected_handles);
    auto response = reinterpret_cast<fs::OnOpenMsg*>(buf);
    ASSERT_EQ(response->primary.hdr.ordinal, fuchsia_io_NodeOnOpenOrdinal);
    ASSERT_EQ(response->primary.s, ZX_OK);
    ASSERT_EQ(response->extra.tag, expected_tag);
    zx_handle_close_many(handles, actual_handles);

    END_HELPER;
}

// Validate some size information and expected fields without fully decoding the
// FIDL message, for opening a path from a directory where we expect to fail.
bool FidlOpenErrorValidator(const zx::channel& directory, const char* path) {
    BEGIN_HELPER;

    zx::channel client;
    ASSERT_TRUE(OpenHelper(directory, path, &client));

    char buf[8192];
    zx_handle_t handles[4];
    uint32_t actual_bytes;
    uint32_t actual_handles;
    ASSERT_EQ(client.read(0, buf, handles, sizeof(buf), fbl::count_of(handles),
                          &actual_bytes, &actual_handles), ZX_OK);
    ASSERT_EQ(actual_bytes, sizeof(fuchsia_io_NodeOnOpenEvent));
    ASSERT_EQ(actual_handles, 0);
    auto response = reinterpret_cast<fuchsia_io_NodeOnOpenEvent*>(buf);
    ASSERT_EQ(response->hdr.ordinal, fuchsia_io_NodeOnOpenOrdinal);
    ASSERT_EQ(response->s, ZX_ERR_NOT_FOUND);

    END_HELPER;
}

// Ensure that our hand-rolled FIDL messages within devfs and memfs are acting correctly
// for open event messages (on both success and error).
bool TestFidlOpen() {
    BEGIN_TEST;

    {
        zx::channel dev_client, dev_server;
        ASSERT_EQ(zx::channel::create(0, &dev_client, &dev_server), ZX_OK);
        fdio_ns_t* ns;
        ASSERT_EQ(fdio_ns_get_installed(&ns), ZX_OK);
        ASSERT_EQ(fdio_ns_connect(ns, "/dev", ZX_FS_RIGHT_READABLE, dev_server.release()), ZX_OK);
        ASSERT_TRUE(FidlOpenValidator(dev_client, "zero", fuchsia_io_NodeInfoTag_device, 1));
        ASSERT_TRUE(FidlOpenValidator(dev_client, "class/platform-bus/000", fuchsia_io_NodeInfoTag_device, 1));
        ASSERT_TRUE(FidlOpenErrorValidator(dev_client, "this-path-better-not-actually-exist"));
        ASSERT_TRUE(FidlOpenErrorValidator(dev_client, "zero/this-path-better-not-actually-exist"));
    }

    {
        zx::channel dev_client, dev_server;
        ASSERT_EQ(zx::channel::create(0, &dev_client, &dev_server), ZX_OK);
        fdio_ns_t* ns;
        ASSERT_EQ(fdio_ns_get_installed(&ns), ZX_OK);
        ASSERT_EQ(fdio_ns_connect(ns, "/boot", ZX_FS_RIGHT_READABLE, dev_server.release()), ZX_OK);
        ASSERT_TRUE(FidlOpenValidator(dev_client, "lib", fuchsia_io_NodeInfoTag_directory, 0));
        ASSERT_TRUE(FidlOpenErrorValidator(dev_client, "this-path-better-not-actually-exist"));
    }

    END_TEST;
}

bool TestFidlBasic() {
    BEGIN_TEST;

    fuchsia_io_NodeInfo info = {};
    {
        zx::channel client, server;
        ASSERT_EQ(zx::channel::create(0, &client, &server), ZX_OK);
        ASSERT_EQ(fdio_service_connect("/dev/class", server.release()), ZX_OK);
        memset(&info, 0, sizeof(info));
        ASSERT_EQ(fuchsia_io_FileDescribe(client.get(), &info), ZX_OK);
        ASSERT_EQ(info.tag, fuchsia_io_NodeInfoTag_directory);
    }

    {
        zx::channel client, server;
        ASSERT_EQ(zx::channel::create(0, &client, &server), ZX_OK);
        ASSERT_EQ(fdio_service_connect("/dev/zero", server.release()), ZX_OK);
        memset(&info, 0, sizeof(info));
        ASSERT_EQ(fuchsia_io_FileDescribe(client.get(), &info), ZX_OK);
        ASSERT_EQ(info.tag, fuchsia_io_NodeInfoTag_device);
        ASSERT_NE(info.device.event, ZX_HANDLE_INVALID);
        zx_handle_close(info.device.event);
    }

    END_TEST;
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

bool CheckLocalEvent(watch_buffer_t* wb, const char** name, uint8_t* event) {
    if (wb->ptr != nullptr) {
        // Used a cached event
        *event = wb->ptr[0];
        ASSERT_LT(static_cast<size_t>(wb->ptr[1]), sizeof(wb->name_buf));
        memcpy(wb->name_buf, wb->ptr + 2, wb->ptr[1]);
        wb->name_buf[wb->ptr[1]] = 0;
        *name = reinterpret_cast<const char*>(wb->name_buf);
        wb->ptr += wb->ptr[1] + 2;
        ASSERT_LE((uintptr_t)wb->ptr, (uintptr_t) wb->buf + wb->size);
        if ((uintptr_t) wb->ptr == (uintptr_t) wb->buf + wb->size) {
            wb->ptr = nullptr;
        }
        return true;
    }
    return false;
}

// Read the next event off the channel.  Storage for |*name| will be reused
// between calls.
bool ReadEvent(watch_buffer_t* wb, const zx::channel& c, const char** name,
                uint8_t* event) {
    if (wb->ptr == nullptr) {
        zx_signals_t observed;
        ASSERT_EQ(c.wait_one(ZX_CHANNEL_READABLE, zx::deadline_after(zx::sec(5)), &observed),
                  ZX_OK);
        ASSERT_EQ(observed & ZX_CHANNEL_READABLE, ZX_CHANNEL_READABLE);
        uint32_t actual;
        ASSERT_EQ(c.read(0, wb->buf, nullptr, sizeof(wb->buf), 0, &actual, nullptr), ZX_OK);
        wb->size = actual;
        wb->ptr = wb->buf;
    }
    return CheckLocalEvent(wb, name, event);
}

bool TestDirectoryWatcherExisting() {
    BEGIN_TEST;

    // Channel pair for fuchsia.io.Directory interface
    zx::channel h, request;
    // Channel pair for directory watch events
    zx::channel watcher, remote_watcher;

    ASSERT_EQ(zx::channel::create(0, &h, &request), ZX_OK);
    ASSERT_EQ(zx::channel::create(0, &watcher, &remote_watcher), ZX_OK);
    ASSERT_EQ(fdio_service_connect("/dev/class", request.release()), ZX_OK);

    zx_status_t status;
    ASSERT_EQ(fuchsia_io_DirectoryWatch(h.get(), fuchsia_io_WATCH_MASK_ALL, 0,
                                        remote_watcher.release(), &status), ZX_OK);
    ASSERT_EQ(status, ZX_OK);

    watch_buffer_t wb = {};
    // We should see nothing but EXISTING events until we see an IDLE event
    while (1) {
        const char* name = nullptr;
        uint8_t event = 0;
        ASSERT_TRUE(ReadEvent(&wb, watcher, &name, &event));
        if (event == fuchsia_io_WATCH_EVENT_IDLE) {
            ASSERT_STR_EQ(name, "");
            break;
        }
        ASSERT_EQ(event, fuchsia_io_WATCH_EVENT_EXISTING);
        ASSERT_STR_NE(name, "");
    }

    END_TEST;
}

bool TestDirectoryWatcherWithClosedHalf() {
    BEGIN_TEST;

    // Channel pair for fuchsia.io.Directory interface
    zx::channel h, request;
    // Channel pair for directory watch events
    zx::channel watcher, remote_watcher;

    ASSERT_EQ(zx::channel::create(0, &h, &request), ZX_OK);
    ASSERT_EQ(zx::channel::create(0, &watcher, &remote_watcher), ZX_OK);
    ASSERT_EQ(fdio_service_connect("/dev/class", request.release()), ZX_OK);

    // Close our half of the watcher before devmgr gets its half.
    watcher.reset();

    zx_status_t status;
    ASSERT_EQ(fuchsia_io_DirectoryWatch(h.get(), fuchsia_io_WATCH_MASK_ALL, 0,
                                        remote_watcher.release(), &status), ZX_OK);
    ASSERT_EQ(status, ZX_OK);
    // If we're here and usermode didn't crash, we didn't hit the bug.

    // Create a new watcher, and see if it's functional at all
    ASSERT_EQ(zx::channel::create(0, &watcher, &remote_watcher), ZX_OK);
    ASSERT_EQ(fuchsia_io_DirectoryWatch(h.get(), fuchsia_io_WATCH_MASK_ALL, 0,
                                        remote_watcher.release(), &status), ZX_OK);
    ASSERT_EQ(status, ZX_OK);

    watch_buffer_t wb = {};
    const char* name = nullptr;
    uint8_t event = 0;
    ASSERT_TRUE(ReadEvent(&wb, watcher, &name, &event));
    ASSERT_EQ(event, fuchsia_io_WATCH_EVENT_EXISTING);

    END_TEST;
}

} // namespace

BEGIN_TEST_CASE(fidl_tests)
RUN_TEST(TestFidlOpen)
RUN_TEST(TestFidlBasic)
RUN_TEST(TestDirectoryWatcherWithClosedHalf)
RUN_TEST(TestDirectoryWatcherExisting)
END_TEST_CASE(fidl_tests)
