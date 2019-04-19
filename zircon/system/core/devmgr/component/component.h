// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddk/binding.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddktl/device.h>
#include <ddktl/protocol/amlogiccanvas.h>
#include <ddktl/protocol/clock.h>
#include <ddktl/protocol/ethernet/board.h>
#include <ddktl/protocol/gpio.h>
#include <ddktl/protocol/i2c.h>
#include <ddktl/protocol/mipicsi.h>
#include <ddktl/protocol/platform/device.h>
#include <ddktl/protocol/power.h>
#include <ddktl/protocol/sysmem.h>
#include <ddktl/protocol/usb/modeswitch.h>
#include <lib/sync/completion.h>
#include <lib/zx/channel.h>

namespace component {

class Component;
using ComponentBase = ddk::Device<Component, ddk::Rxrpcable, ddk::Unbindable>;

class Component : public ComponentBase {
public:
    explicit Component(zx_device_t* parent)
        : ComponentBase(parent), canvas_(parent), clock_(parent),
          eth_board_(parent), gpio_(parent), i2c_(parent),
          mipicsi_(parent), pdev_(parent), power_(parent),
          sysmem_(parent), ums_(parent) {}

    static zx_status_t Bind(void* ctx, zx_device_t* parent);

    zx_status_t DdkRxrpc(zx_handle_t channel);
    void DdkUnbind();
    void DdkRelease();

private:
    struct I2cTransactContext {
        sync_completion_t completion;
        void* read_buf;
        size_t read_length;
        zx_status_t result;
    };

    zx_status_t RpcCanvas(const uint8_t* req_buf, uint32_t req_size, uint8_t* resp_buf,
                          uint32_t* out_resp_size, const zx_handle_t* req_handles,
                          uint32_t req_handle_count, zx_handle_t* resp_handles,
                          uint32_t* resp_handle_count);
    zx_status_t RpcClock(const uint8_t* req_buf, uint32_t req_size, uint8_t* resp_buf,
                         uint32_t* out_resp_size, const zx_handle_t* req_handles,
                         uint32_t req_handle_count, zx_handle_t* resp_handles,
                         uint32_t* resp_handle_count);
    zx_status_t RpcEthBoard(const uint8_t* req_buf, uint32_t req_size, uint8_t* resp_buf,
                            uint32_t* out_resp_size, const zx_handle_t* req_handles,
                            uint32_t req_handle_count, zx_handle_t* resp_handles,
                            uint32_t* resp_handle_count);
    zx_status_t RpcGpio(const uint8_t* req_buf, uint32_t req_size, uint8_t* resp_buf,
                        uint32_t* out_resp_size, const zx_handle_t* req_handles,
                        uint32_t req_handle_count, zx_handle_t* resp_handles,
                        uint32_t* resp_handle_count);
    zx_status_t RpcI2c(const uint8_t* req_buf, uint32_t req_size, uint8_t* resp_buf,
                       uint32_t* out_resp_size, const zx_handle_t* req_handles,
                       uint32_t req_handle_count, zx_handle_t* resp_handles,
                       uint32_t* resp_handle_count);
    zx_status_t RpcPdev(const uint8_t* req_buf, uint32_t req_size, uint8_t* resp_buf,
                        uint32_t* out_resp_size, const zx_handle_t* req_handles,
                        uint32_t req_handle_count, zx_handle_t* resp_handles,
                        uint32_t* resp_handle_count);
    zx_status_t RpcPower(const uint8_t* req_buf, uint32_t req_size, uint8_t* resp_buf,
                         uint32_t* out_resp_size, const zx_handle_t* req_handles,
                         uint32_t req_handle_count, zx_handle_t* resp_handles,
                         uint32_t* resp_handle_count);
    zx_status_t RpcSysmem(const uint8_t* req_buf, uint32_t req_size, uint8_t* resp_buf,
                          uint32_t* out_resp_size, const zx_handle_t* req_handles,
                          uint32_t req_handle_count, zx_handle_t* resp_handles,
                          uint32_t* resp_handle_count);
    zx_status_t RpcUms(const uint8_t* req_buf, uint32_t req_size, uint8_t* resp_buf,
                       uint32_t* out_resp_size, const zx_handle_t* req_handles,
                       uint32_t req_handle_count, zx_handle_t* resp_handles,
                       uint32_t* resp_handle_count);
    zx_status_t RpcMipiCsi(const uint8_t* req_buf, uint32_t req_size, uint8_t* resp_buf,
                           uint32_t* out_resp_size, const zx_handle_t* req_handles,
                           uint32_t req_handle_count, zx_handle_t* resp_handles,
                           uint32_t* resp_handle_count);

    static void I2cTransactCallback(void* cookie, zx_status_t status, const i2c_op_t* op_list,
                                    size_t op_count);

    ddk::AmlogicCanvasProtocolClient canvas_;
    ddk::ClockProtocolClient clock_;
    ddk::EthBoardProtocolClient eth_board_;
    ddk::GpioProtocolClient gpio_;
    ddk::I2cProtocolClient i2c_;
    ddk::MipiCsiProtocolClient mipicsi_;
    ddk::PDevProtocolClient pdev_;
    ddk::PowerProtocolClient power_;
    ddk::SysmemProtocolClient sysmem_;
    ddk::UsbModeSwitchProtocolClient ums_;
};

} // namespace component
