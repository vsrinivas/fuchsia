// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_BUS_DRIVERS_PLATFORM_PLATFORM_PROXY_H_
#define SRC_DEVICES_BUS_DRIVERS_PLATFORM_PLATFORM_PROXY_H_

#include <fuchsia/hardware/platform/device/cpp/banjo.h>
#include <lib/zx/channel.h>

#include <ddktl/device.h>
#include <fbl/vector.h>

#include "proxy-protocol.h"

namespace platform_bus {

class PlatformProxy;

using PlatformProxyType = ddk::Device<PlatformProxy>;

// This is the main class for the proxy side platform bus driver.
// It handles RPC communication with the main platform bus driver in the root devhost.

class PlatformProxy : public PlatformProxyType,
                      public ddk::PDevProtocol<PlatformProxy, ddk::base_protocol> {
 public:
  explicit PlatformProxy(zx_device_t* parent, zx_handle_t rpc_channel)
      : PlatformProxyType(parent), rpc_channel_(rpc_channel) {}

  static zx_status_t Create(void* ctx, zx_device_t* parent, const char* name, const char* args,
                            zx_handle_t rpc_channel);

  // Device protocol implementation.
  void DdkRelease();

  // Platform device protocol implementation.
  zx_status_t PDevGetMmio(uint32_t index, pdev_mmio_t* out_mmio);
  zx_status_t PDevGetInterrupt(uint32_t index, uint32_t flags, zx::interrupt* out_irq);
  zx_status_t PDevGetBti(uint32_t index, zx::bti* out_bti);
  zx_status_t PDevGetSmc(uint32_t index, zx::resource* out_resource);
  zx_status_t PDevGetDeviceInfo(pdev_device_info_t* out_info);
  zx_status_t PDevGetBoardInfo(pdev_board_info_t* out_info);

  zx_status_t Rpc(const platform_proxy_req_t* req, size_t req_length, platform_proxy_rsp_t* resp,
                  size_t resp_length, const zx_handle_t* in_handles, size_t in_handle_count,
                  zx_handle_t* out_handles, size_t out_handle_count, size_t* out_actual);

  inline zx_status_t Rpc(const platform_proxy_req_t* req, size_t req_length,
                         platform_proxy_rsp_t* resp, size_t resp_length) {
    return Rpc(req, req_length, resp, resp_length, nullptr, 0, nullptr, 0, nullptr);
  }

 private:
  struct Mmio {
    zx_paddr_t base;
    size_t length;
    zx::resource resource;
  };
  struct Irq {
    uint32_t irq;
    // ZX_INTERRUPT_MODE_* flags
    uint32_t mode;
    zx::resource resource;
  };

  DISALLOW_COPY_ASSIGN_AND_MOVE(PlatformProxy);

  zx_status_t Init(zx_device_t* parent);

  const zx::channel rpc_channel_;

  char name_[ZX_MAX_NAME_LEN];
  uint32_t metadata_count_;

  fbl::Vector<Mmio> mmios_;
  fbl::Vector<Irq> irqs_;
};

}  // namespace platform_bus

#endif  // SRC_DEVICES_BUS_DRIVERS_PLATFORM_PLATFORM_PROXY_H_
