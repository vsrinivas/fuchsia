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

#include "multiple_device_test.h"
#include "src/lib/testing/loop_fixture/real_loop.h"

class InspectManagerTestCase : public zxtest::Test {
 public:
  InspectManagerTestCase() : loop_(&kAsyncLoopConfigNoAttachToCurrentThread) {
    loop_.StartThread("inspect_test_thread");
    inspect_manager_ = std::make_unique<InspectManager>(loop_.dispatcher());
  }

  InspectManager& inspect_manager() { return *inspect_manager_; }

 private:
  std::unique_ptr<InspectManager> inspect_manager_;
  async::Loop loop_;
};

TEST_F(InspectManagerTestCase, DirectoryEntries) {
  // Check that sub-directories are created
  uint8_t buffer[4096];
  size_t length;
  {
    fs::vdircookie_t cookie = {};
    EXPECT_EQ(inspect_manager().diagnostics_dir().Readdir(&cookie, buffer, sizeof(buffer), &length),
              ZX_OK);
    fs::DirentChecker dc(buffer, length);
    dc.ExpectEntry(".", V_TYPE_DIR);
    dc.ExpectEntry("driver_manager", V_TYPE_DIR);
    dc.ExpectEnd();
  }

  // Check entries of diagnostics/driver_manager
  {
    fbl::RefPtr<fs::Vnode> node;
    inspect_manager().diagnostics_dir().Lookup(&node, "driver_manager");
    fs::vdircookie_t cookie = {};
    EXPECT_EQ(node->Readdir(&cookie, buffer, sizeof(buffer), &length), ZX_OK);
    fs::DirentChecker dc(buffer, length);
    dc.ExpectEntry(".", V_TYPE_DIR);
    dc.ExpectEntry("driver_host", V_TYPE_DIR);
    dc.ExpectEntry("dm.inspect", V_TYPE_FILE);
    dc.ExpectEnd();
  }
}

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
    for (const auto& p : props) {
      printf("%s", p.name().c_str());
      switch (p.format()) {
        case inspect::PropertyFormat::kInt:
          printf(" - %ld\n", p.Get<inspect::IntPropertyValue>().value());
          break;
        case inspect::PropertyFormat::kUint:
          printf(" - %lu\n", p.Get<inspect::UintPropertyValue>().value());
          break;
        case inspect::PropertyFormat::kString:
          printf(" - %s\n", p.Get<inspect::StringPropertyValue>().value().c_str());
          break;
        default:
          printf("format not supported\n");
          break;
      }
    }
  }

 private:
  async::Executor executor_;
  fit::result<inspect::Hierarchy> hierarchy_;
};

class DeviceInspectTestCase : public MultipleDeviceTestCase, public InspectTestHelper {};

TEST_F(DeviceInspectTestCase, DeviceProperties) {
  size_t test_index;
  ASSERT_NO_FATAL_FAILURES(
      AddDevice(platform_bus(), "test-device", 99 /* protocol id */, "", &test_index));

  ReadInspect(coordinator()->inspect_manager().inspector());

  // Check properties of test-device
  auto* test_device = hierarchy().GetByPath({"devices", "test-device"});
  ASSERT_TRUE(test_device);
  PrintAllProperties(test_device->node());

  // state : kActive
  CheckProperty<inspect::StringPropertyValue>(test_device->node(), "state",
                                              inspect::StringPropertyValue("kActive"));

  // protocol_id : 99
  CheckProperty<inspect::UintPropertyValue>(test_device->node(), "protocol_id",
                                            inspect::UintPropertyValue(99));

  // flags : 128
  CheckProperty<inspect::UintPropertyValue>(test_device->node(), "flags",
                                            inspect::UintPropertyValue(128));

  // driver_host_local_id : 3
  CheckProperty<inspect::UintPropertyValue>(test_device->node(), "driver_host_local_id",
                                            inspect::UintPropertyValue(3));

  // topological_path : /dev/sys/platform-bus/test-device
  CheckProperty<inspect::StringPropertyValue>(
      test_device->node(), "topological_path",
      inspect::StringPropertyValue("/dev/sys/platform-bus/test-device"));

  // type : Device
  CheckProperty<inspect::StringPropertyValue>(test_device->node(), "type",
                                              inspect::StringPropertyValue("Device"));

  // driver : ""
  CheckProperty<inspect::StringPropertyValue>(test_device->node(), "driver",
                                              inspect::StringPropertyValue(""));
}

TEST_F(DeviceInspectTestCase, AddRemoveDevice) {
  // Get the initial device count is incremented
  ReadInspect(coordinator()->inspect_manager().inspector());
  const auto* device_count =
      hierarchy().node().get_property<inspect::UintPropertyValue>("device_count");
  ASSERT_TRUE(device_count);
  uint32_t initial_count = device_count->value();

  // Add test-device
  size_t test_index;
  ASSERT_NO_FATAL_FAILURES(
      AddDevice(platform_bus(), "test-device", 99 /* protocol id */, "", &test_index));

  // Check count incremented and device is listed
  ReadInspect(coordinator()->inspect_manager().inspector());
  const auto* current_count =
      hierarchy().node().get_property<inspect::UintPropertyValue>("device_count");
  ASSERT_TRUE(current_count);
  EXPECT_EQ(initial_count + 1, current_count->value());

  auto* test_device = hierarchy().GetByPath({"devices", "test-device"});
  ASSERT_TRUE(test_device);

  // Remove device
  RemoveDevice(test_index);

  // Check count decremented and device is not listed
  ReadInspect(coordinator()->inspect_manager().inspector());
  current_count = hierarchy().node().get_property<inspect::UintPropertyValue>("device_count");
  ASSERT_TRUE(current_count);
  EXPECT_EQ(initial_count, current_count->value());

  test_device = hierarchy().GetByPath({"devices", "test-device"});
  ASSERT_FALSE(test_device);
}

TEST_F(DeviceInspectTestCase, PropertyChange) {
  size_t test_index;
  ASSERT_NO_FATAL_FAILURES(AddDevice(platform_bus(), "test-device", 0, "", &test_index));

  // Check that change in state gets reflected in inspect
  ReadInspect(coordinator()->inspect_manager().inspector());
  auto* test_device = hierarchy().GetByPath({"devices", "test-device"});
  ASSERT_TRUE(test_device);

  // state: kActive
  CheckProperty<inspect::StringPropertyValue>(test_device->node(), "state",
                                              inspect::StringPropertyValue("kActive"));

  device(test_index)->device->set_state(Device::State::kResumed);

  // state: kResumed
  ReadInspect(coordinator()->inspect_manager().inspector());
  test_device = hierarchy().GetByPath({"devices", "test-device"});
  ASSERT_TRUE(test_device);
  CheckProperty<inspect::StringPropertyValue>(test_device->node(), "state",
                                              inspect::StringPropertyValue("kResumed"));
}
