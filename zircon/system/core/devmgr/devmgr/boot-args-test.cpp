// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <unittest/unittest.h>

#include "boot-args.h"

bool CreateBootArgs(const char* config, size_t size, devmgr::BootArgs* boot_args) {
    BEGIN_HELPER;

    zx::vmo vmo;
    zx_status_t status = zx::vmo::create(size, 0, &vmo);
    ASSERT_EQ(ZX_OK, status);

    status = vmo.write(config, 0, size);
    ASSERT_EQ(ZX_OK, status);

    status = devmgr::BootArgs::Create(std::move(vmo), size, boot_args);
    ASSERT_EQ(ZX_OK, status);

    END_HELPER;
}

bool Get() {
    BEGIN_TEST;

    const char config[] = "key1=value1\0key2=value2";

    devmgr::BootArgs boot_args;
    ASSERT_TRUE(CreateBootArgs(config, sizeof(config), &boot_args));
    ASSERT_STR_EQ("value1", boot_args.Get("key1"));
    ASSERT_STR_EQ("value2", boot_args.Get("key2"));

    END_TEST;
}

bool GetBool() {
    BEGIN_TEST;

    const char config[] = "key1\0key2=hello\0key3=false\0key4=off\0key5=0";

    devmgr::BootArgs boot_args;
    ASSERT_TRUE(CreateBootArgs(config, sizeof(config), &boot_args));
    ASSERT_TRUE(boot_args.GetBool("key1", false));
    ASSERT_TRUE(boot_args.GetBool("key2", false));
    ASSERT_TRUE(boot_args.GetBool("missing", true));
    ASSERT_FALSE(boot_args.GetBool("key3", false));
    ASSERT_FALSE(boot_args.GetBool("key4", false));
    ASSERT_FALSE(boot_args.GetBool("key5", false));

    END_TEST;
}

bool Collect() {
    BEGIN_TEST;

    const char config[] = "key1\0key2=value2\0key3=value3\0yek=eulav";

    devmgr::BootArgs boot_args;
    ASSERT_TRUE(CreateBootArgs(config, sizeof(config), &boot_args));
    fbl::Vector<const char*> out;
    boot_args.Collect("key", &out);
    ASSERT_EQ(3, out.size());
    ASSERT_STR_EQ("key1", out[0]);
    ASSERT_STR_EQ("key2=value2", out[1]);
    ASSERT_STR_EQ("key3=value3", out[2]);

    END_TEST;
}

BEGIN_TEST_CASE(boot_args_tests)
RUN_TEST(Get)
RUN_TEST(GetBool)
RUN_TEST(Collect)
END_TEST_CASE(boot_args_tests)
