// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <grpcpp/client_context.h>
#include <grpcpp/create_channel.h>
#include <grpcpp/support/config.h>
#include <gtest/gtest.h>

#include "src/tests/end_to_end/setui/emulator_controller.grpc.pb.h"
#include "src/tests/end_to_end/setui/emulator_controller.pb.h"

namespace {

class BrightnessTest : public testing::Test {
 public:
  std::shared_ptr<grpc::Channel> channel;

  static void Init(int argc, char** argv) {
    // parse argc, argv and set addr_ and port_
    for (size_t i = 1; i < static_cast<size_t>(argc); i++) {
      if (strcmp(argv[i], "--port") == 0 && i + 1 < static_cast<size_t>(argc)) {
        port_ = argv[++i];
        continue;
      }
      if (strcmp(argv[i], "--address") == 0 && i + 1 < static_cast<size_t>(argc)) {
        addr_ = argv[++i];
        continue;
      }
    }
  }

 protected:
  void SetUp() override {
    // Set up a gRPC connection.
    // Use FEMU default port number, refer to https://fuchsia.dev/reference/tools/fx/cmd/emu.
    channel = grpc::CreateChannel(addr_ + ":" + port_, grpc::InsecureChannelCredentials());
  }

 private:
  inline static std::string addr_ = "127.0.0.1";
  inline static std::string port_ = "5556";
};

TEST_F(BrightnessTest, LightSensorControl) {
  // This test is to make sure controlling light sensor through gRPC works, and prevent breaking
  // changes against gRPC.

  // Get gRPC client.
  auto stub = android::emulation::control::EmulatorController::NewStub(channel);

  // Get the RGBC sensor and check the original RGBC values size is 4.
  grpc::ClientContext context;
  android::emulation::control::SensorValue request;
  request.set_target(
      android::emulation::control::SensorValue_SensorType::SensorValue_SensorType_RGBC_LIGHT);
  android::emulation::control::SensorValue response;
  auto grpc_status = stub->getSensor(&context, request, &response);
  ASSERT_TRUE(grpc_status.ok());
  google::protobuf::RepeatedField<float> res = response.value().data();
  ASSERT_TRUE(res.size() == 4);
}
}  // namespace

int main(int argc, char** argv) {
  testing::InitGoogleTest(&argc, argv);
  BrightnessTest::Init(argc, argv);
  return RUN_ALL_TESTS();
}
