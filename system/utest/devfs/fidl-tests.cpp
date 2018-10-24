// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fbl/type_support.h>
#include <fuchsia/io/c/fidl.h>
#include <lib/fdio/util.h>
#include <lib/fdio/util.h>
#include <lib/zx/channel.h>
#include <unittest/unittest.h>
#include <zircon/syscalls.h>

namespace {

bool TestFidlBasic() {
    BEGIN_TEST;

    zx_handle_t h = ZX_HANDLE_INVALID;
    zx_handle_t request = ZX_HANDLE_INVALID;
    fuchsia_io_NodeInfo info = {};

    ASSERT_EQ(zx_channel_create(0, &h, &request), ZX_OK);
    ASSERT_EQ(fdio_service_connect("/dev/class", request), ZX_OK);
    memset(&info, 0, sizeof(info));
    ASSERT_EQ(fuchsia_io_FileDescribe(h, &info), ZX_OK);
    ASSERT_EQ(info.tag, fuchsia_io_NodeInfoTag_directory);
    zx_handle_close(h);

    ASSERT_EQ(zx_channel_create(0, &h, &request), ZX_OK);
    ASSERT_EQ(fdio_service_connect("/dev/zero", request), ZX_OK);
    memset(&info, 0, sizeof(info));
    ASSERT_EQ(fuchsia_io_FileDescribe(h, &info), ZX_OK);
    ASSERT_EQ(info.tag, fuchsia_io_NodeInfoTag_device);
    ASSERT_NE(info.device.event, ZX_HANDLE_INVALID);
    zx_handle_close(info.device.event);
    zx_handle_close(h);

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
        ASSERT_EQ(c.read(0, wb->buf, sizeof(wb->buf), &actual, nullptr, 0, nullptr), ZX_OK);
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
RUN_TEST(TestFidlBasic)
RUN_TEST(TestDirectoryWatcherWithClosedHalf)
RUN_TEST(TestDirectoryWatcherExisting)
END_TEST_CASE(fidl_tests)
