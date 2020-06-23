// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "inspect.h"

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/async/cpp/executor.h>
#include <lib/inspect/cpp/reader.h>

#include <fs/dir_test_util.h>
#include <zxtest/zxtest.h>

#include "driver_host.h"
#include "src/lib/testing/loop_fixture/real_loop.h"

class InspectTestHelper : public loop_fixture::RealLoop {
 public:
  InspectTestHelper() : executor_(dispatcher()) {}
  // Run a promise to completion on the default async executor.
  void RunPromiseToCompletion(fit::promise<> promise) {
    bool done = false;
    executor_.schedule_task(std::move(promise).and_then([&]() { done = true; }));
    RunLoopUntil([&] { return done; });
    ASSERT_TRUE(done);
  }

  void ReadInspect(inspect::Inspector inspector) {
    hierarchy_ = fit::result<inspect::Hierarchy>();
    RunPromiseToCompletion(inspect::ReadFromInspector(inspector).then(
        [&](fit::result<inspect::Hierarchy>& result) { hierarchy_ = std::move(result); }));
    ASSERT_TRUE(hierarchy_.is_ok());
  }

  inspect::Hierarchy& hierarchy() { return hierarchy_.value(); }

  template <typename T>
  void CheckProperty(const inspect::NodeValue& node, std::string property, T expected_value) {
    const T* actual_value = node.get_property<T>(property);
    ASSERT_TRUE(actual_value);
    EXPECT_EQ(expected_value.value(), actual_value->value());
  }

  // For debugging purpose
  void PrintAllProperties(const inspect::NodeValue& node) {
    const auto& props = node.properties();
    auto* log_sink = zxtest::Runner::GetInstance()->mutable_reporter()->mutable_log_sink();
    for (const auto& p : props) {
      log_sink->Write("%s", p.name().c_str());
      switch (p.format()) {
        case inspect::PropertyFormat::kInt:
          log_sink->Write(" - %ld\n", p.Get<inspect::IntPropertyValue>().value());
          break;
        case inspect::PropertyFormat::kUint:
          log_sink->Write(" - %lu\n", p.Get<inspect::UintPropertyValue>().value());
          break;
        case inspect::PropertyFormat::kString:
          log_sink->Write(" - %s\n", p.Get<inspect::StringPropertyValue>().value().c_str());
          break;
        default:
          log_sink->Write("format not supported\n");
          break;
      }
    }
  }

 private:
  async::Executor executor_;
  fit::result<inspect::Hierarchy> hierarchy_;
};

class DriverHostInspectTestCase : public InspectTestHelper, public zxtest::Test {
 public:
  DriverHostInspectTestCase() : loop_(&kAsyncLoopConfigNoAttachToCurrentThread) {
    loop_.StartThread("dh_inspect_test_thread");
  }

  DriverHostInspect& inspect() { return inspect_; }

 private:
  DriverHostInspect inspect_;
  async::Loop loop_;
};

TEST_F(DriverHostInspectTestCase, DirectoryEntries) {
  // Check that root inspect is created
  uint8_t buffer[4096];
  size_t length;
  {
    fs::vdircookie_t cookie = {};
    EXPECT_EQ(inspect().diagnostics_dir().Readdir(&cookie, buffer, sizeof(buffer), &length), ZX_OK);
    fs::DirentChecker dc(buffer, length);
    dc.ExpectEntry(".", V_TYPE_DIR);
    dc.ExpectEntry("root.inspect", V_TYPE_FILE);
    dc.ExpectEnd();
  }
}

class DriverInspectTestCase : public InspectTestHelper, public zxtest::Test {
 public:
  DriverInspectTestCase() : driver_host_(&kAsyncLoopConfigNoAttachToCurrentThread) {}

  DriverHostContext& driver_host() { return driver_host_; }

 private:
  DriverHostContext driver_host_;
};

TEST_F(DriverInspectTestCase, DriverProperties) {
  fbl::RefPtr<zx_driver> driver;
  ASSERT_OK(zx_driver::Create("test-driver", driver_host().inspect().drivers(), &driver));
  driver->set_name("test");
  driver->set_status(ZX_OK);
  ReadInspect(driver_host().inspect().inspector());

  // Check properties of test-driver
  auto* test_driver = hierarchy().GetByPath({"drivers", "test-driver"});
  ASSERT_TRUE(test_driver);

  // name: "test"
  ASSERT_NO_FATAL_FAILURES(CheckProperty<inspect::StringPropertyValue>(
      test_driver->node(), "name", inspect::StringPropertyValue("test")));

  // status: 0
  ASSERT_NO_FATAL_FAILURES(CheckProperty<inspect::IntPropertyValue>(
      test_driver->node(), "status", inspect::IntPropertyValue(ZX_OK)));
}

TEST_F(DriverInspectTestCase, AddRemoveDriver) {
  // Get the initial driver count
  ReadInspect(driver_host().inspect().inspector());
  const auto* driver_count =
      hierarchy().node().get_property<inspect::UintPropertyValue>("driver_count");
  ASSERT_TRUE(driver_count);
  uint32_t initial_count = driver_count->value();

  // Add test-driver
  fbl::RefPtr<zx_driver> driver;
  ASSERT_OK(zx_driver::Create("test-driver", driver_host().inspect().drivers(), &driver));

  // Check count is incremented and driver is listed
  ReadInspect(driver_host().inspect().inspector());
  const auto* current_count =
      hierarchy().node().get_property<inspect::UintPropertyValue>("driver_count");
  ASSERT_TRUE(current_count);
  EXPECT_EQ(initial_count + 1, current_count->value());

  auto* test_driver = hierarchy().GetByPath({"drivers", "test-driver"});
  ASSERT_TRUE(test_driver);

  // Destroy driver
  driver.reset();

  // Check count is decremented and device is not listed
  ReadInspect(driver_host().inspect().inspector());
  current_count = hierarchy().node().get_property<inspect::UintPropertyValue>("driver_count");
  ASSERT_TRUE(current_count);
  EXPECT_EQ(initial_count, current_count->value());

  test_driver = hierarchy().GetByPath({"drivers", "test-driver"});
  ASSERT_FALSE(test_driver);
}

class DeviceInspectTestCase : public InspectTestHelper, public zxtest::Test {
 public:
  DeviceInspectTestCase() : driver_host_(&kAsyncLoopConfigNoAttachToCurrentThread) {
    zx_driver::Create("test-driver", driver_host_.inspect().drivers(), &drv_);
  }

  DriverHostContext& driver_host() { return driver_host_; }

  zx_driver* driver() { return drv_.get(); }

 private:
  DriverHostContext driver_host_;
  fbl::RefPtr<zx_driver> drv_;
};

TEST_F(DeviceInspectTestCase, DeviceProperties) {
  fbl::RefPtr<zx_device> device;
  ASSERT_OK(zx_device::Create(&(driver_host()), "test-device", driver(), &device));
  device->set_local_id(1);
  device->set_flag(DEV_FLAG_BUSY | DEV_FLAG_ADDED);

  ReadInspect(driver_host().inspect().inspector());

  // Check properties of test-device
  auto* test_device = hierarchy().GetByPath({"drivers", "test-driver", "devices", "test-device"});
  ASSERT_TRUE(test_device);
  ASSERT_NO_FATAL_FAILURES(CheckProperty<inspect::UintPropertyValue>(
      test_device->node(), "local_id", inspect::UintPropertyValue(1)));
  ASSERT_NO_FATAL_FAILURES(CheckProperty<inspect::StringPropertyValue>(
      test_device->node(), "flags", inspect::StringPropertyValue("busy added ")));
}

TEST_F(DeviceInspectTestCase, AddRemoveDevice) {
  fbl::RefPtr<zx_device> device;

  ASSERT_OK(zx_device::Create(&(driver_host()), std::string("test-device"), driver(), &device));

  // Check the device count and check that device is listed
  ReadInspect(driver_host().inspect().inspector());
  auto* test_driver = hierarchy().GetByPath({"drivers", "test-driver"});
  ASSERT_TRUE(test_driver);
  const auto* device_count =
      test_driver->node().get_property<inspect::UintPropertyValue>("device_count");
  ASSERT_TRUE(device_count);
  uint32_t initial_count = device_count->value();
  ASSERT_EQ(initial_count, 1);

  auto* test_device = hierarchy().GetByPath({"drivers", "test-driver", "devices", "test-device"});
  ASSERT_TRUE(test_device);

  // Destroy the device
  // Note: This only makes the device to be marked as dead; driver_host holds onto the list of dead
  // devices
  device->vnode.reset();
  device.reset();

  // Check count decremented and device is not listed
  ReadInspect(driver_host().inspect().inspector());
  test_driver = hierarchy().GetByPath({"drivers", "test-driver"});
  ASSERT_TRUE(test_driver);
  device_count = test_driver->node().get_property<inspect::UintPropertyValue>("device_count");
  ASSERT_TRUE(device_count);
  EXPECT_EQ(device_count->value(), 0);

  test_device = hierarchy().GetByPath({"drivers", "test-driver", "devices", "test-device"});
  ASSERT_FALSE(test_device);
}

TEST_F(DeviceInspectTestCase, CallStats) {
  fbl::RefPtr<zx_device> device;
  ASSERT_OK(zx_device::Create(&(driver_host()), "test-device", driver(), &device));
  device->set_ops(&internal::kDeviceDefaultOps);

  // Make op calls
  device->ReadOp(nullptr, 0, 0, nullptr);
  device->WriteOp(nullptr, 0, 0, nullptr);
  fidl_msg_t dummy_msg = {};
  fidl_message_header_t dummy_hdr = {};
  dummy_msg.bytes = static_cast<void*>(&dummy_hdr);
  device->MessageOp(&dummy_msg, nullptr);

  {
    // Test InspectCallStats::Update() method
    driver_host().inspect().DeviceCreateStats().Update();
  }

  // Check call stats
  ReadInspect(driver_host().inspect().inspector());
  auto* call_stats =
      hierarchy().GetByPath({"drivers", "test-driver", "devices", "test-device", "call_stats"});
  ASSERT_TRUE(call_stats);

  auto* read_op_stat = call_stats->GetByPath({"read_op"});
  ASSERT_TRUE(read_op_stat);
  ASSERT_NO_FATAL_FAILURES(CheckProperty<inspect::UintPropertyValue>(
      read_op_stat->node(), "count", inspect::UintPropertyValue(1)));

  auto* write_op_stat = call_stats->GetByPath({"write_op"});
  ASSERT_TRUE(write_op_stat);
  ASSERT_NO_FATAL_FAILURES(CheckProperty<inspect::UintPropertyValue>(
      write_op_stat->node(), "count", inspect::UintPropertyValue(1)));

  auto* message_op_stat = call_stats->GetByPath({"message_op"});
  ASSERT_TRUE(message_op_stat);
  ASSERT_NO_FATAL_FAILURES(CheckProperty<inspect::UintPropertyValue>(
      message_op_stat->node(), "count", inspect::UintPropertyValue(1)));

  auto* device_create_stat = hierarchy().GetByPath({"call_stats", "device_create"});
  ASSERT_TRUE(device_create_stat);
  ASSERT_NO_FATAL_FAILURES(CheckProperty<inspect::UintPropertyValue>(
      device_create_stat->node(), "count", inspect::UintPropertyValue(1)));
}

TEST_F(DeviceInspectTestCase, ParentChild) {
  fbl::RefPtr<zx_device> parent;
  ASSERT_OK(zx_device::Create(&(driver_host()), "test-parent", driver(), &parent));
  parent->set_local_id(2);

  fbl::RefPtr<zx_device> child;
  ASSERT_OK(zx_device::Create(&(driver_host()), "test-child", driver(), &child));
  child->set_local_id(3);
  child->set_parent(parent);
  parent->add_child(child.get());

  // Check parent-child fields in inspect
  ReadInspect(driver_host().inspect().inspector());
  auto* parent_data = hierarchy().GetByPath({"drivers", "test-driver", "devices", "test-parent"});
  ASSERT_TRUE(parent_data);
  auto* child_data = hierarchy().GetByPath({"drivers", "test-driver", "devices", "test-child"});
  ASSERT_TRUE(child_data);

  ASSERT_NO_FATAL_FAILURES(CheckProperty<inspect::UintPropertyValue>(
      parent_data->node(), "child_count", inspect::UintPropertyValue(1)));

  ASSERT_NO_FATAL_FAILURES(CheckProperty<inspect::StringPropertyValue>(
      child_data->node(), "parent", inspect::StringPropertyValue("test-parent (local-id:2)")));
}
