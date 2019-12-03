// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/drivers/gpu/msd-vsl-gc/src/msd_vsl_connection.h"
#include "gtest/gtest.h"
#include "mock/mock_bus_mapper.h"

class TestMsdVslConnection : public MsdVslConnection::Owner {
 public:
  magma::PlatformBusMapper* GetBusMapper() override { return &mock_bus_mapper_; }

  void ConnectionReleased(MsdVslConnection* connection) override {
    connection_released_ = connection;
  }

  void Released() {
    auto connection =
        std::make_unique<MsdVslConnection>(this, 0, std::make_shared<AddressSpace>(this), 1000);
    MsdVslConnection* connection_ptr = connection.get();
    connection.reset();
    EXPECT_EQ(connection_released_, connection_ptr);
  }

 private:
  MockBusMapper mock_bus_mapper_;
  MsdVslConnection* connection_released_ = nullptr;
};

TEST(MsdVslConnection, Released) { TestMsdVslConnection().Released(); }
