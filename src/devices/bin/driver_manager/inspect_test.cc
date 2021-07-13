// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "inspect.h"

#include <sdk/lib/inspect/testing/cpp/zxtest/inspect.h>
#include <zxtest/zxtest.h>

#include "multiple_device_test.h"
#include "src/lib/storage/vfs/cpp/dir_test_util.h"

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
    fs::VdirCookie cookie;
    EXPECT_EQ(inspect_manager().diagnostics_dir().Readdir(&cookie, buffer, sizeof(buffer), &length),
              ZX_OK);

    fs::DirentChecker dc(buffer, length);
    dc.ExpectEntry(".", V_TYPE_DIR);
    dc.ExpectEntry("driver_manager", V_TYPE_DIR);
    dc.ExpectEntry("class", V_TYPE_DIR);
    dc.ExpectEnd();
  }

  // Check entries of diagnostics/driver_manager
  {
    fbl::RefPtr<fs::Vnode> node;
    inspect_manager().diagnostics_dir().Lookup("driver_manager", &node);

    fs::VdirCookie cookie;
    EXPECT_EQ(node->Readdir(&cookie, buffer, sizeof(buffer), &length), ZX_OK);

    fs::DirentChecker dc(buffer, length);
    dc.ExpectEntry(".", V_TYPE_DIR);
    dc.ExpectEntry("driver_host", V_TYPE_DIR);
    dc.ExpectEntry("fuchsia.inspect.Tree", V_TYPE_FILE);
    dc.ExpectEnd();
  }
}
namespace {
using inspect::InspectTestHelper;
class DeviceInspectTestCase : public MultipleDeviceTestCase, public InspectTestHelper {};
}  // namespace

TEST_F(DeviceInspectTestCase, DeviceProperties) {
  size_t test_index;
  ASSERT_NO_FATAL_FAILURES(
      AddDevice(platform_bus()->device, "test-device", 99 /* protocol id */, "", &test_index));

  ReadInspect(coordinator().inspect_manager().inspector());

  // Check properties of test-device
  auto* test_device = hierarchy().GetByPath({"devices", "test-device"});
  ASSERT_TRUE(test_device);
  PrintAllProperties(test_device->node());

  // state : kActive
  CheckProperty(test_device->node(), "state", inspect::StringPropertyValue("kActive"));

  // protocol_id : 99
  CheckProperty(test_device->node(), "protocol_id", inspect::UintPropertyValue(99));

  // flags : 128
  CheckProperty(test_device->node(), "flags", inspect::UintPropertyValue(128));

  // driver_host_local_id : 3
  CheckProperty(test_device->node(), "driver_host_local_id", inspect::UintPropertyValue(3));

  // topological_path : /dev/sys/platform-bus/test-device
  CheckProperty(test_device->node(), "topological_path",
                inspect::StringPropertyValue("/dev/sys/platform-bus/test-device"));

  // type : Device
  CheckProperty(test_device->node(), "type", inspect::StringPropertyValue("Device"));

  // driver : ""
  CheckProperty(test_device->node(), "driver", inspect::StringPropertyValue(""));
}

TEST_F(DeviceInspectTestCase, AddRemoveDevice) {
  // Get the initial device count is incremented
  ReadInspect(coordinator().inspect_manager().inspector());
  const auto* device_count =
      hierarchy().node().get_property<inspect::UintPropertyValue>("device_count");
  ASSERT_TRUE(device_count);
  uint32_t initial_count = device_count->value();

  // Add test-device
  size_t test_index;
  ASSERT_NO_FATAL_FAILURES(
      AddDevice(platform_bus()->device, "test-device", 99 /* protocol id */, "", &test_index));

  // Check count incremented and device is listed
  ReadInspect(coordinator().inspect_manager().inspector());
  const auto* current_count =
      hierarchy().node().get_property<inspect::UintPropertyValue>("device_count");
  ASSERT_TRUE(current_count);
  EXPECT_EQ(initial_count + 1, current_count->value());

  auto* test_device = hierarchy().GetByPath({"devices", "test-device"});
  ASSERT_TRUE(test_device);

  // Remove device
  RemoveDevice(test_index);

  // Check count decremented and device is not listed
  ReadInspect(coordinator().inspect_manager().inspector());
  current_count = hierarchy().node().get_property<inspect::UintPropertyValue>("device_count");
  ASSERT_TRUE(current_count);
  EXPECT_EQ(initial_count, current_count->value());

  test_device = hierarchy().GetByPath({"devices", "test-device"});
  ASSERT_FALSE(test_device);
}

TEST_F(DeviceInspectTestCase, PropertyChange) {
  size_t test_index;
  ASSERT_NO_FATAL_FAILURES(AddDevice(platform_bus()->device, "test-device", 0, "", &test_index));

  // Check that change in state gets reflected in inspect
  ReadInspect(coordinator().inspect_manager().inspector());
  auto* test_device = hierarchy().GetByPath({"devices", "test-device"});
  ASSERT_TRUE(test_device);

  // state: kActive
  CheckProperty(test_device->node(), "state", inspect::StringPropertyValue("kActive"));

  device(test_index)->device->set_state(Device::State::kResumed);

  // state: kResumed
  ReadInspect(coordinator().inspect_manager().inspector());
  test_device = hierarchy().GetByPath({"devices", "test-device"});
  ASSERT_TRUE(test_device);
  CheckProperty(test_device->node(), "state", inspect::StringPropertyValue("kResumed"));
}

class InspectDevfsTestCase : public MultipleDeviceTestCase {};

TEST_F(InspectDevfsTestCase, DevfsEntries) {
  size_t test_index;
  zx::vmo inspect_vmo, inspect_vmo_duplicate;
  uint32_t test_device_protocol = ZX_PROTOCOL_BLOCK;
  ASSERT_OK(zx::vmo::create(8 * 1024, 0, &inspect_vmo));
  ASSERT_OK(inspect_vmo.duplicate(ZX_RIGHTS_BASIC | ZX_RIGHT_READ | ZX_RIGHT_MAP,
                                  &inspect_vmo_duplicate));

  ASSERT_NO_FATAL_FAILURES(
      AddDevice(platform_bus()->device, "test-device", test_device_protocol /* protocol id */, "",
                false /* has_init */, false /* reply_to_init */, false /* always_init */,
                std::move(inspect_vmo_duplicate) /* inspect */, &test_index));

  // Check that device vmo is listed in devfs
  uint8_t buffer[4096];
  size_t length;
  {
    auto [dir, seqcount] =
        coordinator().inspect_manager().devfs()->GetProtoDir(test_device_protocol);
    ASSERT_NE(dir, nullptr);
    ASSERT_NE(seqcount, nullptr);
    ASSERT_EQ(*seqcount, 1);

    fs::VdirCookie cookie;
    EXPECT_EQ(dir->Readdir(&cookie, buffer, sizeof(buffer), &length), ZX_OK);

    fs::DirentChecker dc(buffer, length);
    dc.ExpectEntry(".", V_TYPE_DIR);
    dc.ExpectEntry("000.inspect", V_TYPE_FILE);
    dc.ExpectEnd();
  }

  // remove device
  RemoveDevice(test_index);

  // Check that protocol directory is removed and hence the inspect vmo is unlisted
  {
    auto [dir, seqcount] =
        coordinator().inspect_manager().devfs()->GetProtoDir(test_device_protocol);
    ASSERT_EQ(dir, nullptr);
  }
}

TEST_F(InspectDevfsTestCase, NoPubProtocolVisibleInClassDirectory) {
  size_t test_index;
  zx::vmo inspect_vmo, inspect_vmo_duplicate;
  uint32_t test_device_protocol = ZX_PROTOCOL_BUTTONS;  // This has PF_NOPUB set
  ASSERT_OK(zx::vmo::create(8 * 1024, 0, &inspect_vmo));
  ASSERT_OK(inspect_vmo.duplicate(ZX_RIGHTS_BASIC | ZX_RIGHT_READ | ZX_RIGHT_MAP,
                                  &inspect_vmo_duplicate));

  ASSERT_NO_FATAL_FAILURES(AddDevice(
      platform_bus()->device, "test-device", test_device_protocol /* protocol id */, "",
      false /* has_init */, false /* reply_to_init */,
      false /* always_init */, std::move(inspect_vmo_duplicate) /* inspect */, &test_index));

  // Check that device vmo is listed in devfs
  uint8_t buffer[4096];
  size_t length;
  {
    auto [dir, seqcount] =
        coordinator().inspect_manager().devfs()->GetProtoDir(test_device_protocol);
    ASSERT_NE(dir, nullptr);
    ASSERT_NE(seqcount, nullptr);
    ASSERT_EQ(*seqcount, 1);

    fs::VdirCookie cookie;
    EXPECT_EQ(dir->Readdir(&cookie, buffer, sizeof(buffer), &length), ZX_OK);

    fs::DirentChecker dc(buffer, length);
    dc.ExpectEntry(".", V_TYPE_DIR);
    dc.ExpectEntry("000.inspect", V_TYPE_FILE);
    dc.ExpectEnd();
  }
}
