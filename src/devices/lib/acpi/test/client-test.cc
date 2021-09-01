// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/devices/lib/acpi/client.h"

#include <fidl/fuchsia.hardware.acpi/cpp/wire.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/fidl/llcpp/arena.h>

#include <zxtest/zxtest.h>

#include "src/devices/lib/acpi/mock/mock-acpi.h"

using MockAcpiDevice = acpi::mock::Device;
namespace facpi = fuchsia_hardware_acpi::wire;

// Intel NHLT DSM UUID: a69f886e-6ceb-4594-a41f-7b5dce24c553
static constexpr auto kNhltUuid =
    acpi::Uuid::Create(0xa69f886e, 0x6ceb, 0x4594, 0xa41f, 0x7b5dce24c553);
static constexpr uint8_t kNhltUuidRaw[] = {
    /* 0000 */ 0x6e, 0x88, 0x9f, 0xa6, 0xeb, 0x6c, 0x94, 0x45,
    /* 0008 */ 0xa4, 0x1f, 0x7b, 0x5d, 0xce, 0x24, 0xc5, 0x53};

class AcpiClientTest : public zxtest::Test {
 public:
  AcpiClientTest() : loop_(&kAsyncLoopConfigNeverAttachToThread) {}

  void SetUp() override {
    ASSERT_OK(loop_.StartThread("acpi-client-test-thread"));
    ASSERT_BYTES_EQ(kNhltUuid.bytes, kNhltUuidRaw, countof(kNhltUuidRaw));

    server_.SetEvaluateObject([this](MockAcpiDevice::EvaluateObjectRequestView request,
                                     MockAcpiDevice::EvaluateObjectCompleter::Sync &sync) {
      ASSERT_BYTES_EQ(request->path.data(), "_DSM", request->path.size());
      ASSERT_EQ(request->mode, facpi::EvaluateObjectMode::kPlainObject);
      ASSERT_EQ(request->parameters.count(), 3);
      auto &params = request->parameters;

      ASSERT_TRUE(params[0].is_buffer_val());
      ASSERT_BYTES_EQ(params[0].buffer_val().data(), kNhltUuidRaw, countof(kNhltUuidRaw));

      ASSERT_TRUE(params[1].is_integer_val());
      ASSERT_EQ(params[1].integer_val(), 1);
      ASSERT_TRUE(params[2].is_integer_val());
      ASSERT_EQ(params[2].integer_val(), 3);

      if (response_ == std::nullopt) {
        sync.ReplyError(facpi::Status::kError);
      } else {
        facpi::EncodedObject reply;
        auto object = fidl::ObjectView<facpi::Object>::FromExternal(&response_.value());
        reply.set_object(object);
        sync.ReplySuccess(reply);
      }
    });
  }

 protected:
  async::Loop loop_;
  MockAcpiDevice server_;
  std::optional<facpi::Object> response_;
};

TEST_F(AcpiClientTest, TestCallDsmFails) {
  auto client = server_.CreateClient(loop_.dispatcher());
  ASSERT_OK(client.status_value());

  auto helper = std::move(client.value());
  auto result = helper.CallDsm(kNhltUuid, 1, 3, std::nullopt);
  ASSERT_OK(result.status_value());
  ASSERT_EQ(result->status_val(), facpi::Status::kError);
}

TEST_F(AcpiClientTest, TestCallDsmSucceeds) {
  fidl::Arena<> alloc;
  facpi::Object obj;
  obj.set_integer_val(alloc, 320);
  response_.emplace(obj);
  auto client = server_.CreateClient(loop_.dispatcher());
  ASSERT_OK(client.status_value());

  auto helper = std::move(client.value());
  auto result = helper.CallDsm(kNhltUuid, 1, 3, std::nullopt);
  ASSERT_OK(result.status_value());
  ASSERT_EQ(result->integer_val(), 320);
}
