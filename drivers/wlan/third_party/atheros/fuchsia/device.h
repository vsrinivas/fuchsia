/*
 * Copyright (c) 2017 The Fuchsia Authors.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#pragma once

#include "hw.h"

#include <ddktl/device.h>
#include <ddktl/protocol/wlan.h>
#include <fbl/unique_ptr.h>
#include <zircon/types.h>

#include <cstdint>
#include <mutex>

#define _ALL_SOURCE
#include <threads.h>

namespace ath10k {

class Hif;
class Device;
using BaseDevice = ddk::Device<Device, ddk::Unbindable>;

class Device : public BaseDevice,
               public ddk::WlanmacProtocol<Device> {
  public:
    Device(zx_device_t* device, fbl::unique_ptr<Hif> hif);

    zx_status_t Bind();
    int Init();

    // ddk::Device methods
    void DdkUnbind();
    void DdkRelease();

    // ddk::WlanmacProtocol methods
    zx_status_t WlanmacQuery(uint32_t options, ethmac_info_t* info);
    zx_status_t WlanmacStart(fbl::unique_ptr<ddk::WlanmacIfcProxy> proxy);
    void WlanmacStop();
    void WlanmacTx(uint32_t options, const void* data, size_t len);
    zx_status_t WlanmacSetChannel(uint32_t options, wlan_channel_t* chan);

  private:
    fbl::unique_ptr<Hif> hif_;
    fbl::unique_ptr<ddk::WlanmacIfcProxy> wlanmac_proxy_ __TA_GUARDED(lock_);

    HwRev rev_ = HwRev::UNKNOWN;
    uint8_t mac_addr_[ETH_MAC_SIZE] = {};

    std::mutex lock_;
    thrd_t init_thread_;
};

}  // namespace ath10k
