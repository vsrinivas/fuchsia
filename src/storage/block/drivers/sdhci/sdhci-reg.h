// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_BLOCK_DRIVERS_SDHCI_SDHCI_REG_H_
#define SRC_STORAGE_BLOCK_DRIVERS_SDHCI_SDHCI_REG_H_

#include <hwreg/bitfields.h>

namespace sdhci {

constexpr size_t kRegisterSetSize = 256;

class BlockSize : public hwreg::RegisterBase<BlockSize, uint16_t> {
 public:
  static auto Get() { return hwreg::RegisterAddr<BlockSize>(0x04); }
};

class BlockCount : public hwreg::RegisterBase<BlockCount, uint16_t> {
 public:
  static auto Get() { return hwreg::RegisterAddr<BlockCount>(0x06); }
};

class Argument : public hwreg::RegisterBase<Argument, uint32_t> {
 public:
  static auto Get() { return hwreg::RegisterAddr<Argument>(0x08); }
};

class TransferMode : public hwreg::RegisterBase<TransferMode, uint16_t> {
 public:
  static constexpr uint16_t kAutoCmdDisable = 0b00;
  static constexpr uint16_t kAutoCmd12 = 0b01;
  static constexpr uint16_t kAutoCmd23 = 0b10;
  static constexpr uint16_t kAutoCmdAutoSelect = 0b11;

  static auto Get() { return hwreg::RegisterAddr<TransferMode>(0x0c); }

  DEF_BIT(5, multi_block);
  DEF_BIT(4, read);
  DEF_FIELD(3, 2, auto_cmd_enable);
  DEF_BIT(1, block_count_enable);
  DEF_BIT(0, dma_enable);
};

class Command : public hwreg::RegisterBase<Command, uint16_t> {
 public:
  static constexpr uint16_t kResponseTypeNone = 0b00;
  static constexpr uint16_t kResponseType136Bits = 0b01;
  static constexpr uint16_t kResponseType48Bits = 0b10;
  static constexpr uint16_t kResponseType48BitsWithBusy = 0b11;

  static constexpr uint16_t kCommandTypeNormal = 0b00;
  static constexpr uint16_t kCommandTypeSuspend = 0b01;
  static constexpr uint16_t kCommandTypeResume = 0b10;
  static constexpr uint16_t kCommandTypeAbort = 0b11;

  static auto Get() { return hwreg::RegisterAddr<Command>(0x0e); }

  DEF_FIELD(13, 8, command_index);
  DEF_FIELD(7, 6, command_type);
  DEF_BIT(5, data_present);
  DEF_BIT(4, command_index_check);
  DEF_BIT(3, command_crc_check);
  DEF_FIELD(1, 0, response_type);
};

class Response : public hwreg::RegisterBase<Response, uint32_t> {
 public:
  static auto Get(uint32_t index) { return hwreg::RegisterAddr<Response>(0x10 + (index * 4)); }
};

class BufferData : public hwreg::RegisterBase<BufferData, uint32_t> {
 public:
  static auto Get() { return hwreg::RegisterAddr<BufferData>(0x20); }
};

class PresentState : public hwreg::RegisterBase<PresentState, uint32_t> {
 public:
  static auto Get() { return hwreg::RegisterAddr<PresentState>(0x24); }

  DEF_FIELD(23, 20, dat_3_0);
  DEF_FIELD(7, 4, dat_7_4);
  DEF_BIT(1, command_inhibit_dat);
  DEF_BIT(0, command_inhibit_cmd);
};

class HostControl1 : public hwreg::RegisterBase<HostControl1, uint8_t> {
 public:
  static constexpr uint8_t kDmaSelect32BitAdma2 = 0b10;
  static constexpr uint8_t kDmaSelect64BitAdma2 = 0b11;

  static auto Get() { return hwreg::RegisterAddr<HostControl1>(0x28); }

  DEF_BIT(5, extended_data_transfer_width);
  DEF_FIELD(4, 3, dma_select);
  DEF_BIT(2, high_speed_enable);
  DEF_BIT(1, data_transfer_width_4bit);
};

class PowerControl : public hwreg::RegisterBase<PowerControl, uint8_t> {
 public:
  static constexpr uint8_t kBusVoltage3V3 = 0b111;
  static constexpr uint8_t kBusVoltage1V8 = 0b101;

  static auto Get() { return hwreg::RegisterAddr<PowerControl>(0x29); }

  DEF_FIELD(3, 1, sd_bus_voltage_vdd1);
  DEF_BIT(0, sd_bus_power_vdd1);
};

class ClockControl : public hwreg::RegisterBase<ClockControl, uint16_t> {
 public:
  static constexpr uint16_t kMaxFrequencySelect = 0x3ff;

  static auto Get() { return hwreg::RegisterAddr<ClockControl>(0x2c); }

  uint16_t frequency_select() {
    return static_cast<uint16_t>(frequency_select_lower_8() | (frequency_select_upper_2() << 8));
  }

  auto& set_frequency_select(uint16_t value) {
    return set_frequency_select_lower_8(value & 0xff)
        .set_frequency_select_upper_2((value >> 8) & 3);
  }

  DEF_FIELD(15, 8, frequency_select_lower_8);
  DEF_FIELD(7, 6, frequency_select_upper_2);
  DEF_BIT(2, sd_clock_enable);
  DEF_BIT(1, internal_clock_stable);
  DEF_BIT(0, internal_clock_enable);
};

class TimeoutControl : public hwreg::RegisterBase<TimeoutControl, uint8_t> {
 public:
  static constexpr uint8_t kDataTimeoutMax = 0b1110;

  static auto Get() { return hwreg::RegisterAddr<TimeoutControl>(0x2e); }

  DEF_FIELD(3, 0, data_timeout_counter);
};

class SoftwareReset : public hwreg::RegisterBase<SoftwareReset, uint8_t> {
 public:
  static auto Get() { return hwreg::RegisterAddr<SoftwareReset>(0x2f); }

  DEF_BIT(2, reset_dat);
  DEF_BIT(1, reset_cmd);
  DEF_BIT(0, reset_all);
};

class InterruptStatus : public hwreg::RegisterBase<InterruptStatus, uint32_t> {
 public:
  static auto Get() { return hwreg::RegisterAddr<InterruptStatus>(0x30); }

  auto& ClearAll() { return set_reg_value(0xffff'ffff); }

  bool ErrorInterrupt() {
    return tuning_error() || adma_error() || auto_cmd_error() || current_limit_error() ||
           data_end_bit_error() || data_crc_error() || data_timeout_error() ||
           command_index_error() || command_end_bit_error() || command_crc_error() ||
           command_timeout_error() || error();
  }

  DEF_BIT(26, tuning_error);
  DEF_BIT(25, adma_error);
  DEF_BIT(24, auto_cmd_error);
  DEF_BIT(23, current_limit_error);
  DEF_BIT(22, data_end_bit_error);
  DEF_BIT(21, data_crc_error);
  DEF_BIT(20, data_timeout_error);
  DEF_BIT(19, command_index_error);
  DEF_BIT(18, command_end_bit_error);
  DEF_BIT(17, command_crc_error);
  DEF_BIT(16, command_timeout_error);
  DEF_BIT(15, error);
  DEF_BIT(8, card_interrupt);
  DEF_BIT(5, buffer_read_ready);
  DEF_BIT(4, buffer_write_ready);
  DEF_BIT(1, transfer_complete);
  DEF_BIT(0, command_complete);
};

class InterruptStatusEnable : public hwreg::RegisterBase<InterruptStatusEnable, uint32_t> {
 public:
  static auto Get() { return hwreg::RegisterAddr<InterruptStatusEnable>(0x34); }

  auto& EnableErrorInterrupts() {
    return set_tuning_error(1)
        .set_adma_error(1)
        .set_auto_cmd_error(1)
        .set_current_limit_error(1)
        .set_data_end_bit_error(1)
        .set_data_crc_error(1)
        .set_data_timeout_error(1)
        .set_command_index_error(1)
        .set_command_end_bit_error(1)
        .set_command_crc_error(1)
        .set_command_timeout_error(1)
        .set_error(1);
  }

  auto& EnableNormalInterrupts() {
    return set_card_interrupt(1)
        .set_buffer_read_ready(1)
        .set_buffer_write_ready(1)
        .set_transfer_complete(1)
        .set_command_complete(1);
  }

  DEF_BIT(26, tuning_error);
  DEF_BIT(25, adma_error);
  DEF_BIT(24, auto_cmd_error);
  DEF_BIT(23, current_limit_error);
  DEF_BIT(22, data_end_bit_error);
  DEF_BIT(21, data_crc_error);
  DEF_BIT(20, data_timeout_error);
  DEF_BIT(19, command_index_error);
  DEF_BIT(18, command_end_bit_error);
  DEF_BIT(17, command_crc_error);
  DEF_BIT(16, command_timeout_error);
  DEF_BIT(15, error);
  DEF_BIT(8, card_interrupt);
  DEF_BIT(5, buffer_read_ready);
  DEF_BIT(4, buffer_write_ready);
  DEF_BIT(1, transfer_complete);
  DEF_BIT(0, command_complete);
};

class InterruptSignalEnable : public hwreg::RegisterBase<InterruptSignalEnable, uint32_t> {
 public:
  static auto Get() { return hwreg::RegisterAddr<InterruptSignalEnable>(0x38); }

  auto& MaskAll() { return set_reg_value(0x0000'0000); }

  auto& EnableErrorInterrupts() {
    return set_tuning_error(1)
        .set_adma_error(1)
        .set_auto_cmd_error(1)
        .set_current_limit_error(1)
        .set_data_end_bit_error(1)
        .set_data_crc_error(1)
        .set_data_timeout_error(1)
        .set_command_index_error(1)
        .set_command_end_bit_error(1)
        .set_command_crc_error(1)
        .set_command_timeout_error(1)
        .set_error(1);
  }

  auto& EnableNormalInterrupts() {
    return set_card_interrupt(1)
        .set_buffer_read_ready(1)
        .set_buffer_write_ready(1)
        .set_transfer_complete(1)
        .set_command_complete(1);
  }

  DEF_BIT(26, tuning_error);
  DEF_BIT(25, adma_error);
  DEF_BIT(24, auto_cmd_error);
  DEF_BIT(23, current_limit_error);
  DEF_BIT(22, data_end_bit_error);
  DEF_BIT(21, data_crc_error);
  DEF_BIT(20, data_timeout_error);
  DEF_BIT(19, command_index_error);
  DEF_BIT(18, command_end_bit_error);
  DEF_BIT(17, command_crc_error);
  DEF_BIT(16, command_timeout_error);
  DEF_BIT(15, error);
  DEF_BIT(8, card_interrupt);
  DEF_BIT(5, buffer_read_ready);
  DEF_BIT(4, buffer_write_ready);
  DEF_BIT(1, transfer_complete);
  DEF_BIT(0, command_complete);
};

class HostControl2 : public hwreg::RegisterBase<HostControl2, uint16_t> {
 public:
  static constexpr uint16_t kUhsModeSdr12 = 0b000;
  static constexpr uint16_t kUhsModeSdr25 = 0b001;
  static constexpr uint16_t kUhsModeSdr50 = 0b010;
  static constexpr uint16_t kUhsModeSdr104 = 0b011;
  static constexpr uint16_t kUhsModeDdr50 = 0b100;
  // Note: this is not standard and may not match all controllers.
  static constexpr uint16_t kUhsModeHs400 = 0b101;

  static auto Get() { return hwreg::RegisterAddr<HostControl2>(0x3e); }

  DEF_BIT(7, use_tuned_clock);
  DEF_BIT(6, execute_tuning);
  DEF_BIT(3, voltage_1v8_signalling_enable);
  DEF_FIELD(2, 0, uhs_mode_select);
};

class Capabilities0 : public hwreg::RegisterBase<Capabilities0, uint32_t> {
 public:
  static auto Get() { return hwreg::RegisterAddr<Capabilities0>(0x40); }

  uint32_t base_clock_frequency_hz() { return base_clock_frequency() * kMhzToHz; }

  DEF_BIT(28, v3_64_bit_system_address_support);
  DEF_BIT(26, voltage_1v8_support);
  DEF_BIT(24, voltage_3v3_support);
  DEF_BIT(19, adma2_support);
  DEF_BIT(18, bus_width_8_support);
  DEF_FIELD(15, 8, base_clock_frequency);

 private:
  static constexpr uint32_t kMhzToHz = 1'000'000;
};

class Capabilities1 : public hwreg::RegisterBase<Capabilities1, uint32_t> {
 public:
  static auto Get() { return hwreg::RegisterAddr<Capabilities1>(0x44); }

  DEF_BIT(13, use_tuning_for_sdr50);
  DEF_BIT(2, ddr50_support);
  DEF_BIT(1, sdr104_support);
  DEF_BIT(0, sdr50_support);
};

class AdmaErrorStatus : public hwreg::RegisterBase<AdmaErrorStatus, uint8_t> {
 public:
  static auto Get() { return hwreg::RegisterAddr<AdmaErrorStatus>(0x54); }
};

class AdmaSystemAddress : public hwreg::RegisterBase<AdmaSystemAddress, uint32_t> {
 public:
  static auto Get(uint32_t index) {
    return hwreg::RegisterAddr<AdmaSystemAddress>(0x58 + (index * 4));
  }
};

class Adma2DescriptorAttributes : public hwreg::RegisterBase<Adma2DescriptorAttributes, uint16_t> {
 public:
  static constexpr uint16_t kTypeData = 0b10;

  static auto Get(uint16_t value = 0) {
    Adma2DescriptorAttributes ret;
    ret.set_reg_value(value);
    return ret;
  }

  DEF_RSVDZ_FIELD(15, 6);
  DEF_FIELD(5, 4, type);
  DEF_RSVDZ_BIT(3);
  DEF_BIT(2, intr);
  DEF_BIT(1, end);
  DEF_BIT(0, valid);
};

class HostControllerVersion : public hwreg::RegisterBase<HostControllerVersion, uint16_t> {
 public:
  static constexpr uint16_t kSpecificationVersion300 = 0x02;

  static auto Get() { return hwreg::RegisterAddr<HostControllerVersion>(0xfe); }

  DEF_FIELD(7, 0, specification_version);
};

}  // namespace sdhci

#endif  // SRC_STORAGE_BLOCK_DRIVERS_SDHCI_SDHCI_REG_H_
