// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/devices/bin/driver_manager/driver_loader.h"

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>

#include <zxtest/zxtest.h>

#include "fbl/ref_ptr.h"
#include "src/devices/bin/driver_manager/v1/init_task.h"
#include "src/devices/bin/driver_manager/v1/resume_task.h"
#include "src/devices/bin/driver_manager/v1/suspend_task.h"
#include "src/devices/bin/driver_manager/v1/unbind_task.h"

class FakeResolver : public internal::PackageResolverInterface {
 public:
  zx::status<std::unique_ptr<Driver>> FetchDriver(const std::string& package_url) override {
    if (map.count(package_url) != 0) {
      auto driver = std::move(map[package_url]);
      map.erase(package_url);
      return zx::ok(std::move(driver));
    }
    return zx::error(ZX_ERR_NOT_FOUND);
  }

  std::map<std::string, std::unique_ptr<Driver>> map;
};

class FakeDriverLoaderIndex final : public fidl::WireServer<fuchsia_driver_framework::DriverIndex> {
 public:
  void MatchDriver(MatchDriverRequestView request, MatchDriverCompleter::Sync& completer) override {
    completer.ReplyError(ZX_ERR_NOT_SUPPORTED);
  }

  void WaitForBaseDrivers(WaitForBaseDriversRequestView request,
                          WaitForBaseDriversCompleter::Sync& completer) override {
    completer.Reply();
  }

  // The fake driver index is used only for drivers-as-components and so it
  // doesn't need any V1 APIs.
  void MatchDriversV1(MatchDriversV1RequestView request,
                      MatchDriversV1Completer::Sync& completer) override {
    fidl::Arena allocator;
    fidl::VectorView<fuchsia_driver_framework::wire::MatchedDriver> drivers(allocator,
                                                                            driver_urls.size());
    size_t index = 0;
    for (auto& driver : driver_urls) {
      drivers[index] = fuchsia_driver_framework::wire::MatchedDriver(allocator);
      drivers[index].set_driver_url(allocator, fidl::StringView::FromExternal(driver));
      index++;
    }
    completer.ReplySuccess(drivers);
  }

  std::vector<std::string> driver_urls;
};

class DriverLoaderTest : public zxtest::Test {
 public:
  void SetUp() override {
    auto endpoints = fidl::CreateEndpoints<fuchsia_driver_framework::DriverIndex>();
    ASSERT_FALSE(endpoints.is_error());
    fidl::BindServer(loop.dispatcher(), std::move(endpoints->server), &driver_index_server);
    driver_index = fidl::WireSharedClient<fuchsia_driver_framework::DriverIndex>(
        std::move(endpoints->client), loop.dispatcher());
  }

  void TearDown() override {}

  async::Loop loop = async::Loop(&kAsyncLoopConfigNeverAttachToThread);
  FakeDriverLoaderIndex driver_index_server;
  FakeResolver resolver;
  fidl::WireSharedClient<fuchsia_driver_framework::DriverIndex> driver_index;
};

TEST_F(DriverLoaderTest, TestFallbackGetsRemoved) {
  std::string not_fallback_libname = "fuchsia_boot:///#not_fallback.so";
  std::string fallback_libname = "fuchsia_boot:///#fallback.so";

  driver_index_server.driver_urls.push_back(not_fallback_libname);
  driver_index_server.driver_urls.push_back(fallback_libname);

  auto not_fallback = std::make_unique<Driver>();
  not_fallback->libname = not_fallback_libname;
  resolver.map[not_fallback_libname] = std::move(not_fallback);

  auto fallback = std::make_unique<Driver>();
  fallback->libname = fallback_libname;
  fallback->fallback = true;
  resolver.map[fallback_libname] = std::move(fallback);

  DriverLoader driver_loader(nullptr, std::move(driver_index), &resolver, loop.dispatcher(), true);

  loop.StartThread("fidl-thread");

  DriverLoader::MatchDeviceConfig config;
  fidl::VectorView<fuchsia_driver_framework::wire::NodeProperty> props{};
  auto drivers = driver_loader.MatchPropertiesDriverIndex(std::move(props), config);
  ASSERT_EQ(drivers.size(), 1);
  ASSERT_EQ(drivers[0]->libname, not_fallback_libname);
}

TEST_F(DriverLoaderTest, TestFallbackAcceptedAfterBaseLoaded) {
  std::string not_fallback_libname = "fuchsia_boot:///#not_fallback.so";
  std::string fallback_libname = "fuchsia_boot:///#fallback.so";

  driver_index_server.driver_urls.push_back(not_fallback_libname);
  driver_index_server.driver_urls.push_back(fallback_libname);

  auto not_fallback = std::make_unique<Driver>();
  not_fallback->libname = not_fallback_libname;
  resolver.map[not_fallback_libname] = std::move(not_fallback);

  auto fallback = std::make_unique<Driver>();
  fallback->libname = fallback_libname;
  fallback->fallback = true;
  resolver.map[fallback_libname] = std::move(fallback);

  DriverLoader driver_loader(nullptr, std::move(driver_index), &resolver, loop.dispatcher(), true);
  loop.StartThread("fidl-thread");

  // Wait for base drivers, which is when we load fallback drivers.
  sync_completion_t base_drivers;
  driver_loader.WaitForBaseDrivers([&base_drivers]() { sync_completion_signal(&base_drivers); });
  sync_completion_wait(&base_drivers, ZX_TIME_INFINITE);

  DriverLoader::MatchDeviceConfig config;
  fidl::VectorView<fuchsia_driver_framework::wire::NodeProperty> props{};
  auto drivers = driver_loader.MatchPropertiesDriverIndex(std::move(props), config);

  ASSERT_EQ(drivers.size(), 2);
  // The non-fallback should always be first.
  ASSERT_EQ(drivers[0]->libname, not_fallback_libname);
  ASSERT_EQ(drivers[1]->libname, fallback_libname);
}

TEST_F(DriverLoaderTest, TestFallbackAcceptedWhenSystemNotRequired) {
  std::string not_fallback_libname = "fuchsia_boot:///#not_fallback.so";
  std::string fallback_libname = "fuchsia_boot:///#fallback.so";

  driver_index_server.driver_urls.push_back(not_fallback_libname);
  driver_index_server.driver_urls.push_back(fallback_libname);

  auto not_fallback = std::make_unique<Driver>();
  not_fallback->libname = not_fallback_libname;
  resolver.map[not_fallback_libname] = std::move(not_fallback);

  auto fallback = std::make_unique<Driver>();
  fallback->libname = fallback_libname;
  fallback->fallback = true;
  resolver.map[fallback_libname] = std::move(fallback);

  DriverLoader driver_loader(nullptr, std::move(driver_index), &resolver, loop.dispatcher(), false);
  loop.StartThread("fidl-thread");

  DriverLoader::MatchDeviceConfig config;
  fidl::VectorView<fuchsia_driver_framework::wire::NodeProperty> props{};
  auto drivers = driver_loader.MatchPropertiesDriverIndex(std::move(props), config);

  ASSERT_EQ(drivers.size(), 2);
  // The non-fallback should always be first.
  ASSERT_EQ(drivers[0]->libname, not_fallback_libname);
  ASSERT_EQ(drivers[1]->libname, fallback_libname);
}

TEST_F(DriverLoaderTest, TestLibname) {
  std::string name1 = "fuchsia_boot:///#driver1.so";
  std::string name2 = "fuchsia_boot:///#driver2.so";

  driver_index_server.driver_urls.push_back(name1);
  driver_index_server.driver_urls.push_back(name2);

  auto driver1 = std::make_unique<Driver>();
  driver1->libname = name1;
  resolver.map[name1] = std::move(driver1);

  auto driver2 = std::make_unique<Driver>();
  driver2->libname = name2;
  resolver.map[name2] = std::move(driver2);

  DriverLoader driver_loader(nullptr, std::move(driver_index), &resolver, loop.dispatcher(), true);
  loop.StartThread("fidl-thread");

  DriverLoader::MatchDeviceConfig config;
  config.libname = name2;
  fidl::VectorView<fuchsia_driver_framework::wire::NodeProperty> props{};
  auto drivers = driver_loader.MatchPropertiesDriverIndex(std::move(props), config);

  ASSERT_EQ(drivers.size(), 1);
  ASSERT_EQ(drivers[0]->libname, name2);
}

TEST_F(DriverLoaderTest, TestLibnameConvertToPath) {
  std::string name1 = "fuchsia-pkg://fuchsia.com/my-package#driver/#driver1.so";
  std::string name2 = "fuchsia-boot:///#driver/driver2.so";

  driver_index_server.driver_urls.push_back(name1);
  driver_index_server.driver_urls.push_back(name2);

  auto driver1 = std::make_unique<Driver>();
  driver1->libname = name1;
  resolver.map[name1] = std::move(driver1);

  auto driver2 = std::make_unique<Driver>();
  driver2->libname = name2;
  resolver.map[name2] = std::move(driver2);

  DriverLoader driver_loader(nullptr, std::move(driver_index), &resolver, loop.dispatcher(), true);
  loop.StartThread("fidl-thread");

  // We can also match libname by the path that the URL turns into.
  DriverLoader::MatchDeviceConfig config;
  config.libname = "/boot/driver/driver2.so";
  fidl::VectorView<fuchsia_driver_framework::wire::NodeProperty> props{};
  auto drivers = driver_loader.MatchPropertiesDriverIndex(std::move(props), config);

  ASSERT_EQ(drivers.size(), 1);
  ASSERT_EQ(drivers[0]->libname, name2);
}

TEST_F(DriverLoaderTest, TestOnlyReturnBaseAndFallback) {
  std::string name1 = "fuchsia-pkg://fuchsia.com/my-package#driver/#driver1.so";
  std::string name2 = "fuchsia-boot:///#driver/driver2.so";
  std::string name3 = "fuchsia-boot:///#driver/driver3.so";

  driver_index_server.driver_urls.push_back(name1);
  driver_index_server.driver_urls.push_back(name2);
  driver_index_server.driver_urls.push_back(name3);

  auto driver1 = std::make_unique<Driver>();
  driver1->libname = name1;
  resolver.map[name1] = std::move(driver1);

  auto driver2 = std::make_unique<Driver>();
  driver2->libname = name2;
  resolver.map[name2] = std::move(driver2);

  auto driver3 = std::make_unique<Driver>();
  driver3->libname = name3;
  driver3->fallback = true;
  resolver.map[name3] = std::move(driver3);

  DriverLoader driver_loader(nullptr, std::move(driver_index), &resolver, loop.dispatcher(), false);
  loop.StartThread("fidl-thread");

  // We can also match libname by the path that the URL turns into.
  DriverLoader::MatchDeviceConfig config;
  config.only_return_base_and_fallback_drivers = true;
  fidl::VectorView<fuchsia_driver_framework::wire::NodeProperty> props{};
  auto drivers = driver_loader.MatchPropertiesDriverIndex(std::move(props), config);

  ASSERT_EQ(drivers.size(), 2);
  ASSERT_EQ(drivers[0]->libname, name1);
  ASSERT_EQ(drivers[1]->libname, name3);
}
