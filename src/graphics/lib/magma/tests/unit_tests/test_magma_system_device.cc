// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "gtest/gtest.h"
#include "magma.h"
#include "mock/mock_msd.h"
#include "sys_driver/magma_system_device.h"

class MsdMockDevice_GetDeviceId : public MsdMockDevice {
 public:
  MsdMockDevice_GetDeviceId(uint32_t device_id) : device_id_(device_id) {}

  uint32_t GetDeviceId() override { return device_id_; }

 private:
  uint32_t device_id_;
};

TEST(MagmaSystemDevice, GetDeviceId) {
  uint32_t test_id = 0xdeadbeef;

  auto msd_dev = new MsdMockDevice_GetDeviceId(test_id);
  auto device = MagmaSystemDevice::Create(MsdDeviceUniquePtr(msd_dev));

  uint32_t device_id = device->GetDeviceId();
  // For now device_id is invalid
  EXPECT_EQ(device_id, test_id);

  uint64_t value;
  EXPECT_TRUE(device->Query(MAGMA_QUERY_DEVICE_ID, &value));
  EXPECT_EQ(value, test_id);
}
