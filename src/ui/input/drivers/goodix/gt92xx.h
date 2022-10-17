// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_INPUT_DRIVERS_GOODIX_GT92XX_H_
#define SRC_UI_INPUT_DRIVERS_GOODIX_GT92XX_H_

#include <fuchsia/hardware/gpio/cpp/banjo.h>
#include <fuchsia/hardware/hidbus/cpp/banjo.h>
#include <lib/ddk/device.h>
#include <lib/device-protocol/i2c-channel.h>
#include <lib/fzl/vmo-mapper.h>
#include <lib/inspect/cpp/inspect.h>
#include <lib/stdcompat/span.h>
#include <lib/zx/interrupt.h>
#include <lib/zx/status.h>
#include <threads.h>
#include <zircon/types.h>

#include <atomic>
#include <utility>

#include <ddktl/device.h>
#include <fbl/mutex.h>
#include <fbl/vector.h>
#include <hid/gt92xx.h>

// clang-format off
#define GT_REG_DSP_CONTROL      0x4010
#define GT_REG_SRAM_BANK        0x4048
#define GT_REG_MEM_CD_ENABLE    0x4049
#define GT_REG_CACHE_ENABLE     0x404b
#define GT_REG_TIMER0_ENABLE    0x40b0
#define GT_REG_SW_RESET         0x4180
#define   GT_HOLD_SS51          0b0100
#define   GT_HOLD_DSP           0b1000
#define GT_REG_CPU_RESET        0x4184
#define GT_REG_BOOTCONTROL_B0   0x4190
#define GT_REG_BOOT_OPTION_B0   0x4218

#define GT_REG_FW_MESSAGE       0x41e4
#define GT_REG_FW_MESSAGE_RETRIES 3

#define GT_REG_HW_INFO          0x4220
#define GT_REG_BOOT_CONTROL     0x5094

#define GT_REG_SLEEP            0x8040
#define GT_REG_CONFIG_DATA      0x8047
#define GT_REG_MAX_X_LO         0x8048
#define GT_REG_MAX_X_HI         0x8049
#define GT_REG_MAX_Y_LO         0x804a
#define GT_REG_MAX_Y_HI         0x804b
#define GT_REG_NUM_FINGERS      0x804c

#define GT_REG_CONFIG_REFRESH   0x812a
#define GT_REG_PRODUCT_INFO     0x8140
#define GT_REG_FW_VERSION       0x8144
#define GT_REG_SENSOR_ID        0x814a
#define GT_REG_TOUCH_STATUS     0x814e
#define GT_REG_REPORTS          0x814f

#define GT_REG_FIRMWARE         0x41e4
#define GT_FIRMWARE_MAGIC       0xbe

#define GT_REG_TOUCH_STATUS_READY   0x80
// clang-format on

namespace goodix {

class Gt92xxDevice : public ddk::Device<Gt92xxDevice, ddk::Unbindable>,
                     public ddk::HidbusProtocol<Gt92xxDevice, ddk::base_protocol> {
 public:
  struct SectionInfo {
    uint16_t address;
    uint8_t sram_bank;
    uint8_t copy_command;
  };

  Gt92xxDevice(zx_device_t* device, ddk::I2cChannel i2c, ddk::GpioProtocolClient intr,
               ddk::GpioProtocolClient reset)
      : ddk::Device<Gt92xxDevice, ddk::Unbindable>(device),
        i2c_(std::move(i2c)),
        int_gpio_(std::move(intr)),
        reset_gpio_(std::move(reset)) {}

  static fbl::Vector<uint8_t> GetConfData();

  static zx_status_t Create(zx_device_t* device);

  void DdkRelease();
  void DdkUnbind(ddk::UnbindTxn txn) __TA_EXCLUDES(client_lock_);

  // Hidbus required methods
  void HidbusStop();
  zx_status_t HidbusGetDescriptor(hid_description_type_t desc_type, uint8_t* out_data_buffer,
                                  size_t data_size, size_t* out_data_actual);
  zx_status_t HidbusGetReport(uint8_t rpt_type, uint8_t rpt_id, uint8_t* data, size_t len,
                              size_t* out_len);
  zx_status_t HidbusSetReport(uint8_t rpt_type, uint8_t rpt_id, const uint8_t* data, size_t len);
  zx_status_t HidbusGetIdle(uint8_t rpt_id, uint8_t* duration);
  zx_status_t HidbusSetIdle(uint8_t rpt_id, uint8_t duration);
  zx_status_t HidbusGetProtocol(uint8_t* protocol);
  zx_status_t HidbusSetProtocol(uint8_t protocol);
  zx_status_t HidbusStart(const hidbus_ifc_protocol_t* ifc) __TA_EXCLUDES(client_lock_);
  zx_status_t HidbusQuery(uint32_t options, hid_info_t* info) __TA_EXCLUDES(client_lock_);

 protected:
  zx_status_t Init();
  std::atomic<bool> running_;
  zx::interrupt irq_;
  int Thread();

 private:
  // Format of data as it is read from the device
  struct FingerReport {
    uint8_t id;
    uint16_t x;
    uint16_t y;
    uint16_t size;
    uint8_t reserved;
  } __PACKED;

  static constexpr uint32_t kMaxPoints = 5;

  static constexpr int kI2cRetries = 5;

  static bool ProductIdsMatch(const uint8_t* firmware_product_id, const uint8_t* chip_product_id);

  zx_status_t ShutDown() __TA_EXCLUDES(client_lock_);

  void LogFirmwareStatus();

  // performs hardware reset using gpio
  void HWReset();

  zx_status_t SetSramBank(uint8_t bank) { return Write(GT_REG_SRAM_BANK, bank); }

  zx_status_t EnableCodeAccess() { return Write(GT_REG_MEM_CD_ENABLE, 1); }
  zx_status_t DisableCodeAccess() { return Write(GT_REG_MEM_CD_ENABLE, 0); }

  zx_status_t DisableCache() { return Write(GT_REG_CACHE_ENABLE, 0); }

  zx_status_t DisableWdt() { return Write(GT_REG_TIMER0_ENABLE, 0); }

  zx_status_t HoldSs51AndDsp() { return Write(GT_REG_SW_RESET, GT_HOLD_SS51 | GT_HOLD_DSP); }
  zx_status_t HoldSs51ReleaseDsp() { return Write(GT_REG_SW_RESET, GT_HOLD_SS51); }
  zx_status_t ReleaseSs51HoldDsp() { return Write(GT_REG_SW_RESET, GT_HOLD_DSP); }
  zx_status_t ReleaseSs51AndDsp() { return Write(GT_REG_SW_RESET, 0); }
  zx::result<bool> Ss51AndDspHeld() {
    auto status = Read(GT_REG_SW_RESET);
    if (status.is_ok()) {
      return zx::ok(status.value() == (GT_HOLD_SS51 | GT_HOLD_DSP));
    }
    return status.take_error();
  }

  zx_status_t TriggerSoftwareReset() { return Write(GT_REG_CPU_RESET, 1); }

  zx_status_t SetBootFromSram() { return Write(GT_REG_BOOTCONTROL_B0, 0b10); }

  zx_status_t SetScramble() { return Write(GT_REG_BOOT_OPTION_B0, 0); }

  zx_status_t WriteCopyCommand(uint8_t command) { return Write(GT_REG_BOOT_CONTROL, command); }
  zx::result<bool> DeviceBusy() {
    auto status = Read(GT_REG_BOOT_CONTROL);
    if (status.is_ok()) {
      return zx::ok(status.value() != 0);
    }
    return status.take_error();
  }

  zx::result<fzl::VmoMapper> LoadAndVerifyFirmware();
  bool IsFirmwareApplicable(const fzl::VmoMapper& firmware_mapper);
  zx_status_t EnterUpdateMode();
  void LeaveUpdateMode();
  zx_status_t WritePayload(uint16_t address, cpp20::span<const uint8_t> data);
  zx_status_t VerifyPayload(uint16_t address, cpp20::span<const uint8_t> data);
  zx_status_t WaitUntilNotBusy();
  zx_status_t WriteDspIsp(cpp20::span<const uint8_t> dsp_isp);
  zx_status_t WriteGwakeOrLinkSection(SectionInfo section_info, cpp20::span<const uint8_t> section);
  zx_status_t WriteGwake(cpp20::span<const uint8_t> section);
  zx_status_t WriteSs51Section(uint8_t section_number, cpp20::span<const uint8_t> section);
  zx_status_t WriteSs51(cpp20::span<const uint8_t> section);
  zx_status_t WriteDsp(cpp20::span<const uint8_t> section);
  zx_status_t WriteBootOrBootIsp(SectionInfo section_info, cpp20::span<const uint8_t> section);
  zx_status_t WriteBoot(cpp20::span<const uint8_t> section);
  zx_status_t WriteBootIsp(cpp20::span<const uint8_t> section);
  zx_status_t WriteLink(cpp20::span<const uint8_t> section);
  zx_status_t WriteLinkSection(uint8_t section_number, cpp20::span<const uint8_t> section);

  zx_status_t UpdateFirmwareIfNeeded();

  zx::result<uint8_t> Read(uint16_t addr);
  zx_status_t Read(uint16_t addr, uint8_t* buf, size_t len);
  zx_status_t Write(uint16_t addr, uint8_t val);
  zx_status_t Write(uint8_t* buf, size_t len);

  ddk::I2cChannel i2c_;
  ddk::GpioProtocolClient int_gpio_;
  ddk::GpioProtocolClient reset_gpio_;

  gt92xx_touch_t gt_rpt_ __TA_GUARDED(client_lock_);
  thrd_t thread_;
  fbl::Mutex client_lock_;
  ddk::HidbusIfcProtocolClient client_ __TA_GUARDED(client_lock_);

  inspect::Inspector inspector_;
  inspect::Node node_;
  inspect::ValueList values_;

  enum {
    kNoFirmware = 0,         // No firmware file was supplied
    kInternalError,          // An internal error was encountered when loading the firmware
    kFirmwareInvalid,        // The firmware file is corrupt or invalid
    kFirmwareNotApplicable,  // The supplied firmware is not applicable to the chip
    kChipFirmwareCurrent,    // The chip firmware is already at the latest version
    kFirmwareUpdateError,    // The chip did something unexpected, or there was an error on the bus
    kFirmwareUpdated,        // The firmware updated completed successfully
    kFirmwareStatusCount,
  } firmware_status_ = kFirmwareUpdateError;
};
}  // namespace goodix

#endif  // SRC_UI_INPUT_DRIVERS_GOODIX_GT92XX_H_
