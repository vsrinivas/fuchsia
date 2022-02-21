// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_INTERNAL_DRIVERS_FRAGMENT_PROXY_PROTOCOL_H_
#define SRC_DEVICES_INTERNAL_DRIVERS_FRAGMENT_PROXY_PROTOCOL_H_

#include <fuchsia/hardware/amlogiccanvas/c/banjo.h>
#include <fuchsia/hardware/pci/c/banjo.h>
#include <fuchsia/hardware/platform/device/c/banjo.h>
#include <fuchsia/hardware/power/c/banjo.h>
#include <fuchsia/hardware/pwm/c/banjo.h>
#include <fuchsia/hardware/tee/c/banjo.h>
#include <fuchsia/hardware/usb/modeswitch/c/banjo.h>
#include <fuchsia/hardware/vreg/c/banjo.h>

#include "src/devices/bus/drivers/pci/proxy_rpc.h"

namespace fragment {

// Maximum transfer size we can proxy.
static constexpr size_t kProxyMaxTransferSize = 4096;

/// Header for RPC requests.
struct ProxyRequest {
  uint32_t txid;
  uint32_t proto_id;
};

/// Header for RPC responses.
struct ProxyResponse {
  uint32_t txid;
  zx_status_t status;
};

// ZX_PROTOCOL_PDEV proxy support.
enum class PdevOp {
  GET_MMIO,
  GET_INTERRUPT,
  GET_BTI,
  GET_SMC,
  GET_DEVICE_INFO,
  GET_BOARD_INFO,
};

struct PdevProxyRequest {
  ProxyRequest header;
  PdevOp op;
  uint32_t index;
  uint32_t flags;
};

struct PdevProxyResponse {
  ProxyResponse header;
  zx_off_t offset;
  size_t size;
  uint32_t flags;
  pdev_device_info_t device_info;
  pdev_board_info_t board_info;
};

// Maximum metadata size that can be returned via PDEV_DEVICE_GET_METADATA.
static constexpr uint32_t PROXY_MAX_METADATA_SIZE =
    (kProxyMaxTransferSize - sizeof(PdevProxyResponse));

struct rpc_pdev_metadata_rsp_t {
  PdevProxyResponse pdev;
  uint8_t metadata[PROXY_MAX_METADATA_SIZE];
};

// ZX_PROTOCOL_GPIO proxy support.
enum class GpioOp {
  CONFIG_IN,
  CONFIG_OUT,
  SET_ALT_FUNCTION,
  READ,
  WRITE,
  GET_INTERRUPT,
  RELEASE_INTERRUPT,
  SET_POLARITY,
  SET_DRIVE_STRENGTH,
};

struct GpioProxyRequest {
  ProxyRequest header;
  GpioOp op;
  uint32_t flags;
  uint32_t polarity;
  uint64_t alt_function;
  uint8_t value;
  uint64_t ds_ua;
};

struct GpioProxyResponse {
  ProxyResponse header;
  uint8_t value;
  uint64_t out_actual_ds_ua;
};

// ZX_PROTOCOL_HDMI proxy support.
enum class HdmiOp {
  CONNECT,
};

struct HdmiProxyRequest {
  ProxyRequest header;
  HdmiOp op;
};

struct HdmiProxyResponse {
  ProxyResponse header;
};

// ZX_PROTOCOL_CODEC proxy support.
enum class CodecOp {
  GET_CHANNEL,
};

struct CodecProxyRequest {
  ProxyRequest header;
  CodecOp op;
};

struct CodecProxyResponse {
  ProxyResponse header;
};

// ZX_PROTOCOL_DAI proxy support.
enum class DaiOp {
  GET_CHANNEL,
};

struct DaiProxyRequest {
  ProxyRequest header;
  DaiOp op;
};

struct DaiProxyResponse {
  ProxyResponse header;
};

// ZX_PROTOCOL_CLOCK proxy support.
enum class ClockOp {
  ENABLE,
  DISABLE,
  IS_ENABLED,
  SET_RATE,
  QUERY_SUPPORTED_RATE,
  GET_RATE,
  SET_INPUT,
  GET_NUM_INPUTS,
  GET_INPUT,
};

struct ClockProxyRequest {
  ProxyRequest header;
  ClockOp op;
  uint64_t rate;
  uint32_t input_idx;
};

struct ClockProxyResponse {
  ProxyResponse header;
  bool is_enabled;
  uint64_t rate;
  uint32_t num_inputs;
  uint32_t current_input;
};

// ZX_PROTOCOL_POWER proxy support.
enum class PowerOp {
  REGISTER,
  UNREGISTER,
  GET_STATUS,
  GET_SUPPORTED_VOLTAGE_RANGE,
  REQUEST_VOLTAGE,
  GET_CURRENT_VOLTAGE,
  WRITE_PMIC_CTRL_REG,
  READ_PMIC_CTRL_REG,
};

struct PowerProxyRequest {
  ProxyRequest header;
  PowerOp op;
  uint32_t set_voltage;
  uint32_t reg_addr;
  uint32_t reg_value;
  uint32_t min_voltage;
  uint32_t max_voltage;
};

struct PowerProxyResponse {
  ProxyResponse header;
  power_domain_status_t status;
  uint32_t min_voltage;
  uint32_t max_voltage;
  uint32_t actual_voltage;
  uint32_t current_voltage;
  uint32_t reg_value;
};

// ZX_PROTOCOL_PWM proxy support.
enum class PwmOp {
  GET_CONFIG,
  SET_CONFIG,
  ENABLE,
  DISABLE,
};

constexpr uint32_t kPwmProxyRequestPadding = 12;
static constexpr uint32_t MAX_MODE_CFG_SIZE = kProxyMaxTransferSize - sizeof(pwm_config_t) -
                                              sizeof(ProxyRequest) - sizeof(PwmOp) -
                                              kPwmProxyRequestPadding;
struct PwmProxyRequest {
  ProxyRequest header;
  PwmOp op;
  pwm_config_t config;
  uint8_t mode_cfg[MAX_MODE_CFG_SIZE];
};
static_assert(sizeof(PwmProxyRequest) < kProxyMaxTransferSize);

struct PwmProxyResponse {
  ProxyResponse header;
  pwm_config_t config;
  uint8_t mode_cfg[MAX_MODE_CFG_SIZE];
};
static_assert(sizeof(PwmProxyResponse) < kProxyMaxTransferSize);

// ZX_PROTOCOL_SYSMEM proxy support.
enum class SysmemOp {
  CONNECT,
  REGISTER_HEAP,
  REGISTER_SECURE_MEM,
  UNREGISTER_SECURE_MEM,
};

struct SysmemProxyRequest {
  ProxyRequest header;
  SysmemOp op;
  uint64_t heap;
};

// ZX_PROTOCOL_TEE proxy support.
enum class TeeOp {
  CONNECT_TO_APPLICATION,
};

struct TeeProxyRequest {
  ProxyRequest header;
  TeeOp op;
  uuid_t application_uuid;
};

// ZX_PROTOCOL_VREG proxy support.
enum class VregOp {
  SET_VOLTAGE_STEP,
  GET_VOLTAGE_STEP,
  GET_REGULATOR_PARAMS,
};

struct VregProxyRequest {
  ProxyRequest header;
  VregOp op;
  uint32_t step;
};

struct VregProxyResponse {
  ProxyResponse header;
  vreg_params_t params;
  uint32_t step;
};

// ZX_PROTOCOL_AMLOGIC_CANVAS proxy support.
enum class AmlogicCanvasOp {
  CONFIG,
  FREE,
};

struct AmlogicCanvasProxyRequest {
  ProxyRequest header;
  AmlogicCanvasOp op;
  size_t offset;
  canvas_info_t info;
  uint8_t canvas_idx;
};

struct AmlogicCanvasProxyResponse {
  ProxyResponse header;
  uint8_t canvas_idx;
};

// ZX_PROTOCOL_ETH_BOARD proxy support.
enum class EthBoardOp {
  RESET_PHY,
};

struct EthBoardProxyRequest {
  ProxyRequest header;
  EthBoardOp op;
};

// ZX_PROTOCOL_I2C proxy support.
enum class I2cOp {
  TRANSACT,
  GET_MAX_TRANSFER_SIZE,
};

struct I2cProxyRequest {
  ProxyRequest header;
  I2cOp op;
  size_t op_count;
  uint32_t flags;
  uint64_t trace_id;
};

struct I2cProxyResponse {
  ProxyResponse header;
  size_t size;
};

struct I2cProxyOp {
  size_t length;
  bool is_read;
  bool stop;
};

enum class SpiOp {
  TRANSMIT,
  RECEIVE,
  EXCHANGE,
  CONNECT_SERVER,
};

struct SpiProxyRequest {
  ProxyRequest header;
  SpiOp op;
  size_t length;
};

struct SpiProxyResponse {
  ProxyResponse header;
};

// ZX_PROTOCOL_USB_MODE_SWITCH proxy support.
enum class UsbModeSwitchOp {
  SET_MODE,
};

struct UsbModeSwitchProxyRequest {
  ProxyRequest header;
  UsbModeSwitchOp op;
  usb_mode_t mode;
};

// ZX_PROTOCOL_REGISTERS proxy supprot.
enum class RegistersOp {
  CONNECT,
};

struct RegistersProxyRequest {
  ProxyRequest header;
  RegistersOp op;
};

struct RegistersProxyResponse {
  ProxyResponse header;
};

// ZX_PROTOCOL_GOLDFISH_PIPE proxy support.
enum class GoldfishPipeOp {
  CREATE,
  DESTROY,
  SET_EVENT,
  OPEN,
  EXEC,
  GET_BTI,
  CONNECT_SYSMEM,
  REGISTER_SYSMEM_HEAP,
};

struct GoldfishPipeProxyRequest {
  ProxyRequest header;
  GoldfishPipeOp op;
  int32_t id;
  uint64_t heap;
};

struct GoldfishPipeProxyResponse {
  ProxyResponse header;
  int32_t id;
};

// ZX_PROTOCOL_GOLDFISH_SYNC proxy support.
enum class GoldfishSyncOp {
  CREATE_TIMELINE,
};

struct GoldfishSyncProxyRequest {
  ProxyRequest header;
  GoldfishSyncOp op;
};

struct GoldfishSyncProxyResponse {
  ProxyResponse header;
};

// ZX_PROTOCOL_GOLDFISH_ADDRESS_SPACE proxy support.
enum class GoldfishAddressSpaceOp {
  OPEN_CHILD_DRIVER,
};

struct GoldfishAddressSpaceProxyRequest {
  ProxyRequest header;
  GoldfishAddressSpaceOp op;
  uint32_t type;
};

struct GoldfishAddressSpaceProxyResponse {
  ProxyResponse header;
};

enum class DsiOp {
  CONNECT,
};

struct DsiProxyRequest {
  ProxyRequest header;
  DsiOp op;
};

struct PciRpcRequest {
  ProxyRequest header;
  pci::PciRpcOp op;
  union {
    pci::PciMsgBar bar;
    pci::PciMsgCfg cfg;
    pci::PciMsgIrq irq;
    pci::PciMsgCapability cap;
    uint32_t bti_index;
    bool enable;
  };
};

struct PciRpcResponse {
  ProxyResponse header;
  pci::PciRpcOp op;
  union {
    pci::PciMsgBar bar;
    pci::PciMsgCfg cfg;
    pci::PciMsgIrq irq;
    pci::PciMsgDeviceInfo info;
    pci::PciMsgCapability cap;
  };
};

// ZX_PROTOCOL_POWER_SENSOR proxy support.
enum class PowerSensorOp {
  CONNECT_SERVER,
};

struct PowerSensorProxyRequest {
  ProxyRequest header;
  PowerSensorOp op;
};

struct PowerSensorProxyResponse {
  ProxyResponse header;
};

}  // namespace fragment

#endif  // SRC_DEVICES_INTERNAL_DRIVERS_FRAGMENT_PROXY_PROTOCOL_H_
