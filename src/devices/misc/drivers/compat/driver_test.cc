// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/devices/misc/drivers/compat/driver.h"

#include <dirent.h>
#include <fidl/fuchsia.boot/cpp/wire_test_base.h>
#include <fidl/fuchsia.driver.framework/cpp/wire_test_base.h>
#include <fidl/fuchsia.io/cpp/wire_test_base.h>
#include <fidl/fuchsia.logger/cpp/wire.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/fdio/directory.h>
#include <lib/gtest/test_loop_fixture.h>

#include <fbl/unique_fd.h>
#include <gtest/gtest.h>

#include "src/devices/misc/drivers/compat/v1_test.h"

namespace fboot = fuchsia_boot;
namespace fdata = fuchsia_data;
namespace fdf = fuchsia_driver_framework;
namespace fio = fuchsia_io;
namespace flogger = fuchsia_logger;
namespace fmem = fuchsia_mem;
namespace frunner = fuchsia_component_runner;

constexpr auto kOpenFlags = fio::wire::kOpenRightReadable | fio::wire::kOpenRightExecutable |
                            fio::wire::kOpenFlagNotDirectory;
constexpr auto kVmoFlags = fio::wire::kVmoFlagRead | fio::wire::kVmoFlagExec;

fmem::wire::Buffer GetBuffer(std::string_view path) {
  auto endpoints = fidl::CreateEndpoints<fio::File>();
  EXPECT_TRUE(endpoints.is_ok());
  zx_status_t status = fdio_open(path.data(), kOpenFlags, endpoints->server.channel().release());
  EXPECT_EQ(ZX_OK, status);
  auto result = fidl::WireCall(endpoints->client)->GetBuffer(kVmoFlags);
  EXPECT_EQ(ZX_OK, result->s);
  return std::move(*result->buffer);
}

class TestNode : public fdf::testing::Node_TestBase {
 public:
  bool HasChildren() const { return !controllers_.empty() || !nodes_.empty(); }

 private:
  void AddChild(AddChildRequestView request, AddChildCompleter::Sync& completer) override {
    controllers_.push_back(std::move(request->controller));
    nodes_.push_back(std::move(request->node));
    completer.ReplySuccess();
  }

  void NotImplemented_(const std::string& name, fidl::CompleterBase& completer) override {
    printf("Not implemented: Node::%s\n", name.data());
  }

  std::vector<fidl::ServerEnd<fdf::NodeController>> controllers_;
  std::vector<fidl::ServerEnd<fdf::Node>> nodes_;
};

class TestRootResource : public fboot::testing::RootResource_TestBase {
 private:
  void Get(GetRequestView request, GetCompleter::Sync& completer) override {
    completer.Close(ZX_ERR_NO_RESOURCES);
  }

  void NotImplemented_(const std::string& name, fidl::CompleterBase& completer) override {
    printf("Not implemented: RootResource::%s\n", name.data());
    completer.Close(ZX_ERR_NOT_SUPPORTED);
  }
};

class TestItems : public fboot::testing::Items_TestBase {
 private:
  void NotImplemented_(const std::string& name, fidl::CompleterBase& completer) override {
    printf("Not implemented: Items::%s\n", name.data());
    completer.Close(ZX_ERR_NOT_SUPPORTED);
  }
};

class TestFile : public fio::testing::File_TestBase {
 public:
  void SetStatus(zx_status_t status) { status_ = status; }
  void SetBuffer(fmem::wire::Buffer buffer) { buffer_ = std::move(buffer); }

 private:
  void GetBuffer(GetBufferRequestView request, GetBufferCompleter::Sync& completer) override {
    completer.Reply(status_, fidl::ObjectView<fmem::wire::Buffer>::FromExternal(&buffer_));
  }

  void NotImplemented_(const std::string& name, fidl::CompleterBase& completer) override {
    printf("Not implemented: File::%s\n", name.data());
    completer.Close(ZX_ERR_NOT_SUPPORTED);
  }

  zx_status_t status_ = ZX_OK;
  fmem::wire::Buffer buffer_;
};

class TestDirectory : public fio::testing::Directory_TestBase {
 public:
  using OpenHandler = fit::function<void(OpenRequestView)>;

  void SetOpenHandler(OpenHandler open_handler) { open_handler_ = std::move(open_handler); }

 private:
  void Open(OpenRequestView request, OpenCompleter::Sync& completer) override {
    open_handler_(std::move(request));
  }

  void NotImplemented_(const std::string& name, fidl::CompleterBase& completer) override {
    printf("Not implemented: Directory::%s\n", name.data());
    completer.Close(ZX_ERR_NOT_SUPPORTED);
  }

  OpenHandler open_handler_;
};

class DriverTest : public gtest::TestLoopFixture {
 public:
  void SetUp() override {
    zx_status_t status = loader_loop_.StartThread();
    ASSERT_EQ(ZX_OK, status);
  }

 protected:
  async_dispatcher_t* loader_dispatcher() { return loader_loop_.dispatcher(); }
  TestNode& node() { return node_; }
  TestFile& compat_file() { return compat_file_; }

  void StartDriver(compat::Driver& driver, std::string_view v1_driver_path) {
    auto node_endpoints = fidl::CreateEndpoints<fdf::Node>();
    ASSERT_TRUE(node_endpoints.is_ok());
    auto outgoing_dir_endpoints = fidl::CreateEndpoints<fio::Directory>();
    ASSERT_TRUE(outgoing_dir_endpoints.is_ok());
    auto pkg_endpoints = fidl::CreateEndpoints<fio::Directory>();
    ASSERT_TRUE(pkg_endpoints.is_ok());
    auto svc_endpoints = fidl::CreateEndpoints<fio::Directory>();
    ASSERT_TRUE(svc_endpoints.is_ok());

    // Bind node.
    fidl::BindServer(dispatcher(), std::move(node_endpoints->server), &node_);

    // Setup and bind "/pkg" directory.
    compat_file_.SetBuffer(GetBuffer("/pkg/driver/compat.so"));
    v1_test_file_.SetBuffer(GetBuffer(v1_driver_path));
    pkg_directory_.SetOpenHandler([this](TestDirectory::OpenRequestView request) {
      fidl::ServerEnd<fio::File> server_end(request->object.TakeChannel());
      if (request->path.get() == "driver/compat.so") {
        fidl::BindServer(dispatcher(), std::move(server_end), &compat_file_);
      } else if (request->path.get() == "driver/v1_test.so") {
        fidl::BindServer(dispatcher(), std::move(server_end), &v1_test_file_);
      } else {
        FAIL() << "Unexpected file: " << request->path.get();
      }
    });
    fidl::BindServer(dispatcher(), std::move(pkg_endpoints->server), &pkg_directory_);

    // Setup and bind "/svc" directory.
    svc_directory_.SetOpenHandler([this](TestDirectory::OpenRequestView request) {
      if (request->path.get() == fidl::DiscoverableProtocolName<flogger::LogSink>) {
        zx_status_t status = fdio_service_connect_by_name(
            fidl::DiscoverableProtocolName<flogger::LogSink>, request->object.channel().release());
        ASSERT_EQ(ZX_OK, status);
      } else if (request->path.get() == fidl::DiscoverableProtocolName<fboot::RootResource>) {
        fidl::ServerEnd<fboot::RootResource> server_end(request->object.TakeChannel());
        fidl::BindServer(dispatcher(), std::move(server_end), &root_resource_);
      } else if (request->path.get() == fidl::DiscoverableProtocolName<fboot::Items>) {
        fidl::ServerEnd<fboot::Items> server_end(request->object.TakeChannel());
        fidl::BindServer(dispatcher(), std::move(server_end), &items_);
      } else {
        FAIL() << "Unexpected service: " << request->path.get();
      }
    });
    fidl::BindServer(dispatcher(), std::move(svc_endpoints->server), &svc_directory_);

    // Setup start args.
    fidl::Arena arena;
    fidl::VectorView<frunner::wire::ComponentNamespaceEntry> ns_entries(arena, 2);
    ns_entries[0].Allocate(arena);
    ns_entries[0].set_path(arena, "/pkg");
    ns_entries[0].set_directory(arena, std::move(pkg_endpoints->client));
    ns_entries[1].Allocate(arena);
    ns_entries[1].set_path(arena, "/svc");
    ns_entries[1].set_directory(arena, std::move(svc_endpoints->client));

    fidl::VectorView<fdata::wire::DictionaryEntry> program_entries(arena, 1);
    program_entries[0].key.Set(arena, "compat");
    program_entries[0].value.set_str(arena, "driver/v1_test.so");
    fdata::wire::Dictionary program(arena);
    program.set_entries(arena, std::move(program_entries));

    fdf::wire::DriverStartArgs start_args(arena);
    start_args.set_node(arena, std::move(node_endpoints->client));
    start_args.set_url(arena, "fuchsia-pkg://fuchsia.com/driver#meta/driver.cm");
    start_args.set_program(arena, std::move(program));
    start_args.set_ns(arena, std::move(ns_entries));
    start_args.set_outgoing_dir(arena, std::move(outgoing_dir_endpoints->server));

    // Start driver.
    auto start = driver.Start(&start_args);
    ASSERT_EQ(ZX_OK, start.status_value());
  }

 private:
  async::Loop loader_loop_{&kAsyncLoopConfigNeverAttachToThread};
  TestNode node_;
  TestRootResource root_resource_;
  TestItems items_;
  TestFile compat_file_;
  TestFile v1_test_file_;
  TestDirectory pkg_directory_;
  TestDirectory svc_directory_;
};

TEST_F(DriverTest, Start) {
  zx_protocol_device_t ops{
      .get_protocol = [](void*, uint32_t, void*) { return ZX_OK; },
  };
  auto driver = std::make_unique<compat::Driver>("test-driver", nullptr, &ops, std::nullopt,
                                                 dispatcher(), loader_dispatcher());
  StartDriver(*driver, "/pkg/driver/v1_test.so");

  // Verify that v1_test.so has added a child device.
  EXPECT_FALSE(node().HasChildren());
  ASSERT_TRUE(RunLoopUntilIdle());
  EXPECT_TRUE(node().HasChildren());

  // Verify that v1_test.so has set a context.
  std::unique_ptr<V1Test> v1_test(static_cast<V1Test*>(driver->Context()));
  ASSERT_NE(nullptr, v1_test.get());

  // Verify v1_test.so state after bind.
  EXPECT_EQ(ZX_OK, v1_test->status);
  EXPECT_TRUE(v1_test->did_bind);
  EXPECT_FALSE(v1_test->did_create);
  EXPECT_FALSE(v1_test->did_release);

  // Verify v1_test.so state after release.
  driver.reset();
  ASSERT_TRUE(RunLoopUntilIdle());
  EXPECT_TRUE(v1_test->did_release);
}

TEST_F(DriverTest, Start_WithCreate) {
  zx_protocol_device_t ops{};
  auto driver = std::make_unique<compat::Driver>("test-driver", nullptr, &ops, std::nullopt,
                                                 dispatcher(), loader_dispatcher());
  StartDriver(*driver, "/pkg/driver/v1_create_test.so");

  // Verify that v1_test.so has added a child device.
  EXPECT_FALSE(node().HasChildren());
  ASSERT_TRUE(RunLoopUntilIdle());
  EXPECT_TRUE(node().HasChildren());

  // Verify that v1_test.so has set a context.
  std::unique_ptr<V1Test> v1_test(static_cast<V1Test*>(driver->Context()));
  ASSERT_NE(nullptr, v1_test.get());

  // Verify v1_test.so state after bind.
  EXPECT_EQ(ZX_OK, v1_test->status);
  EXPECT_FALSE(v1_test->did_bind);
  EXPECT_TRUE(v1_test->did_create);
  EXPECT_FALSE(v1_test->did_release);

  // Verify v1_test.so state after release.
  driver.reset();
  ASSERT_TRUE(RunLoopUntilIdle());
  EXPECT_TRUE(v1_test->did_release);
}

TEST_F(DriverTest, Start_MissingBindAndCreate) {
  zx_protocol_device_t ops{};
  auto driver = std::make_unique<compat::Driver>("test-driver", nullptr, &ops, std::nullopt,
                                                 dispatcher(), loader_dispatcher());
  StartDriver(*driver, "/pkg/driver/v1_missing_test.so");

  // Verify that v1_test.so has not added a child device.
  EXPECT_FALSE(node().HasChildren());
  ASSERT_TRUE(RunLoopUntilIdle());
  EXPECT_FALSE(node().HasChildren());

  // Verify that v1_test.so has not set a context.
  EXPECT_EQ(nullptr, driver->Context());
}

TEST_F(DriverTest, Start_GetBufferFailed) {
  zx_protocol_device_t ops{};
  auto driver = std::make_unique<compat::Driver>("test-driver", nullptr, &ops, std::nullopt,
                                                 dispatcher(), loader_dispatcher());
  compat_file().SetStatus(ZX_ERR_UNAVAILABLE);
  StartDriver(*driver, "/pkg/driver/v1_test.so");

  // Verify that v1_test.so has not added a child device.
  EXPECT_FALSE(node().HasChildren());
  ASSERT_TRUE(RunLoopUntilIdle());
  EXPECT_FALSE(node().HasChildren());

  // Verify that v1_test.so has not set a context.
  EXPECT_EQ(nullptr, driver->Context());
}

TEST_F(DriverTest, Start_BindFailed) {
  zx_protocol_device_t ops{};
  auto driver = std::make_unique<compat::Driver>("test-driver", nullptr, &ops, std::nullopt,
                                                 dispatcher(), loader_dispatcher());
  StartDriver(*driver, "/pkg/driver/v1_test.so");

  // Verify that v1_test.so has added a child device.
  EXPECT_FALSE(node().HasChildren());
  ASSERT_TRUE(RunLoopUntilIdle());
  EXPECT_FALSE(node().HasChildren());

  // Verify that v1_test.so has set a context.
  std::unique_ptr<V1Test> v1_test(static_cast<V1Test*>(driver->Context()));
  ASSERT_NE(nullptr, v1_test.get());

  // Verify v1_test.so state after bind.
  EXPECT_EQ(ZX_ERR_UNAVAILABLE, v1_test->status);
  EXPECT_TRUE(v1_test->did_bind);
  EXPECT_FALSE(v1_test->did_create);
  EXPECT_FALSE(v1_test->did_release);

  // Verify v1_test.so state after release.
  driver.reset();
  ASSERT_TRUE(RunLoopUntilIdle());
  EXPECT_TRUE(v1_test->did_release);
}
