// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/zxio/inception.h>
#include <lib/zxio/zxio.h>

#include <unittest/unittest.h>

bool null_basic_test(void) {
    BEGIN_TEST;

    zxio_t io;
    zxio_null_init(&io);

    zxio_signals_t observed = ZXIO_SIGNAL_NONE;
    ASSERT_EQ(ZX_ERR_NOT_SUPPORTED, zxio_wait_one(&io, ZXIO_READABLE,
                                                  ZX_TIME_INFINITE, &observed));

    ASSERT_EQ(ZX_ERR_NOT_SUPPORTED, zxio_clone_async(&io, 0u, ZX_HANDLE_INVALID));
    ASSERT_EQ(ZX_ERR_NOT_SUPPORTED, zxio_sync(&io));

    zxio_node_attr_t attr = {};
    ASSERT_EQ(ZX_ERR_NOT_SUPPORTED, zxio_attr_get(&io, &attr));
    ASSERT_EQ(ZX_ERR_NOT_SUPPORTED, zxio_attr_set(&io, 0u, &attr));
    char buffer[1024];
    memset(buffer, 0, sizeof(buffer));
    size_t actual = 0u;
    ASSERT_EQ(ZX_ERR_NOT_SUPPORTED, zxio_read(&io, buffer, sizeof(buffer), &actual));
    ASSERT_EQ(ZX_ERR_NOT_SUPPORTED, zxio_read_at(&io, 0u, buffer, sizeof(buffer), &actual));
    ASSERT_EQ(ZX_ERR_NOT_SUPPORTED, zxio_write(&io, buffer, sizeof(buffer), &actual));
    ASSERT_EQ(ZX_ERR_NOT_SUPPORTED, zxio_write_at(&io, 0u, buffer, sizeof(buffer), &actual));
    size_t offset = 0u;
    ASSERT_EQ(ZX_ERR_NOT_SUPPORTED, zxio_seek(&io, 0u, fuchsia_io_SeekOrigin_START,
                                              &offset));
    ASSERT_EQ(ZX_ERR_NOT_SUPPORTED, zxio_truncate(&io, 0u));
    uint32_t flags = 0u;
    ASSERT_EQ(ZX_ERR_NOT_SUPPORTED, zxio_flags_get(&io, &flags));
    ASSERT_EQ(ZX_ERR_NOT_SUPPORTED, zxio_flags_set(&io, flags));
    zx_handle_t vmo = ZX_HANDLE_INVALID;
    size_t size = 0u;
    ASSERT_EQ(ZX_ERR_NOT_SUPPORTED, zxio_vmo_get_copy(&io, &vmo, &size));
    ASSERT_EQ(ZX_ERR_NOT_SUPPORTED, zxio_vmo_get_clone(&io, &vmo, &size));
    ASSERT_EQ(ZX_ERR_NOT_SUPPORTED, zxio_vmo_get_exact(&io, &vmo, &size));

    zxio_t* result = nullptr;
    ASSERT_EQ(ZX_ERR_NOT_SUPPORTED, zxio_open(&io, 0u, 0u, "hello", &result));
    ASSERT_EQ(ZX_ERR_NOT_SUPPORTED,
              zxio_open_async(&io, 0u, 0u, "hello", ZX_HANDLE_INVALID));
    ASSERT_EQ(ZX_ERR_NOT_SUPPORTED, zxio_unlink(&io, "hello"));

    zxio_t io2;
    zxio_null_init(&io2);
    ASSERT_EQ(ZX_ERR_NOT_SUPPORTED, zxio_rename(&io, "one", &io2, "two"));
    ASSERT_EQ(ZX_ERR_NOT_SUPPORTED, zxio_link(&io, "one", &io2, "two"));
    zxio_dirent_iterator_t iter = {};
    char buffer2[ZXIO_DIRENT_ITERATOR_DEFAULT_BUFFER_SIZE];
    ASSERT_EQ(ZX_ERR_NOT_SUPPORTED,
              zxio_dirent_iterator_init(&iter, &io, buffer2, sizeof(buffer2)));
    zxio_dirent_t* entry = nullptr;
    ASSERT_EQ(ZX_ERR_NOT_SUPPORTED, zxio_dirent_iterator_next(&iter, &entry));

    END_TEST;
}

BEGIN_TEST_CASE(null_test)
RUN_TEST(null_basic_test);
END_TEST_CASE(null_test)
