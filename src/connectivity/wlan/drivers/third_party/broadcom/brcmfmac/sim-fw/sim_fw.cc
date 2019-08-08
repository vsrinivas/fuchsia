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
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/brcmu_d11.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/common.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/debug.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/fwil.h"

void SimFirmware::GetChipInfo(uint32_t* chip, uint32_t* chiprev) {
  *chip = BRCM_CC_4356_CHIP_ID;
  *chiprev = 2;
}

zx_status_t SimFirmware::BusPreinit() {
  // Currently nothing to do
  return ZX_OK;
}

void SimFirmware::BusStop() { ZX_PANIC("%s unimplemented", __FUNCTION__); }

zx_status_t SimFirmware::BusTxData(struct brcmf_netbuf* netbuf) {
  ZX_PANIC("%s unimplemented", __FUNCTION__);
  return ZX_ERR_NOT_SUPPORTED;
}

// Set or get the value of an iovar. The format of the message is a null-terminated string
// containing the iovar name, followed by the value to assign to that iovar.
zx_status_t SimFirmware::BcdcVarOp(brcmf_proto_bcdc_dcmd* dcmd, uint8_t* data, size_t len,
                                   bool is_set) {
  zx_status_t status = ZX_OK;

  char* str_begin = reinterpret_cast<char*>(data);
  uint8_t* str_end = static_cast<uint8_t*>(std::memchr(str_begin, '\0', dcmd->len));
  if (str_end == nullptr) {
    BRCMF_ERR("SET_VAR: iovar name not null-terminated\n");
    return ZX_ERR_INVALID_ARGS;
  }

  size_t str_len = str_end - data;

  // IovarsSet returns the input unchanged
  // IovarsGet modifies the buffer in-place
  if (is_set) {
    void* value_start = str_end + 1;
    size_t value_len = dcmd->len - (str_len + 1);
    status = IovarsSet(str_begin, value_start, value_len);
  } else {
    status = IovarsGet(str_begin, data, dcmd->len);
  }

  if (status == ZX_OK) {
    bcdc_response_.Set(reinterpret_cast<uint8_t*>(dcmd), len);
  } else {
    // Return empty message on failure
    bcdc_response_.Clear();
  }
  return status;
}

// Process a TX CTL message. These have a BCDC header, followed by a payload that is determined
// by the type of command.
zx_status_t SimFirmware::BusTxCtl(unsigned char* msg, unsigned int len) {
  brcmf_proto_bcdc_dcmd* dcmd;
  constexpr size_t hdr_size = sizeof(struct brcmf_proto_bcdc_dcmd);
  if (len < hdr_size) {
    BRCMF_ERR("Message length (%u) smaller than BCDC header size (%zd)\n", len, hdr_size);
    return ZX_ERR_INVALID_ARGS;
  }
  dcmd = reinterpret_cast<brcmf_proto_bcdc_dcmd*>(msg);
  // The variable-length payload immediately follows the header
  uint8_t* data = reinterpret_cast<uint8_t*>(dcmd) + hdr_size;

  if (dcmd->len > (len - hdr_size)) {
    BRCMF_ERR("BCDC total message length (%zd) exceeds buffer size (%u)\n",
              dcmd->len + hdr_size, len);
    return ZX_ERR_INVALID_ARGS;
  }

  zx_status_t status = ZX_OK;
  switch (dcmd->cmd) {
    // Get/Set a firmware IOVAR. This message is comprised of a NULL-terminated string
    // for the variable name, followed by the value to assign to it.
    case BRCMF_C_SET_VAR:
    case BRCMF_C_GET_VAR:
      status = BcdcVarOp(dcmd, data, len, dcmd->cmd == BRCMF_C_SET_VAR);
      break;
    case BRCMF_C_GET_REVINFO: {
      struct brcmf_rev_info_le rev_info;
      hw_.GetRevInfo(&rev_info);
      if (dcmd->len < sizeof(rev_info)) {
        BRCMF_ERR(
            "Insufficient space (%u bytes) in message buffer to save revision "
            "info (%zu bytes)\n",
            dcmd->len, sizeof(rev_info));
      }
      std::memcpy(data, &rev_info, sizeof(rev_info));
      bcdc_response_.Set(msg, len);
      break;
    }
    case BRCMF_C_GET_VERSION: {
      // GET_VERSION is a bit of a misnomer. It's really the 802.11 supported spec
      // (e.g., n or ac).
      constexpr uint32_t iotype = BRCMU_D11AC_IOTYPE;
      if (dcmd->len < sizeof(iotype)) {
        BRCMF_ERR(
            "Insufficient space (%u bytes) in message buffer to save iotype "
            "info (%zu bytes)\n",
            dcmd->len, sizeof(iotype));
      }
      std::memcpy(data, &iotype, sizeof(iotype));
      bcdc_response_.Set(msg, len);
      break;
    }
    case BRCMF_C_SET_SCAN_CHANNEL_TIME:
    case BRCMF_C_SET_SCAN_UNASSOC_TIME:
      BRCMF_ERR("Ignoring firmware message %d\n", dcmd->cmd);
      bcdc_response_.Set(msg, len);
      status = ZX_OK;
      break;
    default:
      BRCMF_ERR("Unimplemented firmware message %d\n", dcmd->cmd);
      status = ZX_ERR_NOT_SUPPORTED;
      break;
  }
  return status;
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
  strlcpy((char*)fw_name, "sim-fake-fw.bin", BRCMF_FW_NAME_LEN);
  return ZX_OK;
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

zx_status_t SimFirmware::IovarsSet(const char* name, const void* value, size_t value_len) {
  if (!std::strcmp(name, "cur_etheraddr")) {
    if (value_len == ETH_ALEN) {
      return hw_.SetMacAddr(static_cast<const uint8_t*>(value));
    } else {
      return ZX_ERR_INVALID_ARGS;
    }
  }

  // FIXME: For now, just pretend that we successfully set the value even when we did nothing
  BRCMF_ERR("Ignoring request to set iovar '%s'\n", name);
  return ZX_OK;
}

const char* kFirmwareVer = "wl0: Sep 10 2018 16:37:38 version 7.35.79 (r487924) FWID 01-c76ab99a";

zx_status_t SimFirmware::IovarsGet(const char* name, void* value_out, size_t value_len) {
  if (!std::strcmp(name, "ver")) {
    if (value_len >= (strlen(kFirmwareVer) + 1)) {
      strlcpy(static_cast<char*>(value_out), kFirmwareVer, value_len);
    } else {
      return ZX_ERR_INVALID_ARGS;
    }
  } else {
    // FIXME: We should return an error for an unrecognized firmware variable
    BRCMF_ERR("Ignoring request to read iovar '%s'\n", name);
    memset(value_out, 0, value_len);
  }
  return ZX_OK;
}
