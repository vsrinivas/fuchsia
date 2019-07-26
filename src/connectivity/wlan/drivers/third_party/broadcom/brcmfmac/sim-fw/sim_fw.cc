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

#include "sim_fw.h"

#include <zircon/assert.h>

#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/bcdc.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/brcm_hw_ids.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/debug.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/fwil.h"

void SimFirmware::GetChipInfo(uint32_t* chip, uint32_t* chiprev) {
  *chip = BRCM_CC_4356_CHIP_ID;
  *chiprev = 2;
}

zx_status_t SimFirmware::BusPreinit() {
  ZX_PANIC("%s unimplemented", __FUNCTION__);
  return ZX_ERR_NOT_SUPPORTED;
}

void SimFirmware::BusStop() { ZX_PANIC("%s unimplemented", __FUNCTION__); }

zx_status_t SimFirmware::BusTxData(struct brcmf_netbuf* netbuf) {
  ZX_PANIC("%s unimplemented", __FUNCTION__);
  return ZX_ERR_NOT_SUPPORTED;
}

// Process a TX CTL message. These have a BCDC header, followed by a payload that is determined
// by the type of command.
zx_status_t SimFirmware::BusTxCtl(unsigned char* msg, unsigned int len) {
  brcmf_proto_bcdc_dcmd* hdr;
  constexpr size_t hdr_size = sizeof(*hdr);
  if (len < hdr_size) {
    BRCMF_ERR("Message length (%u) smaller than BCDC header size (%zd)\n", len, hdr_size);
    return ZX_ERR_INVALID_ARGS;
  }
  hdr = reinterpret_cast<brcmf_proto_bcdc_dcmd*>(msg);

  size_t bcdc_data_len = len - hdr_size;
  if (hdr->len > bcdc_data_len) {
    BRCMF_ERR("BCDC total message length (%zd) exceeds buffer size (%u)\n", hdr->len + hdr_size,
              len);
    return ZX_ERR_INVALID_ARGS;
  }
  uint8_t* bcdc_data = static_cast<uint8_t*>(msg) + hdr_size;

  zx_status_t status;
  switch (hdr->cmd) {
    // Set a firmware IOVAR. This message is comprised of a NULL-terminated string
    // for the variable name, followed by the value to assign to it.
    case BRCMF_C_SET_VAR: {
      uint8_t* str_end = static_cast<uint8_t*>(std::memchr(bcdc_data, '\0', bcdc_data_len));
      if (str_end == nullptr) {
        BRCMF_ERR("iovar name not null-terminated\n");
        return ZX_ERR_INVALID_ARGS;
      }
      status = IovarsSet(reinterpret_cast<char*>(bcdc_data), static_cast<void*>(str_end + 1),
                         bcdc_data + bcdc_data_len - (str_end + 1));
      if (status == ZX_OK) {
        // Return original message on success
        bcdc_response_.Set(static_cast<uint8_t*>(msg), len);
      } else {
        // Return empty message on failure
        bcdc_response_.Clear();
      }
    } break;
    default:
      BRCMF_ERR("Unimplemented firmware message %d\n", hdr->cmd);
      return ZX_ERR_NOT_SUPPORTED;
  }
  return ZX_OK;
}

// Process an RX CTL message. We simply pass back the results of the previous TX CTL
// operation, which has been stored in bcdc_response_. In real hardware, we may have to
// indicate that the TX CTL operation has not completed. In simulated hardware, we perform
// all operations synchronously.
zx_status_t SimFirmware::BusRxCtl(unsigned char* msg, uint len, int* rxlen_out) {
  if (bcdc_response_.IsClear()) {
    return ZX_ERR_UNAVAILABLE;
  }

  size_t actual_len;
  zx_status_t result = bcdc_response_.Get(msg, len, &actual_len);
  if (result == ZX_OK) {
    // Responses are not re-sent on subsequent requests
    bcdc_response_.Clear();
    *rxlen_out = actual_len;
  }
  return result;
}

struct pktq* SimFirmware::BusGetTxQueue() {
  ZX_PANIC("%s unimplemented", __FUNCTION__);
  return nullptr;
}

void SimFirmware::BusWowlConfig(bool enabled) { ZX_PANIC("%s unimplemented", __FUNCTION__); }

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

void SimFirmware::BcdcResponse::Clear() { len_ = 0; }

zx_status_t SimFirmware::BcdcResponse::Get(uint8_t* data, size_t len, size_t* len_out) {
  if (len < len_) {
    return ZX_ERR_BUFFER_TOO_SMALL;
  }

  std::memcpy(data, msg_, len_);
  *len_out = len_;
  return ZX_OK;
}

bool SimFirmware::BcdcResponse::IsClear() { return len_ == 0; }

void SimFirmware::BcdcResponse::Set(uint8_t* data, size_t new_len) {
  ZX_DEBUG_ASSERT(new_len <= sizeof(msg_));
  len_ = new_len;
  memcpy(msg_, data, new_len);
}

zx_status_t SimFirmware::IovarsSet(const char* name, void* value, size_t len) {
  if (std::strcmp(name, "cur_etheraddr")) {
    if (len == ETH_ALEN) {
      return hw_.setMacAddr(static_cast<uint8_t*>(value));
    } else {
      return ZX_ERR_INVALID_ARGS;
    }
  }

  // FIXME: For now, just pretend that we successfully set the value even when we did nothing
  return ZX_OK;
}
