// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/devices/bin/driver_host2/driver_host.h"

#include <fidl/fuchsia.driverhost.test/cpp/wire.h>
#include <fuchsia/io/cpp/fidl_test_base.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fdio/directory.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/inspect/cpp/reader.h>
#include <lib/inspect/testing/cpp/inspect.h>
#include <lib/service/llcpp/service.h>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "src/lib/storage/vfs/cpp/pseudo_dir.h"
#include "src/lib/storage/vfs/cpp/service.h"
#include "src/lib/storage/vfs/cpp/synchronous_vfs.h"

namespace fdata = fuchsia_data;
namespace fdf = fuchsia_driver_framework;
namespace fio = fuchsia::io;
namespace frunner = fuchsia_component_runner;
namespace ftest = fuchsia_driverhost_test;

using Completer = fidl::WireServer<fdf::DriverHost>::StartCompleter::Sync;
using namespace inspect::testing;

class FakeContext : public fpromise::context {
 public:
  fpromise::executor* executor() const override {
    EXPECT_TRUE(false);
    return nullptr;
  }

  fpromise::suspended_task suspend_task() override {
    EXPECT_TRUE(false);
    return fpromise::suspended_task();
  }
};

class TestFile : public fio::testing::File_TestBase {
 public:
  TestFile(std::string_view path) : path_(std::move(path)) {}

 private:
  void GetBackingMemory(fio::VmoFlags flags, GetBackingMemoryCallback callback) override {
    EXPECT_EQ(fio::VmoFlags::READ | fio::VmoFlags::EXECUTE | fio::VmoFlags::PRIVATE_CLONE, flags);
    auto endpoints = fidl::CreateEndpoints<fuchsia_io::File>();
    ASSERT_TRUE(endpoints.is_ok());
    EXPECT_EQ(ZX_OK, fdio_open(path_.data(), fio::OPEN_RIGHT_READABLE | fio::OPEN_RIGHT_EXECUTABLE,
                               endpoints->server.channel().release()));

    fidl::WireSyncClient<fuchsia_io::File> file(std::move(endpoints->client));
    fidl::WireResult result = file->GetBackingMemory(fuchsia_io::wire::VmoFlags(uint32_t(flags)));
    EXPECT_TRUE(result.ok()) << result.FormatDescription();
    auto& response = result.value();
    switch (response.result.Which()) {
      case fuchsia_io::wire::File2GetBackingMemoryResult::Tag::kErr:
        callback(fio::File2_GetBackingMemory_Result::WithErr(std::move(response.result.err())));
        break;
      case fuchsia_io::wire::File2GetBackingMemoryResult::Tag::kResponse:
        callback(fio::File2_GetBackingMemory_Result::WithResponse(
            fio::File2_GetBackingMemory_Response(std::move(response.result.response().vmo))));
        break;
    }
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
  TestTransaction(zx_status_t& epitaph) : epitaph_(epitaph) {}

 private:
  std::unique_ptr<Transaction> TakeOwnership() override {
    return std::make_unique<TestTransaction>(epitaph_);
  }

  zx_status_t Reply(fidl::OutgoingMessage* message, fidl::WriteOptions) override {
    EXPECT_TRUE(false);
    return ZX_OK;
  }

  void Close(zx_status_t epitaph) override { epitaph_ = epitaph; }

  zx_status_t& epitaph_;
};

struct StartDriverResult {
  fidl::ClientEnd<fdf::Driver> driver;
  fidl::ClientEnd<fuchsia_io::Directory> outgoing_dir;
};

class DriverHostTest : public testing::Test {
 protected:
  async::Loop& loop() { return loop_; }
  async::Loop& second_loop() { return second_loop_; }
  fidl::WireServer<fdf::DriverHost>& driver_host() { return *driver_host_; }
  void set_driver_host(std::unique_ptr<DriverHost> driver_host) {
    driver_host_ = std::move(driver_host);
  }

  void AddEntry(fs::Service::Connector connector) {
    EXPECT_EQ(ZX_OK, svc_dir_->AddEntry(fidl::DiscoverableProtocolName<ftest::Incoming>,
                                        fbl::MakeRefCounted<fs::Service>(std::move(connector))));
  }

  StartDriverResult StartDriver(fidl::VectorView<fdf::wire::NodeSymbol> symbols = {},
                                fidl::ClientEnd<fuchsia_driver_framework::Node>* node = nullptr,
                                zx_status_t expected_epitaph = ZX_OK) {
    zx_status_t epitaph = ZX_OK;
    TestTransaction transaction(epitaph);
    fidl::Arena arena;

    auto pkg_endpoints = fidl::CreateEndpoints<fuchsia_io::Directory>();
    EXPECT_TRUE(pkg_endpoints.is_ok());
    auto svc_endpoints = fidl::CreateEndpoints<fuchsia_io::Directory>();
    EXPECT_TRUE(svc_endpoints.is_ok());

    fidl::VectorView<frunner::wire::ComponentNamespaceEntry> ns_entries(arena, 2);
    ns_entries[0].Allocate(arena);
    ns_entries[0].set_path(arena, "/pkg");
    ns_entries[0].set_directory(std::move(pkg_endpoints->client));
    ns_entries[1].Allocate(arena);
    ns_entries[1].set_path(arena, "/svc");
    ns_entries[1].set_directory(std::move(svc_endpoints->client));

    TestFile file("/pkg/driver/test_driver.so");
    fidl::Binding<fio::File> file_binding(&file);
    TestDirectory pkg_directory;
    fidl::Binding<fio::Directory> pkg_binding(&pkg_directory);
    pkg_binding.Bind(pkg_endpoints->server.TakeChannel(), loop_.dispatcher());
    pkg_directory.SetOpenHandler(
        [this, &file_binding](uint32_t flags, std::string path, auto object) {
          EXPECT_EQ(fio::OPEN_RIGHT_READABLE | fio::OPEN_RIGHT_EXECUTABLE, flags);
          EXPECT_EQ("driver/library.so", path);
          file_binding.Bind(object.TakeChannel(), loop_.dispatcher());
        });
    EXPECT_EQ(ZX_OK, vfs_.ServeDirectory(svc_dir_, std::move(svc_endpoints->server)));

    fidl::VectorView<fdata::wire::DictionaryEntry> program_entries(arena, 1);
    program_entries[0].key.Set(arena, "binary");
    program_entries[0].value.set_str(arena, "driver/library.so");

    auto outgoing_dir_endpoints = fidl::CreateEndpoints<fuchsia_io::Directory>();
    EXPECT_TRUE(outgoing_dir_endpoints.is_ok());
    auto driver_endpoints = fidl::CreateEndpoints<fdf::Driver>();
    EXPECT_TRUE(driver_endpoints.is_ok());
    {
      fdata::wire::Dictionary dictionary(arena);
      dictionary.set_entries(arena, std::move(program_entries));

      fdf::wire::DriverStartArgs driver_start_args(arena);
      if (node != nullptr) {
        driver_start_args.set_node(std::move(*node));
      }
      driver_start_args.set_symbols(arena, std::move(symbols));
      driver_start_args.set_url(arena, "fuchsia-pkg://fuchsia.com/driver#meta/driver.cm");
      driver_start_args.set_program(arena, std::move(dictionary));
      driver_start_args.set_ns(arena, std::move(ns_entries));
      driver_start_args.set_outgoing_dir(std::move(outgoing_dir_endpoints->server));
      Completer completer(&transaction);
      fidl::WireRequest<fdf::DriverHost::Start> request(driver_start_args,
                                                        std::move(driver_endpoints->server));
      driver_host().Start(&request, completer);
    }
    EXPECT_EQ(ZX_OK, loop().RunUntilIdle());
    EXPECT_EQ(expected_epitaph, epitaph);

    return {
        .driver = std::move(driver_endpoints->client),
        .outgoing_dir = std::move(outgoing_dir_endpoints->client),
    };
  }

  inspect::Hierarchy Inspect() {
    FakeContext context;
    auto inspector = driver_host_->Inspect()(context).take_value();
    return inspect::ReadFromInspector(inspector)(context).take_value();
  }

 private:
  inspect::Inspector inspector_;
  async::Loop second_loop_{&kAsyncLoopConfigNeverAttachToThread};
  async::Loop loop_{&kAsyncLoopConfigNeverAttachToThread};
  std::unique_ptr<DriverHost> driver_host_ =
      std::make_unique<DriverHost>(inspector_, loop_, loop_.dispatcher());
  fs::SynchronousVfs vfs_{loop_.dispatcher()};
  fbl::RefPtr<fs::PseudoDir> svc_dir_ = fbl::MakeRefCounted<fs::PseudoDir>();
};

// Start a single driver in the driver host.
TEST_F(DriverHostTest, Start_SingleDriver) {
  auto [driver, outgoing_dir] = StartDriver();

  // Stop the driver. As it's the last driver in the driver host, this will
  // cause the driver host to stop.
  driver.reset();
  EXPECT_EQ(ZX_ERR_CANCELED, loop().RunUntilIdle());
  EXPECT_EQ(ASYNC_LOOP_QUIT, loop().GetState());
}

// Start multiple drivers in the driver host.
TEST_F(DriverHostTest, Start_MultipleDrivers) {
  auto [driver_1, outgoing_dir_1] = StartDriver();
  auto [driver_2, outgoing_dir_2] = StartDriver();

  driver_1.reset();
  EXPECT_EQ(ZX_OK, loop().RunUntilIdle());
  EXPECT_EQ(ASYNC_LOOP_RUNNABLE, loop().GetState());

  driver_2.reset();
  EXPECT_EQ(ZX_ERR_CANCELED, loop().RunUntilIdle());
  EXPECT_EQ(ASYNC_LOOP_QUIT, loop().GetState());
}

// Start a single driver, and connect to its outgoing service.
TEST_F(DriverHostTest, Start_OutgoingServices) {
  auto [driver, outgoing_dir] = StartDriver();

  // Note, we skip the leading '/' in the default path to form a relative path.
  auto path = fidl::DiscoverableProtocolDefaultPath<ftest::Outgoing> + 1;
  auto client_end = service::ConnectAt<ftest::Outgoing>(outgoing_dir, path);
  ASSERT_TRUE(client_end.is_ok());

  class EventHandler : public fidl::WireAsyncEventHandler<ftest::Outgoing> {
   public:
    EventHandler() = default;

    zx_status_t status() const { return status_; }

    void on_fidl_error(fidl::UnbindInfo info) override { status_ = info.status(); }

   private:
    zx_status_t status_ = ZX_ERR_INVALID_ARGS;
  };

  EventHandler event_handler;
  fidl::WireClient<ftest::Outgoing> outgoing(std::move(*client_end), loop().dispatcher(),
                                             &event_handler);
  EXPECT_EQ(ZX_OK, loop().RunUntilIdle());
  EXPECT_EQ(ZX_ERR_STOP, event_handler.status());

  driver.reset();
  EXPECT_EQ(ZX_ERR_CANCELED, loop().RunUntilIdle());
  EXPECT_EQ(ASYNC_LOOP_QUIT, loop().GetState());
}

// Start a single driver, and receive an incoming connection to our service.
TEST_F(DriverHostTest, Start_IncomingServices) {
  bool connected = false;
  AddEntry([&connected](auto request) {
    connected = true;
    return ZX_OK;
  });
  auto [driver, outgoing_dir] = StartDriver();
  EXPECT_TRUE(connected);

  driver.reset();
  EXPECT_EQ(ZX_ERR_CANCELED, loop().RunUntilIdle());
  EXPECT_EQ(ASYNC_LOOP_QUIT, loop().GetState());
}

// Start a single driver, and return an error on start.
TEST_F(DriverHostTest, Start_ReturnError) {
  zx_status_t error = ZX_ERR_STOP;
  fidl::Arena arena;
  fidl::VectorView<fdf::wire::NodeSymbol> symbols(arena, 1);
  symbols[0].Allocate(arena);
  symbols[0].set_name(arena, "error");
  symbols[0].set_address(arena, reinterpret_cast<zx_vaddr_t>(&error));
  auto [driver, outgoing_dir] = StartDriver(std::move(symbols), nullptr, error);

  driver.reset();
  EXPECT_EQ(ZX_OK, loop().RunUntilIdle());
  // We never started our first driver, so the driver host would not attempt to
  // quit the loop after the last driver has stopped.
  EXPECT_EQ(ASYNC_LOOP_RUNNABLE, loop().GetState());
}

static bool called = false;
void Func() { called = true; }

// Start a single driver, and receive a call to a shared function.
TEST_F(DriverHostTest, Start_NodeSymbols) {
  fidl::Arena arena;
  fidl::VectorView<fdf::wire::NodeSymbol> symbols(arena, 1);
  symbols[0].Allocate(arena);
  symbols[0].set_name(arena, "func");
  symbols[0].set_address(arena, reinterpret_cast<zx_vaddr_t>(Func));
  auto [driver, outgoing_dir] = StartDriver(std::move(symbols));
  EXPECT_TRUE(called);

  driver.reset();
  EXPECT_EQ(ZX_ERR_CANCELED, loop().RunUntilIdle());
  EXPECT_EQ(ASYNC_LOOP_QUIT, loop().GetState());
}

// Start a single driver, and verify that a different dispatcher is used.
TEST_F(DriverHostTest, Start_DifferentDispatcher) {
  inspect::Inspector inspector;
  auto driver_host = std::make_unique<DriverHost>(inspector, loop(), second_loop().dispatcher());
  set_driver_host(std::move(driver_host));

  async_dispatcher_t* dispatcher = nullptr;
  fidl::Arena arena;
  fidl::VectorView<fdf::wire::NodeSymbol> symbols(arena, 1);
  symbols[0].Allocate(arena);
  symbols[0].set_name(arena, "dispatcher");
  symbols[0].set_address(arena, reinterpret_cast<zx_vaddr_t>(&dispatcher));
  auto [driver, outgoing_dir] = StartDriver(std::move(symbols));
  EXPECT_EQ(ZX_OK, second_loop().RunUntilIdle());
  EXPECT_EQ(second_loop().dispatcher(), dispatcher);

  driver.reset();
  EXPECT_EQ(ZX_OK, loop().RunUntilIdle());
  EXPECT_EQ(ZX_OK, second_loop().RunUntilIdle());
  EXPECT_EQ(ASYNC_LOOP_QUIT, loop().GetState());
}

// Start a driver with invalid arguments.
TEST_F(DriverHostTest, Start_InvalidStartArgs) {
  zx_status_t epitaph = ZX_OK;
  TestTransaction transaction(epitaph);

  // DriverStartArgs::ns is missing "/pkg" entry.
  auto endpoints = fidl::CreateEndpoints<fdf::Driver>();
  ASSERT_TRUE(endpoints.is_ok());
  {
    Completer completer(&transaction);
    fidl::WireRequest<fdf::DriverHost::Start> request(fdf::wire::DriverStartArgs(),
                                                      std::move(endpoints->server));
    driver_host().Start(&request, completer);
  }
  EXPECT_EQ(ZX_ERR_INVALID_ARGS, epitaph);

  // DriverStartArgs::ns not set.
  endpoints = fidl::CreateEndpoints<fdf::Driver>();
  ASSERT_TRUE(endpoints.is_ok());
  {
    Completer completer(&transaction);
    fidl::Arena arena;
    fdf::wire::DriverStartArgs driver_start_args(arena);
    driver_start_args.set_url(arena, "fuchsia-pkg://fuchsia.com/driver#meta/driver.cm");

    fidl::WireRequest<fdf::DriverHost::Start> request(std::move(driver_start_args),
                                                      std::move(endpoints->server));
    driver_host().Start(&request, completer);
  }
  EXPECT_EQ(ZX_ERR_INVALID_ARGS, epitaph);

  // DriverStartArgs::ns is missing "/pkg" entry.
  endpoints = fidl::CreateEndpoints<fdf::Driver>();
  ASSERT_TRUE(endpoints.is_ok());
  {
    Completer completer(&transaction);
    fidl::Arena arena;
    fdf::wire::DriverStartArgs driver_start_args(arena);
    driver_start_args.set_url(arena, "fuchsia-pkg://fuchsia.com/driver#meta/driver.cm");
    driver_start_args.set_ns(arena);
    fidl::WireRequest<fdf::DriverHost::Start> request(std::move(driver_start_args),
                                                      std::move(endpoints->server));
    driver_host().Start(&request, completer);
  }
  EXPECT_EQ(ZX_ERR_NOT_FOUND, epitaph);

  endpoints = fidl::CreateEndpoints<fdf::Driver>();
  ASSERT_TRUE(endpoints.is_ok());
  {
    fidl::Arena arena;
    // DriverStartArgs::program not set.
    fidl::VectorView<frunner::wire::ComponentNamespaceEntry> entries1(arena, 1);
    entries1[0].Allocate(arena);
    entries1[0].set_path(arena, "/pkg");
    entries1[0].set_directory(fidl::ClientEnd<fuchsia_io::Directory>());

    fdf::wire::DriverStartArgs driver_start_args(arena);
    driver_start_args.set_url(arena, "fuchsia-pkg://fuchsia.com/driver#meta/driver.cm");
    driver_start_args.set_ns(arena, std::move(entries1));

    Completer completer(&transaction);
    fidl::WireRequest<fdf::DriverHost::Start> request(std::move(driver_start_args),
                                                      std::move(endpoints->server));
    driver_host().Start(&request, completer);
  }
  EXPECT_EQ(ZX_ERR_INVALID_ARGS, epitaph);

  endpoints = fidl::CreateEndpoints<fdf::Driver>();
  ASSERT_TRUE(endpoints.is_ok());
  {
    // DriverStartArgs::program is missing "binary" entry.
    fidl::Arena arena;
    fidl::VectorView<frunner::wire::ComponentNamespaceEntry> entries2(arena, 1);
    entries2[0].Allocate(arena);
    entries2[0].set_path(arena, "/pkg");
    entries2[0].set_directory(fidl::ClientEnd<fuchsia_io::Directory>());

    fdf::wire::DriverStartArgs driver_start_args(arena);
    driver_start_args.set_url(arena, "fuchsia-pkg://fuchsia.com/driver#meta/driver.cm");
    driver_start_args.set_program(arena);
    driver_start_args.set_ns(arena, std::move(entries2));

    Completer completer(&transaction);
    fidl::WireRequest<fdf::DriverHost::Start> request(std::move(driver_start_args),
                                                      std::move(endpoints->server));
    driver_host().Start(&request, completer);
  }
  EXPECT_EQ(ZX_ERR_NOT_FOUND, epitaph);
}

// Start a driver with an invalid client-end.
TEST_F(DriverHostTest, InvalidHandleRights) {
  bool connected = false;
  AddEntry([&connected](auto request) {
    connected = true;
    return ZX_OK;
  });
  auto endpoints = fidl::CreateEndpoints<fdf::Node>();
  ASSERT_TRUE(endpoints.is_ok());
  auto& client_end = endpoints->client.channel();
  ASSERT_EQ(ZX_OK, client_end.replace(ZX_RIGHT_TRANSFER, &client_end));
  // This should fail when node rights are not ZX_DEFAULT_CHANNEL_RIGHTS.
  StartDriver({}, &endpoints->client, ZX_ERR_INVALID_ARGS);
  EXPECT_FALSE(connected);
}

// Start a driver with an invalid binary.
TEST_F(DriverHostTest, Start_InvalidBinary) {
  zx_status_t epitaph = ZX_OK;
  TestTransaction transaction(epitaph);
  fidl::Arena arena;

  auto pkg_endpoints = fidl::CreateEndpoints<fuchsia_io::Directory>();
  ASSERT_TRUE(pkg_endpoints.is_ok());
  fidl::VectorView<frunner::wire::ComponentNamespaceEntry> ns_entries(arena, 1);
  ns_entries[0].Allocate(arena);
  ns_entries[0].set_path(arena, "/pkg");
  ns_entries[0].set_directory(std::move(pkg_endpoints->client));
  TestFile file("/pkg/driver/test_not_driver.so");
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
  fidl::VectorView<fdata::wire::DictionaryEntry> program_entries(arena, 1);
  program_entries[0].key.Set(arena, "binary");
  program_entries[0].value.set_str(arena, "driver/library.so");

  auto driver_endpoints = fidl::CreateEndpoints<fdf::Driver>();
  ASSERT_TRUE(driver_endpoints.is_ok());
  {
    Completer completer(&transaction);
    fdata::wire::Dictionary dictionary(arena);
    dictionary.set_entries(arena, std::move(program_entries));

    fdf::wire::DriverStartArgs driver_start_args(arena);
    driver_start_args.set_url(arena, "fuchsia-pkg://fuchsia.com/driver#meta/driver.cm");
    driver_start_args.set_program(arena, std::move(dictionary));
    driver_start_args.set_ns(arena, std::move(ns_entries));

    fidl::WireRequest<fdf::DriverHost::Start> request(std::move(driver_start_args),
                                                      std::move(driver_endpoints->server));
    driver_host().Start(&request, completer);
  }
  EXPECT_EQ(ZX_OK, loop().RunUntilIdle());
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
                            PropertyList(UnorderedElementsAre(StringIs(
                                "url", "fuchsia-pkg://fuchsia.com/driver#meta/driver.cm")))))),
                        AllOf(NodeMatches(AllOf(
                            NameMatches("driver-2"),
                            PropertyList(UnorderedElementsAre(StringIs(
                                "url", "fuchsia-pkg://fuchsia.com/driver#meta/driver.cm"))))))))));

  driver_1.reset();
  driver_2.reset();
  EXPECT_EQ(ZX_ERR_CANCELED, loop().RunUntilIdle());
  EXPECT_EQ(ASYNC_LOOP_QUIT, loop().GetState());
}
