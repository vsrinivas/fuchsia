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

#include "device.h"

namespace ath10k {

Device::Device(mx_device_t* device, pci_protocol_t* pci)
  : BaseDevice(device), pci_(pci) {}

mx_status_t Device::Bind() {
    DdkAdd("ath10k");
    return MX_OK;
}

void Device::DdkUnbind() {
    DdkRemove();
}

void Device::DdkRelease() {
    delete this;
}

mx_status_t Device::WlanmacQuery(uint32_t options, ethmac_info_t* info) {
    info->mtu = 1500;
    std::memcpy(info->mac, mac_addr_, ETH_MAC_SIZE);
    info->features |= ETHMAC_FEATURE_WLAN;
    return MX_OK;
}

mx_status_t Device::WlanmacStart(fbl::unique_ptr<ddk::WlanmacIfcProxy> proxy) {
    std::lock_guard<std::mutex> guard(lock_);
    if (wlanmac_proxy_ != nullptr) {
        return MX_ERR_ALREADY_BOUND;
    }
    wlanmac_proxy_.swap(proxy);
    return MX_OK;
}

void Device::WlanmacStop() {
    std::lock_guard<std::mutex> guard(lock_);
    wlanmac_proxy_.reset();
}

void Device::WlanmacTx(uint32_t options, const void* data, size_t len) {
    // TODO
}

mx_status_t Device::WlanmacSetChannel(uint32_t options, wlan_channel_t* chan) {
    // TODO
    return MX_OK;
}

}  // namespace ath10k
