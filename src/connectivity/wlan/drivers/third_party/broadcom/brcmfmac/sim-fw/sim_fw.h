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

#ifndef SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_BROADCOM_BRCMFMAC_SIM_FW_SIM_FW_H_
#define SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_BROADCOM_BRCMFMAC_SIM_FW_SIM_FW_H_

#include <stdint.h>
#include <string>
#include <sys/types.h>
#include <zircon/types.h>

#include "sim_hw.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/bcdc.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/core.h"

class SimFirmware {
  class BcdcResponse {
   public:
    void Clear();

    // Copy data into buffer. If buffer is not large enough, set len_out to needed size and
    // return ZX_ERR_BUFFER_TOO_SMALL.
    zx_status_t Get(uint8_t* data, size_t len, size_t* len_out);

    bool IsClear();

    // Copy data from buffer.
    void Set(uint8_t* data, size_t new_len);

   private:
    size_t len_;
    uint8_t msg_[BRCMF_DCMD_MAXLEN];
  };

 public:
  void GetChipInfo(uint32_t* chip, uint32_t* chiprev);

  // Bus operations
  zx_status_t BusPreinit();
  void BusStop();
  zx_status_t BusTxData(struct brcmf_netbuf* netbuf);
  zx_status_t BusTxCtl(unsigned char* msg, uint len);
  zx_status_t BusRxCtl(unsigned char* msg, uint len, int* rxlen_out);
  struct pktq* BusGetTxQueue();
  void BusWowlConfig(bool enabled);
  size_t BusGetRamsize();
  zx_status_t BusGetMemdump(void* data, size_t len);
  zx_status_t BusGetFwName(uint chip, uint chiprev, unsigned char* fw_name);
  zx_status_t BusGetBootloaderMacAddr(uint8_t* mac_addr);

 private:
  // BCDC interface
  zx_status_t BcdcVarOp(brcmf_proto_bcdc_dcmd* msg, uint8_t* data, size_t len, bool is_set);

  // Firmware iovars
  zx_status_t IovarsSet(const char* name, const void* value, size_t value_len);
  zx_status_t IovarsGet(const char* name, void* value_out, size_t value_len);

  // Next message to pass back to a BCDC Rx Ctl request
  BcdcResponse bcdc_response_;

  // Simulated hardware state
  SimHardware hw_;
};

#endif  // SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_BROADCOM_BRCMFMAC_SIM_FW_SIM_FW_H_
