// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/devices/misc/drivers/compat/loader.h"

#include <fidl/fuchsia.ldsvc/cpp/wire_test_base.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/gtest/test_loop_fixture.h>

#include <gtest/gtest.h>

namespace fldsvc = fuchsia_ldsvc;

namespace {

zx_koid_t GetKoid(zx::vmo& vmo) {
  zx_info_handle_basic_t info{};
  zx_status_t status = vmo.get_info(ZX_INFO_HANDLE_BASIC, &info, sizeof(info), nullptr, nullptr);
  EXPECT_EQ(ZX_OK, status);
  return info.koid;
}

class TestEventHandler : public fidl::WireAsyncEventHandler<fldsvc::Loader> {
 public:
  fidl::Reason Reason() const { return error_.reason(); }

 private:
  void on_fidl_error(fidl::UnbindInfo error) { error_ = error; }

  fidl::UnbindInfo error_;
};

class TestLoader : public fldsvc::testing::Loader_TestBase {
 public:
  void SetLoadObjectVmo(zx::vmo vmo) { vmo_ = std::move(vmo); }

 private:
  // fidl::WireServer<fuchsia_ldsvc::Loader>
  void LoadObject(LoadObjectRequestView request, LoadObjectCompleter::Sync& completer) override {
    completer.Reply(ZX_OK, std::move(vmo_));
  }

  void Config(ConfigRequestView request, ConfigCompleter::Sync& completer) override {
    completer.Reply(ZX_OK);
  }

  void NotImplemented_(const std::string& name, fidl::CompleterBase& completer) override {
    printf("Not implemented: Loader::%s\n", name.data());
  }

  zx::vmo vmo_;
};

}  // namespace

class LoaderTest : public gtest::TestLoopFixture {};

TEST_F(LoaderTest, LoadObject) {
  auto endpoints = fidl::CreateEndpoints<fldsvc::Loader>();

  // Create VMO for backing loader reply.
  zx::vmo mylib_vmo;
  zx_status_t status = zx::vmo::create(zx_system_get_page_size(), 0, &mylib_vmo);
  ASSERT_EQ(ZX_OK, status);
  zx_koid_t mylib_koid = GetKoid(mylib_vmo);

  // Create backing loader.
  TestLoader backing_loader;
  fidl::BindServer<fidl::WireServer<fldsvc::Loader>>(dispatcher(), std::move(endpoints->server),
                                                     &backing_loader);
  backing_loader.SetLoadObjectVmo(std::move(mylib_vmo));

  // Create VMO of compat driver for compat loader.
  zx::vmo loader_vmo;
  status = zx::vmo::create(zx_system_get_page_size(), 0, &loader_vmo);
  ASSERT_EQ(ZX_OK, status);
  zx_koid_t loader_koid = GetKoid(loader_vmo);

  // Create compat loader.
  compat::Loader loader(dispatcher());
  status = loader.Bind(std::move(endpoints->client), std::move(loader_vmo)).status_value();
  ASSERT_EQ(ZX_OK, status);
  status = loader.Bind({}, {}).status_value();
  ASSERT_EQ(ZX_ERR_ALREADY_BOUND, status);

  // Create loader client.
  endpoints = fidl::CreateEndpoints<fldsvc::Loader>();
  fidl::BindServer<fidl::WireServer<fldsvc::Loader>>(dispatcher(), std::move(endpoints->server),
                                                     &loader);
  fidl::WireClient<fldsvc::Loader> client(std::move(endpoints->client), dispatcher());

  // Test that loading a random library fetches a VMO from the backing loader.
  client->LoadObject("mylib.so", [mylib_koid](auto* response) {
    EXPECT_EQ(ZX_OK, response->rv);
    zx_koid_t actual_koid = GetKoid(response->object);
    EXPECT_EQ(mylib_koid, actual_koid);
  });

  ASSERT_TRUE(RunLoopUntilIdle());

  // Test that loading the driver library fetches a VMO from the compat loader.
  client->LoadObject(compat::kLibDriverName, [loader_koid](auto* response) {
    EXPECT_EQ(ZX_OK, response->rv);
    zx_koid_t actual_koid = GetKoid(response->object);
    EXPECT_EQ(loader_koid, actual_koid);
  });

  ASSERT_TRUE(RunLoopUntilIdle());

  // Test that loading the driver library a second returns an error. We should
  // only see a single request for the driver library by the dynamic loader.
  client->LoadObject(compat::kLibDriverName,
                     [](auto* response) { EXPECT_EQ(ZX_ERR_NOT_FOUND, response->rv); });

  ASSERT_TRUE(RunLoopUntilIdle());
}

TEST_F(LoaderTest, DoneClosesConnection) {
  auto endpoints = fidl::CreateEndpoints<fldsvc::Loader>();

  // Create backing loader.
  TestLoader backing_loader;
  fidl::BindServer<fidl::WireServer<fldsvc::Loader>>(dispatcher(), std::move(endpoints->server),
                                                     &backing_loader);

  // Create compat loader.
  compat::Loader loader(dispatcher());
  zx_status_t status = loader.Bind(std::move(endpoints->client), zx::vmo()).status_value();
  ASSERT_EQ(ZX_OK, status);

  // Create event handler.
  TestEventHandler handler;

  // Create loader client.
  endpoints = fidl::CreateEndpoints<fldsvc::Loader>();
  fidl::BindServer<fidl::WireServer<fldsvc::Loader>>(dispatcher(), std::move(endpoints->server),
                                                     &loader);
  fidl::WireClient<fldsvc::Loader> client(std::move(endpoints->client), dispatcher(), &handler);

  // Test that done closes the connection.
  client->Done();

  ASSERT_TRUE(RunLoopUntilIdle());

  EXPECT_EQ(fidl::Reason::kPeerClosed, handler.Reason());
}

TEST_F(LoaderTest, ConfigSucceeds) {
  auto endpoints = fidl::CreateEndpoints<fldsvc::Loader>();

  // Create backing loader.
  TestLoader backing_loader;
  fidl::BindServer<fidl::WireServer<fldsvc::Loader>>(dispatcher(), std::move(endpoints->server),
                                                     &backing_loader);

  // Create compat loader.
  compat::Loader loader(dispatcher());
  zx_status_t status = loader.Bind(std::move(endpoints->client), zx::vmo()).status_value();
  ASSERT_EQ(ZX_OK, status);

  // Create loader client.
  endpoints = fidl::CreateEndpoints<fldsvc::Loader>();
  fidl::BindServer<fidl::WireServer<fldsvc::Loader>>(dispatcher(), std::move(endpoints->server),
                                                     &loader);
  fidl::WireClient<fldsvc::Loader> client(std::move(endpoints->client), dispatcher());

  // Test that config returns success.
  client->Config("", [](auto* response) { EXPECT_EQ(ZX_OK, response->rv); });

  ASSERT_TRUE(RunLoopUntilIdle());
}

TEST_F(LoaderTest, CloneSucceeds) {
  auto endpoints = fidl::CreateEndpoints<fldsvc::Loader>();

  // Create backing loader.
  TestLoader backing_loader;
  fidl::BindServer<fidl::WireServer<fldsvc::Loader>>(dispatcher(), std::move(endpoints->server),
                                                     &backing_loader);

  // Create compat loader.
  compat::Loader loader(dispatcher());
  zx_status_t status = loader.Bind(std::move(endpoints->client), zx::vmo()).status_value();
  ASSERT_EQ(ZX_OK, status);

  // Create loader client.
  endpoints = fidl::CreateEndpoints<fldsvc::Loader>();
  fidl::BindServer<fidl::WireServer<fldsvc::Loader>>(dispatcher(), std::move(endpoints->server),
                                                     &loader);
  fidl::WireClient<fldsvc::Loader> client(std::move(endpoints->client), dispatcher());

  // Test that clone returns success.
  endpoints = fidl::CreateEndpoints<fldsvc::Loader>();
  client->Clone(std::move(endpoints->server),
                [](auto* response) { EXPECT_EQ(ZX_OK, response->rv); });

  ASSERT_TRUE(RunLoopUntilIdle());
}

TEST_F(LoaderTest, NoBackingLoader) {
  auto endpoints = fidl::CreateEndpoints<fldsvc::Loader>();

  // Create compat loader.
  compat::Loader loader(dispatcher());
  zx_status_t status = loader.Bind(std::move(endpoints->client), {}).status_value();
  ASSERT_EQ(ZX_OK, status);

  // Close the server end of the backing loader channel.
  endpoints->server.reset();

  // Create loader client.
  endpoints = fidl::CreateEndpoints<fldsvc::Loader>();
  fidl::BindServer<fidl::WireServer<fldsvc::Loader>>(dispatcher(), std::move(endpoints->server),
                                                     &loader);
  fidl::WireClient<fldsvc::Loader> client(std::move(endpoints->client), dispatcher());

  // Test that functions that call the backing loader fail.
  client->LoadObject("mylib.so", [](auto* response) { EXPECT_EQ(ZX_ERR_CANCELED, response->rv); });
  client->Config("", [](auto* response) { EXPECT_EQ(ZX_ERR_CANCELED, response->rv); });

  ASSERT_TRUE(RunLoopUntilIdle());
}
