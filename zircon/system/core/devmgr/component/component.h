// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddk/binding.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/protocol/amlogiccanvas.h>
#include <ddk/protocol/clock.h>
#include <ddk/protocol/gpio.h>
#include <ddk/protocol/power.h>
#include <ddk/protocol/sysmem.h>
#include <ddktl/device.h>
#include <lib/zx/channel.h>

namespace component {

class Component;
using ComponentBase = ddk::Device<Component, ddk::Rxrpcable, ddk::Unbindable>;

class Component : public ComponentBase {
public:
    explicit Component(zx_device_t* parent);

    static zx_status_t Bind(void* ctx, zx_device_t* parent);

    zx_status_t DdkRxrpc(zx_handle_t channel);
    void DdkUnbind();
    void DdkRelease();

private:
    zx_status_t RpcCanvas(const uint8_t* req_buf, uint32_t req_size, uint8_t* resp_buf,
                          uint32_t* out_resp_size, const zx_handle_t* req_handles,
                          uint32_t req_handle_count, zx_handle_t* resp_handles,
                          uint32_t* resp_handle_count);
    zx_status_t RpcClock(const uint8_t* req_buf, uint32_t req_size, uint8_t* resp_buf,
                         uint32_t* out_resp_size, const zx_handle_t* req_handles,
                         uint32_t req_handle_count, zx_handle_t* resp_handles,
                         uint32_t* resp_handle_count);
    zx_status_t RpcGpio(const uint8_t* req_buf, uint32_t req_size, uint8_t* resp_buf,
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

    amlogic_canvas_protocol_t canvas_ = {};
    clock_protocol_t clock_ = {};
    gpio_protocol_t gpio_ = {};
    power_protocol_t power_ = {};
    sysmem_protocol_t sysmem_ = {};
};

} // namespace component
