// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/devices/board/drivers/x86/acpi-dev/dev-ec.h"

#include <acpica/acpi.h>
#include <zxtest/zxtest.h>

#include "src/devices/board/lib/acpi/test/device.h"
#include "src/devices/board/lib/acpi/test/mock-acpi.h"
#include "src/devices/testing/mock-ddk/mock-device.h"

namespace acpi_ec {

constexpr uint16_t kDataPort = 0xaf;
constexpr uint16_t kCmdPort = 0xbf;
constexpr uint32_t kGpeNumber = 11;

struct Command {
  EcCmd cmd;
  uint8_t arg1;
  uint8_t arg2;
  uint8_t arg_count = 0;
};

class EcTest;
class TestIoPort : public IoPortInterface {
 public:
  explicit TestIoPort(EcTest* test) : test_(test) {}

  void outp(uint16_t port, uint8_t data) override;
  uint8_t inp(uint16_t port) override;
  zx_status_t Map(uint16_t port) override { return ZX_OK; }

  // Fire a query event.
  void SetUpQuery();
  // Put the given value into the data register and fire the GPE.
  void SetNextData(uint8_t value);

 private:
  // handle incoming data
  bool HandleData(uint8_t data) {
    cmd_->arg_count++;
    switch (cmd_->cmd) {
      case EcCmd::kQuery:
        ZX_ASSERT_MSG(false, "query takes no arguments");
        return false;
      case EcCmd::kRead:
        ZX_ASSERT_MSG(cmd_->arg_count == 1, "read takes one argument");
        cmd_->arg1 = data;
        return true;
      case EcCmd::kWrite:
        ZX_ASSERT_MSG(cmd_->arg_count < 3, "write takes two arguments");
        if (cmd_->arg_count == 1) {
          cmd_->arg1 = data;
          return false;
        } else {
          cmd_->arg2 = data;
          return true;
        }
    }
  }

 private:
  uint8_t status_ = 0;
  std::optional<uint8_t> next_data_;
  EcTest* test_;
  std::optional<Command> cmd_;
};

class EcTest : public zxtest::Test {
 public:
  void SetUp() override {
    fake_root_ = MockDevice::FakeRootParent();
    auto dev = std::make_unique<acpi::test::Device>("/");
    acpi_.SetDeviceRoot(std::move(dev));

    auto ec_dev = std::make_unique<acpi::test::Device>("EC0_");
    auto* dev_ptr = ec_dev.get();
    acpi_.GetDeviceRoot()->AddChild(std::move(ec_dev));
    dev_ptr->AddMethodCallback("_GPE", [](std::optional<std::vector<ACPI_OBJECT>>) {
      ACPI_OBJECT* obj = static_cast<ACPI_OBJECT*>(AcpiOsAllocate(sizeof(*obj)));
      obj->Integer.Type = ACPI_TYPE_INTEGER;
      obj->Integer.Value = kGpeNumber;
      return acpi::ok(acpi::UniquePtr<ACPI_OBJECT>(obj));
    });

    dev_ptr->AddResource(ACPI_RESOURCE{
        .Type = ACPI_RESOURCE_TYPE_IO,
        .Data = {.Io = {.Minimum = kDataPort, .Maximum = kDataPort}},
    });
    dev_ptr->AddResource(ACPI_RESOURCE{
        .Type = ACPI_RESOURCE_TYPE_IO,
        .Data = {.Io = {.Minimum = kCmdPort, .Maximum = kCmdPort}},
    });
    acpi_ec_dev_ = dev_ptr;

    auto io_port = std::make_unique<TestIoPort>(this);
    io_ = io_port.get();
    auto device = std::make_unique<EcDevice>(fake_root_.get(), &acpi_, dev_ptr, std::move(io_port));
    ASSERT_OK(device->Init());
    // DDK takes ownership of the device.
    ec_dev_ = device.release();
  }

  void TearDown() override {
    device_async_remove(fake_root_->GetLatestChild());
    mock_ddk::ReleaseFlaggedDevices(fake_root_->GetLatestChild());
  }

  void RunCommand(Command& cmd) { hook_(cmd); }

  void FireGpe() { acpi_.FireGpe(kGpeNumber); }

 protected:
  std::shared_ptr<zx_device> fake_root_;
  EcDevice* ec_dev_;
  acpi::test::MockAcpi acpi_;
  acpi::test::Device* acpi_ec_dev_;
  TestIoPort* io_;
  std::function<void(Command&)> hook_;
};

void TestIoPort::outp(uint16_t port, uint8_t data) {
  switch (port) {
    case kCmdPort:
      ZX_ASSERT(!cmd_.has_value());
      if (data == EcCmd::kQuery) {
        Command cmd{
            .cmd = EcCmd::kQuery,
        };
        test_->RunCommand(cmd);
      } else {
        cmd_.emplace(Command{
            .cmd = static_cast<EcCmd>(data),
        });
      }
      test_->FireGpe();
      break;
    case kDataPort:
      ZX_ASSERT(cmd_.has_value());
      if (HandleData(data)) {
        std::optional<Command> cmd;
        cmd_.swap(cmd);
        test_->RunCommand(cmd.value());
      }
      test_->FireGpe();
      break;
  }
}

uint8_t TestIoPort::inp(uint16_t port) {
  switch (port) {
    case kCmdPort:
      return status_;
    case kDataPort: {
      std::optional<uint8_t> retval;
      retval.swap(next_data_);
      status_ &= EcStatus::kObf;
      test_->FireGpe();
      return retval.value();
    }
    default:
      ZX_ASSERT_MSG(false, "Unknown port");
  }
}

void TestIoPort::SetUpQuery() {
  status_ |= EcStatus::kSciEvt;
  test_->FireGpe();
}

void TestIoPort::SetNextData(uint8_t value) {
  next_data_.emplace(value);
  status_ |= EcStatus::kObf;
  test_->FireGpe();
}

TEST_F(EcTest, SmokeTest) {}

TEST_F(EcTest, QueryTest) {
  hook_ = [this](Command& cmd) {
    if (cmd.cmd == EcCmd::kQuery) {
      io_->SetNextData(0x12);
    } else {
      ASSERT_TRUE(false);
    }
  };
  sync_completion_t done;
  acpi_ec_dev_->AddMethodCallback("_Q12", [&done](auto unused) {
    sync_completion_signal(&done);
    return acpi::ok(nullptr);
  });

  io_->SetUpQuery();
  sync_completion_wait(&done, ZX_TIME_INFINITE);
}

// This test acts like an ACPI method that calls I/O operations from within Query().
TEST_F(EcTest, IoInsideQueryTest) {
  hook_ = [this](Command& cmd) {
    if (cmd.cmd == EcCmd::kQuery) {
      io_->SetNextData(0x12);
    } else if (cmd.cmd == EcCmd::kRead) {
      ASSERT_EQ(cmd.arg1, 0x87);
      io_->SetNextData(0x99);
    } else if (cmd.cmd == EcCmd::kWrite) {
      ASSERT_EQ(cmd.arg1, 0x87);
      ASSERT_EQ(cmd.arg2, 0x77);
    }
  };
  sync_completion_t done;
  acpi_ec_dev_->AddMethodCallback("_Q12", [&done, this](auto unused) {
    UINT64 value = 0;
    EXPECT_EQ(
        AE_OK,
        acpi_ec_dev_->AddressSpaceOp(ACPI_ADR_SPACE_EC, ACPI_READ, 0x87, 8, &value).status_value());
    EXPECT_EQ(value, 0x99);

    value = 0x77;
    EXPECT_EQ(AE_OK, acpi_ec_dev_->AddressSpaceOp(ACPI_ADR_SPACE_EC, ACPI_WRITE, 0x87, 8, &value)
                         .status_value());
    sync_completion_signal(&done);
    return acpi::ok(nullptr);
  });

  io_->SetUpQuery();
  sync_completion_wait(&done, ZX_TIME_INFINITE);
}

}  // namespace acpi_ec
