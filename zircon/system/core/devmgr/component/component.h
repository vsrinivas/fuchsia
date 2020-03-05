// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSTEM_CORE_DEVMGR_COMPONENT_COMPONENT_H_
#define ZIRCON_SYSTEM_CORE_DEVMGR_COMPONENT_COMPONENT_H_

#include <lib/sync/completion.h>
#include <lib/zx/channel.h>

#include <ddk/binding.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddktl/device.h>
#include <ddktl/protocol/amlogiccanvas.h>
#include <ddktl/protocol/buttons.h>
#include <ddktl/protocol/clock.h>
#include <ddktl/protocol/codec.h>
#include <ddktl/protocol/ethernet/board.h>
#include <ddktl/protocol/gpio.h>
#include <ddktl/protocol/i2c.h>
#include <ddktl/protocol/platform/device.h>
#include <ddktl/protocol/power.h>
#include <ddktl/protocol/powerimpl.h>
#include <ddktl/protocol/pwm.h>
#include <ddktl/protocol/spi.h>
#include <ddktl/protocol/sysmem.h>
#include <ddktl/protocol/tee.h>
#include <ddktl/protocol/usb/modeswitch.h>

namespace component {

template <typename ProtoClientType, typename ProtoType>
class ProtocolClient {
 public:
  ProtocolClient() { parent_ = nullptr; }
  ProtocolClient(zx_device_t* parent, uint32_t proto_id);
  ProtoClientType& proto_client() { return proto_client_; }
  ~ProtocolClient() {
    if (is_session_) {
      ZX_DEBUG_ASSERT(device_close_protocol_session_multibindable(parent_, proto_.ctx) == ZX_OK);
    }
  }

 private:
  bool is_session_ = false;
  ProtoType proto_;
  ProtoClientType proto_client_;
  zx_device_t* parent_;
};

class Component;
using ComponentBase =
    ddk::Device<Component, ddk::Rxrpcable, ddk::UnbindableNew, ddk::GetProtocolable>;

class Component : public ComponentBase {
 public:
  explicit Component(zx_device_t* parent)
      : ComponentBase(parent),
        canvas_client_(parent, ZX_PROTOCOL_AMLOGIC_CANVAS),
        buttons_client_(parent, ZX_PROTOCOL_BUTTONS),
        clock_client_(parent, ZX_PROTOCOL_CLOCK),
        eth_board_client_(parent, ZX_PROTOCOL_ETH_BOARD),
        gpio_client_(parent, ZX_PROTOCOL_GPIO),
        i2c_client_(parent, ZX_PROTOCOL_I2C),
        codec_client_(parent, ZX_PROTOCOL_CODEC),
        pdev_client_(parent, ZX_PROTOCOL_PDEV),
        power_client_(parent, ZX_PROTOCOL_POWER),
        pwm_client_(parent, ZX_PROTOCOL_PWM),
        spi_client_(parent, ZX_PROTOCOL_SPI),
        sysmem_client_(parent, ZX_PROTOCOL_SYSMEM),
        tee_client_(parent, ZX_PROTOCOL_TEE),
        ums_client_(parent, ZX_PROTOCOL_USB_MODE_SWITCH),
        power_impl_client_(parent, ZX_PROTOCOL_POWER_IMPL) {}

  static zx_status_t Bind(void* ctx, zx_device_t* parent);

  zx_status_t DdkRxrpc(zx_handle_t channel);
  void DdkUnbindNew(ddk::UnbindTxn txn);
  void DdkRelease();
  zx_status_t DdkGetProtocol(uint32_t proto_id, void* out_protocol);

 private:
  struct I2cTransactContext {
    sync_completion_t completion;
    void* read_buf;
    size_t read_length;
    zx_status_t result;
  };
  struct CodecTransactContext {
    sync_completion_t completion;
    zx_status_t status;
    void* buffer;
    size_t size;
  };
  zx_status_t RpcCanvas(const uint8_t* req_buf, uint32_t req_size, uint8_t* resp_buf,
                        uint32_t* out_resp_size, zx::handle* req_handles, uint32_t req_handle_count,
                        zx::handle* resp_handles, uint32_t* resp_handle_count);
  zx_status_t RpcButtons(const uint8_t* req_buf, uint32_t req_size, uint8_t* resp_buf,
                         uint32_t* out_resp_size, zx::handle* req_handles,
                         uint32_t req_handle_count, zx::handle* resp_handles,
                         uint32_t* resp_handle_count);
  zx_status_t RpcClock(const uint8_t* req_buf, uint32_t req_size, uint8_t* resp_buf,
                       uint32_t* out_resp_size, zx::handle* req_handles, uint32_t req_handle_count,
                       zx::handle* resp_handles, uint32_t* resp_handle_count);
  zx_status_t RpcEthBoard(const uint8_t* req_buf, uint32_t req_size, uint8_t* resp_buf,
                          uint32_t* out_resp_size, zx::handle* req_handles,
                          uint32_t req_handle_count, zx::handle* resp_handles,
                          uint32_t* resp_handle_count);
  zx_status_t RpcGpio(const uint8_t* req_buf, uint32_t req_size, uint8_t* resp_buf,
                      uint32_t* out_resp_size, zx::handle* req_handles, uint32_t req_handle_count,
                      zx::handle* resp_handles, uint32_t* resp_handle_count);
  zx_status_t RpcI2c(const uint8_t* req_buf, uint32_t req_size, uint8_t* resp_buf,
                     uint32_t* out_resp_size, zx::handle* req_handles, uint32_t req_handle_count,
                     zx::handle* resp_handles, uint32_t* resp_handle_count);
  zx_status_t RpcPdev(const uint8_t* req_buf, uint32_t req_size, uint8_t* resp_buf,
                      uint32_t* out_resp_size, zx::handle* req_handles, uint32_t req_handle_count,
                      zx::handle* resp_handles, uint32_t* resp_handle_count);
  zx_status_t RpcPower(const uint8_t* req_buf, uint32_t req_size, uint8_t* resp_buf,
                       uint32_t* out_resp_size, zx::handle* req_handles, uint32_t req_handle_count,
                       zx::handle* resp_handles, uint32_t* resp_handle_count);
  zx_status_t RpcPwm(const uint8_t* req_buf, uint32_t req_size, uint8_t* resp_buf,
                     uint32_t* out_resp_size, zx::handle* req_handles, uint32_t req_handle_count,
                     zx::handle* resp_handles, uint32_t* resp_handle_count);
  zx_status_t RpcSpi(const uint8_t* req_buf, uint32_t req_size, uint8_t* resp_buf,
                     uint32_t* out_resp_size, zx::handle* req_handles, uint32_t req_handle_count,
                     zx::handle* resp_handles, uint32_t* resp_handle_count);
  zx_status_t RpcSysmem(const uint8_t* req_buf, uint32_t req_size, uint8_t* resp_buf,
                        uint32_t* out_resp_size, zx::handle* req_handles, uint32_t req_handle_count,
                        zx::handle* resp_handles, uint32_t* resp_handle_count);
  zx_status_t RpcTee(const uint8_t* req_buf, uint32_t req_size, uint8_t* resp_buf,
                     uint32_t* out_resp_size, zx::handle* req_handles, uint32_t req_handle_count,
                     zx::handle* resp_handles, uint32_t* resp_handle_count);
  zx_status_t RpcUms(const uint8_t* req_buf, uint32_t req_size, uint8_t* resp_buf,
                     uint32_t* out_resp_size, zx::handle* req_handles, uint32_t req_handle_count,
                     zx::handle* resp_handles, uint32_t* resp_handle_count);
  zx_status_t RpcCodec(const uint8_t* req_buf, uint32_t req_size, uint8_t* resp_buf,
                       uint32_t* out_resp_size, zx::handle* req_handles, uint32_t req_handle_count,
                       zx::handle* resp_handles, uint32_t* resp_handle_count);

  static void I2cTransactCallback(void* cookie, zx_status_t status, const i2c_op_t* op_list,
                                  size_t op_count);

  static void CodecTransactCallback(void* cookie, zx_status_t status,
                                    const dai_supported_formats_t* formats_list,
                                    size_t formats_count);
  ProtocolClient<ddk::AmlogicCanvasProtocolClient, amlogic_canvas_protocol_t> canvas_client_;
  ProtocolClient<ddk::ButtonsProtocolClient, buttons_protocol_t> buttons_client_;
  ProtocolClient<ddk::ClockProtocolClient, clock_protocol_t> clock_client_;
  ProtocolClient<ddk::EthBoardProtocolClient, eth_board_protocol_t> eth_board_client_;
  ProtocolClient<ddk::GpioProtocolClient, gpio_protocol_t> gpio_client_;
  ProtocolClient<ddk::I2cProtocolClient, i2c_protocol_t> i2c_client_;
  ProtocolClient<ddk::CodecProtocolClient, codec_protocol_t> codec_client_;
  ProtocolClient<ddk::PDevProtocolClient, pdev_protocol_t> pdev_client_;
  ProtocolClient<ddk::PowerProtocolClient, power_protocol_t> power_client_;
  ProtocolClient<ddk::PwmProtocolClient, pwm_protocol_t> pwm_client_;
  ProtocolClient<ddk::SpiProtocolClient, spi_protocol_t> spi_client_;
  ProtocolClient<ddk::SysmemProtocolClient, sysmem_protocol_t> sysmem_client_;
  ProtocolClient<ddk::TeeProtocolClient, tee_protocol_t> tee_client_;
  ProtocolClient<ddk::UsbModeSwitchProtocolClient, usb_mode_switch_protocol_t> ums_client_;
  ProtocolClient<ddk::PowerImplProtocolClient, power_impl_protocol_t> power_impl_client_;
};

}  // namespace component

#endif  // ZIRCON_SYSTEM_CORE_DEVMGR_COMPONENT_COMPONENT_H_
