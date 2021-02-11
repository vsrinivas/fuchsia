// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/devices/bin/driver_manager/driver_runner.h"

#include <fuchsia/driver/framework/cpp/fidl_test_base.h>
#include <fuchsia/io/cpp/fidl_test_base.h>
#include <fuchsia/sys2/cpp/fidl_test_base.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/gtest/test_loop_fixture.h>
#include <lib/sys/cpp/testing/component_context_provider.h>

#include <gtest/gtest.h>

namespace fdata = llcpp::fuchsia::data;
namespace fdf = fuchsia::driver::framework;
namespace fio = fuchsia::io;
namespace frunner = llcpp::fuchsia::component::runner;
namespace fsys = fuchsia::sys2;

class TestRealm : public fsys::testing::Realm_TestBase {
 public:
  using BindChildHandler =
      fit::function<void(fsys::ChildRef child, fidl::InterfaceRequest<fio::Directory> exposed_dir)>;
  using CreateChildHandler =
      fit::function<void(fsys::CollectionRef collection, fsys::ChildDecl decl)>;

  void SetBindChildHandler(BindChildHandler bind_child_handler) {
    bind_child_handler_ = std::move(bind_child_handler);
  }

  void SetCreateChildHandler(CreateChildHandler create_child_handler) {
    create_child_handler_ = std::move(create_child_handler);
  }

 private:
  void BindChild(fsys::ChildRef child, fidl::InterfaceRequest<fio::Directory> exposed_dir,
                 BindChildCallback callback) override {
    bind_child_handler_(std::move(child), std::move(exposed_dir));
    callback(fsys::Realm_BindChild_Result(fit::ok()));
  }

  void CreateChild(fsys::CollectionRef collection, fsys::ChildDecl decl,
                   CreateChildCallback callback) override {
    create_child_handler_(std::move(collection), std::move(decl));
    callback(fsys::Realm_CreateChild_Result(fit::ok()));
  }

  void NotImplemented_(const std::string& name) override {
    printf("Not implemented: Realm::%s\n", name.data());
  }

  BindChildHandler bind_child_handler_;
  CreateChildHandler create_child_handler_;
};

class TestDirectory : public fio::testing::Directory_TestBase {
 public:
  using OpenHandler =
      fit::function<void(std::string path, fidl::InterfaceRequest<fio::Node> object)>;

  void SetOpenHandler(OpenHandler open_handler) { open_handler_ = std::move(open_handler); }

 private:
  void Clone(uint32_t flags, fidl::InterfaceRequest<fio::Node> object) override {
    EXPECT_EQ(ZX_FS_FLAG_CLONE_SAME_RIGHTS, flags);
  }

  void Open(uint32_t flags, uint32_t mode, std::string path,
            fidl::InterfaceRequest<fio::Node> object) override {
    open_handler_(std::move(path), std::move(object));
  }

  void NotImplemented_(const std::string& name) override {
    printf("Not implemented: Directory::%s\n", name.data());
  }

  OpenHandler open_handler_;
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

  zx_status_t Reply(fidl::OutgoingMessage* message) override {
    EXPECT_TRUE(false);
    return ZX_OK;
  }

  void Close(zx_status_t epitaph) override { EXPECT_TRUE(close_); }

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
    fidl::InterfaceRequestHandler<fsys::Realm> handler = [this](auto request) {
      EXPECT_EQ(ZX_OK, realm_binding_.Bind(std::move(request), loop_.dispatcher()));
    };
    ASSERT_EQ(ZX_OK, provider_.context()->outgoing()->AddPublicService(std::move(handler)));
  }

 protected:
  async::Loop& loop() { return loop_; }
  TestRealm& realm() { return realm_; }
  TestDirectory& driver_host_dir() { return driver_host_dir_; }
  TestDriverHost& driver_host() { return driver_host_; }
  fidl::Binding<fio::Directory>& driver_dir_binding() { return driver_dir_binding_; }

  zx::channel ConnectToRealm() {
    fsys::RealmPtr realm;
    provider_.ConnectToPublicService(realm.NewRequest(loop_.dispatcher()));
    return realm.Unbind().TakeChannel();
  }

  DriverIndex CreateDriverIndex() {
    return DriverIndex([](auto args) -> zx::status<MatchResult> {
      std::string_view name(args.name().data(), args.name().size());
      if (name == "root") {
        return zx::ok(MatchResult{
            .url = "fuchsia-boot:///#meta/root-driver.cm",
            .matched_args = std::move(args),
        });
      } else if (name == "second") {
        return zx::ok(MatchResult{
            .url = "fuchsia-boot:///#meta/second-driver.cm",
            .matched_args = std::move(args),
        });
      } else {
        return zx::error(ZX_ERR_NOT_FOUND);
      }
    });
  }

  void StartDriverHost(std::string coll, std::string name) {
    realm().SetCreateChildHandler(
        [coll, name](fsys::CollectionRef collection, fsys::ChildDecl decl) {
          EXPECT_EQ(coll, collection.name);
          EXPECT_EQ(name, decl.name());
          EXPECT_EQ("fuchsia-boot:///#meta/driver_host2.cm", decl.url());
        });
    realm().SetBindChildHandler([this, coll, name](fsys::ChildRef child, auto exposed_dir) {
      EXPECT_EQ(coll, child.collection.value_or(""));
      EXPECT_EQ(name, child.name);
      EXPECT_EQ(ZX_OK, driver_host_dir_binding_.Bind(std::move(exposed_dir), loop_.dispatcher()));
    });
    driver_host_dir().SetOpenHandler([this](std::string path, auto object) {
      EXPECT_EQ(fdf::DriverHost::Name_, path);
      EXPECT_EQ(ZX_OK, driver_host_binding_.Bind(object.TakeChannel(), loop_.dispatcher()));
    });
  }

  zx::channel StartDriver(DriverRunner* driver_runner, Driver driver) {
    fidl::StringView binary = fidl::unowned_str(driver.binary);
    auto colocate = driver.colocate ? fidl::StringView("true") : fidl::StringView("false");
    fdata::DictionaryEntry program_entries[] = {
        {
            .key = "binary",
            .value = fdata::DictionaryValue::WithStr(fidl::unowned_ptr(&binary)),
        },
        {
            .key = "colocate",
            .value = fdata::DictionaryValue::WithStr(fidl::unowned_ptr(&colocate)),
        },
    };
    auto entries = fidl::unowned_vec(program_entries);
    auto program = fdata::Dictionary::Builder(std::make_unique<fdata::Dictionary::Frame>())
                       .set_entries(fidl::unowned_ptr(&entries))
                       .build();
    auto url = fidl::unowned_str(driver.url);
    fidl::VectorView<frunner::ComponentNamespaceEntry> ns;
    auto outgoing_endpoints = fidl::CreateEndpoints<llcpp::fuchsia::io::Directory>();
    EXPECT_EQ(ZX_OK, outgoing_endpoints.status_value());
    auto start_info = frunner::ComponentStartInfo::UnownedBuilder()
                          .set_resolved_url(fidl::unowned_ptr(&url))
                          .set_program(fidl::unowned_ptr(&program))
                          .set_ns(fidl::unowned_ptr(&ns))
                          .set_outgoing_dir(fidl::unowned_ptr((&outgoing_endpoints->server)));
    zx::channel controller_client_end, controller_server_end;
    EXPECT_EQ(ZX_OK, zx::channel::create(0, &controller_client_end, &controller_server_end));
    TestTransaction transaction(driver.close);
    {
      frunner::ComponentRunner::RawChannelInterface::StartCompleter::Sync completer(&transaction);
      static_cast<frunner::ComponentRunner::RawChannelInterface*>(driver_runner)
          ->Start(start_info.build(), std::move(controller_server_end), completer);
    }
    loop().RunUntilIdle();
    return controller_client_end;
  }

  zx::status<zx::channel> StartRootDriver(std::string_view name, DriverRunner* driver_runner) {
    realm().SetCreateChildHandler([](fsys::CollectionRef collection, fsys::ChildDecl decl) {
      EXPECT_EQ("drivers", collection.name);
      EXPECT_EQ("driver-0", decl.name());
      EXPECT_EQ("fuchsia-boot:///#meta/root-driver.cm", decl.url());
    });
    realm().SetBindChildHandler([this](fsys::ChildRef child, auto exposed_dir) {
      EXPECT_EQ("drivers", child.collection);
      EXPECT_EQ("driver-0", child.name);
      EXPECT_EQ(ZX_OK, driver_dir_binding_.Bind(std::move(exposed_dir), loop_.dispatcher()));
    });
    auto start = driver_runner->StartRootDriver(name);
    if (start.is_error()) {
      return start.take_error();
    }
    loop().RunUntilIdle();

    StartDriverHost("driver_hosts", "driver_host-1");
    zx::channel controller =
        StartDriver(driver_runner, {
                                       .url = "fuchsia-boot:///#meta/root-driver.cm",
                                       .binary = "driver/root-driver.so",
                                   });
    return zx::ok(std::move(controller));
  }

  void Unbind() {
    driver_host_binding_.Unbind();
    loop().RunUntilIdle();
  }

 private:
  TestRealm realm_;
  TestDirectory driver_host_dir_;
  TestDirectory driver_dir_;
  TestDriverHost driver_host_;
  fidl::Binding<fsys::Realm> realm_binding_{&realm_};
  fidl::Binding<fio::Directory> driver_host_dir_binding_{&driver_host_dir_};
  fidl::Binding<fio::Directory> driver_dir_binding_{&driver_dir_};
  fidl::Binding<fdf::DriverHost> driver_host_binding_{&driver_host_};
  async::Loop loop_{&kAsyncLoopConfigNoAttachToCurrentThread};
  sys::testing::ComponentContextProvider provider_{loop_.dispatcher()};
};

// Start the root driver.
TEST_F(DriverRunnerTest, StartRootDriver) {
  auto driver_index = CreateDriverIndex();
  DriverRunner driver_runner(ConnectToRealm(), &driver_index, loop().dispatcher());

  driver_host().SetStartHandler([](fdf::DriverStartArgs start_args, auto driver) {
    auto& entries = start_args.program().entries();
    EXPECT_EQ(2u, entries.size());
    EXPECT_EQ("binary", entries[0].key);
    EXPECT_EQ("driver/root-driver.so", entries[0].value->str());
    EXPECT_EQ("colocate", entries[1].key);
    EXPECT_EQ("false", entries[1].value->str());
  });
  ASSERT_TRUE(StartRootDriver("root", &driver_runner).is_ok());

  Unbind();
}

// Start the root driver, and add a child node owned by the root driver.
TEST_F(DriverRunnerTest, StartRootDriver_AddOwnedChild) {
  auto driver_index = CreateDriverIndex();
  DriverRunner driver_runner(ConnectToRealm(), &driver_index, loop().dispatcher());

  driver_host().SetStartHandler([this](fdf::DriverStartArgs start_args, auto driver) {
    auto& entries = start_args.program().entries();
    EXPECT_EQ(2u, entries.size());
    EXPECT_EQ("binary", entries[0].key);
    EXPECT_EQ("driver/root-driver.so", entries[0].value->str());
    EXPECT_EQ("colocate", entries[1].key);
    EXPECT_EQ("false", entries[1].value->str());

    fdf::NodePtr root_node;
    EXPECT_EQ(ZX_OK, root_node.Bind(start_args.mutable_node()->TakeChannel(), loop().dispatcher()));
    fdf::NodeAddArgs args;
    args.set_name("second");
    fdf::NodeControllerPtr node_controller;
    fdf::NodePtr second_node;
    root_node->AddChild(std::move(args), node_controller.NewRequest(loop().dispatcher()),
                        second_node.NewRequest(loop().dispatcher()));
  });
  ASSERT_TRUE(StartRootDriver("root", &driver_runner).is_ok());

  Unbind();
}

// Start the root driver, add a child node, then remove it.
TEST_F(DriverRunnerTest, StartRootDriver_RemoveOwnedChild) {
  auto driver_index = CreateDriverIndex();
  DriverRunner driver_runner(ConnectToRealm(), &driver_index, loop().dispatcher());

  fdf::NodeControllerPtr node_controller;
  fdf::NodePtr second_node;
  driver_host().SetStartHandler([this, &node_controller, &second_node](
                                    fdf::DriverStartArgs start_args, auto driver) {
    auto& entries = start_args.program().entries();
    EXPECT_EQ(2u, entries.size());
    EXPECT_EQ("binary", entries[0].key);
    EXPECT_EQ("driver/root-driver.so", entries[0].value->str());
    EXPECT_EQ("colocate", entries[1].key);
    EXPECT_EQ("false", entries[1].value->str());

    fdf::NodePtr root_node;
    EXPECT_EQ(ZX_OK, root_node.Bind(start_args.mutable_node()->TakeChannel(), loop().dispatcher()));
    fdf::NodeAddArgs args;
    args.set_name("second");
    root_node->AddChild(std::move(args), node_controller.NewRequest(loop().dispatcher()),
                        second_node.NewRequest(loop().dispatcher()));
  });
  ASSERT_TRUE(StartRootDriver("root", &driver_runner).is_ok());

  node_controller->Remove();
  loop().RunUntilIdle();
  ASSERT_FALSE(second_node.is_bound());

  Unbind();
}

// Start the root driver, and add a child node with duplicate symbols. The child
// node is unowned, so if we did not have duplicate symbols, the second driver
// would bind to it.
TEST_F(DriverRunnerTest, StartRootDriver_AddUnownedChild_DuplicateSymbols) {
  auto driver_index = CreateDriverIndex();
  DriverRunner driver_runner(ConnectToRealm(), &driver_index, loop().dispatcher());

  fdf::NodeControllerPtr node_controller;
  driver_host().SetStartHandler([this, &node_controller](fdf::DriverStartArgs start_args,
                                                         auto driver) {
    auto& entries = start_args.program().entries();
    EXPECT_EQ(2u, entries.size());
    EXPECT_EQ("binary", entries[0].key);
    EXPECT_EQ("driver/root-driver.so", entries[0].value->str());
    EXPECT_EQ("colocate", entries[1].key);
    EXPECT_EQ("false", entries[1].value->str());

    fdf::NodePtr root_node;
    EXPECT_EQ(ZX_OK, root_node.Bind(start_args.mutable_node()->TakeChannel(), loop().dispatcher()));
    fdf::NodeAddArgs args;
    args.set_name("second");
    args.mutable_symbols()->emplace_back(
        std::move(fdf::NodeSymbol().set_name("sym").set_address(0xfeed)));
    args.mutable_symbols()->emplace_back(
        std::move(fdf::NodeSymbol().set_name("sym").set_address(0xf00d)));
    root_node->AddChild(std::move(args), node_controller.NewRequest(loop().dispatcher()), {});
  });
  ASSERT_TRUE(StartRootDriver("root", &driver_runner).is_ok());

  loop().RunUntilIdle();
  ASSERT_FALSE(node_controller.is_bound());

  Unbind();
}

// Start an unknown driver.
TEST_F(DriverRunnerTest, StartRootDriver_UnknownDriver) {
  auto driver_index = CreateDriverIndex();
  DriverRunner driver_runner(ConnectToRealm(), &driver_index, loop().dispatcher());
  ASSERT_TRUE(StartRootDriver("unknown-driver", &driver_runner).is_error());
}

// Start the root driver, and then start a second driver in a new driver host.
TEST_F(DriverRunnerTest, StartSecondDriver_NewDriverHost) {
  auto driver_index = CreateDriverIndex();
  DriverRunner driver_runner(ConnectToRealm(), &driver_index, loop().dispatcher());

  driver_host().SetStartHandler([this](fdf::DriverStartArgs start_args, auto driver) {
    auto& entries = start_args.program().entries();
    EXPECT_EQ(2u, entries.size());
    EXPECT_EQ("binary", entries[0].key);
    EXPECT_EQ("driver/root-driver.so", entries[0].value->str());
    EXPECT_EQ("colocate", entries[1].key);
    EXPECT_EQ("false", entries[1].value->str());

    realm().SetCreateChildHandler([](fsys::CollectionRef collection, fsys::ChildDecl decl) {
      EXPECT_EQ("drivers", collection.name);
      EXPECT_EQ("driver-2", decl.name());
      EXPECT_EQ("fuchsia-boot:///#meta/second-driver.cm", decl.url());
    });
    realm().SetBindChildHandler([this](fsys::ChildRef child, auto exposed_dir) {
      EXPECT_EQ("drivers", child.collection);
      EXPECT_EQ("driver-2", child.name);
      EXPECT_EQ(ZX_OK, driver_dir_binding().Bind(std::move(exposed_dir), loop().dispatcher()));
    });

    fdf::NodePtr root_node;
    EXPECT_EQ(ZX_OK, root_node.Bind(start_args.mutable_node()->TakeChannel(), loop().dispatcher()));
    fdf::NodeAddArgs args;
    args.set_name("second");
    args.mutable_offers()->emplace_back("fuchsia.package.Protocol");
    args.mutable_symbols()->emplace_back(
        std::move(fdf::NodeSymbol().set_name("sym").set_address(0xfeed)));
    fdf::NodeControllerPtr node_controller;
    root_node->AddChild(std::move(args), node_controller.NewRequest(loop().dispatcher()), {});
  });
  ASSERT_TRUE(StartRootDriver("root", &driver_runner).is_ok());

  driver_host().SetStartHandler([](fdf::DriverStartArgs start_args, auto driver) {
    auto& offers = start_args.offers();
    EXPECT_EQ(1u, offers.size());
    EXPECT_EQ("fuchsia.package.Protocol", offers[0]);
    EXPECT_TRUE(start_args.symbols().empty());
    auto& entries = start_args.program().entries();
    EXPECT_EQ(2u, entries.size());
    EXPECT_EQ("binary", entries[0].key);
    EXPECT_EQ("driver/second-driver.so", entries[0].value->str());
    EXPECT_EQ("colocate", entries[1].key);
    EXPECT_EQ("false", entries[1].value->str());
    EXPECT_TRUE(start_args.exposed_dir().is_valid());
  });
  StartDriverHost("driver_hosts", "driver_host-3");
  StartDriver(&driver_runner, {
                                  .url = "fuchsia-boot:///#meta/second-driver.cm",
                                  .binary = "driver/second-driver.so",
                              });

  Unbind();
}

// Start the root driver, and then start a second driver in the same driver
// host.
TEST_F(DriverRunnerTest, StartSecondDriver_SameDriverHost) {
  auto driver_index = CreateDriverIndex();
  DriverRunner driver_runner(ConnectToRealm(), &driver_index, loop().dispatcher());

  driver_host().SetStartHandler([this](fdf::DriverStartArgs start_args, auto driver) {
    auto& entries = start_args.program().entries();
    EXPECT_EQ(2u, entries.size());
    EXPECT_EQ("binary", entries[0].key);
    EXPECT_EQ("driver/root-driver.so", entries[0].value->str());
    EXPECT_EQ("colocate", entries[1].key);
    EXPECT_EQ("false", entries[1].value->str());

    realm().SetCreateChildHandler([](fsys::CollectionRef collection, fsys::ChildDecl decl) {
      EXPECT_EQ("drivers", collection.name);
      EXPECT_EQ("driver-2", decl.name());
      EXPECT_EQ("fuchsia-boot:///#meta/second-driver.cm", decl.url());
    });
    realm().SetBindChildHandler([this](fsys::ChildRef child, auto exposed_dir) {
      EXPECT_EQ("drivers", child.collection);
      EXPECT_EQ("driver-2", child.name);
      EXPECT_EQ(ZX_OK, driver_dir_binding().Bind(std::move(exposed_dir), loop().dispatcher()));
    });

    fdf::NodePtr root_node;
    EXPECT_EQ(ZX_OK, root_node.Bind(start_args.mutable_node()->TakeChannel(), loop().dispatcher()));
    fdf::NodeAddArgs args;
    args.set_name("second");
    args.mutable_offers()->emplace_back("fuchsia.package.Protocol");
    args.mutable_symbols()->emplace_back(
        std::move(fdf::NodeSymbol().set_name("sym").set_address(0xfeed)));
    fdf::NodeControllerPtr node_controller;
    root_node->AddChild(std::move(args), node_controller.NewRequest(loop().dispatcher()), {});
  });
  ASSERT_TRUE(StartRootDriver("root", &driver_runner).is_ok());

  driver_host().SetStartHandler([](fdf::DriverStartArgs start_args, auto driver) {
    auto& offers = start_args.offers();
    EXPECT_EQ(1u, offers.size());
    EXPECT_EQ("fuchsia.package.Protocol", offers[0]);
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
    EXPECT_TRUE(start_args.exposed_dir().is_valid());
  });
  StartDriver(&driver_runner, {
                                  .url = "fuchsia-boot:///#meta/second-driver.cm",
                                  .binary = "driver/second-driver.so",
                                  .colocate = true,
                              });

  Unbind();
}

// Start the root driver, and then start a second driver that we match based on
// node properties.
TEST_F(DriverRunnerTest, StartSecondDriver_UseProperties) {
  DriverIndex driver_index([](auto args) -> zx::status<MatchResult> {
    if (args.has_name() && strncmp("root", args.name().data(), args.name().size()) == 0) {
      return zx::ok(MatchResult{
          .url = "fuchsia-boot:///#meta/root-driver.cm",
          .matched_args = std::move(args),
      });
    } else if (args.has_properties() && args.properties()[0].key() == 0x1985 &&
               args.properties()[0].value() == 0x2301) {
      return zx::ok(MatchResult{
          .url = "fuchsia-boot:///#meta/second-driver.cm",
          .matched_args = std::move(args),
      });
    } else {
      return zx::error(ZX_ERR_NOT_FOUND);
    }
  });
  DriverRunner driver_runner(ConnectToRealm(), &driver_index, loop().dispatcher());

  driver_host().SetStartHandler([this](fdf::DriverStartArgs start_args, auto driver) {
    auto& entries = start_args.program().entries();
    EXPECT_EQ(2u, entries.size());
    EXPECT_EQ("binary", entries[0].key);
    EXPECT_EQ("driver/root-driver.so", entries[0].value->str());
    EXPECT_EQ("colocate", entries[1].key);
    EXPECT_EQ("false", entries[1].value->str());

    realm().SetCreateChildHandler([](fsys::CollectionRef collection, fsys::ChildDecl decl) {
      EXPECT_EQ("drivers", collection.name);
      EXPECT_EQ("driver-2", decl.name());
      EXPECT_EQ("fuchsia-boot:///#meta/second-driver.cm", decl.url());
    });
    realm().SetBindChildHandler([this](fsys::ChildRef child, auto exposed_dir) {
      EXPECT_EQ("drivers", child.collection);
      EXPECT_EQ("driver-2", child.name);
      EXPECT_EQ(ZX_OK, driver_dir_binding().Bind(std::move(exposed_dir), loop().dispatcher()));
    });

    fdf::NodePtr root_node;
    EXPECT_EQ(ZX_OK, root_node.Bind(start_args.mutable_node()->TakeChannel(), loop().dispatcher()));
    fdf::NodeAddArgs args;
    args.mutable_properties()->emplace_back(
        std::move(fdf::NodeProperty().set_key(0x1985).set_value(0x2301)));
    fdf::NodeControllerPtr node_controller;
    root_node->AddChild(std::move(args), node_controller.NewRequest(loop().dispatcher()), {});
  });
  ASSERT_TRUE(StartRootDriver("root", &driver_runner).is_ok());

  driver_host().SetStartHandler([](fdf::DriverStartArgs start_args, auto driver) {
    auto& entries = start_args.program().entries();
    EXPECT_EQ(2u, entries.size());
    EXPECT_EQ("binary", entries[0].key);
    EXPECT_EQ("driver/second-driver.so", entries[0].value->str());
    EXPECT_EQ("colocate", entries[1].key);
    EXPECT_EQ("true", entries[1].value->str());
    EXPECT_TRUE(start_args.exposed_dir().is_valid());
  });
  StartDriver(&driver_runner, {
                                  .url = "fuchsia-boot:///#meta/second-driver.cm",
                                  .binary = "driver/second-driver.so",
                                  .colocate = true,
                              });

  Unbind();
}

// Start the root driver, and then add a child node that does not bind to a
// second driver.
TEST_F(DriverRunnerTest, StartSecondDriver_UnknownNode) {
  auto driver_index = CreateDriverIndex();
  DriverRunner driver_runner(ConnectToRealm(), &driver_index, loop().dispatcher());

  driver_host().SetStartHandler([this](fdf::DriverStartArgs start_args, auto driver) {
    auto& entries = start_args.program().entries();
    EXPECT_EQ(2u, entries.size());
    EXPECT_EQ("binary", entries[0].key);
    EXPECT_EQ("driver/root-driver.so", entries[0].value->str());
    EXPECT_EQ("colocate", entries[1].key);
    EXPECT_EQ("false", entries[1].value->str());

    fdf::NodePtr root_node;
    EXPECT_EQ(ZX_OK, root_node.Bind(start_args.mutable_node()->TakeChannel(), loop().dispatcher()));
    fdf::NodeAddArgs args;
    args.set_name("unknown-node");
    fdf::NodeControllerPtr node_controller;
    root_node->AddChild(std::move(args), node_controller.NewRequest(loop().dispatcher()), {});
  });
  ASSERT_TRUE(StartRootDriver("root", &driver_runner).is_ok());

  StartDriver(&driver_runner, {.close = true});

  Unbind();
}

// Start the second driver, and then unbind its associated node.
TEST_F(DriverRunnerTest, StartSecondDriver_UnbindSecondNode) {
  auto driver_index = CreateDriverIndex();
  DriverRunner driver_runner(ConnectToRealm(), &driver_index, loop().dispatcher());

  driver_host().SetStartHandler([this](fdf::DriverStartArgs start_args, auto driver) {
    auto& entries = start_args.program().entries();
    EXPECT_EQ(2u, entries.size());
    EXPECT_EQ("binary", entries[0].key);
    EXPECT_EQ("driver/root-driver.so", entries[0].value->str());
    EXPECT_EQ("colocate", entries[1].key);
    EXPECT_EQ("false", entries[1].value->str());

    realm().SetCreateChildHandler([](fsys::CollectionRef collection, fsys::ChildDecl decl) {
      EXPECT_EQ("drivers", collection.name);
      EXPECT_EQ("driver-2", decl.name());
      EXPECT_EQ("fuchsia-boot:///#meta/second-driver.cm", decl.url());
    });
    realm().SetBindChildHandler([this](fsys::ChildRef child, auto exposed_dir) {
      EXPECT_EQ("drivers", child.collection);
      EXPECT_EQ("driver-2", child.name);
      EXPECT_EQ(ZX_OK, driver_dir_binding().Bind(std::move(exposed_dir), loop().dispatcher()));
    });

    fdf::NodePtr root_node;
    EXPECT_EQ(ZX_OK, root_node.Bind(start_args.mutable_node()->TakeChannel(), loop().dispatcher()));
    fdf::NodeAddArgs args;
    args.set_name("second");
    fdf::NodeControllerPtr node_controller;
    root_node->AddChild(std::move(args), node_controller.NewRequest(loop().dispatcher()), {});
  });
  ASSERT_TRUE(StartRootDriver("root", &driver_runner).is_ok());

  fdf::NodePtr second_node;
  driver_host().SetStartHandler([this, &second_node](fdf::DriverStartArgs start_args, auto driver) {
    auto& entries = start_args.program().entries();
    EXPECT_EQ(2u, entries.size());
    EXPECT_EQ("binary", entries[0].key);
    EXPECT_EQ("driver/second-driver.so", entries[0].value->str());
    EXPECT_EQ("colocate", entries[1].key);
    EXPECT_EQ("false", entries[1].value->str());

    EXPECT_EQ(ZX_OK,
              second_node.Bind(start_args.mutable_node()->TakeChannel(), loop().dispatcher()));
  });
  ;
  StartDriverHost("driver_hosts", "driver_host-3");
  zx::channel second_driver_controller =
      StartDriver(&driver_runner, {
                                      .url = "fuchsia-boot:///#meta/second-driver.cm",
                                      .binary = "driver/second-driver.so",
                                  });

  // Unbinding the second node stops the driver bound to it.
  second_node.Unbind();
  loop().RunUntilIdle();
  zx_signals_t signals = 0;
  ASSERT_EQ(ZX_OK, second_driver_controller.wait_one(ZX_CHANNEL_PEER_CLOSED, zx::time::infinite(),
                                                     &signals));
  ASSERT_TRUE(signals & ZX_CHANNEL_PEER_CLOSED);

  Unbind();
}

// Start the second driver, and then unbind the root node.
TEST_F(DriverRunnerTest, StartSecondDriver_UnbindRootNode) {
  auto driver_index = CreateDriverIndex();
  DriverRunner driver_runner(ConnectToRealm(), &driver_index, loop().dispatcher());

  fdf::NodePtr root_node;
  driver_host().SetStartHandler([this, &root_node](fdf::DriverStartArgs start_args, auto driver) {
    auto& entries = start_args.program().entries();
    EXPECT_EQ(2u, entries.size());
    EXPECT_EQ("binary", entries[0].key);
    EXPECT_EQ("driver/root-driver.so", entries[0].value->str());
    EXPECT_EQ("colocate", entries[1].key);
    EXPECT_EQ("false", entries[1].value->str());

    realm().SetCreateChildHandler([](fsys::CollectionRef collection, fsys::ChildDecl decl) {
      EXPECT_EQ("drivers", collection.name);
      EXPECT_EQ("driver-2", decl.name());
      EXPECT_EQ("fuchsia-boot:///#meta/second-driver.cm", decl.url());
    });
    realm().SetBindChildHandler([this](fsys::ChildRef child, auto exposed_dir) {
      EXPECT_EQ("drivers", child.collection);
      EXPECT_EQ("driver-2", child.name);
      EXPECT_EQ(ZX_OK, driver_dir_binding().Bind(std::move(exposed_dir), loop().dispatcher()));
    });

    EXPECT_EQ(ZX_OK, root_node.Bind(start_args.mutable_node()->TakeChannel(), loop().dispatcher()));
    fdf::NodeAddArgs args;
    args.set_name("second");
    fdf::NodeControllerPtr node_controller;
    root_node->AddChild(std::move(args), node_controller.NewRequest(loop().dispatcher()), {});
  });
  auto root_driver = StartRootDriver("root", &driver_runner);
  ASSERT_TRUE(root_driver.is_ok());

  driver_host().SetStartHandler([](fdf::DriverStartArgs start_args, auto driver) {
    auto& entries = start_args.program().entries();
    EXPECT_EQ(2u, entries.size());
    EXPECT_EQ("binary", entries[0].key);
    EXPECT_EQ("driver/second-driver.so", entries[0].value->str());
    EXPECT_EQ("colocate", entries[1].key);
    EXPECT_EQ("false", entries[1].value->str());
  });
  ;
  StartDriverHost("driver_hosts", "driver_host-3");
  zx::channel second_driver_controller =
      StartDriver(&driver_runner, {
                                      .url = "fuchsia-boot:///#meta/second-driver.cm",
                                      .binary = "driver/second-driver.so",
                                  });

  // Unbinding the root node stops all drivers.
  root_node.Unbind();
  loop().RunUntilIdle();
  zx_signals_t signals = 0;
  ASSERT_EQ(ZX_OK, second_driver_controller.wait_one(ZX_CHANNEL_PEER_CLOSED, zx::time::infinite(),
                                                     &signals));
  ASSERT_TRUE(signals & ZX_CHANNEL_PEER_CLOSED);
  ASSERT_EQ(ZX_OK, root_driver->wait_one(ZX_CHANNEL_PEER_CLOSED, zx::time::infinite(), &signals));
  ASSERT_TRUE(signals & ZX_CHANNEL_PEER_CLOSED);

  Unbind();
}
