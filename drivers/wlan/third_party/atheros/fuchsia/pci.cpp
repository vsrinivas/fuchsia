/*
 * Copyright (c) 2005-2011 Atheros Communications Inc.
 * Copyright (c) 2011-2013 Qualcomm Atheros, Inc.
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

#include "pci.h"

#include "hw.h"

#include <ddk/debug.h>
#include <zircon/assert.h>

namespace ath10k {

zx_status_t PciBus::Bind() {
    return ZX_OK;
}

zx_status_t PciBus::Init(HwRev* rev) {
    ZX_DEBUG_ASSERT(rev != nullptr);

    zx_pcie_device_info_t pci_info = {};
    pci_.GetDeviceInfo(&pci_info);
    zxlogf(INFO, "ath10k: VID %04x DID %04x\n", pci_info.vendor_id, pci_info.device_id);
    zxlogf(INFO, "ath10k: base class %02x sub class %02x\n",
                  pci_info.base_class, pci_info.sub_class);

    switch (pci_info.device_id) {
    case QCA988X_2_0_DID:
        *rev = HwRev::QCA988X;
        pci_ps_ = false;
        break;
    case QCA6174_2_1_DID:
    case QCA6164_2_1_DID:
        *rev = HwRev::QCA6174;
        pci_ps_ = true;
        break;
    case QCA99X0_2_0_DID:
        *rev = HwRev::QCA99X0;
        pci_ps_ = false;
        break;
    case QCA9377_1_0_DID:
        *rev = HwRev::QCA9377;
        pci_ps_ = true;
        break;
    case QCA9984_1_0_DID:
        *rev = HwRev::QCA9984;
        pci_ps_ = false;
        break;
    case QCA9887_1_0_DID:
        *rev = HwRev::QCA9887;
        pci_ps_ = false;
        break;
    case QCA9888_2_0_DID:
        *rev = HwRev::QCA9888;
        pci_ps_ = false;
        break;
    default:
        // binding.c DIDs must match this switch statement.
        ZX_PANIC("unsupported device id = %04x\n", pci_info.device_id);
    }

    return ZX_OK;
}

zx_status_t PciBus::TxSg(uint8_t pipe_id, HifSGItem* items, size_t n_items) {
    return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t PciBus::DiagRead(uint32_t address, void* buf, size_t buf_len) {
    return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t PciBus::DiagWrite(uint32_t address, const void* buf, size_t buf_len) {
    return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t PciBus::ExchangeBmiMsg(void* req, uint32_t req_len, void* resp, uint32_t* resp_len) {
    return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t PciBus::Start() {
    return ZX_ERR_NOT_SUPPORTED;
}

void PciBus::Stop() {
}

zx_status_t PciBus::MapServiceToPipe(uint16_t service_id, uint8_t* ul_pipe, uint8_t* dl_pipe) {
    return ZX_ERR_NOT_SUPPORTED;
}

void PciBus::GetDefaultPipe(uint8_t* ul_pipe, uint8_t* dl_pipe) {
}

void PciBus::SendCompleteCheck(uint8_t pipe_id, int force) {
}

uint16_t PciBus::GetFreeQueueNumber(uint8_t pipe_id) {
    return 0;

}

uint32_t PciBus::Read32(uint32_t address) {
    return 0;

}

void PciBus::Write32(uint32_t address, uint32_t value) {
}

zx_status_t PciBus::PowerUp() {
    return ZX_ERR_NOT_SUPPORTED;
}

void PciBus::PowerDown() {
}

zx_status_t PciBus::Suspend() {
    return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t PciBus::Resume() {
    return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t PciBus::FetchCalEeprom(void** data, size_t* data_len) {
    return ZX_ERR_NOT_SUPPORTED;
}

}  // namespace ath10k
