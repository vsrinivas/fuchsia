// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <endian.h>
#include <fuchsia/hardware/gpio/cpp/banjo-mock.h>
#include <fuchsia/hardware/gpio/cpp/banjo.h>
#include <lib/fake-i2c/fake-i2c.h>
#include <lib/fzl/vmo-mapper.h>

#include <array>

#include <zxtest/zxtest.h>

#include "gt92xx.h"
#include "src/devices/testing/mock-ddk/mock-device.h"

namespace {

bool enable_load_firmware = false;
bool corrupt_firmware_checksum = false;

}  // namespace

// Must not be in a namespace in order to override the weak implementation in fake_ddk.
zx_status_t load_firmware_from_driver(zx_driver_t* drv, zx_device_t* device, const char* path,
                                      zx_handle_t* fw, size_t* size) {
  constexpr uint8_t kFirmwareTestData[] = {0x52, 0xc0, 0xb3, 0x37, 0x84, 0x2c, 0xf0, 0xbc,
                                           0x88, 0xe7, 0xca, 0x28, 0x93, 0x9f, 0xed, 0x86,
                                           0xd6, 0x06, 0x4b, 0xb1, 0x72, 0x65, 0x45, 0x48,
                                           0x6d, 0xcf, 0x06, 0x86, 0xe7, 0xac, 0x39, 0x6f};

  constexpr uint8_t kFirmwareHeader[] = {
      0x00, 0x01, 0x60, 0x00,                          // Firmware size excluding header
      '9',  '2',  '9',  '3',  '\0', '\0', '\0', '\0',  // Product ID string
      0x61, 0x05,                                      // Firmware version number
  };
  constexpr size_t kFirmwareSize = sizeof(kFirmwareHeader) + 0x16000;

  if (!enable_load_firmware) {
    return ZX_ERR_NOT_FOUND;
  }

  zx::vmo firmware_vmo;
  fzl::VmoMapper firmware_mapper;
  zx_status_t status = firmware_mapper.CreateAndMap(
      kFirmwareSize, ZX_VM_PERM_READ | ZX_VM_PERM_WRITE, nullptr, &firmware_vmo);
  if (status != ZX_OK) {
    return status;
  }

  uint8_t* firmware = reinterpret_cast<uint8_t*>(firmware_mapper.start());
  memcpy(firmware, kFirmwareHeader, sizeof(kFirmwareHeader));

  constexpr size_t kSectionOffsets[] = {
      0x0000,  0x2000, 0x4000, 0x6000,   // SS51
      0xa800,  0xc800, 0xe800, 0x10800,  // Gwake
      0x15000,                           // DSP ISP
  };

  for (size_t offset : kSectionOffsets) {
    memcpy(firmware + sizeof(kFirmwareHeader) + offset, kFirmwareTestData,
           sizeof(kFirmwareTestData));
  }

  uint16_t checksum = 0;
  // Skip the first uint16 and put the checksum there after.
  for (size_t i = 1; i < (kFirmwareSize - sizeof(kFirmwareHeader)) / sizeof(checksum); i++) {
    checksum += be16toh(reinterpret_cast<uint16_t*>(firmware + sizeof(kFirmwareHeader))[i]);
  }
  checksum = htobe16(-checksum);
  if (corrupt_firmware_checksum) {
    checksum++;
  }

  memcpy(firmware + sizeof(kFirmwareHeader), &checksum, sizeof(checksum));

  *fw = firmware_vmo.release();
  *size = kFirmwareSize;
  return ZX_OK;
}

namespace goodix {

class FakeTouchDevice : public fake_i2c::FakeI2c {
 private:
  uint8_t product_info_[sizeof(uint32_t) + sizeof(uint16_t)] = {'9', '2', '9', '3', 0x04, 0x61};

 public:
  enum ControllerState {
    kIdle,
    kReadingDspIsp,
    kReadingGwake,
    kReadingSs51,
    kReadingDsp,
    kReadingBoot,
    kReadingBootIsp,
    kReadingLink,
    kReadingFirstSs51Section,
    kReady,
  };

  void SetFirmwareMessageInvalid() { firmware_message_ = 0; }
  void SetProductInfo(std::array<uint8_t, sizeof(product_info_)> product_info) {
    memcpy(product_info_, product_info.data(), sizeof(product_info_));
  }
  void SetCorruptSectionRead() { corrupt_section_read_ = true; }
  bool FirmwareWritten() const { return firmware_written_; }
  ControllerState CurrentState() const { return current_state_; }

 protected:
  zx_status_t Transact(const uint8_t* write_buffer, size_t write_buffer_size, uint8_t* read_buffer,
                       size_t* read_buffer_size) override {
    if (write_buffer_size < 2) {
      return ZX_ERR_NOT_SUPPORTED;
    }

    const uint16_t address = (write_buffer[0] << 8) | write_buffer[1];
    write_buffer += 2;
    write_buffer_size -= 2;

    ControllerState next_state = current_state_;

    if (address == GT_REG_SW_RESET) {
      if (write_buffer_size == 0) {
        read_buffer[0] = sw_reset_;
        *read_buffer_size = 1;
      } else {
        sw_reset_ = write_buffer[0];
      }
    } else if (address == GT_REG_CPU_RESET && write_buffer_size == 1) {
      next_state = kReadingDspIsp;
    } else if (address == GT_REG_BOOT_CONTROL) {
      if (write_buffer_size == 0) {
        read_buffer[0] = 0;  // Always not busy
        *read_buffer_size = 1;
      } else if (write_buffer[0] == 0x00 && current_state_ == kReadingDspIsp) {
        // This assumes that Gwake is always written, even though it is technically optional.
        next_state = kReadingGwake;
      } else if (write_buffer[0] == 0xd && current_state_ == kReadingGwake) {
        next_state = kReadingSs51;
      } else if (write_buffer[0] == 0x4 && current_state_ == kReadingSs51) {
        next_state = kReadingDsp;
      } else if (write_buffer[0] == 0x5 && current_state_ == kReadingDsp) {
        next_state = kReadingBoot;
      } else if (write_buffer[0] == 0x6 && current_state_ == kReadingBoot) {
        next_state = kReadingBootIsp;
      } else if (write_buffer[0] == 0x7 && current_state_ == kReadingBootIsp) {
        next_state = kReadingLink;
      } else if (write_buffer[0] == 0x9 && current_state_ == kReadingLink) {
        next_state = kReadingFirstSs51Section;
      } else if (write_buffer[0] == 0x1) {
        // Boot ISP and Link are optional, but the process always ends with writing the first SS51
        // section.
        if (current_state_ == kReadingFirstSs51Section || current_state_ == kReadingBootIsp ||
            current_state_ == kReadingLink) {
          next_state = kReady;
        }
      }
    } else if (address == GT_REG_FIRMWARE && write_buffer_size == 0) {
      read_buffer[0] = firmware_message_;
      *read_buffer_size = 1;
    } else if (address == GT_REG_HW_INFO && write_buffer_size == 0) {
      memset(read_buffer, 0, sizeof(uint32_t));
      *read_buffer_size = sizeof(uint32_t);
    } else if (address == GT_REG_PRODUCT_INFO && write_buffer_size == 0) {
      memcpy(read_buffer, product_info_, sizeof(product_info_));
      *read_buffer_size = sizeof(product_info_);
    } else if ((address >= 0x9000 && address < 0xb000) || (address >= 0xc000)) {
      // This needs to be reset for the config check, at this point in the firmware download the
      // message value is no longer used.
      firmware_message_ = GT_FIRMWARE_MAGIC;

      // Map [0x9000, 0xb000) to [0x8000, 0xa000) -- that way the top bits can masked off to
      // get an offset.
      const uint16_t offset = (address ^ (address < 0xb000 ? 0x1000 : 0)) & 0x1fff;
      const size_t remaining_size = std::min<size_t>(sizeof(section_) - offset, 256);
      if (write_buffer_size == 0) {
        memcpy(read_buffer, &section_[offset], remaining_size);
        *read_buffer_size = remaining_size;
      } else if (write_buffer_size <= remaining_size) {
        firmware_written_ = true;
        memcpy(&section_[offset], write_buffer, write_buffer_size);
        if (corrupt_section_read_) {
          section_[offset]++;
        }
      } else {
        return ZX_ERR_IO;
      }
    } else if (write_buffer_size == 0) {
      *read_buffer = 0;
      *read_buffer_size = 1;
    }

    current_state_ = next_state;
    return ZX_OK;
  }

 private:
  uint8_t sw_reset_ = 0;
  uint8_t section_[0x2000];
  uint8_t firmware_message_ = GT_FIRMWARE_MAGIC;
  bool corrupt_section_read_ = false;
  bool firmware_written_ = false;

  ControllerState current_state_ = kIdle;
};

class Gt92xxTest : public Gt92xxDevice {
 public:
  Gt92xxTest(ddk::I2cChannel i2c, ddk::GpioProtocolClient intr, ddk::GpioProtocolClient reset,
             zx_device_t* parent)
      : Gt92xxDevice(parent, i2c, intr, reset) {}

  void Running(bool run) { Gt92xxDevice::running_.store(run); }

  zx_status_t Init() { return Gt92xxDevice::Init(); }

  void Trigger() { irq_.trigger(0, zx::time()); }

  zx_status_t StartThread() {
    EXPECT_OK(zx::interrupt::create(zx::resource(), 0, ZX_INTERRUPT_VIRTUAL, &irq_));

    auto thunk = [](void* arg) -> int { return reinterpret_cast<Gt92xxTest*>(arg)->Thread(); };

    Running(true);
    int ret = thrd_create_with_name(&test_thread_, thunk, this, "gt92xx-test-thread");
    return (ret == thrd_success) ? ZX_OK : ZX_ERR_BAD_STATE;
  }

  zx_status_t StopThread() {
    Running(false);
    irq_.trigger(0, zx::time());
    int ret = thrd_join(test_thread_, nullptr);
    return (ret == thrd_success) ? ZX_OK : ZX_ERR_BAD_STATE;
  }

  thrd_t test_thread_;
};

class GoodixTest : public zxtest::Test {
 public:
  void SetUp() override { enable_load_firmware = true; }
  void TearDown() override {
    enable_load_firmware = false;
    corrupt_firmware_checksum = false;
  }
};

TEST_F(GoodixTest, FirmwareTest) {
  ddk::MockGpio reset;
  ddk::MockGpio intr;
  FakeTouchDevice i2c;

  // Initial reset
  reset.ExpectConfigOut(ZX_OK, 0).ExpectWrite(ZX_OK, 1);
  intr.ExpectConfigOut(ZX_OK, 0)
      .ExpectConfigIn(ZX_OK, GPIO_PULL_UP)
      .ExpectGetInterrupt(ZX_OK, ZX_INTERRUPT_MODE_EDGE_LOW, {});

  // Entering update mode
  reset.ExpectConfigOut(ZX_OK, 0).ExpectConfigOut(ZX_OK, 1);
  intr.ExpectConfigOut(ZX_OK, 0);

  // Leaving update mode
  reset.ExpectConfigOut(ZX_OK, 0).ExpectConfigOut(ZX_OK, 1).ExpectConfigIn(ZX_OK, 0);
  intr.ExpectConfigIn(ZX_OK, GPIO_PULL_UP)
      .ExpectConfigOut(ZX_OK, 0)
      .ExpectConfigOut(ZX_OK, 0)
      .ExpectConfigIn(ZX_OK, GPIO_PULL_UP);

  auto fake_parent = MockDevice::FakeRootParent();
  Gt92xxTest device(i2c.GetProto(), intr.GetProto(), reset.GetProto(), fake_parent.get());
  EXPECT_OK(device.Init());
  EXPECT_TRUE(i2c.FirmwareWritten());
  EXPECT_EQ(i2c.CurrentState(), FakeTouchDevice::kReady);

  ASSERT_NO_FATAL_FAILURES(reset.VerifyAndClear());
  ASSERT_NO_FATAL_FAILURES(intr.VerifyAndClear());
}

TEST_F(GoodixTest, FirmwareCurrent) {
  ddk::MockGpio reset;
  ddk::MockGpio intr;
  FakeTouchDevice i2c;

  i2c.SetProductInfo({'9', '2', '9', '3', 0x05, 0x61});

  // Initial reset
  reset.ExpectConfigOut(ZX_OK, 0).ExpectWrite(ZX_OK, 1);
  intr.ExpectConfigOut(ZX_OK, 0)
      .ExpectConfigIn(ZX_OK, GPIO_PULL_UP)
      .ExpectGetInterrupt(ZX_OK, ZX_INTERRUPT_MODE_EDGE_LOW, {});

  auto fake_parent = MockDevice::FakeRootParent();
  Gt92xxTest device(i2c.GetProto(), intr.GetProto(), reset.GetProto(), fake_parent.get());
  EXPECT_OK(device.Init());
  EXPECT_FALSE(i2c.FirmwareWritten());
  EXPECT_EQ(i2c.CurrentState(), FakeTouchDevice::kIdle);

  ASSERT_NO_FATAL_FAILURES(reset.VerifyAndClear());
  ASSERT_NO_FATAL_FAILURES(intr.VerifyAndClear());
}

TEST_F(GoodixTest, FirmwareNotApplicable) {
  ddk::MockGpio reset;
  ddk::MockGpio intr;
  FakeTouchDevice i2c;

  // Wrong product ID
  i2c.SetProductInfo({'9', '2', '9', '5', 0x04, 0x61});

  // Initial reset
  reset.ExpectConfigOut(ZX_OK, 0).ExpectWrite(ZX_OK, 1);
  intr.ExpectConfigOut(ZX_OK, 0)
      .ExpectConfigIn(ZX_OK, GPIO_PULL_UP)
      .ExpectGetInterrupt(ZX_OK, ZX_INTERRUPT_MODE_EDGE_LOW, {});

  auto fake_parent = MockDevice::FakeRootParent();
  Gt92xxTest device(i2c.GetProto(), intr.GetProto(), reset.GetProto(), fake_parent.get());
  EXPECT_OK(device.Init());
  EXPECT_FALSE(i2c.FirmwareWritten());
  EXPECT_EQ(i2c.CurrentState(), FakeTouchDevice::kIdle);

  ASSERT_NO_FATAL_FAILURES(reset.VerifyAndClear());
  ASSERT_NO_FATAL_FAILURES(intr.VerifyAndClear());
}

TEST_F(GoodixTest, ForceFirmwareUpdate) {
  ddk::MockGpio reset;
  ddk::MockGpio intr;
  FakeTouchDevice i2c;

  // Wrong product ID
  i2c.SetProductInfo({'9', '2', '9', '5', 0x04, 0x61});

  // Send an unknown firmware message so that the product ID/version check is skipped
  i2c.SetFirmwareMessageInvalid();

  // Initial reset
  reset.ExpectConfigOut(ZX_OK, 0).ExpectWrite(ZX_OK, 1);
  intr.ExpectConfigOut(ZX_OK, 0)
      .ExpectConfigIn(ZX_OK, GPIO_PULL_UP)
      .ExpectGetInterrupt(ZX_OK, ZX_INTERRUPT_MODE_EDGE_LOW, {});

  // Entering update mode
  reset.ExpectConfigOut(ZX_OK, 0).ExpectConfigOut(ZX_OK, 1);
  intr.ExpectConfigOut(ZX_OK, 0);

  // Leaving update mode
  reset.ExpectConfigOut(ZX_OK, 0).ExpectConfigOut(ZX_OK, 1).ExpectConfigIn(ZX_OK, 0);
  intr.ExpectConfigIn(ZX_OK, GPIO_PULL_UP)
      .ExpectConfigOut(ZX_OK, 0)
      .ExpectConfigOut(ZX_OK, 0)
      .ExpectConfigIn(ZX_OK, GPIO_PULL_UP);

  auto fake_parent = MockDevice::FakeRootParent();
  Gt92xxTest device(i2c.GetProto(), intr.GetProto(), reset.GetProto(), fake_parent.get());
  EXPECT_OK(device.Init());
  EXPECT_TRUE(i2c.FirmwareWritten());
  EXPECT_EQ(i2c.CurrentState(), FakeTouchDevice::kReady);

  ASSERT_NO_FATAL_FAILURES(reset.VerifyAndClear());
  ASSERT_NO_FATAL_FAILURES(intr.VerifyAndClear());
}

TEST_F(GoodixTest, BadFirmwareChecksum) {
  ddk::MockGpio reset;
  ddk::MockGpio intr;
  FakeTouchDevice i2c;

  // Initial reset
  reset.ExpectConfigOut(ZX_OK, 0).ExpectWrite(ZX_OK, 1);
  intr.ExpectConfigOut(ZX_OK, 0).ExpectConfigIn(ZX_OK, GPIO_PULL_UP);

  corrupt_firmware_checksum = true;

  auto fake_parent = MockDevice::FakeRootParent();
  Gt92xxTest device(i2c.GetProto(), intr.GetProto(), reset.GetProto(), fake_parent.get());
  EXPECT_NOT_OK(device.Init());
  EXPECT_FALSE(i2c.FirmwareWritten());
  EXPECT_EQ(i2c.CurrentState(), FakeTouchDevice::kIdle);

  ASSERT_NO_FATAL_FAILURES(reset.VerifyAndClear());
  ASSERT_NO_FATAL_FAILURES(intr.VerifyAndClear());
}

TEST_F(GoodixTest, ReadbackCheckFail) {
  ddk::MockGpio reset;
  ddk::MockGpio intr;
  FakeTouchDevice i2c;

  i2c.SetCorruptSectionRead();

  // Initial reset
  reset.ExpectConfigOut(ZX_OK, 0).ExpectWrite(ZX_OK, 1);
  intr.ExpectConfigOut(ZX_OK, 0).ExpectConfigIn(ZX_OK, GPIO_PULL_UP);

  // Entering update mode
  reset.ExpectConfigOut(ZX_OK, 0).ExpectConfigOut(ZX_OK, 1);
  intr.ExpectConfigOut(ZX_OK, 0);

  auto fake_parent = MockDevice::FakeRootParent();
  Gt92xxTest device(i2c.GetProto(), intr.GetProto(), reset.GetProto(), fake_parent.get());
  EXPECT_NOT_OK(device.Init());
  EXPECT_TRUE(i2c.FirmwareWritten());
  EXPECT_EQ(i2c.CurrentState(), FakeTouchDevice::kReadingDspIsp);

  ASSERT_NO_FATAL_FAILURES(reset.VerifyAndClear());
  ASSERT_NO_FATAL_FAILURES(intr.VerifyAndClear());
}

}  // namespace goodix
