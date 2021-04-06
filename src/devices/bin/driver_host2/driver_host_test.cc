// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/devices/bin/driver_host2/driver_host.h"

#include <fuchsia/driverhost/test/llcpp/fidl.h>
#include <fuchsia/io/cpp/fidl_test_base.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fdio/directory.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/gtest/test_loop_fixture.h>
#include <lib/inspect/cpp/reader.h>
#include <lib/inspect/testing/cpp/inspect.h>

#include <fbl/string_printf.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "src/lib/storage/vfs/cpp/pseudo_dir.h"
#include "src/lib/storage/vfs/cpp/service.h"
#include "src/lib/storage/vfs/cpp/synchronous_vfs.h"

namespace fdata = fuchsia_data;
namespace fdf = fuchsia_driver_framework;
namespace fio = fuchsia::io;
namespace fmem = fuchsia::mem;
namespace frunner = fuchsia_component_runner;
namespace ftest = fuchsia_driverhost_test;

using Completer = fdf::DriverHost::Interface::StartCompleter::Sync;
using namespace inspect::testing;

class fake_context : public fit::context {
 public:
  fit::executor* executor() const override {
    EXPECT_TRUE(false);
    return nullptr;
  }

  fit::suspended_task suspend_task() override {
    EXPECT_TRUE(false);
    return fit::suspended_task();
  }
};

class TestFile : public fio::testing::File_TestBase {
 public:
  TestFile(std::string_view path) : path_(std::move(path)) {}

 private:
  void GetBuffer(uint32_t flags, GetBufferCallback callback) override {
    EXPECT_EQ(fio::VMO_FLAG_READ | fio::VMO_FLAG_EXEC | fio::VMO_FLAG_PRIVATE, flags);
    zx::channel client_end, server_end;
    ASSERT_EQ(ZX_OK, zx::channel::create(0, &client_end, &server_end));
    EXPECT_EQ(ZX_OK, fdio_open(path_.data(), fio::OPEN_RIGHT_READABLE | fio::OPEN_RIGHT_EXECUTABLE,
                               server_end.release()));

    fidl::WireSyncClient<fuchsia_io::File> file(std::move(client_end));
    auto result = file.GetBuffer(flags);
    EXPECT_TRUE(result.ok());
    auto buffer = fmem::Buffer::New();
    buffer->vmo = std::move(result->buffer->vmo);
    buffer->size = result->buffer->size;
    callback(ZX_OK, std::move(buffer));
  }

  void NotImplemented_(const std::string& name) override {
    printf("Not implemented: File::%s\n", name.data());
  }

  std::string_view path_;
};

class TestDirectory : public fio::testing::Directory_TestBase {
 public:
  using OpenHandler = fit::function<void(uint32_t flags, std::string path,
                                         fidl::InterfaceRequest<fio::Node> object)>;

  void SetOpenHandler(OpenHandler open_handler) { open_handler_ = std::move(open_handler); }

 private:
  void Open(uint32_t flags, uint32_t mode, std::string path,
            fidl::InterfaceRequest<fio::Node> object) override {
    open_handler_(flags, std::move(path), std::move(object));
  }

  void NotImplemented_(const std::string& name) override {
    printf("Not implemented: Directory::%s\n", name.data());
  }

  OpenHandler open_handler_;
};

class TestTransaction : public fidl::Transaction {
 public:
  TestTransaction(zx_status_t* epitaph) : epitaph_(epitaph) {}

 private:
  std::unique_ptr<Transaction> TakeOwnership() override {
    return std::make_unique<TestTransaction>(epitaph_);
  }

  zx_status_t Reply(fidl::OutgoingMessage* message) override {
    EXPECT_TRUE(false);
    return ZX_OK;
  }

  void Close(zx_status_t epitaph) override { *epitaph_ = epitaph; }

  zx_status_t* const epitaph_;
};

struct StartDriverResult {
  zx::channel driver;
  zx::channel outgoing_dir;
};

class DriverHostTest : public gtest::TestLoopFixture {
 public:
  void TearDown() override {
    loop_.Shutdown();
    TestLoopFixture::TearDown();
  }

 protected:
  async::Loop& loop() { return loop_; }
  fdf::DriverHost::Interface* driver_host() { return &driver_host_; }

  void AddEntry(fs::Service::Connector connector) {
    EXPECT_EQ(ZX_OK, svc_dir_->AddEntry(ftest::Incoming::Name,
                                        fbl::MakeRefCounted<fs::Service>(std::move(connector))));
  }

  StartDriverResult StartDriver(fidl::VectorView<fdf::wire::NodeSymbol> symbols = {},
                                fidl::ClientEnd<fuchsia_driver_framework::Node>* node = nullptr,
                                zx_status_t expected_epitaph = ZX_OK) {
    zx_status_t epitaph = ZX_OK;
    TestTransaction transaction(&epitaph);
    fidl::FidlAllocator allocator;

    auto pkg_endpoints = fidl::CreateEndpoints<fuchsia_io::Directory>();
    EXPECT_TRUE(pkg_endpoints.is_ok());
    auto svc_endpoints = fidl::CreateEndpoints<fuchsia_io::Directory>();
    EXPECT_TRUE(svc_endpoints.is_ok());

    fidl::VectorView<frunner::wire::ComponentNamespaceEntry> ns_entries(allocator, 2);
    ns_entries[0].Allocate(allocator);
    ns_entries[0].set_path(allocator, "/pkg");
    ns_entries[0].set_directory(allocator, std::move(pkg_endpoints->client));
    ns_entries[1].Allocate(allocator);
    ns_entries[1].set_path(allocator, "/svc");
    ns_entries[1].set_directory(allocator, std::move(svc_endpoints->client));

    TestFile file("/pkg/driver/test_driver.so");
    fidl::Binding<fio::File> file_binding(&file);
    TestDirectory pkg_directory;
    fidl::Binding<fio::Directory> pkg_binding(&pkg_directory);
    pkg_binding.Bind(pkg_endpoints->server.TakeChannel(), loop().dispatcher());
    pkg_directory.SetOpenHandler(
        [this, &file_binding](uint32_t flags, std::string path, auto object) {
          EXPECT_EQ(fio::OPEN_RIGHT_READABLE | fio::OPEN_RIGHT_EXECUTABLE, flags);
          EXPECT_EQ("driver/library.so", path);
          file_binding.Bind(object.TakeChannel(), loop().dispatcher());
        });
    EXPECT_EQ(ZX_OK, vfs_.ServeDirectory(svc_dir_, std::move(svc_endpoints->server)));

    fidl::VectorView<fdata::wire::DictionaryEntry> program_entries(allocator, 1);
    program_entries[0].key.Set(allocator, "binary");
    program_entries[0].value.set_str(allocator, "driver/library.so");

    auto outgoing_dir_endpoints = fidl::CreateEndpoints<fuchsia_io::Directory>();
    EXPECT_TRUE(outgoing_dir_endpoints.is_ok());
    auto driver_endpoints = fidl::CreateEndpoints<fdf::Driver>();
    EXPECT_TRUE(driver_endpoints.is_ok());
    {
      fdata::wire::Dictionary dictionary(allocator);
      dictionary.set_entries(allocator, std::move(program_entries));

      fdf::wire::DriverStartArgs driver_start_args(allocator);
      if (node != nullptr) {
        driver_start_args.set_node(allocator, std::move(*node));
      }
      driver_start_args.set_symbols(allocator, std::move(symbols));
      driver_start_args.set_url(allocator, "fuchsia-pkg://fuchsia.com/driver#meta/driver.cm");
      driver_start_args.set_program(allocator, std::move(dictionary));
      driver_start_args.set_ns(allocator, std::move(ns_entries));
      driver_start_args.set_outgoing_dir(allocator, std::move(outgoing_dir_endpoints->server));
      Completer completer(&transaction);
      driver_host()->Start(std::move(driver_start_args), std::move(driver_endpoints->server),
                           completer);
    }
    loop().RunUntilIdle();
    EXPECT_EQ(expected_epitaph, epitaph);

    return {
        .driver = driver_endpoints->client.TakeChannel(),
        .outgoing_dir = outgoing_dir_endpoints->client.TakeChannel(),
    };
  }

  inspect::Hierarchy Inspect() {
    fake_context context;
    auto inspector = driver_host_.Inspect()(context).take_value();
    return inspect::ReadFromInspector(inspector)(context).take_value();
  }

 private:
  inspect::Inspector inspector_;
  async::Loop loop_{&kAsyncLoopConfigNoAttachToCurrentThread};
  DriverHost driver_host_{&inspector_, &loop_};
  fs::SynchronousVfs vfs_{loop_.dispatcher()};
  fbl::RefPtr<fs::PseudoDir> svc_dir_ = fbl::MakeRefCounted<fs::PseudoDir>();
};

// Start a single driver in the driver host.
TEST_F(DriverHostTest, Start_SingleDriver) {
  auto [driver, outgoing_dir] = StartDriver();

  // Stop the driver. As it's the last driver in the driver host, this will
  // cause the driver host to stop.
  driver.reset();
  loop().RunUntilIdle();
  EXPECT_EQ(ASYNC_LOOP_QUIT, loop().GetState());
}

// Start multiple drivers in the driver host.
TEST_F(DriverHostTest, Start_MultipleDrivers) {
  auto [driver_1, outgoing_dir_1] = StartDriver();
  auto [driver_2, outgoing_dir_2] = StartDriver();

  driver_1.reset();
  loop().RunUntilIdle();
  EXPECT_EQ(ASYNC_LOOP_RUNNABLE, loop().GetState());

  driver_2.reset();
  loop().RunUntilIdle();
  EXPECT_EQ(ASYNC_LOOP_QUIT, loop().GetState());
}

// Start a single driver, and connect to its outgoing service.
TEST_F(DriverHostTest, Start_OutgoingServices) {
  auto [driver, outgoing_dir] = StartDriver();

  auto path = fbl::StringPrintf("svc/%s", ftest::Outgoing::Name);
  zx::channel client_end, server_end;
  EXPECT_EQ(ZX_OK, zx::channel::create(0, &client_end, &server_end));
  zx_status_t status =
      fdio_service_connect_at(outgoing_dir.get(), path.data(), server_end.release());
  EXPECT_EQ(ZX_OK, status);

  class EventHandler : public fidl::WireAsyncEventHandler<ftest::Outgoing> {
   public:
    EventHandler() = default;

    zx_status_t status() const { return status_; }

    void Unbound(fidl::UnbindInfo info) override { status_ = info.status; }

   private:
    zx_status_t status_ = ZX_ERR_INVALID_ARGS;
  };

  auto event_handler = std::make_shared<EventHandler>();
  fidl::Client<ftest::Outgoing> outgoing(std::move(client_end), loop().dispatcher(), event_handler);
  loop().RunUntilIdle();
  EXPECT_EQ(ZX_ERR_STOP, event_handler->status());

  driver.reset();
  loop().RunUntilIdle();
  EXPECT_EQ(ASYNC_LOOP_QUIT, loop().GetState());
}

// Start a single driver, and receive an incoming connection to our service.
TEST_F(DriverHostTest, Start_IncomingServices) {
  bool connected = false;
  AddEntry([&connected](zx::channel request) {
    connected = true;
    return ZX_OK;
  });
  auto [driver, outgoing_dir] = StartDriver();
  EXPECT_TRUE(connected);

  driver.reset();
  loop().RunUntilIdle();
  EXPECT_EQ(ASYNC_LOOP_QUIT, loop().GetState());
}

static bool called = false;
void func() { called = true; }

// Start a single driver, and receive a call to a shared function.
TEST_F(DriverHostTest, Start_NodeSymbols) {
  fidl::FidlAllocator allocator;
  fidl::VectorView<fdf::wire::NodeSymbol> symbols(allocator, 1);
  symbols[0].Allocate(allocator);
  symbols[0].set_name(allocator, "func");
  symbols[0].set_address(allocator, reinterpret_cast<zx_vaddr_t>(func));
  auto [driver, outgoing_dir] = StartDriver(std::move(symbols));
  EXPECT_TRUE(called);

  driver.reset();
  loop().RunUntilIdle();
  EXPECT_EQ(ASYNC_LOOP_QUIT, loop().GetState());
}

// Start a driver with invalid arguments.
TEST_F(DriverHostTest, Start_InvalidStartArgs) {
  zx_status_t epitaph = ZX_OK;
  TestTransaction transaction(&epitaph);

  // DriverStartArgs::ns is missing "/pkg" entry.
  zx::channel driver_client_end, driver_server_end;
  ASSERT_EQ(ZX_OK, zx::channel::create(0, &driver_client_end, &driver_server_end));
  {
    Completer completer(&transaction);
    driver_host()->Start(fdf::wire::DriverStartArgs(), std::move(driver_server_end), completer);
  }
  EXPECT_EQ(ZX_ERR_INVALID_ARGS, epitaph);

  // DriverStartArgs::ns not set.
  ASSERT_EQ(ZX_OK, zx::channel::create(0, &driver_client_end, &driver_server_end));
  {
    Completer completer(&transaction);
    fidl::FidlAllocator allocator;
    fdf::wire::DriverStartArgs driver_start_args(allocator);
    driver_start_args.set_url(allocator, "fuchsia-pkg://fuchsia.com/driver#meta/driver.cm");

    driver_host()->Start(std::move(driver_start_args),
                         fidl::ServerEnd<fdf::Driver>(std::move(driver_server_end)), completer);
  }
  EXPECT_EQ(ZX_ERR_INVALID_ARGS, epitaph);

  // DriverStartArgs::ns is missing "/pkg" entry.
  // TODO(fxbug.dev/65212): Migrate the use of |zx::channel::create| to |fidl::CreateEndpoints|.
  ASSERT_EQ(ZX_OK, zx::channel::create(0, &driver_client_end, &driver_server_end));
  {
    Completer completer(&transaction);
    fidl::FidlAllocator allocator;
    fdf::wire::DriverStartArgs driver_start_args(allocator);
    driver_start_args.set_url(allocator, "fuchsia-pkg://fuchsia.com/driver#meta/driver.cm");
    driver_start_args.set_ns(allocator);
    driver_host()->Start(std::move(driver_start_args),
                         fidl::ServerEnd<fdf::Driver>(std::move(driver_server_end)), completer);
  }
  EXPECT_EQ(ZX_ERR_NOT_FOUND, epitaph);

  ASSERT_EQ(ZX_OK, zx::channel::create(0, &driver_client_end, &driver_server_end));
  {
    fidl::FidlAllocator allocator;
    // DriverStartArgs::program not set.
    fidl::VectorView<frunner::wire::ComponentNamespaceEntry> entries1(allocator, 1);
    entries1[0].Allocate(allocator);
    entries1[0].set_path(allocator, "/pkg");
    entries1[0].set_directory(allocator, fidl::ClientEnd<fuchsia_io::Directory>());

    fdf::wire::DriverStartArgs driver_start_args(allocator);
    driver_start_args.set_url(allocator, "fuchsia-pkg://fuchsia.com/driver#meta/driver.cm");
    driver_start_args.set_ns(allocator, std::move(entries1));

    Completer completer(&transaction);
    driver_host()->Start(std::move(driver_start_args),
                         fidl::ServerEnd<fdf::Driver>(std::move(driver_server_end)), completer);
  }
  EXPECT_EQ(ZX_ERR_INVALID_ARGS, epitaph);

  ASSERT_EQ(ZX_OK, zx::channel::create(0, &driver_client_end, &driver_server_end));
  {
    // DriverStartArgs::program is missing "binary" entry.
    fidl::FidlAllocator allocator;
    fidl::VectorView<frunner::wire::ComponentNamespaceEntry> entries2(allocator, 1);
    entries2[0].Allocate(allocator);
    entries2[0].set_path(allocator, "/pkg");
    entries2[0].set_directory(allocator, fidl::ClientEnd<fuchsia_io::Directory>());

    fdf::wire::DriverStartArgs driver_start_args(allocator);
    driver_start_args.set_url(allocator, "fuchsia-pkg://fuchsia.com/driver#meta/driver.cm");
    driver_start_args.set_program(allocator);
    driver_start_args.set_ns(allocator, std::move(entries2));

    Completer completer(&transaction);
    driver_host()->Start(std::move(driver_start_args),
                         fidl::ServerEnd<fdf::Driver>(std::move(driver_server_end)), completer);
  }
  EXPECT_EQ(ZX_ERR_NOT_FOUND, epitaph);
}

// Start a driver with an invalid client-end.
TEST_F(DriverHostTest, InvalidHandleRights) {
  bool connected = false;
  AddEntry([&connected](zx::channel request) {
    connected = true;
    return ZX_OK;
  });
  zx::channel client, server;
  ASSERT_EQ(ZX_OK, zx::channel::create(0, &client, &server));
  ASSERT_EQ(ZX_OK, client.replace(ZX_RIGHT_TRANSFER, &client));
  fidl::ClientEnd<fdf::Node> client_end(std::move(client));
  // This should fail when node rights are not ZX_DEFAULT_CHANNEL_RIGHTS.
  StartDriver({}, &client_end, ZX_ERR_INVALID_ARGS);
  EXPECT_FALSE(connected);
}

// Start a driver with an invalid binary.
TEST_F(DriverHostTest, Start_InvalidBinary) {
  zx_status_t epitaph = ZX_OK;
  TestTransaction transaction(&epitaph);
  fidl::FidlAllocator allocator;

  zx::channel pkg_client_end, pkg_server_end;
  EXPECT_EQ(ZX_OK, zx::channel::create(0, &pkg_client_end, &pkg_server_end));
  fidl::VectorView<frunner::wire::ComponentNamespaceEntry> ns_entries(allocator, 1);
  ns_entries[0].Allocate(allocator);
  ns_entries[0].set_path(allocator, "/pkg");
  ns_entries[0].set_directory(allocator,
                              fidl::ClientEnd<fuchsia_io::Directory>(std::move(pkg_client_end)));
  TestFile file("/pkg/driver/test_not_driver.so");
  fidl::Binding<fio::File> file_binding(&file);
  TestDirectory pkg_directory;
  fidl::Binding<fio::Directory> pkg_binding(&pkg_directory);
  pkg_binding.Bind(std::move(pkg_server_end), loop().dispatcher());
  pkg_directory.SetOpenHandler(
      [this, &file_binding](uint32_t flags, std::string path, auto object) {
        EXPECT_EQ(fio::OPEN_RIGHT_READABLE | fio::OPEN_RIGHT_EXECUTABLE, flags);
        EXPECT_EQ("driver/library.so", path);
        file_binding.Bind(object.TakeChannel(), loop().dispatcher());
      });
  fidl::VectorView<fdata::wire::DictionaryEntry> program_entries(allocator, 1);
  program_entries[0].key.Set(allocator, "binary");
  program_entries[0].value.set_str(allocator, "driver/library.so");

  zx::channel driver_client_end, driver_server_end;
  EXPECT_EQ(ZX_OK, zx::channel::create(0, &driver_client_end, &driver_server_end));
  {
    Completer completer(&transaction);
    fdata::wire::Dictionary dictionary(allocator);
    dictionary.set_entries(allocator, std::move(program_entries));

    fdf::wire::DriverStartArgs driver_start_args(allocator);
    driver_start_args.set_url(allocator, "fuchsia-pkg://fuchsia.com/driver#meta/driver.cm");
    driver_start_args.set_program(allocator, std::move(dictionary));
    driver_start_args.set_ns(allocator, std::move(ns_entries));

    driver_host()->Start(std::move(driver_start_args), std::move(driver_server_end), completer);
  }
  loop().RunUntilIdle();
  EXPECT_EQ(ZX_ERR_NOT_FOUND, epitaph);
}

// Start multiple drivers and inspect the driver host.
TEST_F(DriverHostTest, StartAndInspect) {
  auto [driver_1, outgoing_dir_1] = StartDriver();
  auto [driver_2, outgoing_dir_2] = StartDriver();

  EXPECT_THAT(Inspect(),
              AllOf(NodeMatches(NameMatches("root")),
                    ChildrenMatch(UnorderedElementsAre(
                        AllOf(NodeMatches(AllOf(
                            NameMatches("driver-1"),
                            PropertyList(UnorderedElementsAre(
                                StringIs("url", "fuchsia-pkg://fuchsia.com/driver#meta/driver.cm"),
                                StringIs("binary", "driver/library.so")))))),
                        AllOf(NodeMatches(AllOf(
                            NameMatches("driver-2"),
                            PropertyList(UnorderedElementsAre(
                                StringIs("url", "fuchsia-pkg://fuchsia.com/driver#meta/driver.cm"),
                                StringIs("binary", "driver/library.so"))))))))));

  driver_1.reset();
  driver_2.reset();
  loop().RunUntilIdle();
  EXPECT_EQ(ASYNC_LOOP_QUIT, loop().GetState());
}
