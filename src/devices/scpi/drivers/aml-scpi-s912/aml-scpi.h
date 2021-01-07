// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_SCPI_DRIVERS_AML_SCPI_S912_AML_SCPI_H_
#define SRC_DEVICES_SCPI_DRIVERS_AML_SCPI_S912_AML_SCPI_H_

#include <fuchsia/hardware/mailbox/cpp/banjo.h>
#include <fuchsia/hardware/scpi/cpp/banjo.h>
#include <fuchsia/hardware/thermal/c/fidl.h>
#include <lib/device-protocol/pdev.h>
#include <lib/device-protocol/platform-device.h>
#include <lib/mmio/mmio.h>
#include <lib/sync/completion.h>
#include <threads.h>

#include <optional>

#include <ddk/debug.h>
#include <ddk/platform-defs.h>
#include <ddktl/device.h>
#include <hw/reg.h>

#define SCPI_ERROR(fmt, ...) zxlogf(ERROR, "[%s %d]" fmt, __func__, __LINE__, ##__VA_ARGS__)
#define SCPI_INFO(fmt, ...) zxlogf(INFO, "[%s %d]" fmt, __func__, __LINE__, ##__VA_ARGS__)
#define VALID_CMD(cmd) (cmd > SCPI_CMD_INVALID && cmd < SCPI_CMD_MAX)

#define CMD_ID_SHIFT 0
#define CMD_ID_MASK 0xff
#define CMD_SENDER_ID_SHIFT 8
#define CMD_SENDER_ID_MASK 0xff
#define CMD_DATA_SIZE_SHIFT 20
#define CMD_DATA_SIZE_MASK 0x1ff
#define PACK_SCPI_CMD(cmd, sender, txsz)                    \
  ((((cmd)&CMD_ID_MASK) << CMD_ID_SHIFT) |                  \
   (((sender)&CMD_SENDER_ID_MASK) << CMD_SENDER_ID_SHIFT) | \
   (((txsz)&CMD_DATA_SIZE_MASK) << CMD_DATA_SIZE_SHIFT))

namespace scpi {

class AmlSCPI;
using DeviceType = ddk::Device<AmlSCPI, ddk::Unbindable>;

class AmlSCPI : public DeviceType, public ddk::ScpiProtocol<AmlSCPI, ddk::base_protocol> {
 public:
  DISALLOW_COPY_AND_ASSIGN_ALLOW_MOVE(AmlSCPI);

  explicit AmlSCPI(zx_device_t* parent) : DeviceType(parent), mailbox_(parent) {}

  static zx_status_t Create(zx_device_t* parent);

  // DDK Hooks.
  void DdkRelease();
  void DdkUnbind(ddk::UnbindTxn txn);

  // ZX_PROTOCOL_SCPI protocol.
  zx_status_t ScpiGetSensor(const char* name, uint32_t* out_sensor_id);
  zx_status_t ScpiGetSensorValue(uint32_t sensor_id, uint32_t* out_sensor_value);
  zx_status_t ScpiGetDvfsInfo(uint8_t power_domain, scpi_opp_t* out_opps);
  zx_status_t ScpiGetDvfsIdx(uint8_t power_domain, uint16_t* out_index);
  zx_status_t ScpiSetDvfsIdx(uint8_t power_domain, uint16_t index);

 private:
  zx_status_t GetMailbox(uint32_t cmd, mailbox_type_t* mailbox);
  zx_status_t ExecuteCommand(void* rx_buf, size_t rx_size, void* tx_buf, size_t tx_size,
                             uint32_t cmd, uint32_t client_id);
  zx_status_t Bind();

  enum {
    SCPI_CL_NONE,
    SCPI_CL_CLOCKS,
    SCPI_CL_DVFS,
    SCPI_CL_POWER,
    SCPI_CL_THERMAL,
    SCPI_CL_REMOTE,
    SCPI_CL_LED_TIMER,
    SCPI_MAX,
  };

  enum {
    SCPI_CMD_INVALID = 0x00,
    SCPI_CMD_SCPI_READY = 0x01,
    SCPI_CMD_SCPI_CAPABILITIES = 0x02,
    SCPI_CMD_EVENT = 0x03,
    SCPI_CMD_SET_CSS_PWR_STATE = 0x04,
    SCPI_CMD_GET_CSS_PWR_STATE = 0x05,
    SCPI_CMD_CFG_PWR_STATE_STAT = 0x06,
    SCPI_CMD_GET_PWR_STATE_STAT = 0x07,
    SCPI_CMD_SYS_PWR_STATE = 0x08,
    SCPI_CMD_L2_READY = 0x09,
    SCPI_CMD_SET_AP_TIMER = 0x0a,
    SCPI_CMD_CANCEL_AP_TIME = 0x0b,
    SCPI_CMD_DVFS_CAPABILITIES = 0x0c,
    SCPI_CMD_GET_DVFS_INFO = 0x0d,
    SCPI_CMD_SET_DVFS = 0x0e,
    SCPI_CMD_GET_DVFS = 0x0f,
    SCPI_CMD_GET_DVFS_STAT = 0x10,
    SCPI_CMD_SET_RTC = 0x11,
    SCPI_CMD_GET_RTC = 0x12,
    SCPI_CMD_CLOCK_CAPABILITIES = 0x13,
    SCPI_CMD_SET_CLOCK_INDEX = 0x14,
    SCPI_CMD_SET_CLOCK_VALUE = 0x15,
    SCPI_CMD_GET_CLOCK_VALUE = 0x16,
    SCPI_CMD_PSU_CAPABILITIES = 0x17,
    SCPI_CMD_SET_PSU = 0x18,
    SCPI_CMD_GET_PSU = 0x19,
    SCPI_CMD_SENSOR_CAPABILITIES = 0x1a,
    SCPI_CMD_SENSOR_INFO = 0x1b,
    SCPI_CMD_SENSOR_VALUE = 0x1c,
    SCPI_CMD_SENSOR_CFG_PERIODIC = 0x1d,
    SCPI_CMD_SENSOR_CFG_BOUNDS = 0x1e,
    SCPI_CMD_SENSOR_ASYNC_VALUE = 0x1f,
    SCPI_CMD_SET_USR_DATA = 0x20,
    SCPI_CMD_MAX = 0x21,
  };

  static constexpr uint32_t aml_high_priority_cmds[] = {
      SCPI_CMD_GET_DVFS,
      SCPI_CMD_SET_DVFS,
      SCPI_CMD_SET_CLOCK_VALUE,
  };

  static constexpr uint32_t aml_low_priority_cmds[] = {
      SCPI_CMD_GET_DVFS_INFO,
      SCPI_CMD_SENSOR_CAPABILITIES,
      SCPI_CMD_SENSOR_INFO,
      SCPI_CMD_SENSOR_VALUE,
  };

  static constexpr uint32_t aml_secure_cmds[] = {
      SCPI_CMD_SET_CSS_PWR_STATE,
      SCPI_CMD_SYS_PWR_STATE,
  };

  ddk::MailboxProtocolClient mailbox_;
  mtx_t lock_;
  scpi_opp_t* scpi_opp[fuchsia_hardware_thermal_MAX_DVFS_DOMAINS] = {};
};

}  // namespace scpi

#endif  // SRC_DEVICES_SCPI_DRIVERS_AML_SCPI_S912_AML_SCPI_H_
