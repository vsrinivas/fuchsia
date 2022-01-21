// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/devices/bin/driver_manager/driver_runner.h"

#include <fuchsia/component/cpp/fidl_test_base.h>
#include <fuchsia/component/decl/cpp/fidl.h>
#include <fuchsia/driver/framework/cpp/fidl_test_base.h>
#include <fuchsia/io/cpp/fidl_test_base.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/fit/defer.h>
#include <lib/gtest/test_loop_fixture.h>
#include <lib/inspect/cpp/reader.h>
#include <lib/inspect/testing/cpp/inspect.h>
#include <lib/sys/cpp/testing/component_context_provider.h>

#include <list>

#include "src/devices/bin/driver_manager/fake_driver_index.h"

namespace fdata = fuchsia_data;
namespace fdf = fuchsia::driver::framework;
namespace fio = fuchsia::io;
namespace fprocess = fuchsia_process;
namespace frunner = fuchsia_component_runner;
namespace fcomponent = fuchsia::component;
namespace fdecl = fuchsia::component::decl;

using namespace testing;
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

fidl::AnyTeardownObserver TeardownWatcher(size_t index, std::vector<size_t>& indices) {
  return fidl::ObserveTeardown([&indices = indices, index] { indices.emplace_back(index); });
}

class TestRealm : public fcomponent::testing::Realm_TestBase {
 public:
  using CreateChildHandler = fit::function<void(fdecl::CollectionRef collection, fdecl::Child decl,
                                                std::vector<fdecl::Offer> offers)>;
  using OpenExposedDirHandler = fit::function<void(
      fdecl::ChildRef child, fidl::InterfaceRequest<fio::Directory> exposed_dir)>;

  void SetCreateChildHandler(CreateChildHandler create_child_handler) {
    create_child_handler_ = std::move(create_child_handler);
  }

  void SetOpenExposedDirHandler(OpenExposedDirHandler open_exposed_dir_handler) {
    open_exposed_dir_handler_ = std::move(open_exposed_dir_handler);
  }

  fidl::VectorView<fprocess::wire::HandleInfo> GetHandles() {
    return fidl::VectorView<fprocess::wire::HandleInfo>::FromExternal(handles_);
  }

 private:
  void CreateChild(fdecl::CollectionRef collection, fdecl::Child decl,
                   fcomponent::CreateChildArgs args, CreateChildCallback callback) override {
    handles_.clear();
    for (auto& info : *args.mutable_numbered_handles()) {
      handles_.push_back(fprocess::wire::HandleInfo{
          .handle = std::move(info.handle),
          .id = info.id,
      });
    }
    create_child_handler_(std::move(collection), std::move(decl),
                          std::move(*args.mutable_dynamic_offers()));
    callback(fcomponent::Realm_CreateChild_Result(fpromise::ok()));
  }

  void OpenExposedDir(fdecl::ChildRef child, fidl::InterfaceRequest<fio::Directory> exposed_dir,
                      OpenExposedDirCallback callback) override {
    open_exposed_dir_handler_(std::move(child), std::move(exposed_dir));
    callback(fcomponent::Realm_OpenExposedDir_Result(fpromise::ok()));
  }

  void NotImplemented_(const std::string& name) override {
    printf("Not implemented: Realm::%s\n", name.data());
  }

  CreateChildHandler create_child_handler_;
  OpenExposedDirHandler open_exposed_dir_handler_;
  std::vector<fprocess::wire::HandleInfo> handles_;
};

class TestDirectory : public fio::testing::Directory_TestBase {
 public:
  using OpenHandler =
      fit::function<void(std::string path, fidl::InterfaceRequest<fio::Node> object)>;

  TestDirectory(async_dispatcher_t* dispatcher) : dispatcher_(dispatcher) {}

  void Bind(fidl::InterfaceRequest<fio::Directory> request) {
    bindings_.AddBinding(this, std::move(request), dispatcher_);
  }

  void SetOpenHandler(OpenHandler open_handler) { open_handler_ = std::move(open_handler); }

 private:
  void Clone(uint32_t flags, fidl::InterfaceRequest<fio::Node> object) override {
    EXPECT_EQ(ZX_FS_FLAG_CLONE_SAME_RIGHTS, flags);
    fidl::InterfaceRequest<fio::Directory> dir(object.TakeChannel());
    Bind(std::move(dir));
  }

  void Open(uint32_t flags, uint32_t mode, std::string path,
            fidl::InterfaceRequest<fio::Node> object) override {
    open_handler_(std::move(path), std::move(object));
  }

  void NotImplemented_(const std::string& name) override {
    printf("Not implemented: Directory::%s\n", name.data());
  }

  async_dispatcher_t* dispatcher_;
  fidl::BindingSet<fio::Directory> bindings_;
  OpenHandler open_handler_;
};

class TestDriver : public fdf::testing::Driver_TestBase {
 public:
  TestDriver(fdf::NodePtr node) : node_(std::move(node)) {}

  fdf::NodePtr& node() { return node_; }

  using StopHandler = fit::function<void()>;
  void SetStopHandler(StopHandler handler) { stop_handler_ = std::move(handler); }

  void set_close_bindings(fit::function<void()> close) { close_binding_ = std::move(close); }

  void close_binding() { close_binding_(); }

  void Stop() override { stop_handler_(); }

 private:
  fit::function<void()> close_binding_;
  StopHandler stop_handler_;
  fdf::NodePtr node_;

  void NotImplemented_(const std::string& name) override {
    printf("Not implemented: Driver::%s\n", name.data());
  }
};

class TestDriverHost : public fdf::testing::DriverHost_TestBase {
 public:
  using StartHandler = fit::function<void(fdf::DriverStartArgs start_args,
                                          fidl::InterfaceRequest<fdf::Driver> driver)>;

  void SetStartHandler(StartHandler start_handler) { start_handler_ = std::move(start_handler); }

 private:
  void Start(fdf::DriverStartArgs start_args, fidl::InterfaceRequest<fdf::Driver> driver) override {
    start_handler_(std::move(start_args), std::move(driver));
  }

  void NotImplemented_(const std::string& name) override {
    printf("Not implemented: DriverHost::%s\n", name.data());
  }

  StartHandler start_handler_;
};

class TestTransaction : public fidl::Transaction {
 public:
  TestTransaction(bool close) : close_(close) {}

 private:
  std::unique_ptr<Transaction> TakeOwnership() override {
    EXPECT_TRUE(false);
    return nullptr;
  }

  zx_status_t Reply(fidl::OutgoingMessage* message, fidl::WriteOptions write_options) override {
    EXPECT_TRUE(false);
    return ZX_OK;
  }

  void Close(zx_status_t epitaph) override {
    EXPECT_TRUE(close_) << "epitaph: " << zx_status_get_string(epitaph);
  }

  bool close_;
};

struct Driver {
  std::string url;
  std::string binary;
  bool colocate = false;
  bool close = false;
};

class DriverRunnerTest : public gtest::TestLoopFixture {
 public:
  void SetUp() override {
    TestLoopFixture::SetUp();
    fidl::InterfaceRequestHandler<fcomponent::Realm> handler = [this](auto request) {
      EXPECT_EQ(ZX_OK, realm_binding_.Bind(std::move(request), dispatcher()));
    };
    ASSERT_EQ(ZX_OK, provider_.context()->outgoing()->AddPublicService(std::move(handler)));
  }

 protected:
  inspect::Inspector& inspector() { return inspector_; }
  TestRealm& realm() { return realm_; }
  TestDirectory& driver_dir() { return driver_dir_; }
  TestDriverHost& driver_host() { return driver_host_; }

  fidl::ClientEnd<fuchsia_component::Realm> ConnectToRealm() {
    fcomponent::RealmPtr realm;
    provider_.ConnectToPublicService(realm.NewRequest(dispatcher()));
    return fidl::ClientEnd<fuchsia_component::Realm>(realm.Unbind().TakeChannel());
  }

  FakeDriverIndex CreateDriverIndex() {
    return FakeDriverIndex(dispatcher(), [](auto args) -> zx::status<FakeDriverIndex::MatchResult> {
      if (args.name().get() == "second") {
        return zx::ok(FakeDriverIndex::MatchResult{
            .url = "fuchsia-boot:///#meta/second-driver.cm",
        });
      } else if (args.name().get() == "part-1") {
        return zx::ok(FakeDriverIndex::MatchResult{
            .url = "fuchsia-boot:///#meta/composite-driver.cm",
            .node_index = std::make_optional(0u),
            .num_nodes = std::make_optional(2u),
        });
      } else if (args.name().get() == "part-2") {
        return zx::ok(FakeDriverIndex::MatchResult{
            .url = "fuchsia-boot:///#meta/composite-driver.cm",
            .node_index = std::make_optional(1u),
            .num_nodes = std::make_optional(2u),
        });
      } else {
        return zx::error(ZX_ERR_NOT_FOUND);
      }
    });
  }

  void StartDriverHost(std::string coll, std::string name) {
    realm().SetCreateChildHandler(
        [coll, name](fdecl::CollectionRef collection, fdecl::Child decl, auto offers) {
          EXPECT_EQ(coll, collection.name);
          EXPECT_EQ(name, decl.name());
          EXPECT_EQ("#meta/driver_host2.cm", decl.url());
        });
    realm().SetOpenExposedDirHandler([this, coll, name](fdecl::ChildRef child, auto exposed_dir) {
      EXPECT_EQ(coll, child.collection.value_or(""));
      EXPECT_EQ(name, child.name);
      driver_host_dir_.Bind(std::move(exposed_dir));
    });
    driver_host_dir_.SetOpenHandler([this](std::string path, auto object) {
      EXPECT_EQ(fdf::DriverHost::Name_, path);
      EXPECT_EQ(ZX_OK, driver_host_binding_.Bind(object.TakeChannel(), dispatcher()));
    });
  }

  void StopDriverComponent(fidl::ClientEnd<frunner::ComponentController> component) {
    fidl::WireClient client(std::move(component), dispatcher());
    auto stop_result = client->Stop();
    ASSERT_EQ(ZX_OK, stop_result.status());
    RunLoopUntilIdle();
  }

  fidl::ClientEnd<frunner::ComponentController> StartDriver(DriverRunner& driver_runner,
                                                            Driver driver) {
    fidl::Arena arena;

    fidl::VectorView<fdata::wire::DictionaryEntry> program_entries(arena, 2);
    program_entries[0].key.Set(arena, "binary");
    program_entries[0].value.set_str(arena, arena, driver.binary);
    program_entries[1].key.Set(arena, "colocate");
    program_entries[1].value.set_str(arena, arena, driver.colocate ? "true" : "false");

    fdata::wire::Dictionary program(arena);
    program.set_entries(arena, std::move(program_entries));

    auto outgoing_endpoints = fidl::CreateEndpoints<fuchsia_io::Directory>();
    EXPECT_EQ(ZX_OK, outgoing_endpoints.status_value());

    frunner::wire::ComponentStartInfo start_info(arena);
    start_info.set_resolved_url(arena, arena, driver.url)
        .set_program(arena, std::move(program))
        .set_ns(arena)
        .set_outgoing_dir(std::move(outgoing_endpoints->server))
        .set_numbered_handles(arena, realm().GetHandles());

    auto controller_endpoints = fidl::CreateEndpoints<frunner::ComponentController>();
    EXPECT_EQ(ZX_OK, controller_endpoints.status_value());
    TestTransaction transaction(driver.close);
    {
      fidl::WireServer<frunner::ComponentRunner>::StartCompleter::Sync completer(&transaction);
      fidl::WireRequest<frunner::ComponentRunner::Start> request(
          start_info, std::move(controller_endpoints->server));
      static_cast<fidl::WireServer<frunner::ComponentRunner>&>(driver_runner)
          .Start(&request, completer);
    }
    RunLoopUntilIdle();
    return std::move(controller_endpoints->client);
  }

  zx::status<fidl::ClientEnd<frunner::ComponentController>> StartRootDriver(
      std::string url, DriverRunner& driver_runner) {
    realm().SetCreateChildHandler(
        [](fdecl::CollectionRef collection, fdecl::Child decl, auto offers) {
          EXPECT_EQ("boot-drivers", collection.name);
          EXPECT_EQ("root", decl.name());
          EXPECT_EQ("fuchsia-boot:///#meta/root-driver.cm", decl.url());
        });
    realm().SetOpenExposedDirHandler([this](fdecl::ChildRef child, auto exposed_dir) {
      EXPECT_EQ("boot-drivers", child.collection);
      EXPECT_EQ("root", child.name);
      driver_dir_.Bind(std::move(exposed_dir));
    });
    auto start = driver_runner.StartRootDriver(url);
    if (start.is_error()) {
      return start.take_error();
    }
    EXPECT_TRUE(RunLoopUntilIdle());

    StartDriverHost("driver-hosts", "driver-host-0");
    auto controller = StartDriver(driver_runner, {
                                                     .url = "fuchsia-boot:///#meta/root-driver.cm",
                                                     .binary = "driver/root-driver.so",
                                                 });
    return zx::ok(std::move(controller));
  }

  void Unbind() {
    driver_host_binding_.Unbind();
    EXPECT_TRUE(RunLoopUntilIdle());
  }

  TestDriver* BindDriver(fidl::InterfaceRequest<fdf::Driver> request, fdf::NodePtr node) {
    auto driver = std::make_unique<TestDriver>(std::move(node));
    auto driver_ptr = driver.get();
    driver_bindings_.AddBinding(driver_ptr, std::move(request), dispatcher(),
                                [driver = std::move(driver)](auto) {});
    driver_ptr->set_close_bindings(
        [this, driver_ptr]() { driver_bindings_.CloseBinding(driver_ptr, ZX_OK); });
    driver_ptr->SetStopHandler([driver_ptr]() { driver_ptr->close_binding(); });
    return driver_ptr;
  }

  inspect::Hierarchy Inspect(DriverRunner& driver_runner) {
    FakeContext context;
    auto inspector = driver_runner.Inspect()(context).take_value();
    return inspect::ReadFromInspector(inspector)(context).take_value();
  }

 private:
  TestRealm realm_;
  TestDirectory driver_host_dir_{dispatcher()};
  TestDirectory driver_dir_{dispatcher()};
  TestDriverHost driver_host_;
  fidl::Binding<fcomponent::Realm> realm_binding_{&realm_};
  fidl::Binding<fdf::DriverHost> driver_host_binding_{&driver_host_};
  fidl::BindingSet<fdf::Driver> driver_bindings_;

  inspect::Inspector inspector_;
  sys::testing::ComponentContextProvider provider_{dispatcher()};
};

// Start the root driver.
TEST_F(DriverRunnerTest, StartRootDriver) {
  auto driver_index = CreateDriverIndex();
  auto driver_index_client = driver_index.Connect();
  ASSERT_EQ(ZX_OK, driver_index_client.status_value());
  DriverRunner driver_runner(ConnectToRealm(), std::move(*driver_index_client), inspector(),
                             dispatcher());

  auto defer = fit::defer([this] { Unbind(); });

  driver_host().SetStartHandler([this](fdf::DriverStartArgs start_args, auto request) {
    auto& entries = start_args.program().entries();
    EXPECT_EQ(2u, entries.size());
    EXPECT_EQ("binary", entries[0].key);
    EXPECT_EQ("driver/root-driver.so", entries[0].value->str());
    EXPECT_EQ("colocate", entries[1].key);
    EXPECT_EQ("false", entries[1].value->str());

    fdf::NodePtr node;
    ASSERT_EQ(ZX_OK, node.Bind(start_args.mutable_node()->TakeChannel()));
    BindDriver(std::move(request), std::move(node));
  });

  auto root_driver = StartRootDriver("fuchsia-boot:///#meta/root-driver.cm", driver_runner);
  ASSERT_EQ(ZX_OK, root_driver.status_value());

  StopDriverComponent(std::move(root_driver.value()));
}

// Start the root driver, and add a child node owned by the root driver.
TEST_F(DriverRunnerTest, StartRootDriver_AddOwnedChild) {
  auto driver_index = CreateDriverIndex();
  auto driver_index_client = driver_index.Connect();
  ASSERT_EQ(ZX_OK, driver_index_client.status_value());
  DriverRunner driver_runner(ConnectToRealm(), std::move(*driver_index_client), inspector(),
                             dispatcher());
  auto defer = fit::defer([this] { Unbind(); });

  driver_host().SetStartHandler([this](fdf::DriverStartArgs start_args, auto request) {
    auto& entries = start_args.program().entries();
    EXPECT_EQ(2u, entries.size());
    EXPECT_EQ("binary", entries[0].key);
    EXPECT_EQ("driver/root-driver.so", entries[0].value->str());
    EXPECT_EQ("colocate", entries[1].key);
    EXPECT_EQ("false", entries[1].value->str());

    fdf::NodePtr root_node;
    EXPECT_EQ(ZX_OK, root_node.Bind(std::move(*start_args.mutable_node()), dispatcher()));
    fdf::NodeAddArgs args;
    args.set_name("second");
    fdf::NodeControllerPtr node_controller;
    fdf::NodePtr second_node;
    root_node->AddChild(std::move(args), node_controller.NewRequest(dispatcher()),
                        second_node.NewRequest(dispatcher()),
                        [](auto result) { EXPECT_FALSE(result.is_err()); });
    BindDriver(std::move(request), std::move(root_node));
  });
  auto root_driver = StartRootDriver("fuchsia-boot:///#meta/root-driver.cm", driver_runner);
  ASSERT_EQ(ZX_OK, root_driver.status_value());

  StopDriverComponent(std::move(root_driver.value()));
}

// Start the root driver, add a child node, then remove it.
TEST_F(DriverRunnerTest, StartRootDriver_RemoveOwnedChild) {
  auto driver_index = CreateDriverIndex();
  auto driver_index_client = driver_index.Connect();
  ASSERT_EQ(ZX_OK, driver_index_client.status_value());
  DriverRunner driver_runner(ConnectToRealm(), std::move(*driver_index_client), inspector(),
                             dispatcher());
  auto defer = fit::defer([this] { Unbind(); });

  fdf::NodeControllerPtr node_controller;

  TestDriver* root_test_driver = nullptr;
  fdf::NodePtr second_node;
  driver_host().SetStartHandler([this, &root_test_driver, &node_controller, &second_node](
                                    fdf::DriverStartArgs start_args, auto request) {
    auto& entries = start_args.program().entries();
    EXPECT_EQ(2u, entries.size());
    EXPECT_EQ("binary", entries[0].key);
    EXPECT_EQ("driver/root-driver.so", entries[0].value->str());
    EXPECT_EQ("colocate", entries[1].key);
    EXPECT_EQ("false", entries[1].value->str());

    fdf::NodePtr root_node;
    EXPECT_EQ(ZX_OK, root_node.Bind(std::move(*start_args.mutable_node()), dispatcher()));

    fdf::NodeAddArgs args;
    args.set_name("second");
    root_node->AddChild(std::move(args), node_controller.NewRequest(dispatcher()),
                        second_node.NewRequest(dispatcher()),
                        [](auto result) { EXPECT_FALSE(result.is_err()); });
    root_test_driver = BindDriver(std::move(request), std::move(root_node));
  });
  auto root_driver = StartRootDriver("fuchsia-boot:///#meta/root-driver.cm", driver_runner);
  ASSERT_EQ(ZX_OK, root_driver.status_value());

  node_controller->Remove();
  EXPECT_TRUE(RunLoopUntilIdle());
  EXPECT_FALSE(second_node.is_bound());
  ASSERT_NE(nullptr, root_test_driver);
  EXPECT_TRUE(root_test_driver->node().is_bound());

  StopDriverComponent(std::move(root_driver.value()));
}

// Start the root driver, and add a child node with an invalid name.
TEST_F(DriverRunnerTest, StartRootDriver_AddOwnedChild_InvalidName) {
  auto driver_index = CreateDriverIndex();
  auto driver_index_client = driver_index.Connect();
  ASSERT_EQ(ZX_OK, driver_index_client.status_value());
  DriverRunner driver_runner(ConnectToRealm(), std::move(*driver_index_client), inspector(),
                             dispatcher());
  auto defer = fit::defer([this] { Unbind(); });

  TestDriver* root_test_driver = nullptr;
  fdf::NodePtr invalid_node;
  driver_host().SetStartHandler(
      [this, &root_test_driver, &invalid_node](fdf::DriverStartArgs start_args, auto request) {
        fdf::NodePtr root_node;
        EXPECT_EQ(ZX_OK, root_node.Bind(std::move(*start_args.mutable_node()), dispatcher()));

        fdf::NodeAddArgs args;
        args.set_name("second.invalid");
        fdf::NodeControllerPtr node_controller;
        root_node->AddChild(std::move(args), node_controller.NewRequest(dispatcher()),
                            invalid_node.NewRequest(dispatcher()),
                            [](auto result) { EXPECT_TRUE(result.is_err()); });
        root_test_driver = BindDriver(std::move(request), std::move(root_node));
      });
  auto root_driver = StartRootDriver("fuchsia-boot:///#meta/root-driver.cm", driver_runner);
  ASSERT_EQ(ZX_OK, root_driver.status_value());

  EXPECT_FALSE(invalid_node.is_bound());
  ASSERT_NE(nullptr, root_test_driver);
  EXPECT_TRUE(root_test_driver->node().is_bound());

  StopDriverComponent(std::move(root_driver.value()));
}

// Start the root driver, and add two child nodes with duplicate names.
TEST_F(DriverRunnerTest, StartRootDriver_AddOwnedChild_DuplicateNames) {
  auto driver_index = CreateDriverIndex();
  auto driver_index_client = driver_index.Connect();
  ASSERT_EQ(ZX_OK, driver_index_client.status_value());
  DriverRunner driver_runner(ConnectToRealm(), std::move(*driver_index_client), inspector(),
                             dispatcher());
  auto defer = fit::defer([this] { Unbind(); });

  TestDriver* root_test_driver = nullptr;
  fdf::NodePtr second_node, invalid_node;
  driver_host().SetStartHandler([this, &root_test_driver, &second_node, &invalid_node](
                                    fdf::DriverStartArgs start_args, auto request) {
    fdf::NodePtr root_node;
    EXPECT_EQ(ZX_OK, root_node.Bind(std::move(*start_args.mutable_node()), dispatcher()));

    fdf::NodeAddArgs args;
    args.set_name("second");
    fdf::NodeControllerPtr node_controller;
    root_node->AddChild(std::move(args), node_controller.NewRequest(dispatcher()),
                        second_node.NewRequest(dispatcher()),
                        [](auto result) { EXPECT_FALSE(result.is_err()); });
    args.set_name("second");
    root_node->AddChild(std::move(args), node_controller.NewRequest(dispatcher()),
                        invalid_node.NewRequest(dispatcher()),
                        [](auto result) { EXPECT_TRUE(result.is_err()); });
    root_test_driver = BindDriver(std::move(request), std::move(root_node));
  });
  auto root_driver = StartRootDriver("fuchsia-boot:///#meta/root-driver.cm", driver_runner);
  ASSERT_EQ(ZX_OK, root_driver.status_value());

  EXPECT_FALSE(invalid_node.is_bound());
  EXPECT_TRUE(second_node.is_bound());
  ASSERT_NE(nullptr, root_test_driver);
  EXPECT_TRUE(root_test_driver->node().is_bound());

  StopDriverComponent(std::move(root_driver.value()));
}

// Start the root driver, and add a child node with an offer that is missing a
// source.
TEST_F(DriverRunnerTest, StartRootDriver_AddUnownedChild_OfferMissingSource) {
  auto driver_index = CreateDriverIndex();
  auto driver_index_client = driver_index.Connect();
  ASSERT_EQ(ZX_OK, driver_index_client.status_value());
  DriverRunner driver_runner(ConnectToRealm(), std::move(*driver_index_client), inspector(),
                             dispatcher());
  auto defer = fit::defer([this] { Unbind(); });

  fdf::NodeControllerPtr node_controller;
  driver_host().SetStartHandler(
      [this, &node_controller](fdf::DriverStartArgs start_args, auto request) {
        fdf::NodePtr root_node;
        EXPECT_EQ(ZX_OK, root_node.Bind(std::move(*start_args.mutable_node()), dispatcher()));

        fdf::NodeAddArgs args;
        args.set_name("second");
        args.mutable_offers()->emplace_back().set_protocol(
            std::move(fdecl::OfferProtocol().set_target_name("fuchsia.package.Renamed")));
        root_node->AddChild(std::move(args), node_controller.NewRequest(dispatcher()), {},
                            [](auto result) { EXPECT_TRUE(result.is_err()); });
        BindDriver(std::move(request), std::move(root_node));
      });
  auto root_driver = StartRootDriver("fuchsia-boot:///#meta/root-driver.cm", driver_runner);
  ASSERT_EQ(ZX_OK, root_driver.status_value());

  ASSERT_FALSE(node_controller.is_bound());

  StopDriverComponent(std::move(root_driver.value()));
}

// Start the root driver, and add a child node with one offer that has a source
// and another that has a target.
TEST_F(DriverRunnerTest, StartRootDriver_AddUnownedChild_OfferHasRef) {
  auto driver_index = CreateDriverIndex();
  auto driver_index_client = driver_index.Connect();
  ASSERT_EQ(ZX_OK, driver_index_client.status_value());
  DriverRunner driver_runner(ConnectToRealm(), std::move(*driver_index_client), inspector(),
                             dispatcher());
  auto defer = fit::defer([this] { Unbind(); });

  fdf::NodeControllerPtr node_controller;
  driver_host().SetStartHandler(
      [this, &node_controller](fdf::DriverStartArgs start_args, auto request) {
        fdf::NodePtr root_node;
        EXPECT_EQ(ZX_OK, root_node.Bind(std::move(*start_args.mutable_node()), dispatcher()));

        fdf::NodeAddArgs args;
        args.set_name("second");
        args.mutable_offers()->emplace_back().set_protocol(
            std::move(fdecl::OfferProtocol()
                          .set_source(std::move(fdecl::Ref().set_self(fdecl::SelfRef())))
                          .set_source_name("fuchsia.package.Protocol")));
        args.mutable_offers()->emplace_back().set_protocol(
            std::move(fdecl::OfferProtocol()
                          .set_target(std::move(fdecl::Ref().set_self(fdecl::SelfRef())))
                          .set_source_name("fuchsia.package.Protocol")));
        root_node->AddChild(std::move(args), node_controller.NewRequest(dispatcher()), {},
                            [](auto result) { EXPECT_TRUE(result.is_err()); });
        BindDriver(std::move(request), std::move(root_node));
      });
  auto root_driver = StartRootDriver("fuchsia-boot:///#meta/root-driver.cm", driver_runner);
  ASSERT_EQ(ZX_OK, root_driver.status_value());

  ASSERT_FALSE(node_controller.is_bound());

  StopDriverComponent(std::move(root_driver.value()));
}

// Start the root driver, and add a child node with duplicate symbols. The child
// node is unowned, so if we did not have duplicate symbols, the second driver
// would bind to it.
TEST_F(DriverRunnerTest, StartRootDriver_AddUnownedChild_DuplicateSymbols) {
  auto driver_index = CreateDriverIndex();
  auto driver_index_client = driver_index.Connect();
  ASSERT_EQ(ZX_OK, driver_index_client.status_value());
  DriverRunner driver_runner(ConnectToRealm(), std::move(*driver_index_client), inspector(),
                             dispatcher());
  auto defer = fit::defer([this] { Unbind(); });

  fdf::NodeControllerPtr node_controller;
  driver_host().SetStartHandler(
      [this, &node_controller](fdf::DriverStartArgs start_args, auto request) {
        fdf::NodePtr root_node;
        EXPECT_EQ(ZX_OK, root_node.Bind(std::move(*start_args.mutable_node()), dispatcher()));

        fdf::NodeAddArgs args;
        args.set_name("second");
        args.mutable_symbols()->emplace_back(
            std::move(fdf::NodeSymbol().set_name("sym").set_address(0xfeed)));
        args.mutable_symbols()->emplace_back(
            std::move(fdf::NodeSymbol().set_name("sym").set_address(0xf00d)));
        root_node->AddChild(std::move(args), node_controller.NewRequest(dispatcher()), {},
                            [](auto result) { EXPECT_TRUE(result.is_err()); });
        BindDriver(std::move(request), std::move(root_node));
      });
  auto root_driver = StartRootDriver("fuchsia-boot:///#meta/root-driver.cm", driver_runner);
  ASSERT_EQ(ZX_OK, root_driver.status_value());

  ASSERT_FALSE(node_controller.is_bound());

  StopDriverComponent(std::move(root_driver.value()));
}

// Start the root driver, and add a child node that has a symbol without an
// address.
TEST_F(DriverRunnerTest, StartRootDriver_AddUnownedChild_SymbolMissingAddress) {
  auto driver_index = CreateDriverIndex();
  auto driver_index_client = driver_index.Connect();
  ASSERT_EQ(ZX_OK, driver_index_client.status_value());
  DriverRunner driver_runner(ConnectToRealm(), std::move(*driver_index_client), inspector(),
                             dispatcher());
  auto defer = fit::defer([this] { Unbind(); });

  fdf::NodeControllerPtr node_controller;
  driver_host().SetStartHandler(
      [this, &node_controller](fdf::DriverStartArgs start_args, auto request) {
        fdf::NodePtr root_node;
        EXPECT_EQ(ZX_OK, root_node.Bind(std::move(*start_args.mutable_node()), dispatcher()));

        fdf::NodeAddArgs args;
        args.set_name("second");
        args.mutable_symbols()->emplace_back(std::move(fdf::NodeSymbol().set_name("sym")));
        root_node->AddChild(std::move(args), node_controller.NewRequest(dispatcher()), {},
                            [](auto result) { EXPECT_TRUE(result.is_err()); });
        BindDriver(std::move(request), std::move(root_node));
      });
  auto root_driver = StartRootDriver("fuchsia-boot:///#meta/root-driver.cm", driver_runner);
  ASSERT_EQ(ZX_OK, root_driver.status_value());

  ASSERT_FALSE(node_controller.is_bound());

  StopDriverComponent(std::move(root_driver.value()));
}

// Start the root driver, and add a child node that has a symbol without a name.
TEST_F(DriverRunnerTest, StartRootDriver_AddUnownedChild_SymbolMissingName) {
  auto driver_index = CreateDriverIndex();
  auto driver_index_client = driver_index.Connect();
  ASSERT_EQ(ZX_OK, driver_index_client.status_value());
  DriverRunner driver_runner(ConnectToRealm(), std::move(*driver_index_client), inspector(),
                             dispatcher());
  auto defer = fit::defer([this] { Unbind(); });

  fdf::NodeControllerPtr node_controller;
  driver_host().SetStartHandler(
      [this, &node_controller](fdf::DriverStartArgs start_args, auto request) {
        fdf::NodePtr root_node;
        EXPECT_EQ(ZX_OK, root_node.Bind(std::move(*start_args.mutable_node()), dispatcher()));

        fdf::NodeAddArgs args;
        args.set_name("second");
        args.mutable_symbols()->emplace_back(std::move(fdf::NodeSymbol().set_address(0xfeed)));
        root_node->AddChild(std::move(args), node_controller.NewRequest(dispatcher()), {},
                            [](auto result) { EXPECT_TRUE(result.is_err()); });
        BindDriver(std::move(request), std::move(root_node));
      });
  auto root_driver = StartRootDriver("fuchsia-boot:///#meta/root-driver.cm", driver_runner);
  ASSERT_EQ(ZX_OK, root_driver.status_value());

  ASSERT_FALSE(node_controller.is_bound());

  StopDriverComponent(std::move(root_driver.value()));
}

// Start the root driver, and then start a second driver in a new driver host.
TEST_F(DriverRunnerTest, StartSecondDriver_NewDriverHost) {
  auto driver_index = CreateDriverIndex();
  auto driver_index_client = driver_index.Connect();
  ASSERT_EQ(ZX_OK, driver_index_client.status_value());
  DriverRunner driver_runner(ConnectToRealm(), std::move(*driver_index_client), inspector(),
                             dispatcher());
  auto defer = fit::defer([this] { Unbind(); });

  fdf::NodeControllerPtr node_controller;
  bool did_bind = false;
  node_controller.events().OnBind = [&did_bind] { did_bind = true; };
  driver_host().SetStartHandler(
      [this, &node_controller](fdf::DriverStartArgs start_args, auto request) {
        auto& entries = start_args.program().entries();
        EXPECT_EQ(2u, entries.size());
        EXPECT_EQ("binary", entries[0].key);
        EXPECT_EQ("driver/root-driver.so", entries[0].value->str());
        EXPECT_EQ("colocate", entries[1].key);
        EXPECT_EQ("false", entries[1].value->str());

        realm().SetCreateChildHandler([](fdecl::CollectionRef collection, fdecl::Child decl,
                                         std::vector<fdecl::Offer> offers) {
          EXPECT_EQ("boot-drivers", collection.name);
          EXPECT_EQ("root.second", decl.name());
          EXPECT_EQ("fuchsia-boot:///#meta/second-driver.cm", decl.url());

          EXPECT_EQ(1u, offers.size());
          ASSERT_TRUE(offers[0].is_protocol());
          auto& protocol = offers[0].protocol();

          ASSERT_TRUE(protocol.has_source());
          ASSERT_TRUE(protocol.source().is_child());
          auto& source_ref = protocol.source().child();
          EXPECT_EQ("root", source_ref.name);
          EXPECT_EQ("boot-drivers", source_ref.collection.value_or("missing"));

          ASSERT_TRUE(protocol.has_source_name());
          EXPECT_EQ("fuchsia.package.Protocol", protocol.source_name());

          ASSERT_TRUE(protocol.has_target_name());
          EXPECT_EQ("fuchsia.package.Renamed", protocol.target_name());
        });
        realm().SetOpenExposedDirHandler([this](fdecl::ChildRef child, auto exposed_dir) {
          EXPECT_EQ("boot-drivers", child.collection);
          EXPECT_EQ("root.second", child.name);
          driver_dir().Bind(std::move(exposed_dir));
        });

        fdf::NodePtr root_node;
        EXPECT_EQ(ZX_OK, root_node.Bind(std::move(*start_args.mutable_node()), dispatcher()));
        fdf::NodeAddArgs args;
        args.set_name("second");
        args.mutable_offers()->emplace_back().set_protocol(
            std::move(fdecl::OfferProtocol()
                          .set_source_name("fuchsia.package.Protocol")
                          .set_target_name("fuchsia.package.Renamed")));
        args.mutable_symbols()->emplace_back(
            std::move(fdf::NodeSymbol().set_name("sym").set_address(0xfeed)));
        root_node->AddChild(std::move(args), node_controller.NewRequest(dispatcher()), {},
                            [](auto result) { EXPECT_FALSE(result.is_err()); });
        BindDriver(std::move(request), std::move(root_node));
      });
  auto root_driver = StartRootDriver("fuchsia-boot:///#meta/root-driver.cm", driver_runner);
  ASSERT_EQ(ZX_OK, root_driver.status_value());

  EXPECT_TRUE(did_bind);

  driver_host().SetStartHandler([](fdf::DriverStartArgs start_args, auto request) {
    EXPECT_FALSE(start_args.has_symbols());
    auto& entries = start_args.program().entries();
    EXPECT_EQ(2u, entries.size());
    EXPECT_EQ("binary", entries[0].key);
    EXPECT_EQ("driver/second-driver.so", entries[0].value->str());
    EXPECT_EQ("colocate", entries[1].key);
    EXPECT_EQ("false", entries[1].value->str());
  });
  StartDriverHost("driver-hosts", "driver-host-1");
  auto second_driver =
      StartDriver(driver_runner, {
                                     .url = "fuchsia-boot:///#meta/second-driver.cm",
                                     .binary = "driver/second-driver.so",
                                 });

  StopDriverComponent(std::move(root_driver.value()));
}

// Start the root driver, and then start a second driver in the same driver
// host.
TEST_F(DriverRunnerTest, StartSecondDriver_SameDriverHost) {
  auto driver_index = CreateDriverIndex();
  auto driver_index_client = driver_index.Connect();
  ASSERT_EQ(ZX_OK, driver_index_client.status_value());
  DriverRunner driver_runner(ConnectToRealm(), std::move(*driver_index_client), inspector(),
                             dispatcher());
  auto defer = fit::defer([this] { Unbind(); });

  fdf::NodeControllerPtr node_controller;
  bool did_bind = false;
  node_controller.events().OnBind = [&did_bind] { did_bind = true; };
  driver_host().SetStartHandler(
      [this, &node_controller](fdf::DriverStartArgs start_args, auto request) {
        auto& entries = start_args.program().entries();
        EXPECT_EQ(2u, entries.size());
        EXPECT_EQ("binary", entries[0].key);
        EXPECT_EQ("driver/root-driver.so", entries[0].value->str());
        EXPECT_EQ("colocate", entries[1].key);
        EXPECT_EQ("false", entries[1].value->str());

        realm().SetCreateChildHandler(
            [](fdecl::CollectionRef collection, fdecl::Child decl, auto offers) {
              EXPECT_EQ("boot-drivers", collection.name);
              EXPECT_EQ("root.second", decl.name());
              EXPECT_EQ("fuchsia-boot:///#meta/second-driver.cm", decl.url());
            });
        realm().SetOpenExposedDirHandler([this](fdecl::ChildRef child, auto exposed_dir) {
          EXPECT_EQ("boot-drivers", child.collection);
          EXPECT_EQ("root.second", child.name);
          driver_dir().Bind(std::move(exposed_dir));
        });

        fdf::NodePtr root_node;
        EXPECT_EQ(ZX_OK, root_node.Bind(std::move(*start_args.mutable_node()), dispatcher()));
        fdf::NodeAddArgs args;
        args.set_name("second");
        args.mutable_offers()->emplace_back().set_protocol(
            std::move(fdecl::OfferProtocol()
                          .set_source_name("fuchsia.package.Protocol")
                          .set_target_name("fuchsia.package.Renamed")));
        args.mutable_symbols()->emplace_back(
            std::move(fdf::NodeSymbol().set_name("sym").set_address(0xfeed)));
        root_node->AddChild(std::move(args), node_controller.NewRequest(dispatcher()), {},
                            [](auto result) { EXPECT_FALSE(result.is_err()); });
        BindDriver(std::move(request), std::move(root_node));
      });
  auto root_driver = StartRootDriver("fuchsia-boot:///#meta/root-driver.cm", driver_runner);
  ASSERT_EQ(ZX_OK, root_driver.status_value());
  EXPECT_TRUE(did_bind);

  driver_host().SetStartHandler([](fdf::DriverStartArgs start_args, auto request) {
    auto& symbols = start_args.symbols();
    EXPECT_EQ(1u, symbols.size());
    EXPECT_EQ("sym", symbols[0].name());
    EXPECT_EQ(0xfeedu, symbols[0].address());
    auto& entries = start_args.program().entries();
    EXPECT_EQ(2u, entries.size());
    EXPECT_EQ("binary", entries[0].key);
    EXPECT_EQ("driver/second-driver.so", entries[0].value->str());
    EXPECT_EQ("colocate", entries[1].key);
    EXPECT_EQ("true", entries[1].value->str());
  });
  auto second_driver =
      StartDriver(driver_runner, {
                                     .url = "fuchsia-boot:///#meta/second-driver.cm",
                                     .binary = "driver/second-driver.so",
                                     .colocate = true,
                                 });

  StopDriverComponent(std::move(root_driver.value()));
}

// Start the root driver, and then start a second driver that we match based on
// node properties.
TEST_F(DriverRunnerTest, StartSecondDriver_UseProperties) {
  FakeDriverIndex driver_index(
      dispatcher(), [](auto args) -> zx::status<FakeDriverIndex::MatchResult> {
        if (args.has_properties() && args.properties()[0].key().is_int_value() &&
            args.properties()[0].key().int_value() == 0x1985 &&
            args.properties()[0].value().is_int_value() &&

            args.properties()[0].value().int_value() == 0x2301) {
          return zx::ok(FakeDriverIndex::MatchResult{
              .url = "fuchsia-boot:///#meta/second-driver.cm",
          });
        } else {
          return zx::error(ZX_ERR_NOT_FOUND);
        }
      });
  auto driver_index_client = driver_index.Connect();
  ASSERT_EQ(ZX_OK, driver_index_client.status_value());
  DriverRunner driver_runner(ConnectToRealm(), std::move(*driver_index_client), inspector(),
                             dispatcher());
  auto defer = fit::defer([this] { Unbind(); });

  fdf::NodeControllerPtr node_controller;
  driver_host().SetStartHandler(
      [this, &node_controller](fdf::DriverStartArgs start_args, auto request) {
        auto& entries = start_args.program().entries();
        EXPECT_EQ(2u, entries.size());
        EXPECT_EQ("binary", entries[0].key);
        EXPECT_EQ("driver/root-driver.so", entries[0].value->str());
        EXPECT_EQ("colocate", entries[1].key);
        EXPECT_EQ("false", entries[1].value->str());

        realm().SetCreateChildHandler(
            [](fdecl::CollectionRef collection, fdecl::Child decl, auto offers) {
              EXPECT_EQ("boot-drivers", collection.name);
              EXPECT_EQ("root.second", decl.name());
              EXPECT_EQ("fuchsia-boot:///#meta/second-driver.cm", decl.url());
            });
        realm().SetOpenExposedDirHandler([this](fdecl::ChildRef child, auto exposed_dir) {
          EXPECT_EQ("boot-drivers", child.collection);
          EXPECT_EQ("root.second", child.name);
          driver_dir().Bind(std::move(exposed_dir));
        });

        fdf::NodePtr root_node;
        EXPECT_EQ(ZX_OK, root_node.Bind(std::move(*start_args.mutable_node()), dispatcher()));
        fdf::NodeAddArgs args;
        args.set_name("second");
        args.mutable_properties()->emplace_back(
            std::move(fdf::NodeProperty()
                          .set_key(fdf::NodePropertyKey::WithIntValue(0x1985))
                          .set_value(fdf::NodePropertyValue::WithIntValue(0x2301))));
        root_node->AddChild(std::move(args), node_controller.NewRequest(dispatcher()), {},
                            [](auto result) { EXPECT_FALSE(result.is_err()); });
        BindDriver(std::move(request), std::move(root_node));
      });
  auto root_driver = StartRootDriver("fuchsia-boot:///#meta/root-driver.cm", driver_runner);
  ASSERT_EQ(ZX_OK, root_driver.status_value());

  driver_host().SetStartHandler([](fdf::DriverStartArgs start_args, auto request) {
    auto& entries = start_args.program().entries();
    EXPECT_EQ(2u, entries.size());
    EXPECT_EQ("binary", entries[0].key);
    EXPECT_EQ("driver/second-driver.so", entries[0].value->str());
    EXPECT_EQ("colocate", entries[1].key);
    EXPECT_EQ("true", entries[1].value->str());
  });
  StartDriver(driver_runner, {
                                 .url = "fuchsia-boot:///#meta/second-driver.cm",
                                 .binary = "driver/second-driver.so",
                                 .colocate = true,
                             });

  StopDriverComponent(std::move(root_driver.value()));
}

// Start the root driver, and then add a child node that does not bind to a
// second driver.
TEST_F(DriverRunnerTest, StartSecondDriver_UnknownNode) {
  auto driver_index = CreateDriverIndex();
  auto driver_index_client = driver_index.Connect();
  ASSERT_EQ(ZX_OK, driver_index_client.status_value());
  DriverRunner driver_runner(ConnectToRealm(), std::move(*driver_index_client), inspector(),
                             dispatcher());
  auto defer = fit::defer([this] { Unbind(); });

  driver_host().SetStartHandler([this](fdf::DriverStartArgs start_args, auto request) {
    auto& entries = start_args.program().entries();
    EXPECT_EQ(2u, entries.size());
    EXPECT_EQ("binary", entries[0].key);
    EXPECT_EQ("driver/root-driver.so", entries[0].value->str());
    EXPECT_EQ("colocate", entries[1].key);
    EXPECT_EQ("false", entries[1].value->str());

    fdf::NodePtr root_node;
    EXPECT_EQ(ZX_OK, root_node.Bind(std::move(*start_args.mutable_node()), dispatcher()));
    fdf::NodeAddArgs args;
    args.set_name("unknown-node");
    fdf::NodeControllerPtr node_controller;
    root_node->AddChild(std::move(args), node_controller.NewRequest(dispatcher()), {},
                        [](auto result) { EXPECT_FALSE(result.is_err()); });
    BindDriver(std::move(request), std::move(root_node));
  });

  auto root_driver = StartRootDriver("fuchsia-boot:///#meta/root-driver.cm", driver_runner);
  ASSERT_EQ(ZX_OK, root_driver.status_value());

  StartDriver(driver_runner, {.close = true});
  ASSERT_EQ(1u, driver_runner.NumOrphanedNodes());

  StopDriverComponent(std::move(root_driver.value()));
}

// Start the second driver, and then unbind its associated node.
TEST_F(DriverRunnerTest, StartSecondDriver_UnbindSecondNode) {
  auto driver_index = CreateDriverIndex();
  auto driver_index_client = driver_index.Connect();
  ASSERT_EQ(ZX_OK, driver_index_client.status_value());
  DriverRunner driver_runner(ConnectToRealm(), std::move(*driver_index_client), inspector(),
                             dispatcher());
  auto defer = fit::defer([this] { Unbind(); });

  fdf::NodeControllerPtr node_controller;
  driver_host().SetStartHandler(
      [this, &node_controller](fdf::DriverStartArgs start_args, auto request) {
        realm().SetCreateChildHandler(
            [](fdecl::CollectionRef collection, fdecl::Child decl, auto offers) {});
        realm().SetOpenExposedDirHandler([this](fdecl::ChildRef child, auto exposed_dir) {
          driver_dir().Bind(std::move(exposed_dir));
        });

        fdf::NodePtr root_node;
        EXPECT_EQ(ZX_OK, root_node.Bind(std::move(*start_args.mutable_node()), dispatcher()));
        fdf::NodeAddArgs args;
        args.set_name("second");
        root_node->AddChild(std::move(args), node_controller.NewRequest(dispatcher()), {},
                            [](auto result) { EXPECT_FALSE(result.is_err()); });
        BindDriver(std::move(request), std::move(root_node));
      });
  auto root_driver = StartRootDriver("fuchsia-boot:///#meta/root-driver.cm", driver_runner);
  ASSERT_EQ(ZX_OK, root_driver.status_value());

  TestDriver* second_test_driver = nullptr;
  driver_host().SetStartHandler(
      [this, &second_test_driver](fdf::DriverStartArgs start_args, auto request) {
        fdf::NodePtr second_node;
        EXPECT_EQ(ZX_OK, second_node.Bind(std::move(*start_args.mutable_node()), dispatcher()));
        second_test_driver = BindDriver(std::move(request), std::move(second_node));
      });

  StartDriverHost("driver-hosts", "driver-host-1");
  auto second_driver =
      StartDriver(driver_runner, {
                                     .url = "fuchsia-boot:///#meta/second-driver.cm",
                                     .binary = "driver/second-driver.so",
                                 });

  // Unbinding the second node stops the driver bound to it.
  second_test_driver->node().Unbind();
  EXPECT_TRUE(RunLoopUntilIdle());
  zx_signals_t signals = 0;
  ASSERT_EQ(ZX_OK, second_driver.channel().wait_one(ZX_CHANNEL_PEER_CLOSED, zx::time::infinite(),
                                                    &signals));
  ASSERT_TRUE(signals & ZX_CHANNEL_PEER_CLOSED);

  StopDriverComponent(std::move(root_driver.value()));
}

// Start the second driver, and then close the associated Driver protocol
// channel.
TEST_F(DriverRunnerTest, StartSecondDriver_CloseSecondDriver) {
  auto driver_index = CreateDriverIndex();
  auto driver_index_client = driver_index.Connect();
  ASSERT_EQ(ZX_OK, driver_index_client.status_value());
  DriverRunner driver_runner(ConnectToRealm(), std::move(*driver_index_client), inspector(),
                             dispatcher());
  auto defer = fit::defer([this] { Unbind(); });

  fdf::NodeControllerPtr node_controller;
  driver_host().SetStartHandler(
      [this, &node_controller](fdf::DriverStartArgs start_args, auto request) {
        realm().SetCreateChildHandler(
            [](fdecl::CollectionRef collection, fdecl::Child decl, auto offers) {});
        realm().SetOpenExposedDirHandler([this](fdecl::ChildRef child, auto exposed_dir) {
          driver_dir().Bind(std::move(exposed_dir));
        });

        fdf::NodePtr root_node;
        EXPECT_EQ(ZX_OK, root_node.Bind(std::move(*start_args.mutable_node()), dispatcher()));
        fdf::NodeAddArgs args;
        args.set_name("second");
        root_node->AddChild(std::move(args), node_controller.NewRequest(dispatcher()), {},
                            [](auto result) { EXPECT_FALSE(result.is_err()); });
        BindDriver(std::move(request), std::move(root_node));
      });
  auto root_driver = StartRootDriver("fuchsia-boot:///#meta/root-driver.cm", driver_runner);
  ASSERT_EQ(ZX_OK, root_driver.status_value());

  fdf::NodePtr second_node;
  fidl::InterfaceRequest<fdf::Driver> second_request;
  driver_host().SetStartHandler(
      [this, &second_node, &second_request](fdf::DriverStartArgs start_args, auto request) {
        second_request = std::move(request);
        EXPECT_EQ(ZX_OK, second_node.Bind(std::move(*start_args.mutable_node()), dispatcher()));
      });

  StartDriverHost("driver-hosts", "driver-host-1");
  auto second_driver =
      StartDriver(driver_runner, {
                                     .url = "fuchsia-boot:///#meta/second-driver.cm",
                                     .binary = "driver/second-driver.so",
                                 });

  // Closing the Driver protocol channel of the second driver causes the driver
  // to be stopped.
  second_request.TakeChannel();
  EXPECT_TRUE(RunLoopUntilIdle());
  zx_signals_t signals = 0;
  ASSERT_EQ(ZX_OK, second_driver.channel().wait_one(ZX_CHANNEL_PEER_CLOSED, zx::time::infinite(),
                                                    &signals));
  ASSERT_TRUE(signals & ZX_CHANNEL_PEER_CLOSED);

  StopDriverComponent(std::move(root_driver.value()));
}

// Start a chain of drivers, and then unbind the second driver's node.
TEST_F(DriverRunnerTest, StartDriverChain_UnbindSecondNode) {
  FakeDriverIndex driver_index(dispatcher(),
                               [](auto args) -> zx::status<FakeDriverIndex::MatchResult> {
                                 std::string name(args.name().get());
                                 return zx::ok(FakeDriverIndex::MatchResult{
                                     .url = "fuchsia-boot:///#meta/" + name + "-driver.cm",
                                 });
                               });
  auto driver_index_client = driver_index.Connect();
  ASSERT_EQ(ZX_OK, driver_index_client.status_value());
  DriverRunner driver_runner(ConnectToRealm(), std::move(*driver_index_client), inspector(),
                             dispatcher());
  auto defer = fit::defer([this] { Unbind(); });

  fdf::NodeControllerPtr node_controller;
  driver_host().SetStartHandler(
      [this, &node_controller](fdf::DriverStartArgs start_args, auto request) {
        realm().SetCreateChildHandler(
            [](fdecl::CollectionRef collection, fdecl::Child decl, auto offers) {});
        realm().SetOpenExposedDirHandler([this](fdecl::ChildRef child, auto exposed_dir) {
          driver_dir().Bind(std::move(exposed_dir));
        });

        fdf::NodePtr root_node;
        EXPECT_EQ(ZX_OK, root_node.Bind(std::move(*start_args.mutable_node()), dispatcher()));
        fdf::NodeAddArgs args;
        args.set_name("node-0");
        root_node->AddChild(std::move(args), node_controller.NewRequest(dispatcher()), {},
                            [](auto result) { EXPECT_FALSE(result.is_err()); });
        BindDriver(std::move(request), std::move(root_node));
      });
  auto root_driver = StartRootDriver("fuchsia-boot:///#meta/root-driver.cm", driver_runner);
  ASSERT_EQ(ZX_OK, root_driver.status_value());

  constexpr size_t kMaxNodes = 10;
  fdf::NodePtr second_node;
  std::vector<fidl::ClientEnd<frunner::ComponentController>> drivers;
  for (size_t i = 1; i <= kMaxNodes; i++) {
    driver_host().SetStartHandler(
        [this, &second_node, &node_controller, i](fdf::DriverStartArgs start_args, auto request) {
          realm().SetCreateChildHandler(
              [](fdecl::CollectionRef collection, fdecl::Child decl, auto offers) {});
          realm().SetOpenExposedDirHandler([this](fdecl::ChildRef child, auto exposed_dir) {
            driver_dir().Bind(std::move(exposed_dir));
          });

          fdf::NodePtr node;
          EXPECT_EQ(ZX_OK, node.Bind(std::move(*start_args.mutable_node()), dispatcher()));
          // Only add a node that a driver will be bound to.
          if (i != kMaxNodes) {
            fdf::NodeAddArgs args;
            args.set_name("node-" + std::to_string(i));
            node->AddChild(std::move(args), node_controller.NewRequest(dispatcher()), {},
                           [](auto result) { EXPECT_FALSE(result.is_err()); });
          }
          auto driver = BindDriver(std::move(request), std::move(node));
          if (!second_node.is_bound()) {
            second_node = std::move(driver->node());
          }
        });

    StartDriverHost("driver-hosts", "driver-host-" + std::to_string(i));
    drivers.emplace_back(StartDriver(driver_runner, {
                                                        .url = "fuchsia-boot:///#meta/node-" +
                                                               std::to_string(i - 1) + "-driver.cm",
                                                        .binary = "driver/driver.so",
                                                    }));
  }

  // Unbinding the second node stops all drivers bound in the sub-tree, in a
  // depth-first order.
  std::vector<size_t> indices;
  std::vector<fidl::WireSharedClient<frunner::ComponentController>> clients;
  for (auto& driver : drivers) {
    clients.emplace_back(std::move(driver), dispatcher(),
                         TeardownWatcher(clients.size() + 1, indices));
  }
  second_node.Unbind();
  EXPECT_TRUE(RunLoopUntilIdle());
  EXPECT_THAT(indices, ElementsAre(10, 9, 8, 7, 6, 5, 4, 3, 2, 1));

  StopDriverComponent(std::move(root_driver.value()));
}

// Start the second driver, and then unbind the root node.
TEST_F(DriverRunnerTest, StartSecondDriver_UnbindRootNode) {
  auto driver_index = CreateDriverIndex();
  auto driver_index_client = driver_index.Connect();
  ASSERT_EQ(ZX_OK, driver_index_client.status_value());
  DriverRunner driver_runner(ConnectToRealm(), std::move(*driver_index_client), inspector(),
                             dispatcher());
  auto defer = fit::defer([this] { Unbind(); });

  fdf::NodeControllerPtr node_controller;
  fdf::NodePtr root_node;
  driver_host().SetStartHandler(
      [this, &node_controller, &root_node](fdf::DriverStartArgs start_args, auto request) {
        realm().SetCreateChildHandler(
            [](fdecl::CollectionRef collection, fdecl::Child decl, auto offers) {});
        realm().SetOpenExposedDirHandler([this](fdecl::ChildRef child, auto exposed_dir) {
          driver_dir().Bind(std::move(exposed_dir));
        });

        fdf::NodePtr node;
        EXPECT_EQ(ZX_OK, node.Bind(std::move(*start_args.mutable_node()), dispatcher()));
        fdf::NodeAddArgs args;
        args.set_name("second");
        node->AddChild(std::move(args), node_controller.NewRequest(dispatcher()), {},
                       [](auto result) { EXPECT_FALSE(result.is_err()); });
        auto driver = BindDriver(std::move(request), std::move(node));
        root_node = std::move(driver->node());
      });
  auto root_driver = StartRootDriver("fuchsia-boot:///#meta/root-driver.cm", driver_runner);
  ASSERT_EQ(ZX_OK, root_driver.status_value());

  driver_host().SetStartHandler([this](fdf::DriverStartArgs start_args, auto request) {
    fdf::NodePtr second_node;
    EXPECT_EQ(ZX_OK, second_node.Bind(std::move(*start_args.mutable_node()), dispatcher()));
    BindDriver(std::move(request), std::move(second_node));
  });

  StartDriverHost("driver-hosts", "driver-host-1");
  auto second_driver =
      StartDriver(driver_runner, {
                                     .url = "fuchsia-boot:///#meta/second-driver.cm",
                                     .binary = "driver/second-driver.so",
                                 });

  // Unbinding the root node stops all drivers.
  std::vector<size_t> indices;
  fidl::WireSharedClient<frunner::ComponentController> root_client(
      std::move(*root_driver), dispatcher(), TeardownWatcher(0, indices));
  fidl::WireSharedClient<frunner::ComponentController> second_client(
      std::move(second_driver), dispatcher(), TeardownWatcher(1, indices));
  root_node.Unbind();
  EXPECT_TRUE(RunLoopUntilIdle());
  EXPECT_THAT(indices, ElementsAre(1, 0));
}

// Start the second driver, and then stop the root driver.
TEST_F(DriverRunnerTest, StartSecondDriver_StopRootDriver) {
  auto driver_index = CreateDriverIndex();
  auto driver_index_client = driver_index.Connect();
  ASSERT_EQ(ZX_OK, driver_index_client.status_value());
  DriverRunner driver_runner(ConnectToRealm(), std::move(*driver_index_client), inspector(),
                             dispatcher());
  auto defer = fit::defer([this] { Unbind(); });

  fdf::NodeControllerPtr node_controller;
  driver_host().SetStartHandler(
      [this, &node_controller](fdf::DriverStartArgs start_args, auto request) {
        realm().SetCreateChildHandler(
            [](fdecl::CollectionRef collection, fdecl::Child decl, auto offers) {});
        realm().SetOpenExposedDirHandler([this](fdecl::ChildRef child, auto exposed_dir) {
          driver_dir().Bind(std::move(exposed_dir));
        });

        fdf::NodePtr root_node;
        EXPECT_EQ(ZX_OK, root_node.Bind(std::move(*start_args.mutable_node()), dispatcher()));
        fdf::NodeAddArgs args;
        args.set_name("second");
        root_node->AddChild(std::move(args), node_controller.NewRequest(dispatcher()), {},
                            [](auto result) { EXPECT_FALSE(result.is_err()); });
        BindDriver(std::move(request), std::move(root_node));
      });
  auto root_driver = StartRootDriver("fuchsia-boot:///#meta/root-driver.cm", driver_runner);
  ASSERT_EQ(ZX_OK, root_driver.status_value());

  driver_host().SetStartHandler([this](fdf::DriverStartArgs start_args, auto request) {
    fdf::NodePtr node;
    EXPECT_EQ(ZX_OK, node.Bind(std::move(*start_args.mutable_node()), dispatcher()));
    BindDriver(std::move(request), std::move(node));
  });

  StartDriverHost("driver-hosts", "driver-host-1");
  auto second_driver =
      StartDriver(driver_runner, {
                                     .url = "fuchsia-boot:///#meta/second-driver.cm",
                                     .binary = "driver/second-driver.so",
                                 });

  // Stopping the root driver stops all drivers.
  std::vector<size_t> indices;
  fidl::WireSharedClient<frunner::ComponentController> root_client(
      std::move(*root_driver), dispatcher(), TeardownWatcher(0, indices));
  fidl::WireSharedClient<frunner::ComponentController> second_client(
      std::move(second_driver), dispatcher(), TeardownWatcher(1, indices));
  root_client->Stop();
  EXPECT_TRUE(RunLoopUntilIdle());
  EXPECT_THAT(indices, ElementsAre(1, 0));
}

// Start the second driver, stop the root driver, and block while waiting on the
// second driver to shut down.
TEST_F(DriverRunnerTest, StartSecondDriver_BlockOnSecondDriver) {
  auto driver_index = CreateDriverIndex();
  auto driver_index_client = driver_index.Connect();
  ASSERT_EQ(ZX_OK, driver_index_client.status_value());
  DriverRunner driver_runner(ConnectToRealm(), std::move(*driver_index_client), inspector(),
                             dispatcher());
  auto defer = fit::defer([this] { Unbind(); });

  fdf::NodeControllerPtr node_controller;
  driver_host().SetStartHandler(
      [this, &node_controller](fdf::DriverStartArgs start_args, auto request) {
        realm().SetCreateChildHandler(
            [](fdecl::CollectionRef collection, fdecl::Child decl, auto offers) {});
        realm().SetOpenExposedDirHandler([this](fdecl::ChildRef child, auto exposed_dir) {
          driver_dir().Bind(std::move(exposed_dir));
        });

        fdf::NodePtr root_node;
        EXPECT_EQ(ZX_OK, root_node.Bind(std::move(*start_args.mutable_node()), dispatcher()));
        fdf::NodeAddArgs args;
        args.set_name("second");
        root_node->AddChild(std::move(args), node_controller.NewRequest(dispatcher()), {},
                            [](auto result) { EXPECT_FALSE(result.is_err()); });
        BindDriver(std::move(request), std::move(root_node));
      });
  auto root_driver = StartRootDriver("fuchsia-boot:///#meta/root-driver.cm", driver_runner);
  ASSERT_EQ(ZX_OK, root_driver.status_value());

  TestDriver* second_test_driver = nullptr;
  driver_host().SetStartHandler(
      [this, &second_test_driver](fdf::DriverStartArgs start_args, auto request) {
        fdf::NodePtr node;
        EXPECT_EQ(ZX_OK, node.Bind(std::move(*start_args.mutable_node()), dispatcher()));
        second_test_driver = BindDriver(std::move(request), std::move(node));
      });

  StartDriverHost("driver-hosts", "driver-host-1");
  auto second_driver =
      StartDriver(driver_runner, {
                                     .url = "fuchsia-boot:///#meta/second-driver.cm",
                                     .binary = "driver/second-driver.so",
                                 });
  // When the second driver gets asked to stop, don't drop the binding,
  // which means DriverRunner will wait for the binding to drop.
  second_test_driver->SetStopHandler([]() {});

  // Stopping the root driver stops all drivers, but is blocked waiting on the
  // second driver to stop.
  std::vector<size_t> indices;
  fidl::WireSharedClient<frunner::ComponentController> root_client(
      std::move(*root_driver), dispatcher(), TeardownWatcher(0, indices));
  fidl::WireSharedClient<frunner::ComponentController> second_client(
      std::move(second_driver), dispatcher(), TeardownWatcher(1, indices));
  root_client->Stop();
  EXPECT_TRUE(RunLoopUntilIdle());
  // Nothing has shut down yet, since we are waiting.
  EXPECT_THAT(indices, ElementsAre());

  // Attempt to add a child node to a removed node.
  bool is_error = false;
  second_test_driver->node()->AddChild({}, node_controller.NewRequest(dispatcher()), {},
                                       [&is_error](auto result) { is_error = result.is_err(); });
  EXPECT_TRUE(RunLoopUntilIdle());
  EXPECT_TRUE(is_error);

  // Unbind the second node, indicating the second driver has stopped, thereby
  // continuing the stop sequence.
  second_test_driver->close_binding();
  EXPECT_TRUE(RunLoopUntilIdle());
  EXPECT_THAT(indices, ElementsAre(1, 0));
}

// Start a composite driver.
TEST_F(DriverRunnerTest, StartCompositeDriver) {
  auto driver_index = CreateDriverIndex();
  auto driver_index_client = driver_index.Connect();
  ASSERT_EQ(ZX_OK, driver_index_client.status_value());
  DriverRunner driver_runner(ConnectToRealm(), std::move(*driver_index_client), inspector(),
                             dispatcher());
  auto defer = fit::defer([this] { Unbind(); });

  fdf::NodeControllerPtr node_controller;
  driver_host().SetStartHandler(
      [this, &node_controller](fdf::DriverStartArgs start_args, auto request) {
        realm().SetCreateChildHandler(
            [](fdecl::CollectionRef collection, fdecl::Child decl, auto offers) {});
        realm().SetOpenExposedDirHandler([this](fdecl::ChildRef child, auto exposed_dir) {
          driver_dir().Bind(std::move(exposed_dir));
        });

        fdf::NodePtr root_node;
        EXPECT_EQ(ZX_OK, root_node.Bind(std::move(*start_args.mutable_node()), dispatcher()));
        fdf::NodeAddArgs args;
        args.set_name("part-1");
        args.mutable_offers()->emplace_back().set_protocol(
            std::move(fdecl::OfferProtocol()
                          .set_source_name("fuchsia.package.ProtocolA")
                          .set_target_name("fuchsia.package.RenamedA")));
        root_node->AddChild(std::move(args), node_controller.NewRequest(dispatcher()), {},
                            [](auto result) { EXPECT_FALSE(result.is_err()); });
        args.set_name("part-2");
        args.mutable_offers()->emplace_back().set_protocol(
            std::move(fdecl::OfferProtocol()
                          .set_source_name("fuchsia.package.ProtocolB")
                          .set_target_name("fuchsia.package.RenamedB")));
        root_node->AddChild(std::move(args), node_controller.NewRequest(dispatcher()), {},
                            [](auto result) { EXPECT_FALSE(result.is_err()); });
        BindDriver(std::move(request), std::move(root_node));
      });
  auto root_driver = StartRootDriver("fuchsia-boot:///#meta/root-driver.cm", driver_runner);
  ASSERT_EQ(ZX_OK, root_driver.status_value());

  driver_host().SetStartHandler([this](fdf::DriverStartArgs start_args, auto request) {
    auto& entries = start_args.program().entries();
    EXPECT_EQ(2u, entries.size());
    EXPECT_EQ("binary", entries[0].key);
    EXPECT_EQ("driver/composite-driver.so", entries[0].value->str());
    EXPECT_EQ("colocate", entries[1].key);
    EXPECT_EQ("true", entries[1].value->str());

    fdf::NodePtr node;
    ASSERT_EQ(ZX_OK, node.Bind(start_args.mutable_node()->TakeChannel()));
    BindDriver(std::move(request), std::move(node));
  });
  auto composite_driver =
      StartDriver(driver_runner, {
                                     .url = "fuchsia-boot:///#meta/composite-driver.cm",
                                     .binary = "driver/composite-driver.so",
                                     .colocate = true,
                                 });
  StopDriverComponent(std::move(root_driver.value()));
}

// Start a driver and inspect the driver runner.
TEST_F(DriverRunnerTest, StartAndInspect) {
  auto driver_index = CreateDriverIndex();
  auto driver_index_client = driver_index.Connect();
  ASSERT_EQ(ZX_OK, driver_index_client.status_value());
  DriverRunner driver_runner(ConnectToRealm(), std::move(*driver_index_client), inspector(),
                             dispatcher());
  auto defer = fit::defer([this] { Unbind(); });

  driver_host().SetStartHandler([this](fdf::DriverStartArgs start_args, auto request) {
    realm().SetCreateChildHandler(
        [](fdecl::CollectionRef collection, fdecl::Child decl, auto offers) {});
    realm().SetOpenExposedDirHandler([this](fdecl::ChildRef child, auto exposed_dir) {
      driver_dir().Bind(std::move(exposed_dir));
    });

    fdf::NodePtr root_node;
    EXPECT_EQ(ZX_OK, root_node.Bind(std::move(*start_args.mutable_node()), dispatcher()));
    fdf::NodeAddArgs args;
    args.set_name("second");
    args.mutable_offers()->emplace_back().set_protocol(
        std::move(fdecl::OfferProtocol()
                      .set_source_name("fuchsia.package.ProtocolA")
                      .set_target_name("fuchsia.package.RenamedA")));
    args.mutable_offers()->emplace_back().set_protocol(
        std::move(fdecl::OfferProtocol()
                      .set_source_name("fuchsia.package.ProtocolB")
                      .set_target_name("fuchsia.package.RenamedB")));
    args.mutable_symbols()->emplace_back(
        std::move(fdf::NodeSymbol().set_name("symbol-A").set_address(0x2301)));
    args.mutable_symbols()->emplace_back(
        std::move(fdf::NodeSymbol().set_name("symbol-B").set_address(0x1985)));
    fdf::NodeControllerPtr node_controller;
    root_node->AddChild(std::move(args), node_controller.NewRequest(dispatcher()), {},
                        [](auto result) { EXPECT_FALSE(result.is_err()); });
    BindDriver(std::move(request), std::move(root_node));
  });
  auto root_driver = StartRootDriver("fuchsia-boot:///#meta/root-driver.cm", driver_runner);
  ASSERT_EQ(ZX_OK, root_driver.status_value());

  EXPECT_THAT(
      Inspect(driver_runner),
      AllOf(NodeMatches(NameMatches("root")),
            ChildrenMatch(ElementsAre(AllOf(
                NodeMatches(NameMatches("root")),
                ChildrenMatch(ElementsAre(AllOf(NodeMatches(AllOf(
                    NameMatches("second"),
                    PropertyList(UnorderedElementsAre(
                        StringIs("offers", "fuchsia.package.RenamedA, fuchsia.package.RenamedB"),
                        StringIs("symbols", "symbol-A, symbol-B")))))))))))));

  StopDriverComponent(std::move(root_driver.value()));
}

// Start a composite driver and inspect the driver runner.
TEST_F(DriverRunnerTest, StartAndInspect_CompositeDriver) {
  auto driver_index = CreateDriverIndex();
  auto driver_index_client = driver_index.Connect();
  ASSERT_EQ(ZX_OK, driver_index_client.status_value());
  DriverRunner driver_runner(ConnectToRealm(), std::move(*driver_index_client), inspector(),
                             dispatcher());
  auto defer = fit::defer([this] { Unbind(); });

  fdf::NodeControllerPtr node_controller;
  driver_host().SetStartHandler(
      [this, &node_controller](fdf::DriverStartArgs start_args, auto request) {
        realm().SetCreateChildHandler(
            [](fdecl::CollectionRef collection, fdecl::Child decl, auto offers) {});
        realm().SetOpenExposedDirHandler([this](fdecl::ChildRef child, auto exposed_dir) {
          driver_dir().Bind(std::move(exposed_dir));
        });

        fdf::NodePtr root_node;
        EXPECT_EQ(ZX_OK, root_node.Bind(std::move(*start_args.mutable_node()), dispatcher()));
        fdf::NodeAddArgs args;
        args.set_name("part-1");
        args.mutable_offers()->emplace_back().set_protocol(
            std::move(fdecl::OfferProtocol()
                          .set_source_name("fuchsia.package.ProtocolA")
                          .set_target_name("fuchsia.package.RenamedA")));
        root_node->AddChild(std::move(args), node_controller.NewRequest(dispatcher()), {},
                            [](auto result) { EXPECT_FALSE(result.is_err()); });
        args.set_name("part-2");
        args.mutable_offers()->emplace_back().set_protocol(
            std::move(fdecl::OfferProtocol()
                          .set_source_name("fuchsia.package.ProtocolB")
                          .set_target_name("fuchsia.package.RenamedB")));
        root_node->AddChild(std::move(args), node_controller.NewRequest(dispatcher()), {},
                            [](auto result) { EXPECT_FALSE(result.is_err()); });
        BindDriver(std::move(request), std::move(root_node));
      });
  auto root_driver = StartRootDriver("fuchsia-boot:///#meta/root-driver.cm", driver_runner);
  ASSERT_EQ(ZX_OK, root_driver.status_value());

  driver_host().SetStartHandler([this](fdf::DriverStartArgs start_args, auto request) {
    fdf::NodePtr composite_node;
    EXPECT_EQ(ZX_OK, composite_node.Bind(std::move(*start_args.mutable_node()), dispatcher()));
    fdf::NodeAddArgs args;
    args.set_name("child");
    fdf::NodeControllerPtr node_controller;
    composite_node->AddChild(std::move(args), node_controller.NewRequest(dispatcher()), {},
                             [](auto result) { EXPECT_FALSE(result.is_err()); });
    BindDriver(std::move(request), std::move(composite_node));
  });
  auto composite_driver =
      StartDriver(driver_runner, {
                                     .url = "fuchsia-boot:///#meta/composite-driver.cm",
                                     .binary = "driver/composite-driver.so",
                                     .colocate = true,
                                 });

  EXPECT_THAT(
      Inspect(driver_runner),
      AllOf(NodeMatches(NameMatches("root")),
            ChildrenMatch(ElementsAre(AllOf(
                NodeMatches(NameMatches("root")),
                ChildrenMatch(UnorderedElementsAre(
                    AllOf(NodeMatches(AllOf(NameMatches("part-1"),
                                            PropertyList(ElementsAre(
                                                StringIs("offers", "fuchsia.package.RenamedA"))))),
                          ChildrenMatch(ElementsAre(AllOf(
                              NodeMatches(NameMatches("composite")),
                              ChildrenMatch(ElementsAre(NodeMatches(NameMatches("child")))))))),
                    AllOf(NodeMatches(AllOf(NameMatches("part-2"),
                                            PropertyList(ElementsAre(
                                                StringIs("offers", "fuchsia.package.RenamedB"))))),
                          ChildrenMatch(ElementsAre(AllOf(NodeMatches(NameMatches("composite")),
                                                          ChildrenMatch(IsEmpty()))))))))))));

  StopDriverComponent(std::move(root_driver.value()));
}
