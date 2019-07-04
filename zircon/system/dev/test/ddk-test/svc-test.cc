// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/boot/c/fidl.h>
#include <fuchsia/scheduler/c/fidl.h>
#include <lib/fdio/directory.h>
#include <lib/zx/channel.h>
#include <lib/zx/debuglog.h>
#include <lib/zx/profile.h>
#include <lib/zx/vmo.h>
#include <unittest/unittest.h>

constexpr char kFactoryItemsPath[] = "/svc/" fuchsia_boot_FactoryItems_Name;
constexpr char kItemsPath[] = "/svc/" fuchsia_boot_Items_Name;
constexpr char kLogPath[] = "/svc/" fuchsia_boot_Log_Name;
constexpr char kProfileProviderPath[] = "/svc/" fuchsia_scheduler_ProfileProvider_Name;
constexpr char kRootResourcePath[] = "/svc/" fuchsia_boot_RootResource_Name;

static bool test_open_factory_items(void) {
    BEGIN_TEST;

    zx::channel client, server;
    zx_status_t status = zx::channel::create(0, &client, &server);
    ASSERT_EQ(ZX_OK, status, "zx::channel::create failed");

    status = fdio_service_connect(kFactoryItemsPath, server.release());
    ASSERT_EQ(ZX_OK, status, "fdio_service_connect failed");

    zx::vmo payload;
    uint32_t length;
    status = fuchsia_boot_FactoryItemsGet(client.get(), 0, payload.reset_and_get_address(), &length);
    ASSERT_EQ(ZX_OK, status, "fuchsia_boot_FactoryItemsGet failed");

    END_TEST;
}

static bool test_open_items(void) {
    BEGIN_TEST;

    zx::channel client, server;
    zx_status_t status = zx::channel::create(0, &client, &server);
    ASSERT_EQ(ZX_OK, status, "zx::channel::create failed");

    status = fdio_service_connect(kItemsPath, server.release());
    ASSERT_EQ(ZX_OK, status, "fdio_service_connect failed");

    zx::vmo payload;
    uint32_t length;
    status = fuchsia_boot_ItemsGet(client.get(), 0, 0, payload.reset_and_get_address(), &length);
    ASSERT_EQ(ZX_OK, status, "fuchsia_boot_ItemsGet failed");

    END_TEST;
}

static bool test_open_log(void) {
    BEGIN_TEST;

    zx::channel client, server;
    zx_status_t status = zx::channel::create(0, &client, &server);
    ASSERT_EQ(ZX_OK, status, "zx::channel::create failed");

    status = fdio_service_connect(kLogPath, server.release());
    ASSERT_EQ(ZX_OK, status, "fdio_service_connect failed");

    zx::debuglog log;
    status = fuchsia_boot_LogGet(client.get(), log.reset_and_get_address());
    ASSERT_EQ(ZX_OK, status, "fuchsia_boot_LogGet failed");

    END_TEST;
}

static bool test_open_profile_provider(void) {
    BEGIN_TEST;

    zx::channel client, server;
    zx_status_t status = zx::channel::create(0, &client, &server);
    ASSERT_EQ(ZX_OK, status, "zx::channel::create failed");

    status = fdio_service_connect(kProfileProviderPath, server.release());
    ASSERT_EQ(ZX_OK, status, "fdio_service_connect failed");

    zx_status_t fidl_status = ZX_ERR_INTERNAL;
    zx::profile profile;
    status = fuchsia_scheduler_ProfileProviderGetProfile(client.get(), 0, "", 0, &fidl_status,
        profile.reset_and_get_address());
    ASSERT_EQ(ZX_OK, status, "fuchsia_scheduler_ProfileProviderGetProfile failed");

    END_TEST;
}

static bool test_open_root_resource(void) {
    BEGIN_TEST;

    zx::channel client, server;
    zx_status_t status = zx::channel::create(0, &client, &server);
    ASSERT_EQ(ZX_OK, status, "zx::channel::create failed");

    status = fdio_service_connect(kRootResourcePath, server.release());
    ASSERT_EQ(ZX_OK, status, "fdio_service_connect failed");

    zx::resource resource;
    status = fuchsia_boot_RootResourceGet(client.get(), resource.reset_and_get_address());
    ASSERT_EQ(ZX_OK, status, "fuchsia_boot_RootResourceGet failed");

    END_TEST;
}

BEGIN_TEST_CASE(svc_tests)
RUN_TEST(test_open_factory_items)
RUN_TEST(test_open_items)
RUN_TEST(test_open_log)
RUN_TEST(test_open_profile_provider)
RUN_TEST(test_open_root_resource)
END_TEST_CASE(svc_tests)

extern "C" {
    struct test_case_element* test_case_ddk_svc = TEST_CASE_ELEMENT(svc_tests);
}
