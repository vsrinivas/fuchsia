// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "harvester.h"

#include <fcntl.h>
#include <fuchsia/sysinfo/c/fidl.h>
#include <lib/async-testutils/dispatcher_stub.h>
#include <lib/fdio/fdio.h>
#include <lib/zx/channel.h>
#include <zircon/process.h>
#include <zircon/status.h>

#include "dockyard_proxy_fake.h"
#include "gtest/gtest.h"

namespace {

zx_status_t get_root_resource(zx_handle_t* root_resource) {
  const char* sysinfo = "/dev/misc/sysinfo";
  int fd = open(sysinfo, O_RDWR);
  if (fd < 0) {
    return ZX_ERR_NOT_FOUND;
  }

  zx::channel channel;
  zx_status_t status =
      fdio_get_service_handle(fd, channel.reset_and_get_address());
  if (status != ZX_OK) {
    return status;
  }

  zx_status_t fidl_status = fuchsia_sysinfo_DeviceGetRootResource(
      channel.get(), &status, root_resource);
  if (fidl_status != ZX_OK) {
    return fidl_status;
  } else if (status != ZX_OK) {
    return status;
  }
  return ZX_OK;
}

class AsyncDispatcherFake : public async::DispatcherStub {
 public:
  zx::time Now() override { return current_time_; }
  void SetTime(zx::time t) { current_time_ = t; }

 private:
  zx::time current_time_;
};

}  // namespace

class SystemMonitorHarvesterTest : public ::testing::Test {
 public:
  void SetUp() {
    // Determine our KOID.
    zx_info_handle_basic_t info;
    zx_status_t status = zx_object_get_info(
        zx_process_self(), ZX_INFO_HANDLE_BASIC, &info, sizeof(info),
        /*actual=*/nullptr, /*available=*/nullptr);
    ASSERT_EQ(status, ZX_OK);
    self_koid_ = std::to_string(info.koid);

    // Create a test harvester.
    std::unique_ptr<harvester::DockyardProxyFake> dockyard_proxy =
        std::make_unique<harvester::DockyardProxyFake>();

    zx_handle_t root_resource;
    zx_status_t ret = get_root_resource(&root_resource);
    EXPECT_EQ(ret, ZX_OK);

    test_harvester = std::make_unique<harvester::Harvester>(
        zx::msec(1), root_resource, &dispatcher, std::move(dockyard_proxy));
  }

  bool CheckString(const std::string& path, std::string* value) {
    return static_cast<harvester::DockyardProxyFake*>(
               test_harvester->dockyard_proxy_.get())
        ->CheckStringSent(path, value);
  }

  bool CheckStringPrefix(const std::string& path, std::string* value) {
    return static_cast<harvester::DockyardProxyFake*>(
               test_harvester->dockyard_proxy_.get())
        ->CheckStringPrefixSent(path, value);
  }

  bool CheckValue(const std::string& path, dockyard::SampleValue* value) {
    return static_cast<harvester::DockyardProxyFake*>(
               test_harvester->dockyard_proxy_.get())
        ->CheckValueSent(path, value);
  }

  // Dump out the state of the fake dockyard proxy.
  void DebugDump() {
    std::cout << "DebugDump:" << std::endl;
    std::cout << "  self_koid_: " << self_koid_ << std::endl;
    std::cout << *static_cast<harvester::DockyardProxyFake*>(
        test_harvester->dockyard_proxy_.get());
    std::cout << std::endl << std::endl << std::flush;
  }

  // Get a dockyard path for our koid with the given |suffix| key.
  std::string KoidPath(const std::string& suffix) {
    std::ostringstream out;
    out << "koid:" << self_koid_ << ":" << suffix;
    return out.str();
  }

  std::unique_ptr<harvester::Harvester> test_harvester;

  AsyncDispatcherFake dispatcher;

 private:
  std::string self_koid_;
};

TEST_F(SystemMonitorHarvesterTest, GatherData) {
  // Perform a data gathering pass. This will send samples to the
  // dockyard_proxy.
  test_harvester->GatherData();

  // Check the results.
  dockyard::SampleValue test_value;
  EXPECT_TRUE(CheckValue("cpu:0:busy_time", &test_value));
  EXPECT_TRUE(CheckValue("memory:device_free_bytes", &test_value));

  std::string test_string;
  EXPECT_TRUE(CheckString(KoidPath("name"), &test_string));
  // This is the name of our generated test process. If the testing harness
  // changes this may need to be updated. The intent is to test for a process
  // that is running.
  EXPECT_EQ("system_monitor_harvester_test.c", test_string);
}

TEST_F(SystemMonitorHarvesterTest, Inspectable) {
  test_harvester->GatherInspectableComponents();
  std::string test_string;
  EXPECT_TRUE(CheckStringPrefix(
      "inspectable:/hub/c/system_monitor_harvester_test.cmx/", &test_string));
  EXPECT_EQ("fuchsia.inspect.Inspect", test_string);
}
