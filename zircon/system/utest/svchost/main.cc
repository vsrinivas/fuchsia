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

#include <zxtest/zxtest.h>

namespace {

// These are integration tests of svchost which check whether certain services are present in
// the /svc directory exposed by svchost. To verify that the services are actually present we need
// to minimally test that they work, since fdio_service_connect succeeding does not actually mean
// the remote end exists (i.e. you won't observe a PEER_CLOSED error until actually trying to use
// the channel).

constexpr char kFactoryItemsPath[] = "/svc/" fuchsia_boot_FactoryItems_Name;
constexpr char kItemsPath[] = "/svc/" fuchsia_boot_Items_Name;
constexpr char kProfileProviderPath[] = "/svc/" fuchsia_scheduler_ProfileProvider_Name;
constexpr char kReadOnlyLogPath[] = "/svc/" fuchsia_boot_ReadOnlyLog_Name;
constexpr char kRootJobPath[] = "/svc/" fuchsia_boot_RootJob_Name;
constexpr char kRootResourcePath[] = "/svc/" fuchsia_boot_RootResource_Name;
constexpr char kWriteOnlyLogPath[] = "/svc/" fuchsia_boot_WriteOnlyLog_Name;

TEST(SvchostTest, FuchsiaBootFactoryItemsPresent) {
  zx::channel client, server;
  zx_status_t status = zx::channel::create(0, &client, &server);
  ASSERT_EQ(ZX_OK, status, "zx::channel::create failed");

  status = fdio_service_connect(kFactoryItemsPath, server.release());
  ASSERT_EQ(ZX_OK, status, "fdio_service_connect failed");

  zx::vmo payload;
  uint32_t length;
  status = fuchsia_boot_FactoryItemsGet(client.get(), 0, payload.reset_and_get_address(), &length);
  ASSERT_EQ(ZX_OK, status, "fuchsia_boot_FactoryItemsGet failed");
}

TEST(SvchostTest, FuchsiaBootItemsPresent) {
  zx::channel client, server;
  zx_status_t status = zx::channel::create(0, &client, &server);
  ASSERT_EQ(ZX_OK, status, "zx::channel::create failed");

  status = fdio_service_connect(kItemsPath, server.release());
  ASSERT_EQ(ZX_OK, status, "fdio_service_connect failed");

  zx::vmo payload;
  uint32_t length;
  status = fuchsia_boot_ItemsGet(client.get(), 0, 0, payload.reset_and_get_address(), &length);
  ASSERT_EQ(ZX_OK, status, "fuchsia_boot_ItemsGet failed");
}

TEST(SvchostTest, FuchsiaReadOnlyBootLogPresent) {
  zx::channel client, server;
  zx_status_t status = zx::channel::create(0, &client, &server);
  ASSERT_EQ(ZX_OK, status, "zx::channel::create failed");

  status = fdio_service_connect(kReadOnlyLogPath, server.release());
  ASSERT_EQ(ZX_OK, status, "fdio_service_connect failed");

  zx::debuglog log;
  status = fuchsia_boot_ReadOnlyLogGet(client.get(), log.reset_and_get_address());
  ASSERT_EQ(ZX_OK, status, "fuchsia_boot_ReadOnlyLogGet failed");
}

TEST(SvchostTest, FuchsiaWriteOnlyBootLogPresent) {
  zx::channel client, server;
  zx_status_t status = zx::channel::create(0, &client, &server);
  ASSERT_EQ(ZX_OK, status, "zx::channel::create failed");

  status = fdio_service_connect(kWriteOnlyLogPath, server.release());
  ASSERT_EQ(ZX_OK, status, "fdio_service_connect failed");

  zx::debuglog log;
  status = fuchsia_boot_WriteOnlyLogGet(client.get(), log.reset_and_get_address());
  ASSERT_EQ(ZX_OK, status, "fuchsia_boot_WriteOnlyLogGet failed");
}

TEST(SvchostTest, FuchsiaSchedulerProfileProviderPresent) {
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
}

TEST(SvchostTest, FuchsiaRootResourcePresent) {
  zx::channel client, server;
  zx_status_t status = zx::channel::create(0, &client, &server);
  ASSERT_EQ(ZX_OK, status, "zx::channel::create failed");

  status = fdio_service_connect(kRootResourcePath, server.release());
  ASSERT_EQ(ZX_OK, status, "fdio_service_connect failed");

  zx::resource resource;
  status = fuchsia_boot_RootResourceGet(client.get(), resource.reset_and_get_address());
  ASSERT_EQ(ZX_OK, status, "fuchsia_boot_RootResourceGet failed");
}

TEST(SvchostTest, FuchsiaRootJobPresent) {
  zx::channel client, server;
  zx_status_t status = zx::channel::create(0, &client, &server);
  ASSERT_EQ(ZX_OK, status, "zx::channel::create failed");

  status = fdio_service_connect(kRootJobPath, server.release());
  ASSERT_EQ(ZX_OK, status, "fdio_service_connect failed");

  zx::job job;
  status = fuchsia_boot_RootJobGet(client.get(), job.reset_and_get_address());
  ASSERT_EQ(ZX_OK, status, "fuchsia_boot_RootJobGet failed");
}

}  // namespace
