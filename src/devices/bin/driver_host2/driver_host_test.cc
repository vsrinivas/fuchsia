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

namespace fdata = llcpp::fuchsia::data;
namespace fdf = llcpp::fuchsia::driver::framework;
namespace fio = fuchsia::io;
namespace fmem = fuchsia::mem;
namespace frunner = llcpp::fuchsia::component::runner;
namespace ftest = llcpp::fuchsia::driverhost::test;

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

    llcpp::fuchsia::io::File::SyncClient file(std::move(client_end));
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

  StartDriverResult StartDriver(
      fidl::VectorView<fdf::wire::NodeSymbol> symbols = {},
      fidl::tracking_ptr<fidl::ClientEnd<llcpp::fuchsia::driver::framework::Node>> node = nullptr,
      zx_status_t expected_epitaph = ZX_OK) {
    zx_status_t epitaph = ZX_OK;
    TestTransaction transaction(&epitaph);

    zx::channel pkg_client_end, pkg_server_end;
    EXPECT_EQ(ZX_OK, zx::channel::create(0, &pkg_client_end, &pkg_server_end));
    zx::channel svc_client_end, svc_server_end;
    EXPECT_EQ(ZX_OK, zx::channel::create(0, &svc_client_end, &svc_server_end));
    frunner::wire::ComponentNamespaceEntry ns_entries[] = {
        frunner::wire::ComponentNamespaceEntry::Builder(
            std::make_unique<frunner::wire::ComponentNamespaceEntry::Frame>())
            .set_path(std::make_unique<fidl::StringView>("/pkg"))
            .set_directory(std::make_unique<fidl::ClientEnd<llcpp::fuchsia::io::Directory>>(
                std::move(pkg_client_end)))
            .build(),
        frunner::wire::ComponentNamespaceEntry::Builder(
            std::make_unique<frunner::wire::ComponentNamespaceEntry::Frame>())
            .set_path(std::make_unique<fidl::StringView>("/svc"))
            .set_directory(std::make_unique<fidl::ClientEnd<llcpp::fuchsia::io::Directory>>(
                std::move(svc_client_end)))
            .build(),
    };
    TestFile file("/pkg/driver/test_driver.so");
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
    EXPECT_EQ(ZX_OK, vfs_.ServeDirectory(svc_dir_, std::move(svc_server_end)));
    fdata::wire::DictionaryEntry program_entries[] = {
        {
            .key = "binary",
            .value = fdata::wire::DictionaryValue::WithStr(
                std::make_unique<fidl::StringView>("driver/library.so")),
        },
    };
    zx::channel outgoing_dir_client_end, outgoing_dir_server_end;
    EXPECT_EQ(ZX_OK, zx::channel::create(0, &outgoing_dir_client_end, &outgoing_dir_server_end));
    zx::channel driver_client_end, driver_server_end;
    EXPECT_EQ(ZX_OK, zx::channel::create(0, &driver_client_end, &driver_server_end));
    {
      Completer completer(&transaction);
      driver_host()->Start(
          fdf::wire::DriverStartArgs::Builder(std::make_unique<fdf::wire::DriverStartArgs::Frame>())
              .set_node(std::move(node))
              .set_symbols(
                  std::make_unique<fidl::VectorView<fdf::wire::NodeSymbol>>(std::move(symbols)))
              .set_url(std::make_unique<fidl::StringView>(
                  "fuchsia-pkg://fuchsia.com/driver#meta/driver.cm"))
              .set_program(std::make_unique<fdata::wire::Dictionary>(
                  fdata::wire::Dictionary::Builder(
                      std::make_unique<fdata::wire::Dictionary::Frame>())
                      .set_entries(std::make_unique<fidl::VectorView<fdata::wire::DictionaryEntry>>(
                          fidl::unowned_vec(program_entries)))
                      .build()))
              .set_ns(std::make_unique<fidl::VectorView<frunner::wire::ComponentNamespaceEntry>>(
                  fidl::unowned_vec(ns_entries)))
              .set_outgoing_dir(std::make_unique<fidl::ServerEnd<llcpp::fuchsia::io::Directory>>(
                  std::move(outgoing_dir_server_end)))
              .build(),
          fidl::ServerEnd<fdf::Driver>(std::move(driver_server_end)), completer);
    }
    loop().RunUntilIdle();
    EXPECT_EQ(expected_epitaph, epitaph);

    return {
        .driver = std::move(driver_client_end),
        .outgoing_dir = std::move(outgoing_dir_client_end),
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

  class EventHandler : public ftest::Outgoing::AsyncEventHandler {
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
  fdf::wire::NodeSymbol symbols[] = {
      fdf::wire::NodeSymbol::Builder(std::make_unique<fdf::wire::NodeSymbol::Frame>())
          .set_name(std::make_unique<fidl::StringView>("func"))
          .set_address(std::make_unique<zx_vaddr_t>(reinterpret_cast<zx_vaddr_t>(func)))
          .build(),
  };
  auto [driver, outgoing_dir] = StartDriver(fidl::unowned_vec(symbols));
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
    driver_host()->Start(
        fdf::wire::DriverStartArgs::Builder(std::make_unique<fdf::wire::DriverStartArgs::Frame>())
            .set_url(std::make_unique<fidl::StringView>(
                "fuchsia-pkg://fuchsia.com/driver#meta/driver.cm"))
            .build(),
        fidl::ServerEnd<fdf::Driver>(std::move(driver_server_end)), completer);
  }
  EXPECT_EQ(ZX_ERR_INVALID_ARGS, epitaph);

  // DriverStartArgs::ns is missing "/pkg" entry.
  // TODO(fxbug.dev/65212): Migrate the use of |zx::channel::create| to |fidl::CreateEndpoints|.
  ASSERT_EQ(ZX_OK, zx::channel::create(0, &driver_client_end, &driver_server_end));
  {
    Completer completer(&transaction);
    driver_host()->Start(
        fdf::wire::DriverStartArgs::Builder(std::make_unique<fdf::wire::DriverStartArgs::Frame>())
            .set_url(std::make_unique<fidl::StringView>(
                "fuchsia-pkg://fuchsia.com/driver#meta/driver.cm"))
            .set_ns(std::make_unique<fidl::VectorView<frunner::wire::ComponentNamespaceEntry>>())
            .build(),
        fidl::ServerEnd<fdf::Driver>(std::move(driver_server_end)), completer);
  }
  EXPECT_EQ(ZX_ERR_NOT_FOUND, epitaph);

  // DriverStartArgs::program not set.
  frunner::wire::ComponentNamespaceEntry entries1[] = {
      frunner::wire::ComponentNamespaceEntry::Builder(
          std::make_unique<frunner::wire::ComponentNamespaceEntry::Frame>())
          .set_path(std::make_unique<fidl::StringView>("/pkg"))
          .set_directory(std::make_unique<fidl::ClientEnd<llcpp::fuchsia::io::Directory>>())
          .build(),
  };
  ASSERT_EQ(ZX_OK, zx::channel::create(0, &driver_client_end, &driver_server_end));
  {
    Completer completer(&transaction);
    driver_host()->Start(
        fdf::wire::DriverStartArgs::Builder(std::make_unique<fdf::wire::DriverStartArgs::Frame>())
            .set_url(std::make_unique<fidl::StringView>(
                "fuchsia-pkg://fuchsia.com/driver#meta/driver.cm"))
            .set_ns(std::make_unique<fidl::VectorView<frunner::wire::ComponentNamespaceEntry>>(
                fidl::unowned_vec(entries1)))
            .build(),
        fidl::ServerEnd<fdf::Driver>(std::move(driver_server_end)), completer);
  }
  EXPECT_EQ(ZX_ERR_INVALID_ARGS, epitaph);

  // DriverStartArgs::program is missing "binary" entry.
  frunner::wire::ComponentNamespaceEntry entries2[] = {
      frunner::wire::ComponentNamespaceEntry::Builder(
          std::make_unique<frunner::wire::ComponentNamespaceEntry::Frame>())
          .set_path(std::make_unique<fidl::StringView>("/pkg"))
          .set_directory(std::make_unique<fidl::ClientEnd<llcpp::fuchsia::io::Directory>>())
          .build(),
  };
  ASSERT_EQ(ZX_OK, zx::channel::create(0, &driver_client_end, &driver_server_end));
  {
    Completer completer(&transaction);
    driver_host()->Start(
        fdf::wire::DriverStartArgs::Builder(std::make_unique<fdf::wire::DriverStartArgs::Frame>())
            .set_url(std::make_unique<fidl::StringView>(
                "fuchsia-pkg://fuchsia.com/driver#meta/driver.cm"))
            .set_program(std::make_unique<fdata::wire::Dictionary>())
            .set_ns(std::make_unique<fidl::VectorView<frunner::wire::ComponentNamespaceEntry>>(
                fidl::unowned_vec(entries2)))
            .build(),
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
  StartDriver({}, fidl::unowned_ptr(&client_end), ZX_ERR_INVALID_ARGS);
  EXPECT_FALSE(connected);
}

// Start a driver with an invalid binary.
TEST_F(DriverHostTest, Start_InvalidBinary) {
  zx_status_t epitaph = ZX_OK;
  TestTransaction transaction(&epitaph);

  zx::channel pkg_client_end, pkg_server_end;
  EXPECT_EQ(ZX_OK, zx::channel::create(0, &pkg_client_end, &pkg_server_end));
  frunner::wire::ComponentNamespaceEntry ns_entries[] = {
      frunner::wire::ComponentNamespaceEntry::Builder(
          std::make_unique<frunner::wire::ComponentNamespaceEntry::Frame>())
          .set_path(std::make_unique<fidl::StringView>("/pkg"))
          .set_directory(std::make_unique<fidl::ClientEnd<llcpp::fuchsia::io::Directory>>(
              std::move(pkg_client_end)))
          .build(),
  };
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
  fdata::wire::DictionaryEntry program_entries[] = {
      {
          .key = "binary",
          .value = fdata::wire::DictionaryValue::WithStr(
              std::make_unique<fidl::StringView>("driver/library.so")),
      },
  };
  zx::channel driver_client_end, driver_server_end;
  EXPECT_EQ(ZX_OK, zx::channel::create(0, &driver_client_end, &driver_server_end));
  {
    Completer completer(&transaction);
    driver_host()->Start(
        fdf::wire::DriverStartArgs::Builder(std::make_unique<fdf::wire::DriverStartArgs::Frame>())
            .set_url(std::make_unique<fidl::StringView>(
                "fuchsia-pkg://fuchsia.com/driver#meta/driver.cm"))
            .set_program(std::make_unique<fdata::wire::Dictionary>(
                fdata::wire::Dictionary::Builder(std::make_unique<fdata::wire::Dictionary::Frame>())
                    .set_entries(std::make_unique<fidl::VectorView<fdata::wire::DictionaryEntry>>(
                        fidl::unowned_vec(program_entries)))
                    .build()))
            .set_ns(std::make_unique<fidl::VectorView<frunner::wire::ComponentNamespaceEntry>>(
                fidl::unowned_vec(ns_entries)))
            .build(),
        std::move(driver_server_end), completer);
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
