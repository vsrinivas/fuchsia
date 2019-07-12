/*
 * Copyright (c) 2019 The Fuchsia Authors
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION
 * OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/brcm_hw_ids.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/debug.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/fwil.h"
#include "sim_fw.h"

void SimFirmware::GetChipInfo(uint32_t* chip, uint32_t* chiprev) {
    *chip = BRCM_CC_4356_CHIP_ID;
    *chiprev = 2;
}

zx_status_t SimFirmware::BusPreinit() {
    ZX_PANIC("%s unimplemented", __FUNCTION__);
    return ZX_ERR_NOT_SUPPORTED;
}

void SimFirmware::BusStop() {
    ZX_PANIC("%s unimplemented", __FUNCTION__);
}

zx_status_t SimFirmware::BusTxData(struct brcmf_netbuf* netbuf) {
    ZX_PANIC("%s unimplemented", __FUNCTION__);
    return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t SimFirmware::BusTxCtl(unsigned char* msg, unsigned int len) {
    ZX_PANIC("%s unimplemented", __FUNCTION__);
    return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t SimFirmware::BusRxCtl(unsigned char* msg, uint len, int* rxlen_out) {
    ZX_PANIC("%s unimplemented", __FUNCTION__);
    return ZX_ERR_NOT_SUPPORTED;
}

struct pktq* SimFirmware::BusGetTxQueue() {
    ZX_PANIC("%s unimplemented", __FUNCTION__);
    return nullptr;
}

void SimFirmware::BusWowlConfig(bool enabled) {
    ZX_PANIC("%s unimplemented", __FUNCTION__);
}

size_t SimFirmware::BusGetRamsize() {
    ZX_PANIC("%s unimplemented", __FUNCTION__);
    return 0;
}

zx_status_t SimFirmware::BusGetMemdump(void* data, size_t len) {
    ZX_PANIC("%s unimplemented", __FUNCTION__);
    return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t SimFirmware::BusGetFwName(uint chip, uint chiprev, unsigned char* fw_name) {
    ZX_PANIC("%s unimplemented", __FUNCTION__);
    return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t SimFirmware::BusGetBootloaderMacAddr(uint8_t* mac_addr) {
    // Rather than simulate a fixed MAC address, return NOT_SUPPORTED, which will force
    // us to use a randomly-generated value
    return ZX_ERR_NOT_SUPPORTED;
}
