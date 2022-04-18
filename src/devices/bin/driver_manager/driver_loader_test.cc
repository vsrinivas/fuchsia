// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/devices/bin/driver_manager/driver_loader.h"

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>

#include <tuple>

#include <zxtest/zxtest.h>

#include "fbl/ref_ptr.h"
#include "src/devices/bin/driver_manager/v1/init_task.h"
#include "src/devices/bin/driver_manager/v1/resume_task.h"
#include "src/devices/bin/driver_manager/v1/suspend_task.h"
#include "src/devices/bin/driver_manager/v1/unbind_task.h"

namespace fdf = fuchsia_driver_framework;

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

class FakeDriverLoaderIndex final : public fidl::WireServer<fdf::DriverIndex> {
 public:
  void MatchDriver(MatchDriverRequestView request, MatchDriverCompleter::Sync& completer) override {
    completer.ReplyError(ZX_ERR_NOT_SUPPORTED);
  }

  void WaitForBaseDrivers(WaitForBaseDriversRequestView request,
                          WaitForBaseDriversCompleter::Sync& completer) override {
    completer.Reply();
  }

  void MatchDriversV1(MatchDriversV1RequestView request,
                      MatchDriversV1Completer::Sync& completer) override {
    fidl::Arena allocator;
    fidl::VectorView<fdf::wire::MatchedDriver> drivers(allocator,
                                                       driver_urls.size() + device_groups.size());
    size_t index = 0;
    for (auto& driver_url_pair : driver_urls) {
      auto driver_info = fdf::wire::MatchedDriverInfo(allocator);
      driver_info.set_driver_url(
          fidl::ObjectView<fidl::StringView>(allocator, allocator, driver_url_pair.first));

      driver_info.set_package_type(driver_url_pair.second);

      drivers[index] = fdf::wire::MatchedDriver::WithDriver(
          fidl::ObjectView<fdf::wire::MatchedDriverInfo>(allocator, driver_info));
      index++;
    }

    for (auto& device_group : device_groups) {
      drivers[index] = fdf::wire::MatchedDriver::WithDeviceGroup(
          fidl::ObjectView<fdf::wire::MatchedDeviceGroupInfo>(allocator, device_group));
      index++;
    }

    completer.ReplySuccess(drivers);
  }

  void AddDeviceGroup(AddDeviceGroupRequestView request,
                      AddDeviceGroupCompleter::Sync& completer) override {
    completer.ReplyError(ZX_ERR_NOT_SUPPORTED);
  }

  // Second item of the pair specifies the driver package type to set on the DriverInfo.
  std::vector<std::pair<std::string, fdf::wire::DriverPackageType>> driver_urls;
  std::vector<fdf::wire::MatchedDeviceGroupInfo> device_groups;
};

class DriverLoaderTest : public zxtest::Test {
 public:
  void SetUp() override {
    auto endpoints = fidl::CreateEndpoints<fdf::DriverIndex>();
    ASSERT_FALSE(endpoints.is_error());
    fidl::BindServer(loop.dispatcher(), std::move(endpoints->server), &driver_index_server);
    driver_index =
        fidl::WireSharedClient<fdf::DriverIndex>(std::move(endpoints->client), loop.dispatcher());
  }

  void TearDown() override {}

  async::Loop loop = async::Loop(&kAsyncLoopConfigNeverAttachToThread);
  FakeDriverLoaderIndex driver_index_server;
  FakeResolver resolver;
  FakeResolver universe_resolver;
  fidl::WireSharedClient<fdf::DriverIndex> driver_index;
};

TEST_F(DriverLoaderTest, TestFallbackGetsRemoved) {
  std::string not_fallback_libname = "fuchsia_boot:///#not_fallback.so";
  std::string fallback_libname = "fuchsia_boot:///#fallback.so";

  driver_index_server.driver_urls.emplace_back(not_fallback_libname,
                                               fdf::wire::DriverPackageType::kBoot);
  driver_index_server.driver_urls.emplace_back(fallback_libname,
                                               fdf::wire::DriverPackageType::kBoot);

  auto not_fallback = std::make_unique<Driver>();
  not_fallback->libname = not_fallback_libname;
  resolver.map[not_fallback_libname] = std::move(not_fallback);

  auto fallback = std::make_unique<Driver>();
  fallback->libname = fallback_libname;
  fallback->fallback = true;
  resolver.map[fallback_libname] = std::move(fallback);

  DriverLoader driver_loader(nullptr, std::move(driver_index), &resolver, loop.dispatcher(), true,
                             nullptr);

  loop.StartThread("fidl-thread");

  DriverLoader::MatchDeviceConfig config;
  fidl::VectorView<fdf::wire::NodeProperty> props{};
  auto drivers = driver_loader.MatchPropertiesDriverIndex(props, config);
  ASSERT_EQ(drivers.size(), 1);
  ASSERT_EQ(std::get<MatchedDriverInfo>(drivers[0]).driver->libname, not_fallback_libname);
}

TEST_F(DriverLoaderTest, TestFallbackAcceptedAfterBaseLoaded) {
  std::string not_fallback_libname = "fuchsia_boot:///#not_fallback.so";
  std::string fallback_libname = "fuchsia_boot:///#fallback.so";

  driver_index_server.driver_urls.emplace_back(not_fallback_libname,
                                               fdf::wire::DriverPackageType::kBoot);
  driver_index_server.driver_urls.emplace_back(fallback_libname,
                                               fdf::wire::DriverPackageType::kBoot);

  auto not_fallback = std::make_unique<Driver>();
  not_fallback->libname = not_fallback_libname;
  resolver.map[not_fallback_libname] = std::move(not_fallback);

  auto fallback = std::make_unique<Driver>();
  fallback->libname = fallback_libname;
  fallback->fallback = true;
  resolver.map[fallback_libname] = std::move(fallback);

  DriverLoader driver_loader(nullptr, std::move(driver_index), &resolver, loop.dispatcher(), true,
                             nullptr);
  loop.StartThread("fidl-thread");

  // Wait for base drivers, which is when we load fallback drivers.
  sync_completion_t base_drivers;
  driver_loader.WaitForBaseDrivers([&base_drivers]() { sync_completion_signal(&base_drivers); });
  sync_completion_wait(&base_drivers, ZX_TIME_INFINITE);

  DriverLoader::MatchDeviceConfig config;
  fidl::VectorView<fdf::wire::NodeProperty> props{};
  auto drivers = driver_loader.MatchPropertiesDriverIndex(props, config);

  ASSERT_EQ(drivers.size(), 2);
  // The non-fallback should always be first.
  ASSERT_EQ(std::get<MatchedDriverInfo>(drivers[0]).driver->libname, not_fallback_libname);
  ASSERT_EQ(std::get<MatchedDriverInfo>(drivers[1]).driver->libname, fallback_libname);
}

TEST_F(DriverLoaderTest, TestFallbackAcceptedWhenSystemNotRequired) {
  std::string not_fallback_libname = "fuchsia_boot:///#not_fallback.so";
  std::string fallback_libname = "fuchsia_boot:///#fallback.so";

  driver_index_server.driver_urls.emplace_back(not_fallback_libname,
                                               fdf::wire::DriverPackageType::kBoot);
  driver_index_server.driver_urls.emplace_back(fallback_libname,
                                               fdf::wire::DriverPackageType::kBoot);

  auto not_fallback = std::make_unique<Driver>();
  not_fallback->libname = not_fallback_libname;
  resolver.map[not_fallback_libname] = std::move(not_fallback);

  auto fallback = std::make_unique<Driver>();
  fallback->libname = fallback_libname;
  fallback->fallback = true;
  resolver.map[fallback_libname] = std::move(fallback);

  DriverLoader driver_loader(nullptr, std::move(driver_index), &resolver, loop.dispatcher(), false,
                             nullptr);
  loop.StartThread("fidl-thread");

  DriverLoader::MatchDeviceConfig config;
  fidl::VectorView<fdf::wire::NodeProperty> props{};
  auto drivers = driver_loader.MatchPropertiesDriverIndex(props, config);

  ASSERT_EQ(drivers.size(), 2);
  // The non-fallback should always be first.
  ASSERT_EQ(std::get<MatchedDriverInfo>(drivers[0]).driver->libname, not_fallback_libname);
  ASSERT_EQ(std::get<MatchedDriverInfo>(drivers[1]).driver->libname, fallback_libname);
}

TEST_F(DriverLoaderTest, TestLibname) {
  std::string name1 = "fuchsia_boot:///#driver1.so";
  std::string name2 = "fuchsia_boot:///#driver2.so";

  driver_index_server.driver_urls.emplace_back(name1, fdf::wire::DriverPackageType::kBoot);
  driver_index_server.driver_urls.emplace_back(name2, fdf::wire::DriverPackageType::kBoot);

  auto driver1 = std::make_unique<Driver>();
  driver1->libname = name1;
  resolver.map[name1] = std::move(driver1);

  auto driver2 = std::make_unique<Driver>();
  driver2->libname = name2;
  resolver.map[name2] = std::move(driver2);

  DriverLoader driver_loader(nullptr, std::move(driver_index), &resolver, loop.dispatcher(), true,
                             nullptr);
  loop.StartThread("fidl-thread");

  DriverLoader::MatchDeviceConfig config;
  config.libname = name2;
  fidl::VectorView<fdf::wire::NodeProperty> props{};
  auto drivers = driver_loader.MatchPropertiesDriverIndex(props, config);

  ASSERT_EQ(drivers.size(), 1);
  ASSERT_EQ(std::get<MatchedDriverInfo>(drivers[0]).driver->libname, name2);
}

TEST_F(DriverLoaderTest, TestLibnameConvertToPath) {
  std::string name1 = "fuchsia-pkg://fuchsia.com/my-package#driver/#driver1.so";
  std::string name2 = "fuchsia-boot:///#driver/driver2.so";

  driver_index_server.driver_urls.emplace_back(name1, fdf::wire::DriverPackageType::kBase);
  driver_index_server.driver_urls.emplace_back(name2, fdf::wire::DriverPackageType::kBoot);

  auto driver1 = std::make_unique<Driver>();
  driver1->libname = name1;
  resolver.map[name1] = std::move(driver1);

  auto driver2 = std::make_unique<Driver>();
  driver2->libname = name2;
  resolver.map[name2] = std::move(driver2);

  DriverLoader driver_loader(nullptr, std::move(driver_index), &resolver, loop.dispatcher(), true,
                             nullptr);
  loop.StartThread("fidl-thread");

  // We can also match libname by the path that the URL turns into.
  DriverLoader::MatchDeviceConfig config;
  config.libname = "/boot/driver/driver2.so";
  fidl::VectorView<fdf::wire::NodeProperty> props{};
  auto drivers = driver_loader.MatchPropertiesDriverIndex(props, config);

  ASSERT_EQ(drivers.size(), 1);
  ASSERT_EQ(std::get<MatchedDriverInfo>(drivers[0]).driver->libname, name2);
}

TEST_F(DriverLoaderTest, TestOnlyReturnBaseAndFallback) {
  std::string name1 = "fuchsia-pkg://fuchsia.com/my-package#driver/#driver1.so";
  std::string name2 = "fuchsia-boot:///#driver/driver2.so";
  std::string name3 = "fuchsia-boot:///#driver/driver3.so";

  driver_index_server.driver_urls.emplace_back(name1, fdf::wire::DriverPackageType::kBase);
  driver_index_server.driver_urls.emplace_back(name2, fdf::wire::DriverPackageType::kBoot);
  driver_index_server.driver_urls.emplace_back(name3, fdf::wire::DriverPackageType::kBoot);

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

  DriverLoader driver_loader(nullptr, std::move(driver_index), &resolver, loop.dispatcher(), false,
                             nullptr);
  loop.StartThread("fidl-thread");

  // We can also match libname by the path that the URL turns into.
  DriverLoader::MatchDeviceConfig config;
  config.only_return_base_and_fallback_drivers = true;
  fidl::VectorView<fdf::wire::NodeProperty> props{};
  auto drivers = driver_loader.MatchPropertiesDriverIndex(props, config);

  ASSERT_EQ(drivers.size(), 2);
  ASSERT_EQ(std::get<MatchedDriverInfo>(drivers[0]).driver->libname, name1);
  ASSERT_EQ(std::get<MatchedDriverInfo>(drivers[1]).driver->libname, name3);
}

TEST_F(DriverLoaderTest, TestReturnOnlyDeviceGroups) {
  fidl::Arena allocator;

  // Add first device group.
  fidl::VectorView<fidl::StringView> node_names_1(allocator, 2);
  node_names_1[0] = fidl::StringView(allocator, "whimbrel");
  node_names_1[1] = fidl::StringView(allocator, "curlew");

  auto device_group_1 = fdf::wire::MatchedDeviceGroupInfo(allocator);
  device_group_1.set_topological_path(
      fidl::ObjectView<fidl::StringView>(allocator, allocator, "test/path/device_group_1"));
  device_group_1.set_composite_name(
      fidl::ObjectView<fidl::StringView>(allocator, allocator, "device_group_1"));
  device_group_1.set_node_names(
      fidl::ObjectView<fidl::VectorView<fidl::StringView>>(allocator, node_names_1));
  device_group_1.set_num_nodes(2);
  device_group_1.set_node_index(1);
  driver_index_server.device_groups.push_back(device_group_1);

  // Add second device group.
  fidl::VectorView<fidl::StringView> node_names_2(allocator, 1);
  node_names_2[0] = fidl::StringView(allocator, "sanderling");

  auto device_group_2 = fdf::wire::MatchedDeviceGroupInfo(allocator);
  device_group_2.set_topological_path(
      fidl::ObjectView<fidl::StringView>(allocator, allocator, "test/path/device_group_2"));
  device_group_2.set_composite_name(
      fidl::ObjectView<fidl::StringView>(allocator, allocator, "device_group_2"));
  device_group_2.set_node_names(
      fidl::ObjectView<fidl::VectorView<fidl::StringView>>(allocator, node_names_1));
  device_group_2.set_num_nodes(1);
  device_group_2.set_node_index(0);
  driver_index_server.device_groups.push_back(device_group_2);

  DriverLoader driver_loader(nullptr, std::move(driver_index), &resolver, loop.dispatcher(), false,
                             nullptr);
  loop.StartThread("fidl-thread");

  DriverLoader::MatchDeviceConfig config;
  fidl::VectorView<fdf::wire::NodeProperty> props{};
  auto drivers = driver_loader.MatchPropertiesDriverIndex(props, config);

  ASSERT_EQ(drivers.size(), 2);

  auto device_group_result_1 = std::get<MatchedDeviceGroupInfo>(drivers[0]);
  ASSERT_STREQ("test/path/device_group_1", device_group_result_1.topological_path);
  ASSERT_EQ(1, device_group_result_1.composite.node);
  ASSERT_EQ(2, device_group_result_1.composite.num_nodes);
  ASSERT_STREQ("device_group_1", device_group_result_1.composite.name);

  auto device_group_result_2 = std::get<MatchedDeviceGroupInfo>(drivers[1]);
  ASSERT_STREQ("test/path/device_group_2", device_group_result_2.topological_path);
  ASSERT_EQ(0, device_group_result_2.composite.node);
  ASSERT_EQ(1, device_group_result_2.composite.num_nodes);
  ASSERT_STREQ("device_group_2", device_group_result_2.composite.name);
}

TEST_F(DriverLoaderTest, TestReturnDriversAndDeviceGroups) {
  fidl::Arena allocator;

  fidl::VectorView<fidl::StringView> node_names(allocator, 2);
  node_names[0] = fidl::StringView(allocator, "whimbrel");
  node_names[1] = fidl::StringView(allocator, "curlew");

  auto device_group = fdf::wire::MatchedDeviceGroupInfo(allocator);
  device_group.set_topological_path(
      fidl::ObjectView<fidl::StringView>(allocator, allocator, "test/path/device_group"));
  device_group.set_composite_name(
      fidl::ObjectView<fidl::StringView>(allocator, allocator, "device_group"));
  device_group.set_node_names(
      fidl::ObjectView<fidl::VectorView<fidl::StringView>>(allocator, node_names));
  device_group.set_num_nodes(2);
  device_group.set_node_index(1);

  auto driver_name = "fuchsia_boot:///#driver.so";
  driver_index_server.device_groups.push_back(device_group);
  driver_index_server.driver_urls.emplace_back(driver_name, fdf::wire::DriverPackageType::kBoot);

  auto driver = std::make_unique<Driver>();
  driver->libname = driver_name;
  resolver.map[driver_name] = std::move(driver);

  DriverLoader driver_loader(nullptr, std::move(driver_index), &resolver, loop.dispatcher(), false,
                             nullptr);
  loop.StartThread("fidl-thread");

  DriverLoader::MatchDeviceConfig config;
  fidl::VectorView<fdf::wire::NodeProperty> props{};
  auto drivers = driver_loader.MatchPropertiesDriverIndex(props, config);

  ASSERT_EQ(drivers.size(), 2);

  // Check driver.
  ASSERT_EQ(driver_name, std::get<MatchedDriverInfo>(drivers[0]).driver->libname);

  // Check device group.
  auto device_group_result = std::get<MatchedDeviceGroupInfo>(drivers[1]);
  ASSERT_STREQ("test/path/device_group", device_group_result.topological_path);
  ASSERT_EQ(1, device_group_result.composite.node);
  ASSERT_EQ(2, device_group_result.composite.num_nodes);
  ASSERT_STREQ("device_group", device_group_result.composite.name);
}

TEST_F(DriverLoaderTest, TestReturnDeviceGroupNoTopologicalPath) {
  fidl::Arena allocator;

  fidl::VectorView<fidl::StringView> node_names(allocator, 2);
  node_names[0] = fidl::StringView(allocator, "whimbrel");

  auto device_group = fdf::wire::MatchedDeviceGroupInfo(allocator);
  device_group.set_composite_name(
      fidl::ObjectView<fidl::StringView>(allocator, allocator, "device_group"));
  device_group.set_num_nodes(1);
  device_group.set_node_index(0);
  driver_index_server.device_groups.push_back(device_group);

  DriverLoader driver_loader(nullptr, std::move(driver_index), &resolver, loop.dispatcher(), false,
                             nullptr);
  loop.StartThread("fidl-thread");

  DriverLoader::MatchDeviceConfig config;
  fidl::VectorView<fdf::wire::NodeProperty> props{};
  auto drivers = driver_loader.MatchPropertiesDriverIndex(props, config);
  ASSERT_EQ(drivers.size(), 0);
}

TEST_F(DriverLoaderTest, TestEphemeralDriver) {
  std::string name1 = "fuchsia-pkg://fuchsia.com/my-package#driver/#driver1.so";
  std::string name2 = "fuchsia-boot:///#driver/driver2.so";

  driver_index_server.driver_urls.emplace_back(name1, fdf::wire::DriverPackageType::kUniverse);
  driver_index_server.driver_urls.emplace_back(name2, fdf::wire::DriverPackageType::kBoot);

  // Add driver 1 to universe resolver since it is a universe driver.
  auto driver1 = std::make_unique<Driver>();
  driver1->libname = name1;
  universe_resolver.map[name1] = std::move(driver1);

  // Add driver 2 to the regular resolver.
  auto driver2 = std::make_unique<Driver>();
  driver2->libname = name2;
  resolver.map[name2] = std::move(driver2);

  DriverLoader driver_loader(nullptr, std::move(driver_index), &resolver, loop.dispatcher(), true,
                             &universe_resolver);
  loop.StartThread("fidl-thread");

  // We should find driver1 from the universe resolver.
  DriverLoader::MatchDeviceConfig config;
  config.libname = name1;
  fidl::VectorView<fdf::wire::NodeProperty> props{};
  auto drivers = driver_loader.MatchPropertiesDriverIndex(props, config);

  ASSERT_EQ(drivers.size(), 1);
  ASSERT_EQ(std::get<MatchedDriverInfo>(drivers[0]).driver->libname, name1);
}
