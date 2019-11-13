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

#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/sim/sim_fw.h"

#include <zircon/assert.h>

#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/bcdc.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/brcm_hw_ids.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/brcmu_d11.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/cfg80211.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/common.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/debug.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/fweh.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/fwil.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/fwil_types.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/sim/sim.h"

namespace wlan::brcmfmac {

SimFirmware::SimFirmware(brcmf_simdev* simdev, simulation::Environment* env)
    : simdev_(simdev), hw_(env) {
  // Configure the chanspec encode/decoder
  d11_inf_.io_type = kIoType;
  brcmu_d11_attach(&d11_inf_);

  // Configure the (simulated) hardware => (simulated) firmware callbacks
  SimHardware::EventHandlers handlers = {
      .rx_beacon_handler = std::bind(&SimFirmware::RxBeacon, this, std::placeholders::_1,
                                     std::placeholders::_2, std::placeholders::_3),
  };
  hw_.SetCallbacks(handlers);
  country_code_ = {};
}

void SimFirmware::GetChipInfo(uint32_t* chip, uint32_t* chiprev) {
  *chip = BRCM_CC_4356_CHIP_ID;
  *chiprev = 2;
}

int32_t SimFirmware::GetPM() { return power_mode_; }

zx_status_t SimFirmware::BusPreinit() {
  // Currently nothing to do
  return ZX_OK;
}

void SimFirmware::BusStop() { ZX_PANIC("%s unimplemented", __FUNCTION__); }

zx_status_t SimFirmware::BusTxData(struct brcmf_netbuf* netbuf) {
  ZX_PANIC("%s unimplemented", __FUNCTION__);
  return ZX_ERR_NOT_SUPPORTED;
}

// Returns a bufer that can be used for BCDC-formatted communications, with the requested
// payload size and an initialized BCDC header. "offset_out" represents the offset of the
// payload within the returned buffer.
std::unique_ptr<std::vector<uint8_t>> SimFirmware::CreateBcdcBuffer(size_t requested_size,
                                                                    size_t* offset_out) {
  size_t header_size = sizeof(brcmf_proto_bcdc_header);
  size_t total_size = header_size + requested_size;

  auto buf = std::make_unique<std::vector<uint8_t>>(total_size);
  auto header = reinterpret_cast<brcmf_proto_bcdc_header*>(buf->data());

  header->flags = (BCDC_PROTO_VER << BCDC_FLAG_VER_SHIFT) & BCDC_FLAG_VER_MASK;
  header->priority = 0xff & BCDC_PRIORITY_MASK;
  header->flags2 = 0;

  // Data immediately follows the header
  header->data_offset = 0;

  *offset_out = header_size;
  return buf;
}

// Set or get the value of an iovar. The format of the message is a null-terminated string
// containing the iovar name, followed by the value to assign to that iovar.
zx_status_t SimFirmware::BcdcVarOp(brcmf_proto_bcdc_dcmd* dcmd, uint8_t* data, size_t len,
                                   bool is_set) {
  zx_status_t status = ZX_OK;

  char* str_begin = reinterpret_cast<char*>(data);
  uint8_t* str_end = static_cast<uint8_t*>(memchr(str_begin, '\0', dcmd->len));
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
    BRCMF_ERR("BCDC total message length (%zd) exceeds buffer size (%u)\n", dcmd->len + hdr_size,
              len);
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
      memcpy(data, &rev_info, sizeof(rev_info));
      bcdc_response_.Set(msg, len);
      break;
    }
    case BRCMF_C_GET_VERSION: {
      // GET_VERSION is a bit of a misnomer. It's really the 802.11 supported spec
      // (e.g., n or ac).
      if (dcmd->len < sizeof(kIoType)) {
        BRCMF_ERR(
            "Insufficient space (%u bytes) in message buffer to save iotype "
            "info (%zu bytes)\n",
            dcmd->len, sizeof(kIoType));
      }
      std::memcpy(data, &kIoType, sizeof(kIoType));
      bcdc_response_.Set(msg, len);
      break;
    }
    case BRCMF_C_SET_SCAN_PASSIVE_TIME:
      if (dcmd->len != sizeof(default_passive_time_)) {
        BRCMF_ERR("Invalid args size to BRCMF_C_SET_SCAN_PASSIVE_TIME (expected %d, saw %d)\n",
                  sizeof(default_passive_time_), dcmd->len);
        return ZX_ERR_INVALID_ARGS;
      }
      default_passive_time_ = *(reinterpret_cast<uint32_t*>(data));
      break;
    case BRCMF_C_SET_PM:
      if (dcmd->len != sizeof(power_mode_)) {
        BRCMF_ERR("Invalid args size to BRCMF_C_SET_PM (expected %d, saw %d)\n",
                  sizeof(power_mode_), dcmd->len);
        return ZX_ERR_INVALID_ARGS;
      }
      power_mode_ = *(reinterpret_cast<int32_t*>(data));
      break;
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

zx_status_t SimFirmware::BusGetFwName(uint chip, uint chiprev, unsigned char* fw_name,
                                      size_t* fw_name_size) {
  constexpr char kFirmwareName[] = "sim-fake-fw.bin";
  strlcpy((char*)fw_name, kFirmwareName, *fw_name_size);
  *fw_name_size = sizeof(kFirmwareName);
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

  memcpy(data, msg_, len_);
  *len_out = len_;
  return ZX_OK;
}

bool SimFirmware::BcdcResponse::IsClear() { return len_ == 0; }

void SimFirmware::BcdcResponse::Set(uint8_t* data, size_t new_len) {
  ZX_DEBUG_ASSERT(new_len <= sizeof(msg_));
  len_ = new_len;
  memcpy(msg_, data, new_len);
}

zx_status_t SimFirmware::HandleIfaceTblReq(const bool add_entry, const void* data,
                                           uint8_t* iface_id) {
  if (add_entry) {
    auto ssid_info = static_cast<const brcmf_mbss_ssid_le*>(data);

    for (int i = 0; i < kMaxIfSupported; i++) {
      if (!iface_tbl_[i].allocated) {
        iface_tbl_[i].allocated = true;
        iface_tbl_[i].iface_id = i;
        iface_tbl_[i].bsscfgidx = ssid_info->bsscfgidx;
        memcpy(iface_tbl_[i].ssid, ssid_info->SSID, ssid_info->SSID_len);
        iface_tbl_[i].ssid_len = ssid_info->SSID_len;
        *iface_id = i;
        return ZX_OK;
      }
    }
  } else {
    auto bsscfgidx = static_cast<const int32_t*>(data);
    for (int i = 0; i < kMaxIfSupported; i++) {
      if (iface_tbl_[i].allocated && iface_tbl_[i].bsscfgidx == *bsscfgidx) {
        *iface_id = iface_tbl_[i].iface_id;
        iface_tbl_[i].allocated = false;
        return ZX_OK;
      }
    }
  }
  return ZX_ERR_IO;
}

zx_status_t SimFirmware::HandleIfaceRequest(const bool add_iface, const void* data,
                                            const size_t len) {
  brcmf_event_msg_be* msg_be;
  struct brcmf_if_event* ifevent;
  uint8_t iface_id;
  size_t data_offset;

  auto buf = CreateEventBuffer(sizeof(brcmf_if_event), &msg_be, &data_offset);
  uint8_t* buffer_data = buf->data();
  ifevent = reinterpret_cast<brcmf_if_event*>(&buffer_data[data_offset]);

  msg_be->event_type = htobe32(BRCMF_E_IF);
  ifevent->role = 1;

  if (HandleIfaceTblReq(add_iface, data, &iface_id) == ZX_OK) {
    if (add_iface) {
      auto ssid_info = static_cast<const brcmf_mbss_ssid_le*>(data);
      ifevent->action = BRCMF_E_IF_ADD;
      ifevent->bsscfgidx = ssid_info->bsscfgidx;
    } else {
      auto bsscfgidx = static_cast<const int32_t*>(data);
      ifevent->action = BRCMF_E_IF_DEL;
      ifevent->bsscfgidx = static_cast<const uint8_t>(*bsscfgidx);
    }
    msg_be->status = htobe32(BRCMF_E_STATUS_SUCCESS);
    sprintf(msg_be->ifname, "wl0.%d", iface_id);
    msg_be->ifidx = iface_id;
    ifevent->ifidx = iface_id;
    msg_be->bsscfgidx = ifevent->bsscfgidx;
  } else {
    msg_be->status = htobe32(BRCMF_E_STATUS_ERROR);
  }
  SendEventToDriver(std::move(buf));
  return ZX_OK;
}

zx_status_t SimFirmware::HandleBssCfgSet(const char* name, const void* value, size_t value_len) {
  if (!std::strcmp(name, "interface_remove")) {
    if (value_len < sizeof(int32_t)) {
      return ZX_ERR_IO;
    } else {
      return HandleIfaceRequest(false, value, value_len);
    }
  }

  if (!std::strcmp(name, "ssid")) {
    if (value_len < sizeof(brcmf_mbss_ssid_le)) {
      return ZX_ERR_IO;
    } else {
      return HandleIfaceRequest(true, value, value_len);
    }
  }

  BRCMF_ERR("Ignoring request to set bsscfg iovar '%s'\n", name);
  return ZX_OK;
}

zx_status_t SimFirmware::IovarsSet(const char* name, const void* value, size_t value_len) {
  const size_t bsscfg_prefix_len = strlen(BRCMF_FWIL_BSSCFG_PREFIX);
  if (!std::strncmp(name, BRCMF_FWIL_BSSCFG_PREFIX, bsscfg_prefix_len)) {
    return HandleBssCfgSet(name + bsscfg_prefix_len, value, value_len);
  }

  if (!std::strcmp(name, "cur_etheraddr")) {
    if (value_len == ETH_ALEN) {
      return SetMacAddr(static_cast<const uint8_t*>(value));
    } else {
      return ZX_ERR_INVALID_ARGS;
    }
  }

  if (!std::strcmp(name, "escan")) {
    return HandleEscanRequest(static_cast<const brcmf_escan_params_le*>(value), value_len);
  }

  if (!std::strcmp(name, "country")) {
    auto cc_req = static_cast<const brcmf_fil_country_le*>(value);
    country_code_ = *cc_req;
    return ZX_OK;
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
  } else if (!std::strcmp(name, "country")) {
    if (value_len >= (sizeof(brcmf_fil_country_le))) {
      memcpy(value_out, &country_code_, sizeof(brcmf_fil_country_le));
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

zx_status_t SimFirmware::SetMacAddr(const uint8_t* mac_addr) {
  memcpy(mac_addr_.data(), mac_addr, ETH_ALEN);
  return ZX_OK;
}

zx_status_t SimFirmware::ScanStart(std::unique_ptr<ScanOpts> opts) {
  if (scan_state_.state != ScanState::STOPPED) {
    // Can't start a scan while another is in progress
    return ZX_ERR_NOT_SUPPORTED;
  }

  if (opts->is_active) {
    BRCMF_ERR("Only explicit passive scanning is supported. Ignoring request\n");
    return ZX_ERR_NOT_SUPPORTED;
  }

  if (opts->channels.size() < 1) {
    // No channels provided. I'm not sure what will happen here on real firmware -- either it will
    // use default channels or refuse to scan.
    BRCMF_ERR("No channels provided to escan start request - ignoring\n");
    return ZX_ERR_NOT_SUPPORTED;
  }

  // Configure state
  scan_state_.state = ScanState::SCANNING;
  scan_state_.opts = std::move(opts);
  scan_state_.channel_index = 0;

  // Start scan
  brcmu_chan chan = {.chspec = scan_state_.opts->channels[scan_state_.channel_index++]};
  d11_inf_.decchspec(&chan);
  hw_.SetChannel(chan.chnum);
  hw_.EnableRx();
  std::function<void()>* callback = new std::function<void()>;
  *callback = std::bind(&SimFirmware::ScanNextChannel, this);
  hw_.RequestCallback(callback, scan_state_.opts->dwell_time);

  return ZX_OK;
}

// If a scan is in progress, switch to the next channel.
void SimFirmware::ScanNextChannel() {
  switch (scan_state_.state) {
    case ScanState::STOPPED:
      // We may see this event if a scan was cancelled -- just ignore it
      return;
    case ScanState::HOME:
      // We don't yet support intermittent scanning
      return;
    case ScanState::SCANNING:
      if (scan_state_.channel_index >= scan_state_.opts->channels.size()) {
        // Scanning complete
        hw_.DisableRx();
        scan_state_.state = ScanState::STOPPED;
        scan_state_.opts->on_done_fn();
        scan_state_.opts = nullptr;
      } else {
        // Scan next channel
        brcmu_chan chan = {.chspec = scan_state_.opts->channels[scan_state_.channel_index++]};
        d11_inf_.decchspec(&chan);
        hw_.SetChannel(chan.chnum);
        std::function<void()>* callback = new std::function<void()>;
        *callback = std::bind(&SimFirmware::ScanNextChannel, this);
        hw_.RequestCallback(callback, scan_state_.opts->dwell_time);
      }
  }
}

// Send an event to the firmware notifying them that the scan has completed.
zx_status_t SimFirmware::HandleEscanRequest(const brcmf_escan_params_le* escan_params,
                                            size_t params_len) {
  if (escan_params->version != BRCMF_ESCAN_REQ_VERSION) {
    BRCMF_ERR("Mismatched escan version (expected %d, saw %d) - ignoring request\n",
              BRCMF_ESCAN_REQ_VERSION, escan_params->version);
    return ZX_ERR_NOT_SUPPORTED;
  }

  switch (escan_params->action) {
    case WL_ESCAN_ACTION_START:
      return EscanStart(escan_params->sync_id, &escan_params->params_le,
                        params_len - offsetof(brcmf_escan_params_le, params_le));
    case WL_ESCAN_ACTION_CONTINUE:
      BRCMF_ERR("Unimplemented escan option WL_ESCAN_ACTION_CONTINUE - ignoring\n");
      return ZX_ERR_NOT_SUPPORTED;
    case WL_ESCAN_ACTION_ABORT:
      BRCMF_ERR("Unimplemented escan option WL_ESCAN_ACTION_ABORT - ignoring\n");
      return ZX_ERR_NOT_SUPPORTED;
    default:
      BRCMF_ERR("Unrecognized escan option %d - ignoring\n", escan_params->action);
      return ZX_ERR_NOT_SUPPORTED;
  }

  return ZX_OK;
}

// When asked to start an escan, we will listen on each of the specified channels for the requested
// duration (dwell time). We accomplish this by setting up a future event for the next channel,
// iterating until we have scanned all channels.
zx_status_t SimFirmware::EscanStart(uint16_t sync_id, const brcmf_scan_params_le* params,
                                    size_t params_len) {
  auto scan_opts = std::make_unique<ScanOpts>();

  scan_opts->sync_id = sync_id;

  switch (params->scan_type) {
  case BRCMF_SCANTYPE_ACTIVE:
    scan_opts->is_active = true;
    break;
  case BRCMF_SCANTYPE_PASSIVE:
    scan_opts->is_active = false;
    break;
  default:
    BRCMF_ERR("Invalid scan type requested: %d\n", params->scan_type);
    return ZX_ERR_INVALID_ARGS;
  }

  size_t num_channels = params->channel_num & BRCMF_SCAN_PARAMS_COUNT_MASK;

  // Configure state
  scan_opts->channels.resize(num_channels);
  std::copy(&params->channel_list[0], &params->channel_list[num_channels],
            scan_opts->channels.data());

  // Determine dwell time. If specified in the request, use that value. Otherwise, if a default
  // dwell time has been specified, use that value. Otherwise, fail.
  if (params->passive_time == static_cast<uint32_t>(-1)) {
    if (default_passive_time_ == static_cast<uint32_t>(-1)) {
      BRCMF_ERR("Attempt to use default passive time, iovar hasn't been set yet\n");
      return ZX_ERR_INVALID_ARGS;
    }
    scan_opts->dwell_time = zx::msec(default_passive_time_);
  } else {
    scan_opts->dwell_time = zx::msec(params->passive_time);
  }

  scan_opts->on_result_fn = std::bind(&SimFirmware::EscanResultSeen, this, std::placeholders::_1,
                                      std::placeholders::_2, std::placeholders::_3);
  scan_opts->on_done_fn = std::bind(&SimFirmware::EscanComplete, this);
  return ScanStart(std::move(scan_opts));
}

void SimFirmware::EscanComplete() {
  brcmf_event_msg_be* msg_be;

  auto buf = CreateEventBuffer(0, &msg_be, nullptr);
  msg_be->event_type = htobe32(BRCMF_E_ESCAN_RESULT);
  msg_be->status = htobe32(BRCMF_E_STATUS_SUCCESS);

  SendEventToDriver(std::move(buf));
}

void SimFirmware::RxBeacon(const wlan_channel_t& channel, const wlan_ssid_t& ssid,
                           const common::MacAddr& bssid) {
  scan_state_.opts->on_result_fn(channel, ssid, bssid);
}

// Handle an Rx Beacon sent to us from the hardware, using it to fill in all of the fields in a
// brcmf_escan_result.
void SimFirmware::EscanResultSeen(const wlan_channel_t& channel, const wlan_ssid_t& ssid,
                                  const common::MacAddr& bssid) {
  // For now, the only IE we will include will be for the SSID
  size_t ssid_ie_size = 2 + ssid.len;

  // scan_result_size includes all BSS info structures (each including IEs). We (like the firmware)
  // only send one result back at a time.
  size_t scan_result_size = roundup(sizeof(brcmf_escan_result_le) + ssid_ie_size, 4);
  size_t scan_result_offset;

  brcmf_event_msg_be* msg_be;

  // Buffer is returned zeroed out with scan_result_offset set
  auto buf = CreateEventBuffer(scan_result_size, &msg_be, &scan_result_offset);

  // Set the scan-specific fields of the event message
  msg_be->event_type = htobe32(BRCMF_E_ESCAN_RESULT);
  msg_be->status = htobe32(BRCMF_E_STATUS_PARTIAL);

  uint8_t* buffer_data = buf->data();
  auto scan_result = reinterpret_cast<brcmf_escan_result_le*>(&buffer_data[scan_result_offset]);
  scan_result->buflen = scan_result_size;
  scan_result->version = BRCMF_BSS_INFO_VERSION;
  scan_result->sync_id = scan_state_.opts->sync_id;
  scan_result->bss_count = 1;

  struct brcmf_bss_info_le* bss_info = &scan_result->bss_info_le;
  bss_info->version = BRCMF_BSS_INFO_VERSION;

  // length of this record (includes IEs)
  bss_info->length = roundup(sizeof(brcmf_bss_info_le) + ssid_ie_size, 4);

  // channel
  bss_info->chanspec = channel_to_chanspec(&d11_inf_, &channel);

  // ssid
  ZX_ASSERT(sizeof(bss_info->SSID) == sizeof(ssid.ssid));
  ZX_ASSERT(ssid.len <= sizeof(ssid.ssid));
  bss_info->SSID_len = 0;  // SSID will go into an IE

  // bssid
  ZX_ASSERT(sizeof(bss_info->BSSID) == common::kMacAddrLen);
  memcpy(bss_info->BSSID, bssid.byte, common::kMacAddrLen);

  // IEs
  bss_info->ie_offset = sizeof(brcmf_bss_info_le);

  // IE: SSID
  size_t ie_offset = scan_result_offset + sizeof(brcmf_escan_result_le);
  size_t ie_len = 0;
  uint8_t* ie_data = &buffer_data[ie_offset];
  ie_data[ie_len++] = IEEE80211_ASSOC_TAG_SSID;
  ie_data[ie_len++] = ssid.len;
  memcpy(&ie_data[ie_len], ssid.ssid, ssid.len);
  ie_len += ssid.len;

  bss_info->ie_length = ie_len;

  // Wrap this in an event and send it back to the driver
  SendEventToDriver(std::move(buf));
}

std::unique_ptr<std::vector<uint8_t>> SimFirmware::CreateEventBuffer(
    size_t requested_size, brcmf_event_msg_be** msg_out_be, size_t* payload_offset_out) {
  size_t total_size = sizeof(brcmf_event) + requested_size;
  size_t event_data_offset;
  auto buf = CreateBcdcBuffer(total_size, &event_data_offset);

  uint8_t* buffer_data = buf->data();
  auto event = reinterpret_cast<brcmf_event*>(&buffer_data[event_data_offset]);

  memcpy(event->eth.h_dest, mac_addr_.data(), ETH_ALEN);
  memcpy(event->eth.h_source, mac_addr_.data(), ETH_ALEN);

  // Disable local bit - we do this because, well, the real firmware does this.
  event->eth.h_source[0] &= ~0x2;
  event->eth.h_proto = htobe16(ETH_P_LINK_CTL);

  auto hdr_be = &event->hdr;
  // hdr_be->subtype unused
  hdr_be->length = htobe16(total_size);
  hdr_be->version = 0;
  memcpy(&hdr_be->oui, BRCM_OUI, sizeof(hdr_be->oui));
  hdr_be->usr_subtype = htobe16(BCMILCP_BCM_SUBTYPE_EVENT);

  // Set the generic fields of the event msg
  *msg_out_be = &event->msg;
  (*msg_out_be)->version = htobe16(2);
  (*msg_out_be)->datalen = htobe32(requested_size);
  memcpy((*msg_out_be)->addr, mac_addr_.data(), ETH_ALEN);
  memcpy((*msg_out_be)->ifname, kDefaultIfcName, strlen(kDefaultIfcName));

  // Payload immediately follows the brcmf_event structure
  if (payload_offset_out != nullptr) {
    *payload_offset_out = event_data_offset + sizeof(brcmf_event);
  }

  return buf;
}

void SimFirmware::SendEventToDriver(std::unique_ptr<std::vector<uint8_t>> buffer) {
  brcmf_sim_rx_event(simdev_, std::move(buffer));
}

}  // namespace wlan::brcmfmac
