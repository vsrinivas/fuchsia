// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_INTERNAL_DRIVERS_FRAGMENT_FRAGMENT_PROXY_H_
#define SRC_DEVICES_INTERNAL_DRIVERS_FRAGMENT_FRAGMENT_PROXY_H_

#include <fuchsia/hardware/amlogiccanvas/cpp/banjo.h>
#include <fuchsia/hardware/audio/cpp/banjo.h>
#include <fuchsia/hardware/buttons/cpp/banjo.h>
#include <fuchsia/hardware/clock/cpp/banjo.h>
#include <fuchsia/hardware/dsi/cpp/banjo.h>
#include <fuchsia/hardware/ethernet/board/cpp/banjo.h>
#include <fuchsia/hardware/goldfish/addressspace/cpp/banjo.h>
#include <fuchsia/hardware/goldfish/pipe/cpp/banjo.h>
#include <fuchsia/hardware/goldfish/sync/cpp/banjo.h>
#include <fuchsia/hardware/gpio/cpp/banjo.h>
#include <fuchsia/hardware/hdmi/cpp/banjo.h>
#include <fuchsia/hardware/i2c/cpp/banjo.h>
#include <fuchsia/hardware/platform/device/cpp/banjo.h>
#include <fuchsia/hardware/power/cpp/banjo.h>
#include <fuchsia/hardware/pwm/cpp/banjo.h>
#include <fuchsia/hardware/registers/cpp/banjo.h>
#include <fuchsia/hardware/rpmb/cpp/banjo.h>
#include <fuchsia/hardware/spi/cpp/banjo.h>
#include <fuchsia/hardware/sysmem/cpp/banjo.h>
#include <fuchsia/hardware/tee/cpp/banjo.h>
#include <fuchsia/hardware/usb/modeswitch/cpp/banjo.h>
#include <fuchsia/hardware/vreg/cpp/banjo.h>
#include <lib/ddk/device.h>
#include <lib/ddk/driver.h>
#include <lib/zx/channel.h>

#include <ddktl/device.h>

#include "proxy-protocol.h"

namespace fragment {

class FragmentProxy;
using FragmentProxyBase = ddk::Device<FragmentProxy, ddk::Unbindable, ddk::GetProtocolable>;

class FragmentProxy : public FragmentProxyBase,
                      public ddk::AmlogicCanvasProtocol<FragmentProxy>,
                      public ddk::ButtonsProtocol<FragmentProxy>,
                      public ddk::ClockProtocol<FragmentProxy>,
                      public ddk::EthBoardProtocol<FragmentProxy>,
                      public ddk::GpioProtocol<FragmentProxy>,
                      public ddk::HdmiProtocol<FragmentProxy>,
                      public ddk::I2cProtocol<FragmentProxy>,
                      public ddk::CodecProtocol<FragmentProxy>,
                      public ddk::DaiProtocol<FragmentProxy>,
                      public ddk::PDevProtocol<FragmentProxy>,
                      public ddk::PowerProtocol<FragmentProxy>,
                      public ddk::PwmProtocol<FragmentProxy>,
                      public ddk::RegistersProtocol<FragmentProxy>,
                      public ddk::RpmbProtocol<FragmentProxy>,
                      public ddk::SpiProtocol<FragmentProxy>,
                      public ddk::SysmemProtocol<FragmentProxy>,
                      public ddk::TeeProtocol<FragmentProxy>,
                      public ddk::UsbModeSwitchProtocol<FragmentProxy>,
                      public ddk::VregProtocol<FragmentProxy>,
                      public ddk::GoldfishAddressSpaceProtocol<FragmentProxy>,
                      public ddk::GoldfishPipeProtocol<FragmentProxy>,
                      public ddk::DsiProtocol<FragmentProxy>,
                      public ddk::GoldfishSyncProtocol<FragmentProxy> {
 public:
  FragmentProxy(zx_device_t* parent, zx::channel rpc)
      : FragmentProxyBase(parent), rpc_(std::move(rpc)) {}

  static zx_status_t Create(void* ctx, zx_device_t* parent, const char* name, const char* args,
                            zx_handle_t raw_rpc);

  zx_status_t DdkGetProtocol(uint32_t, void*);
  void DdkUnbind(ddk::UnbindTxn txn);
  void DdkRelease();

  zx_status_t Rpc(const ProxyRequest* req, size_t req_length, ProxyResponse* resp,
                  size_t resp_length, const zx_handle_t* in_handles, size_t in_handle_count,
                  zx_handle_t* out_handles, size_t out_handle_count, size_t* out_actual);

  zx_status_t Rpc(const ProxyRequest* req, size_t req_length, ProxyResponse* resp,
                  size_t resp_length) {
    return Rpc(req, req_length, resp, resp_length, nullptr, 0, nullptr, 0, nullptr);
  }

  zx_status_t AmlogicCanvasConfig(zx::vmo vmo, size_t offset, const canvas_info_t* info,
                                  uint8_t* out_canvas_idx);
  zx_status_t AmlogicCanvasFree(uint8_t canvas_idx);
  zx_status_t ButtonsGetChannel(zx::channel chan);
  zx_status_t ClockEnable();
  zx_status_t ClockDisable();
  zx_status_t ClockIsEnabled(bool* out_enabled);
  zx_status_t ClockSetRate(uint64_t hz);
  zx_status_t ClockQuerySupportedRate(uint64_t max_rate, uint64_t* out_max_supported_rate);
  zx_status_t ClockGetRate(uint64_t* out_current_rate);
  zx_status_t ClockSetInput(uint32_t idx);
  zx_status_t ClockGetNumInputs(uint32_t* out_num_inputs);
  zx_status_t ClockGetInput(uint32_t* out_current_input);
  zx_status_t EthBoardResetPhy();
  zx_status_t GoldfishAddressSpaceOpenChildDriver(address_space_child_driver_type_t type,
                                                  zx::channel request);
  zx_status_t GoldfishPipeCreate(int32_t* out_id, zx::vmo* out_vmo);
  zx_status_t GoldfishPipeSetEvent(int32_t id, zx::event pipe_event);
  void GoldfishPipeDestroy(int32_t id);
  void GoldfishPipeOpen(int32_t id);
  void GoldfishPipeExec(int32_t id);
  zx_status_t GoldfishPipeGetBti(zx::bti* out_bti);
  zx_status_t GoldfishPipeConnectSysmem(zx::channel connection);
  zx_status_t GoldfishPipeRegisterSysmemHeap(uint64_t heap, zx::channel connection);
  zx_status_t GoldfishSyncCreateTimeline(zx::channel request);
  zx_status_t GpioConfigIn(uint32_t flags);
  zx_status_t GpioConfigOut(uint8_t initial_value);
  zx_status_t GpioSetAltFunction(uint64_t function);
  zx_status_t GpioRead(uint8_t* out_value);
  zx_status_t GpioWrite(uint8_t value);
  zx_status_t GpioGetInterrupt(uint32_t flags, zx::interrupt* out_irq);
  zx_status_t GpioReleaseInterrupt();
  zx_status_t GpioSetPolarity(gpio_polarity_t polarity);
  zx_status_t GpioSetDriveStrength(uint64_t ds_ua, uint64_t* out_actual_ds_ua);
  void HdmiConnect(zx::channel chan);
  void I2cTransact(const i2c_op_t* op_list, size_t op_count, i2c_transact_callback callback,
                   void* cookie);
  zx_status_t I2cGetMaxTransferSize(size_t* out_size);
  zx_status_t I2cGetInterrupt(uint32_t flags, zx::interrupt* out_irq);
  zx_status_t PDevGetMmio(uint32_t index, pdev_mmio_t* out_mmio);
  zx_status_t PDevGetInterrupt(uint32_t index, uint32_t flags, zx::interrupt* out_irq);
  zx_status_t PDevGetBti(uint32_t index, zx::bti* out_bti);
  zx_status_t PDevGetSmc(uint32_t index, zx::resource* out_smc);
  zx_status_t PDevGetDeviceInfo(pdev_device_info_t* out_info);
  zx_status_t PDevGetBoardInfo(pdev_board_info_t* out_info);
  zx_status_t PDevDeviceAdd(uint32_t index, const device_add_args_t* args,
                            zx_device_t** out_device);
  zx_status_t PDevGetProtocol(uint32_t proto_id, uint32_t index, void* out_out_protocol_buffer,
                              size_t out_protocol_size, size_t* out_out_protocol_actual);
  zx_status_t PowerRegisterPowerDomain(uint32_t min_needed_voltage, uint32_t max_supported_voltage);
  zx_status_t PowerUnregisterPowerDomain();
  zx_status_t PowerGetPowerDomainStatus(power_domain_status_t* out_status);
  zx_status_t PowerGetSupportedVoltageRange(uint32_t* min_voltage, uint32_t* max_voltage);
  zx_status_t PowerRequestVoltage(uint32_t _voltage, uint32_t* actual_voltage);
  zx_status_t PowerGetCurrentVoltage(uint32_t index, uint32_t* current_voltage);
  zx_status_t PowerWritePmicCtrlReg(uint32_t reg_addr, uint32_t value);
  zx_status_t PowerReadPmicCtrlReg(uint32_t reg_addr, uint32_t* out_value);
  zx_status_t PwmGetConfig(pwm_config_t* out_config);
  zx_status_t PwmSetConfig(const pwm_config_t* config);
  zx_status_t PwmEnable();
  zx_status_t PwmDisable();
  void RegistersConnect(zx::channel chan);
  void RpmbConnectServer(zx::channel server);
  zx_status_t SpiTransmit(const uint8_t* txdata_list, size_t txdata_count);
  zx_status_t SpiReceive(uint32_t size, uint8_t* out_rxdata_list, size_t rxdata_count,
                         size_t* out_rxdata_actual);
  zx_status_t SpiExchange(const uint8_t* txdata_list, size_t txdata_count, uint8_t* out_rxdata_list,
                          size_t rxdata_count, size_t* out_rxdata_actual);
  void SpiConnectServer(zx::channel server);
  zx_status_t SysmemConnect(zx::channel allocator2_request);
  zx_status_t SysmemRegisterHeap(uint64_t heap, zx::channel heap_connection);
  zx_status_t SysmemRegisterSecureMem(zx::channel tee_connection);
  zx_status_t SysmemUnregisterSecureMem();
  zx_status_t TeeConnectToApplication(const uuid_t* application_uuid, zx::channel tee_app_request,
                                      zx::channel service_provider);
  zx_status_t VregSetVoltageStep(uint32_t step);
  uint32_t VregGetVoltageStep();
  void VregGetRegulatorParams(vreg_params_t* out_params);

  // USB Mode Switch
  zx_status_t UsbModeSwitchSetMode(usb_mode_t mode);

  // DSI
  zx_status_t DsiConnect(zx::channel server);

  zx_status_t CodecConnect(zx::channel chan);
  zx_status_t DaiConnect(zx::channel chan);

 private:
  zx::channel rpc_;
};

}  // namespace fragment

#endif  // SRC_DEVICES_INTERNAL_DRIVERS_FRAGMENT_FRAGMENT_PROXY_H_
