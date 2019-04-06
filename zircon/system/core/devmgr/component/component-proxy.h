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
#include <ddktl/protocol/gpio.h>
#include <ddktl/protocol/platform/device.h>
#include <ddktl/protocol/power.h>
#include <ddktl/protocol/sysmem.h>
#include <lib/zx/channel.h>

#include "proxy-protocol.h"

namespace component {

class ComponentProxy;
using ComponentProxyBase = ddk::Device<ComponentProxy, ddk::Unbindable, ddk::GetProtocolable>;

class ComponentProxy : public ComponentProxyBase,
                       public ddk::AmlogicCanvasProtocol<ComponentProxy>,
                       public ddk::ClockProtocol<ComponentProxy>,
                       public ddk::GpioProtocol<ComponentProxy>,
                       public ddk::PDevProtocol<ComponentProxy>,
                       public ddk::PowerProtocol<ComponentProxy>,
                       public ddk::SysmemProtocol<ComponentProxy> {
public:
    ComponentProxy(zx_device_t* parent, zx::channel rpc)
        : ComponentProxyBase(parent), rpc_(std::move(rpc)) {}

    static zx_status_t Create(void* ctx, zx_device_t* parent, const char* name,
                              const char* args, zx_handle_t raw_rpc);

    zx_status_t DdkGetProtocol(uint32_t, void*);
    void DdkUnbind();
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
    zx_status_t ClockEnable(uint32_t index);
    zx_status_t ClockDisable(uint32_t index);
    zx_status_t GpioConfigIn(uint32_t flags);
    zx_status_t GpioConfigOut(uint8_t initial_value);
    zx_status_t GpioSetAltFunction(uint64_t function);
    zx_status_t GpioRead(uint8_t* out_value);
    zx_status_t GpioWrite(uint8_t value);
    zx_status_t GpioGetInterrupt(uint32_t flags, zx::interrupt* out_irq);
    zx_status_t GpioReleaseInterrupt();
    zx_status_t GpioSetPolarity(gpio_polarity_t polarity);
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
    zx_status_t PowerEnablePowerDomain();
    zx_status_t PowerDisablePowerDomain();
    zx_status_t PowerGetPowerDomainStatus(power_domain_status_t* out_status);
    zx_status_t SysmemConnect(zx::channel allocator2_request);

private:
    zx::channel rpc_;
};

} // namespace component
