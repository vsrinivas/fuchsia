// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fs/service.h>

#include <unittest/unittest.h>

namespace {

bool test_service() {
    BEGIN_TEST;

    // set up a service which can only be bound once (to make it easy to
    // simulate an error to test error reporting behavior from the connector)
    zx::channel bound_channel;
    auto svc = fbl::AdoptRef<fs::Service>(new fs::Service(
        [&bound_channel](zx::channel channel) {
            if (bound_channel)
                return ZX_ERR_IO;
            bound_channel = fbl::move(channel);
            return ZX_OK;
        }));

    // open
    fbl::RefPtr<fs::Vnode> redirect;
    EXPECT_EQ(ZX_OK, svc->ValidateFlags(O_RDONLY));
    EXPECT_EQ(ZX_OK, svc->Open(O_RDONLY, &redirect));
    EXPECT_NULL(redirect);
    EXPECT_EQ(ZX_ERR_NOT_DIR, svc->ValidateFlags(O_DIRECTORY));

    // get attr
    vnattr_t attr;
    EXPECT_EQ(ZX_OK, svc->Getattr(&attr));
    EXPECT_EQ(V_TYPE_FILE, attr.mode);
    EXPECT_EQ(1, attr.nlink);

    // make some channels we can use for testing
    zx::channel c1, c2;
    EXPECT_EQ(ZX_OK, zx::channel::create(0u, &c1, &c2));
    zx_handle_t hc1 = c1.get();

    // serve, the connector will return success the first time
    fs::Vfs vfs;
    EXPECT_EQ(ZX_OK, svc->Serve(&vfs, fbl::move(c1), O_RDONLY));
    EXPECT_EQ(hc1, bound_channel.get());

    // the connector will return failure because bound_channel is still valid
    // we test that the error is propagated back up through Serve
    EXPECT_EQ(ZX_ERR_IO, svc->Serve(&vfs, fbl::move(c2), O_RDONLY));
    EXPECT_EQ(hc1, bound_channel.get());

    END_TEST;
}

} // namespace

BEGIN_TEST_CASE(service_tests)
RUN_TEST(test_service)
END_TEST_CASE(service_tests)
