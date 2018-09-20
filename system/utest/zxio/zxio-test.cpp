// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string.h>
#include <lib/zxio/ops.h>

#include <unittest/unittest.h>

typedef struct myctx {
    uint64_t value[4];
} myctx_t;

static zx_status_t my_close(void* ctx) {
    return ZX_OK;
}

bool ctx_test(void) {
    BEGIN_TEST;

    zxio_ops_t ops;
    memset(&ops, 0, sizeof(ops));
    ops.close = my_close;

    zxio_t* file;
    zx_status_t status = zxio_alloc(&ops, sizeof(myctx_t), &file);
    ASSERT_EQ(ZX_OK, status);
    myctx_t ctx_zero = {};
    myctx_t* ctx_file = static_cast<myctx_t*>(zxio_ctx_get(file));
    EXPECT_EQ(0, memcmp(ctx_file, &ctx_zero, sizeof(myctx_t)));
    status = zxio_close(file);
    ASSERT_EQ(ZX_OK, status);

    END_TEST;
}

BEGIN_TEST_CASE(zxio_test)
RUN_TEST(ctx_test);
END_TEST_CASE(zxio_test)
