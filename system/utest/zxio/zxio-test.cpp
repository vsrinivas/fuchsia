// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string.h>
#include <lib/zxio/ops.h>

#include <unittest/unittest.h>

static zx_status_t my_close(zxio_t* io) {
    return ZX_OK;
}

bool ctx_test(void) {
    BEGIN_TEST;

    zxio_ops_t ops;
    memset(&ops, 0, sizeof(ops));
    ops.close = my_close;

    zxio_t io = {};
    zxio_init(&io, &ops);
    ASSERT_EQ(ZX_OK, zxio_close(&io));

    END_TEST;
}

BEGIN_TEST_CASE(zxio_test)
RUN_TEST(ctx_test);
END_TEST_CASE(zxio_test)
