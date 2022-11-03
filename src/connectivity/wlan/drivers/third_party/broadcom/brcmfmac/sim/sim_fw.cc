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

#include <arpa/inet.h>
#include <fuchsia/hardware/wlan/associnfo/c/banjo.h>
#include <fuchsia/hardware/wlan/fullmac/c/banjo.h>
#include <fuchsia/wlan/common/c/banjo.h>
#include <fuchsia/wlan/ieee80211/cpp/fidl.h>
#include <fuchsia/wlan/internal/c/banjo.h>
#include <zircon/assert.h>
#include <zircon/compiler.h>

#include <cstddef>
#include <memory>
#include <optional>

#include <third_party/bcmdhd/crossdriver/dhd.h>
#include <third_party/bcmdhd/crossdriver/wlioctl.h>
#include <wlan/common/mac_frame.h>

#include "src/connectivity/wlan/drivers/testing/lib/sim-env/sim-frame.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/bits.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/brcm_hw_ids.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/common.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/debug.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/fweh.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/fwil.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/sim/sim.h"
#include "third_party/bcmdhd/crossdriver/include/proto/802.11.h"
#include "zircon/errors.h"
#include "zircon/types.h"

namespace wlan::brcmfmac {

namespace wlan_ieee80211 = ::fuchsia::wlan::ieee80211;

#define SIM_FW_CHK_CMD_LEN(dcmd_len, exp_len) \
  (((dcmd_len) < (exp_len)) ? ZX_ERR_INVALID_ARGS : ZX_OK)

#ifdef USE_MFG_FW
const char* kFirmwareVer =
    "wl0: Oct  5 2018 04:50:34 version 7.45.96.34 (r783496 WLTEST) FWID 01-df2d9ead";
#else
const char* kFirmwareVer = "wl0: Sep 10 2018 16:37:38 version 7.35.79 (r487924) FWID 01-c76ab99a";
#endif

const char* kFirmwareCap =
    "ap sta wme 802.11d 802.11h rm cqa cac dualband ampdu ampdu_tx ampdu_rx amsdurx tdls "
    "radio_pwrsave btamp p2p proptxstatus mchan wds dwds p2po anqpo vht-prop-rates dfrts "
    "txpwrcache stbc-tx stbc-rx-1ss epno pfnx wnm bsstrans mfp ndoe rssi_mon cptlv-4 extsae";

struct IovarMetadata {
  std::string name_;
  std::optional<size_t> value_len_;
  IovarSetHandler set_handler_;
  IovarGetHandler get_handler_;
  // Set this field to the offset of the member in sim_iface_entry_t if using the generic
  // IovarIfaceVarGet/IovarIfaceVarSet methods
  std::optional<size_t> ifentry_offset = std::nullopt;
  // Set this field to the address of the member variable if using the generic IovarGet/IovarSet
  // methods
  std::optional<void*> var_addr = std::nullopt;
};

/* Steps to add an iovar handler:
 *
 * 1. If the iovar corresponds to a member variable in SimFirmware, use the generic handlers,
 * IovarSet(), IovarGet() and set the size and address of the variable OR
 * if the iovar corresponds to a member in sim_iface_entry_t , use the generic handlers,
 * IovarIfaceVarSet(), IovarIfaceVarGet() and set the size and offset of the variable.
 * 2. Or else define the handler function as SimFirmware::Iovar<Name>{Set, Get} in sim-fw.{h, cc}.
 * 3. If the iovar exists in kIovarInfoTable: modify the corresponding entry by replacing the
 * nullptr with your new handler.
 */

zx_status_t SimFirmware::SetupIovarTable() {
  // The table initialized with information of all iovars. Note: passing std::nullopt to the second
  // field means that the size check is customized by handler functions, passing std::nullptr to the
  // third or the fourth field means that the handler is not supported.
  const IovarMetadata kIovarInfoTable[] = {
      {"allmulti", sizeof(sim_iface_entry_t::allmulti), &SimFirmware::IovarIfaceVarSet,
       &SimFirmware::IovarIfaceVarGet, offsetof(sim_iface_entry_t, allmulti)},
      {"ampdu_ba_wsize", sizeof(uint32_t), &SimFirmware::IovarSet, &SimFirmware::IovarGet,
       std::nullopt, &ampdu_ba_wsize_},
      {"arp_ol", sizeof(uint32_t), &SimFirmware::IovarSet, &SimFirmware::IovarGet, std::nullopt,
       &arp_ol_},
      {"arpoe", sizeof(uint32_t), &SimFirmware::IovarSet, &SimFirmware::IovarGet, std::nullopt,
       &arpoe_},
      {"assoc_info", sizeof(brcmf_cfg80211_assoc_ielen_le), nullptr,
       &SimFirmware::IovarAssocInfoGet},
      {"assoc_mgr_cmd", sizeof(assoc_mgr_cmd_t), &SimFirmware::IovarAssocMgrCmdSet, nullptr},
      {"assoc_resp_ies", std::nullopt, nullptr, &SimFirmware::IovarAssocRespIesGet},
      {"assoc_retry_max", sizeof(assoc_max_retries_), &SimFirmware::IovarSet,
       &SimFirmware::IovarGet, std::nullopt, &assoc_max_retries_},
      {"auth", sizeof(sim_iface_entry_t::auth_type), &SimFirmware::IovarIfaceVarSet,
       &SimFirmware::IovarIfaceVarGet, offsetof(sim_iface_entry_t, auth_type)},
      {"bcn_timeout", sizeof(uint32_t), &SimFirmware::IovarSet, &SimFirmware::IovarGet,
       std::nullopt, &beacon_timeout_},
      {"bss", sizeof(brcmf_bss_ctrl), &SimFirmware::IovarBssSet, nullptr},
      {"cap", strlen(kFirmwareCap) + 1, nullptr, &SimFirmware::IovarCapGet},
      {"chanspec", sizeof(sim_iface_entry_t::chanspec), &SimFirmware::IovarChanspecSet,
       &SimFirmware::IovarIfaceVarGet, offsetof(sim_iface_entry_t, chanspec)},
      {"country", sizeof(brcmf_fil_country_le), &SimFirmware::IovarSet, &SimFirmware::IovarGet,
       std::nullopt, &country_code_},
      {"crash", sizeof(uint32_t), &SimFirmware::IovarCrashSet, nullptr},
      {"cur_etheraddr", ETH_ALEN, &SimFirmware::IovarCurEtheraddrSet,
       &SimFirmware::IovarCurEtheraddrGet},
      {"escan", sizeof(brcmf_escan_params_le), &SimFirmware::IovarEscanSet, nullptr},
      {"interface_remove", 0, &SimFirmware::IovarInterfaceRemoveSet, nullptr},
      {"join", sizeof(brcmf_ext_join_params_le), &SimFirmware::IovarJoinSet, nullptr},
      {"mchan", sizeof(mchan_), &SimFirmware::IovarSet, &SimFirmware::IovarGet, std::nullopt,
       &mchan_},
      {"mpc", sizeof(mpc_), &SimFirmware::IovarMpcSet, &SimFirmware::IovarGet, std::nullopt, &mpc_},
      {"ndoe", sizeof(ndoe_), &SimFirmware::IovarSet, &SimFirmware::IovarGet, std::nullopt, &ndoe_},
      {"nmode", sizeof(uint32_t), nullptr, &SimFirmware::IovarNmodeGet},
      {"pfn_macaddr", ETH_ALEN, &SimFirmware::IovarPfnMacaddrSet, &SimFirmware::IovarPfnMacaddrGet},
      {"roam_off", sizeof(roam_off_), &SimFirmware::IovarSet, &SimFirmware::IovarGet, std::nullopt,
       &roam_off_},
      {"rrm", sizeof(uint32_t), nullptr, &SimFirmware::IovarRrmGet},
      {"rxchain", sizeof(uint32_t), nullptr, &SimFirmware::IovarRxchainGet},
      {"snr", sizeof(int32_t), nullptr, &SimFirmware::IovarSnrGet},
      {"ssid", sizeof(brcmf_ssid_le), &SimFirmware::IovarSsidSet, nullptr},
      {"stbc_tx", sizeof(int32_t), &SimFirmware::IovarStbcTxSet, &SimFirmware::IovarStbcTxGet},
      {"tlv", sizeof(sim_iface_entry_t::tlv), &SimFirmware::IovarIfaceVarSet,
       &SimFirmware::IovarIfaceVarGet, offsetof(sim_iface_entry_t, tlv)},
      {"txstreams", sizeof(txstreams_), &SimFirmware::IovarTxstreamsSet, &SimFirmware::IovarGet,
       std::nullopt, &txstreams_},
      {"ver", strlen(kFirmwareVer) + 1, nullptr, &SimFirmware::IovarVerGet},
      {"vht_mode", sizeof(uint32_t), nullptr, &SimFirmware::IovarVhtModeGet},
      {"wme_ac_sta", sizeof(edcf_acparam_t) * 4, nullptr, &SimFirmware::IovarWmeAcStaGet},
      {"wme_apsd", sizeof(uint32_t), nullptr, &SimFirmware::IovarWmeApsdGet},
      {"wnm", sizeof(wnm_), &SimFirmware::IovarSet, &SimFirmware::IovarGet, std::nullopt, &wnm_},
      {"wpa_auth", sizeof(sim_iface_entry_t::wpa_auth), &SimFirmware::IovarIfaceVarSet,
       &SimFirmware::IovarIfaceVarGet, offsetof(sim_iface_entry_t, wpa_auth)},
      {"wsec", sizeof(sim_iface_entry_t::wsec), &SimFirmware::IovarIfaceVarSet,
       &SimFirmware::IovarIfaceVarGet, offsetof(sim_iface_entry_t, wsec)},
      {"wsec_key", sizeof(brcmf_wsec_key_le), &SimFirmware::IovarWsecKeySet,
       &SimFirmware::IovarWsecKeyGet},
      {"wstats_counters", sizeof(wl_wstats_cnt_t), nullptr, &SimFirmware::IovarWstatsCountersGet},
      {"buf_key_b4_m4", sizeof(uint32_t), &SimFirmware::IovarSet, &SimFirmware::IovarGet,
       std::nullopt, &buf_key_b4_m4_},
  };

  for (const auto& it : kIovarInfoTable) {
    iovar_table_.insert({it.name_, SimIovar(it.value_len_, this, it.set_handler_, it.get_handler_,
                                            it.ifentry_offset, it.var_addr)});
  }
  return ZX_OK;
}

SimFirmware::SimFirmware(brcmf_simdev* simdev) : simdev_(simdev), hw_(simdev->env) {
  // Configure the chanspec encode/decoder
  d11_inf_.io_type = kIoType;
  brcmu_d11_attach(&d11_inf_);

  // Configure the (simulated) hardware => (simulated) firmware callbacks
  SimHardware::EventHandlers handlers = {
      .rx_handler = std::bind(&SimFirmware::Rx, this, std::placeholders::_1, std::placeholders::_2),
  };
  hw_.SetCallbacks(handlers);
  country_code_ = {};

  // The real FW always creates the first interface
  struct brcmf_mbss_ssid_le default_mbss = {};
  if (HandleIfaceTblReq(true, &default_mbss, nullptr, 0) != ZX_OK) {
    ZX_PANIC("Unable to create default interface");
  }

  if (SetupIovarTable() != ZX_OK) {
    ZX_PANIC("Unable to setup Iovar Table");
  }
}

SimFirmware::~SimFirmware() = default;

simulation::StationIfc* SimFirmware::GetHardwareIfc() { return &hw_; }

void SimFirmware::GetChipInfo(uint32_t* chip, uint32_t* chiprev) {
  *chip = BRCM_CC_4356_CHIP_ID;
  *chiprev = 2;
}

int32_t SimFirmware::GetPM() { return power_mode_; }

zx_status_t SimFirmware::BusPreinit() {
  // Currently nothing to do
  return ZX_OK;
}

void SimFirmware::BusStop() { BRCMF_ERR("%s unimplemented", __FUNCTION__); }

// Returns a buffer that can be used for BCDC-formatted communications, with the requested
// payload size and an initialized BCDC header. "data_offset" represents any signalling offset
// (in words) and "offset_out" represents the offset of the payload within the returned buffer.
std::unique_ptr<std::vector<uint8_t>> SimFirmware::CreateBcdcBuffer(int16_t ifidx,
                                                                    size_t requested_size,
                                                                    size_t* offset_out) {
  size_t header_size = sizeof(brcmf_proto_bcdc_header);
  size_t total_size = header_size + requested_size;

  auto buf = std::make_unique<std::vector<uint8_t>>(total_size);
  auto header = reinterpret_cast<brcmf_proto_bcdc_header*>(buf->data());

  header->flags = (BCDC_PROTO_VER << BCDC_FLAG_VER_SHIFT) & BCDC_FLAG_VER_MASK;
  header->priority = 0xff & BCDC_PRIORITY_MASK;
  header->flags2 = 0;
  BCDC_SET_IF_IDX(header, ifidx);

  // Data immediately follows the header
  header->data_offset = 0;

  *offset_out = header_size;
  return buf;
}

// Set or get the value of an iovar. The format of the message is a null-terminated string
// containing the iovar name, followed by the value to assign to that iovar.
zx_status_t SimFirmware::BcdcVarOp(uint16_t ifidx, brcmf_proto_bcdc_dcmd* dcmd, uint8_t* data,
                                   size_t len, bool is_set) {
  zx_status_t status = ZX_OK;

  if (is_set) {
    // The command consists of a NUL-terminated name, followed by a value.
    const char* const name_begin = reinterpret_cast<char*>(data);
    const char* const name_end = static_cast<const char*>(memchr(name_begin, '\0', dcmd->len));
    if (name_end == nullptr) {
      BRCMF_DBG(SIM, "SET_VAR: iovar name not null-terminated");
      return ZX_ERR_INVALID_ARGS;
    }
    const char* const value_begin = name_end + 1;
    const size_t value_size = dcmd->len - (value_begin - name_begin);

    // Since we're passing the value as a buffer down to users that may expect to be able to cast
    // directly into it, make a suitably aligned copy here.
    static constexpr auto align_val = static_cast<std::align_val_t>(alignof(std::max_align_t));
    const auto aligned_delete = [](char* buffer) { operator delete[](buffer, align_val); };
    std::unique_ptr<char, decltype(aligned_delete)> value_buffer(
        static_cast<char*>(operator new[](value_size, align_val)), aligned_delete);
    std::memcpy(value_buffer.get(), value_begin, value_size);

    // IovarsSet returns the input unchanged
    status = IovarsSet(ifidx, name_begin, value_buffer.get(), value_size, &dcmd->status);
  } else {
    // IovarsGet modifies the buffer in-place
    status = IovarsGet(ifidx, reinterpret_cast<const char*>(data), data, dcmd->len, &dcmd->status);
    if (dcmd->status != BCME_OK) {
      dcmd->flags |= BCDC_DCMD_ERROR;
    }
    BRCMF_DBG(SIM, "dcmd->status=%s dcmd->flags=0x%08x", brcmf_fil_get_errstr(dcmd->status),
              dcmd->flags);
  }

  if (status == ZX_OK || status == ZX_ERR_IO_REFUSED) {
    if (dcmd->status != BCME_OK)
      dcmd->flags |= BCDC_DCMD_ERROR;
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
  uint32_t value;
  uint32_t ifidx;

  if (len < hdr_size) {
    BRCMF_DBG(SIM, "Message length (%u) smaller than BCDC header size (%zd)", len, hdr_size);
    return ZX_ERR_INVALID_ARGS;
  }
  dcmd = reinterpret_cast<brcmf_proto_bcdc_dcmd*>(msg);
  // The variable-length payload immediately follows the header
  uint8_t* data = reinterpret_cast<uint8_t*>(dcmd) + hdr_size;
  size_t data_len = len - hdr_size;

  if (dcmd->len > data_len) {
    BRCMF_DBG(SIM, "BCDC total message length (%zd) exceeds buffer size (%u)", dcmd->len + hdr_size,
              len);
    // The real firmware allows the true buffer size (dcmd->len) to exceed the length of the txctl
    // itself (len - hdr_size). For an iovar get, we know this is allowed, so the sim firmware
    // should let such a call through.
    if (dcmd->cmd != BRCMF_C_GET_VAR) {
      return ZX_ERR_INVALID_ARGS;
    }
  }

  // Retrieve ifidx from the command and validate if the corresponding
  // IF entry was previously allocated.
  ifidx = BCDC_DCMD_IFIDX(dcmd->flags);
  if (ifidx >= kMaxIfSupported || !iface_tbl_[ifidx].allocated) {
    BRCMF_DBG(SIM, "IF idx: %d invalid or not allocated", ifidx);
    return ZX_ERR_INVALID_ARGS;
  }

  zx_status_t status;
  bcme_status_t fw_err;
  if (err_inj_.CheckIfErrInjCmdEnabled(dcmd->cmd, &status, &fw_err, ifidx)) {
    if (status == ZX_OK) {
      // If the transmission status is ZX_OK, customize the firmware error code which will be
      // sent back through bcdc response.
      if (fw_err != BCME_OK) {
        dcmd->flags |= BCDC_DCMD_ERROR;
      }
      dcmd->status = fw_err;
      bcdc_response_.Set(msg, len);
    }
    // If the transmission status is not ZX_OK, return the error for this command transmission.
    return status;
  }

  // If an err event has been injected for this command, then send event to driver and return.
  brcmf_fweh_event_code ecode;
  brcmf_fweh_event_status_t estatus;
  status_code_t reason;
  uint16_t flags;
  if (err_inj_.CheckIfErrEventInjCmdEnabled(dcmd->cmd, ecode, estatus, reason, flags, ifidx)) {
    SendEventToDriver(0, nullptr, ecode, estatus, ifidx, nullptr, flags, reason, kZeroMac,
                      kAssocEventDelay);
    return ZX_OK;
  }

  status = ZX_OK;
  switch (dcmd->cmd) {
    // Get/Set a firmware IOVAR. This message is comprised of a NULL-terminated string
    // for the variable name, followed by the value to assign to it.
    case BRCMF_C_SET_VAR:
    case BRCMF_C_GET_VAR:
      return BcdcVarOp(ifidx, dcmd, data, len, dcmd->cmd == BRCMF_C_SET_VAR);
      break;
    case BRCMF_C_GET_BANDLIST: {
      const uint32_t bandlist[] = {
          2,  // Number of bands
          WLC_BAND_2G,
          WLC_BAND_5G,
      };
      if ((status = SIM_FW_CHK_CMD_LEN(dcmd->len, sizeof(bandlist))) == ZX_OK) {
        memcpy(data, bandlist, sizeof(bandlist));
      }
      break;
    }
    case BRCMF_C_GET_REVINFO: {
      struct brcmf_rev_info_le rev_info;
      hw_.GetRevInfo(&rev_info);
      if ((status = SIM_FW_CHK_CMD_LEN(dcmd->len, sizeof(rev_info))) == ZX_OK) {
        memcpy(data, &rev_info, sizeof(rev_info));
      }
      break;
    }
    case BRCMF_C_GET_VERSION: {
      // GET_VERSION is a bit of a misnomer. It's really the 802.11 supported spec
      // (e.g., n or ac).
      if ((status = SIM_FW_CHK_CMD_LEN(dcmd->len, sizeof(kIoType))) == ZX_OK) {
        std::memcpy(data, &kIoType, sizeof(kIoType));
      }
      break;
    }
    case BRCMF_C_GET_GET_PKTCNTS: {
      if (!iface_tbl_[kClientIfidx].allocated) {
        BRCMF_ERR("Client iface has not been allocated.");
        return ZX_ERR_BAD_STATE;
      }
      // Fake packet counter arbitrary values.
      const brcmf_pktcnt_le sim_pktcnt = {
          .rx_good_pkt = 5,
          .rx_bad_pkt = 4,
          .tx_good_pkt = 3,
          .tx_bad_pkt = 2,
          .rx_ocast_good_pkt = 1,
      };

      if ((status = SIM_FW_CHK_CMD_LEN(dcmd->len, sizeof(brcmf_pktcnt_le))) == ZX_OK) {
        std::memcpy(data, &sim_pktcnt, sizeof(brcmf_pktcnt_le));
      }
      break;
    }
    case BRCMF_C_SET_PASSIVE_SCAN: {
      // Specify whether to use a passive scan by default (instead of an active scan)
      if ((status = SIM_FW_CHK_CMD_LEN(dcmd->len, sizeof(uint32_t))) == ZX_OK) {
        value = *(reinterpret_cast<uint32_t*>(data));
        default_passive_scan_ = value > 0;
      }
      break;
    }
    case BRCMF_C_SET_PROMISC:
      // Set promiscuous mode
      if ((status = SIM_FW_CHK_CMD_LEN(dcmd->len, sizeof(uint32_t))) == ZX_OK) {
        value = *(reinterpret_cast<uint32_t*>(data));
        ZX_ASSERT_MSG(!value, "Promiscuous mode not supported in simulator");
      }
      break;
    case BRCMF_C_SET_SCAN_PASSIVE_TIME:
      if ((status = SIM_FW_CHK_CMD_LEN(dcmd->len, sizeof(default_passive_time_))) == ZX_OK) {
        default_passive_time_ = *(reinterpret_cast<uint32_t*>(data));
      }
      break;
    case BRCMF_C_SET_PM:
      if ((status = SIM_FW_CHK_CMD_LEN(dcmd->len, sizeof(power_mode_))) == ZX_OK) {
        power_mode_ = *(reinterpret_cast<int32_t*>(data));
      }
      break;
    case BRCMF_C_GET_PM:
      if ((status = SIM_FW_CHK_CMD_LEN(dcmd->len, sizeof(power_mode_))) == ZX_OK) {
        std::memcpy(data, &power_mode_, sizeof(power_mode_));
      }
      break;
    case BRCMF_C_SET_SCAN_CHANNEL_TIME:
    case BRCMF_C_SET_SCAN_UNASSOC_TIME:
      BRCMF_DBG(SIM, "Ignoring firmware message %d", dcmd->cmd);
      break;
    case BRCMF_C_DISASSOC: {
      if ((status = SIM_FW_CHK_CMD_LEN(dcmd->len, sizeof(brcmf_scb_val_le))) == ZX_OK) {
        if (iface_tbl_[kClientIfidx].allocated && kClientIfidx == ifidx) {
          // Initiate Disassoc from AP
          auto scb_val = reinterpret_cast<brcmf_scb_val_le*>(data);
          auto req_bssid = reinterpret_cast<common::MacAddr*>(scb_val->ea);
          if (!assoc_state_.opts) {
            BRCMF_DBG(SIM, "BRCMF_C_DISASSOC issued without assoc - ignore");
            break;
          }
          common::MacAddr bssid(assoc_state_.opts->bssid);
          ZX_ASSERT(bssid == *req_bssid);
          DisassocLocalClient(static_cast<wlan_ieee80211::ReasonCode>(scb_val->val));
        } else if (softap_ifidx_ != std::nullopt && softap_ifidx_ == ifidx) {
          BRCMF_ERR("This iovar is not expected to be used on softAP iface.");
          return ZX_ERR_IO_REFUSED;
        }
      } else {
        // Triggered by link down event in driver (no data)
        if (assoc_state_.state == AssocState::ASSOCIATED) {
          SetAssocState(AssocState::NOT_ASSOCIATED);
        }
        status = ZX_OK;
      }
      break;
    }
    case BRCMF_C_SCB_DEAUTHENTICATE_FOR_REASON: {
      if ((status = SIM_FW_CHK_CMD_LEN(dcmd->len, sizeof(brcmf_scb_val_le))) == ZX_OK) {
        status = ZX_ERR_IO;
        if (softap_ifidx_ != std::nullopt && softap_ifidx_ == ifidx) {
          auto scb_val = reinterpret_cast<brcmf_scb_val_le*>(data);
          auto req_mac = reinterpret_cast<common::MacAddr*>(scb_val->ea);
          auto client = FindClient(*req_mac);
          if (client) {
            // Deauthenticate also disassocs the client
            SendEventToDriver(0, nullptr, BRCMF_E_DEAUTH, BRCMF_E_STATUS_SUCCESS,
                              softap_ifidx_.value(), nullptr, 0, 0, *req_mac);
            SendEventToDriver(0, nullptr, BRCMF_E_DISASSOC, BRCMF_E_STATUS_SUCCESS,
                              softap_ifidx_.value(), nullptr, 0, 0, *req_mac);
            iface_tbl_[softap_ifidx_.value()].ap_config.clients.remove(client);
            status = ZX_OK;
          }
        }
      }
      break;
    }
    case BRCMF_C_SET_ROAM_TRIGGER:
    case BRCMF_C_SET_ROAM_DELTA:
      break;
    case BRCMF_C_UP:
      // The value in the IOVAR does not matter (according to Broadcom)
      iface_tbl_[ifidx].is_up = 1;
      break;
    case BRCMF_C_DOWN: {
      // The value in the IOVAR does not matter (according to Broadcom)
      // If any of the IFs are operational (i.e., client is associated or
      // softap is started) disconnect as appropriate.
      if (softap_ifidx_ == ifidx) {
        StopSoftAP(softap_ifidx_.value());
      } else if (kClientIfidx == ifidx) {
        DisassocLocalClient(wlan_ieee80211::ReasonCode::LEAVING_NETWORK_DISASSOC);
      }
      iface_tbl_[ifidx].is_up = 0;
      break;
    }
    case BRCMF_C_SET_INFRA:
      if ((status = SIM_FW_CHK_CMD_LEN(dcmd->len, sizeof(uint32_t))) == ZX_OK) {
        iface_tbl_[ifidx].ap_config.infra_mode = *(reinterpret_cast<uint32_t*>(data));
      }
      break;
    case BRCMF_C_SET_AP:
      if ((status = SIM_FW_CHK_CMD_LEN(dcmd->len, sizeof(uint32_t))) == ZX_OK) {
        value = *(reinterpret_cast<uint32_t*>(data));
#ifdef USE_MFG_FW
        // Manufacturer firmware does not allow turning off AP if it is already off
        if (!value && !iface_tbl_[ifidx].ap_mode) {
          status = ZX_ERR_IO_REFUSED;
          break;
        }
#endif
        if (value) {
          ZX_ASSERT_MSG(iface_tbl_[ifidx].ap_config.infra_mode, "Only Infra mode AP is supported");
          iface_tbl_[ifidx].ap_mode = true;
        } else
          iface_tbl_[ifidx].ap_mode = false;
        softap_ifidx_ = std::nullopt;
      }
      break;
    case BRCMF_C_SET_BCNPRD:
      if ((status = SIM_FW_CHK_CMD_LEN(dcmd->len, sizeof(uint32_t))) == ZX_OK) {
        // Beacon period
        iface_tbl_[ifidx].ap_config.beacon_period = *(reinterpret_cast<uint32_t*>(data));
      }
      break;
    case BRCMF_C_SET_DTIMPRD:
      if ((status = SIM_FW_CHK_CMD_LEN(dcmd->len, sizeof(uint32_t))) == ZX_OK) {
        // DTIM
        iface_tbl_[ifidx].ap_config.dtim_period = *(reinterpret_cast<uint32_t*>(data));
      }
      break;
    case BRCMF_C_SET_FAKEFRAG:
      if ((status = SIM_FW_CHK_CMD_LEN(dcmd->len, sizeof(uint32_t))) == ZX_OK) {
        // fakefrag
        fakefrag_ = *(reinterpret_cast<uint32_t*>(data));
      }
      break;
    case BRCMF_C_GET_FAKEFRAG:
      if ((status = SIM_FW_CHK_CMD_LEN(dcmd->len, sizeof(uint32_t))) == ZX_OK) {
        // fakefrag
        std::memcpy(data, &fakefrag_, sizeof(uint32_t));
      }
      break;
    case BRCMF_C_SET_SSID: {
      if ((status = SIM_FW_CHK_CMD_LEN(dcmd->len, sizeof(brcmf_join_params))) == ZX_OK) {
        auto join_params = (reinterpret_cast<brcmf_join_params*>(data));
        if (iface_tbl_[ifidx].ap_mode == true) {
          iface_tbl_[ifidx].ap_config.ssid = join_params->ssid_le;
          if (join_params->ssid_le.SSID_len) {
            // non-zero SSID - assume AP start
            ZX_ASSERT(iface_tbl_[ifidx].ap_config.ap_started == false);
            // Schedule a Link Event to be sent to driver (simulating behviour
            // in real HW).
            ScheduleLinkEvent(kStartAPLinkEventDelay, ifidx);
            iface_tbl_[ifidx].ap_config.ap_started = true;
            softap_ifidx_ = ifidx;

            // Set the channel to the value specified in "chanspec" iovar
            wlan_channel_t channel;
            chanspec_to_channel(&d11_inf_, iface_tbl_[ifidx].chanspec, &channel);
            hw_.SetChannel(channel);

            // Set the auth_type of softap to OPEN.
            iface_tbl_[ifidx].auth_type = BRCMF_AUTH_MODE_OPEN;
            iface_tbl_[ifidx].is_up = 1;

            // And Enable Rx
            hw_.EnableRx();
            // Send the AP_STARTED event after a delay
            SendEventToDriver(0, nullptr, BRCMF_E_AP_STARTED, BRCMF_E_STATUS_SUCCESS,
                              softap_ifidx_.value(), nullptr, 0, 0, kZeroMac, kApStartedEventDelay);
          } else {
            // AP stop
            // Note that SoftAP may have been only partially started (maybe one
            // of the iovars in the sequence fails).
            StopSoftAP(ifidx);
            BRCMF_DBG(SIM, "AP Stop processed");
          }
        } else {
          // When iface_tbl_[ifidx].ap_mode == false, start an association
          ZX_ASSERT(join_params->params_le.chanspec_num == 1);
          if (ifidx != kClientIfidx) {
            BRCMF_ERR("Not starting an assoc on client iface.");
            return ZX_ERR_INVALID_ARGS;
          }

          // This is a start point of authentication and association operation, make sure the client
          // iface is allocated.
          if (!iface_tbl_[kClientIfidx].allocated) {
            BRCMF_ERR("client iface has not been allocated.");
            return ZX_ERR_BAD_STATE;
          }

          if (scan_state_.state == ScanState::SCANNING) {
            BRCMF_DBG(SIM, "Scan in progress, aborting scan.");
            hw_.RequestCallback(
                std::bind(&SimFirmware::ScanComplete, this, BRCMF_E_STATUS_NEWASSOC),
                kAbortScanDelay);
          }

          auto assoc_opts = std::make_unique<AssocOpts>();
          wlan_channel_t channel;

          chanspec_to_channel(&d11_inf_, join_params->params_le.chanspec_list[0], &channel);
          iface_tbl_[ifidx].chanspec = join_params->params_le.chanspec_list[0];
          memcpy(assoc_opts->bssid.byte, join_params->params_le.bssid, ETH_ALEN);
          assoc_opts->ssid.len = join_params->ssid_le.SSID_len;
          memcpy(assoc_opts->ssid.data, join_params->ssid_le.SSID,
                 wlan_ieee80211::MAX_SSID_BYTE_LEN);
          AssocInit(std::move(assoc_opts), channel);
          iface_tbl_[ifidx].is_up = 1;

          BRCMF_DBG(SIM, "Auth start from C_SET_SSID");
          // Schedule AuthStart to break the call chain for SAE authentication.
          hw_.RequestCallback(std::bind(&SimFirmware::AuthStart, this), zx::msec(0));
        }
      }
      break;
    }
    case BRCMF_C_GET_RSSI: {
      if ((status = SIM_FW_CHK_CMD_LEN(dcmd->len, sizeof(int32_t))) == ZX_OK) {
        int32_t rssi = -20;
        std::memcpy(data, &rssi, sizeof(rssi));
      }
      break;
    }
    case BRCMF_C_SCAN: {
      // For now this command is not used to start any kind of scan, the iovar "escan" is the entry
      // of scan operation. This command is now only used to abort an escan when channel[0] in the
      // parameter is -1.
      auto scan_params = (reinterpret_cast<brcmf_scan_params_le*>(data));
      ZX_ASSERT_MSG(scan_params->channel_list[0] == static_cast<uint16_t>(-1),
                    "BRCMF_C_SCAN should only be used for aborting an escan now.");

      // Abort the scan. If there is no scan in progress, this becomes a nop.
      if (scan_state_.state == ScanState::SCANNING) {
        hw_.RequestCallback(std::bind(&SimFirmware::ScanComplete, this, BRCMF_E_STATUS_ABORT),
                            kAbortScanDelay);
      }
      break;
    }

    default:
      BRCMF_DBG(SIM, "Unimplemented firmware message %d", dcmd->cmd);
      return ZX_ERR_NOT_SUPPORTED;
  }
  if (status == ZX_OK) {
    bcdc_response_.Set(msg, len);
  }
  return status;
}

zx_status_t SimFirmware::BusTxData(struct brcmf_netbuf* netbuf) {
  if (netbuf->len < BCDC_HEADER_LEN + sizeof(ethhdr)) {
    BRCMF_DBG(SIM, "Data netbuf (%u) smaller than BCDC + ethernet header %lu\n", netbuf->len,
              BCDC_HEADER_LEN + sizeof(ethhdr));
    return ZX_ERR_INVALID_ARGS;
  }

  // Get ifidx from bcdc header
  brcmf_proto_bcdc_header* bcdc_header = (struct brcmf_proto_bcdc_header*)(netbuf);
  uint16_t ifidx = BCDC_GET_IF_IDX(bcdc_header);
  // For now we only support data transmission for client iface, another logic path should be added
  // if we need to support data transmission for softap iface.
  ZX_ASSERT_MSG(ifidx == kClientIfidx,
                "Only data transmission for client iface is supported for now.");
  // Ignore the BCDC Header
  ethhdr* ethFrame = reinterpret_cast<ethhdr*>(netbuf->data + BCDC_HEADER_LEN);

  // Build MAC frame
  simulation::SimQosDataFrame dataFrame{};

  // we can't send data frames if we aren't associated with anything
  if (assoc_state_.opts == nullptr) {
    return ZX_ERR_BAD_STATE;
  }

  // IEEE Std 802.11-2016, 9.4.1.4
  switch (assoc_state_.opts->bss_type) {
    case BSS_TYPE_INDEPENDENT:
      // We don't support INDEPENDENT
      ZX_ASSERT_MSG(false, "Non-infrastructure types not currently supported by sim-fw\n");
      dataFrame.toDS_ = 0;
      dataFrame.fromDS_ = 0;
      dataFrame.addr1_ = common::MacAddr(ethFrame->h_dest);
      dataFrame.addr2_ = common::MacAddr(ethFrame->h_source);
      dataFrame.addr3_ = assoc_state_.opts->bssid;
      break;
    case BSS_TYPE_INFRASTRUCTURE:
      dataFrame.toDS_ = 1;
      dataFrame.fromDS_ = 0;
      dataFrame.addr1_ = assoc_state_.opts->bssid;
      dataFrame.addr2_ = common::MacAddr(ethFrame->h_source);
      dataFrame.addr3_ = common::MacAddr(ethFrame->h_dest);
      // Sim FW currently doesn't distinguish QoS from non-QoS association. If it does, this should
      // only be set for a QoS association.
      dataFrame.qosControl_ = netbuf->priority;
      break;
    default:
      // TODO: support other bss types such as Mesh
      ZX_ASSERT_MSG(false, "Non-infrastructure types not currently supported by sim-fw\n");
      break;
  }

  // For now, since the LLC information would always be the same aside from the redundant ethernet
  // type (Table M2 IEEE 802.11 2016). we will not append/parse LLC headers
  uint32_t payload_size = netbuf->len - BCDC_HEADER_LEN - sizeof(ethhdr);
  dataFrame.payload_.resize(payload_size);
  memcpy(dataFrame.payload_.data(), netbuf->data + BCDC_HEADER_LEN + sizeof(ethhdr), payload_size);

  hw_.Tx(dataFrame);

  if (netbuf->ifc_netbuf) {
    netbuf->ifc_netbuf->Return(ZX_OK);
  }

  brcmf_netbuf_free(netbuf);

  return ZX_OK;
}

// Stop the SoftAP
void SimFirmware::StopSoftAP(uint16_t ifidx) {
  // Disassoc and remove all the associated clients
  for (auto client : iface_tbl_[ifidx].ap_config.clients) {
    simulation::SimDisassocReqFrame disassoc_req_frame(
        iface_tbl_[ifidx].mac_addr, client->mac_addr,
        wlan_ieee80211::ReasonCode::UNSPECIFIED_REASON);
    hw_.Tx(disassoc_req_frame);
  }
  iface_tbl_[ifidx].ap_config.clients.clear();
  SendEventToDriver(0, nullptr, BRCMF_E_LINK, BRCMF_E_STATUS_SUCCESS, ifidx);
  iface_tbl_[ifidx].ap_config.ap_started = false;
  iface_tbl_[ifidx].chanspec = 0;
  softap_ifidx_ = std::nullopt;
}

void SimFirmware::SendAPStartLinkEvent(uint16_t ifidx) {
  SendEventToDriver(0, nullptr, BRCMF_E_LINK, BRCMF_E_STATUS_SUCCESS, ifidx, nullptr,
                    BRCMF_EVENT_MSG_LINK);
}

void SimFirmware::ScheduleLinkEvent(zx::duration when, uint16_t ifidx) {
  hw_.RequestCallback(std::bind(&SimFirmware::SendAPStartLinkEvent, this, ifidx), when);
}

uint16_t SimFirmware::GetNumClients(uint16_t ifidx) {
  if (softap_ifidx_ == std::nullopt) {
    BRCMF_ERR("SoftAP iface has not been allocated or started.");
    return 0;
  }

  if (ifidx != softap_ifidx_) {
    BRCMF_ERR("Input iface is not SoftAP iface.");
    return 0;
  }

  return iface_tbl_[softap_ifidx_.value()].ap_config.clients.size();
}

void SimFirmware::TriggerFirmwareDisassoc(::fuchsia::wlan::ieee80211::ReasonCode reason) {
  DisassocLocalClient(reason);
}

// Process an RX CTL message. We simply pass back the results of the previous TX CTL
// operation, which has been stored in bcdc_response_. In real hardware, we may have to
// indicate that the TX CTL operation has not completed. In simulated hardware, we perform
// all operations synchronously.
//
// This function is a simplified version of brcmf_sdio_bus_rxctl. As much of the
// response `msg_` last stored by BcdcResponse::Set() is written to `data` as possible,
// and the actual size of `msg_` is returned in `len_out`.
zx_status_t SimFirmware::BusRxCtl(unsigned char* msg, uint len, int* rxlen_out) {
  if (bcdc_response_.IsClear()) {
    BRCMF_ERR("no response available");
    return ZX_ERR_UNAVAILABLE;
  }

  zx_status_t status = bcdc_response_.Get(msg, len, rxlen_out);

  // Responses are not re-sent on subsequent requests
  bcdc_response_.Clear();
  return status;
}

zx_status_t SimFirmware::BusFlushTxQueue(int /* ifidx */) {
  // Sim firmware doesn't have a queue
  return ZX_OK;
}

zx_status_t SimFirmware::BusGetBootloaderMacAddr(uint8_t* mac_addr) {
  // Simulate a fixed MAC address.
  static uint8_t fixed_random_macaddr[ETH_ALEN] = {};
  static bool memoized = false;
  zx_status_t status;

  // Use a value provided by the error injector, if one has been set
  auto bootloader_mac_addr = err_inj_.BootloaderMacAddr();
  if (bootloader_mac_addr) {
    memcpy(mac_addr, bootloader_mac_addr->byte, ETH_ALEN);
    return ZX_OK;
  }

  if (!memoized) {
    BRCMF_INFO(
        "Bootloader MAC address not available to simulated firmware. Generating a random mac "
        "address to simulate a fixed MAC address.");
    status = brcmf_gen_random_mac_addr(fixed_random_macaddr);
    if (status != ZX_OK) {
      return status;
    }

    BRCMF_INFO("Generated random mac address: " FMT_MAC, FMT_MAC_ARGS(fixed_random_macaddr));
    memoized = true;
  }

  memcpy(mac_addr, fixed_random_macaddr, sizeof(fixed_random_macaddr));
  return ZX_OK;
}

void SimFirmware::BcdcResponse::Clear() { len_ = 0; }

zx_status_t SimFirmware::BcdcResponse::Get(uint8_t* msg, size_t len, int* rxlen_out) {
  memcpy(msg, msg_, std::min(len, len_));
  ZX_ASSERT_MSG(len_ <= INT_MAX, "Response length exceeds INT_MAX");
  *rxlen_out = static_cast<int>(len_);
  return ZX_OK;
}

bool SimFirmware::BcdcResponse::IsClear() { return len_ == 0; }

void SimFirmware::BcdcResponse::Set(uint8_t* data, size_t new_len) {
  ZX_DEBUG_ASSERT(new_len <= sizeof(msg_));
  len_ = new_len;
  memcpy(msg_, data, new_len);
}

zx_status_t SimFirmware::HandleIfaceTblReq(const bool add_entry, const void* data,
                                           uint8_t* iface_id, int32_t bsscfgidx) {
  if (add_entry) {
    // Allocate the first available entry
    for (int i = 0; i < kMaxIfSupported; i++) {
      if (!iface_tbl_[i].allocated) {
        iface_tbl_[i].allocated = true;
        iface_tbl_[i].iface_id = i;
        iface_tbl_[i].bsscfgidx = bsscfgidx;
        if (iface_id)
          *iface_id = i;
        return ZX_OK;
      }
    }
  } else {
    for (int i = 0; i < kMaxIfSupported; i++) {
      if (iface_tbl_[i].allocated && iface_tbl_[i].bsscfgidx == bsscfgidx) {
        if (iface_id)
          *iface_id = iface_tbl_[i].iface_id;
        // If AP is in started state, send disassoc req to all clients
        if (iface_tbl_[i].ap_mode) {
          if (iface_tbl_[i].ap_config.ap_started) {
            BRCMF_DBG(SIM, "AP is still started...disassoc all clients");
            for (auto client : iface_tbl_[i].ap_config.clients) {
              simulation::SimDisassocReqFrame disassoc_req_frame(
                  iface_tbl_[i].mac_addr, client->mac_addr,
                  wlan_ieee80211::ReasonCode::UNSPECIFIED_REASON);
              hw_.Tx(disassoc_req_frame);
            }
          }
          // Clear out the clients
          iface_tbl_[i].ap_config.clients.clear();
          softap_ifidx_ = std::nullopt;
        }
        iface_tbl_[i] = {};
        BRCMF_DBG(SIM, "Interface Delete ifidx: %d done", i);
        return ZX_OK;
      }
    }
  }
  return ZX_ERR_IO;
}

zx_status_t SimFirmware::HandleIfaceRequest(const bool add_iface, const void* data,
                                            const size_t len, int32_t bsscfgidx) {
  uint8_t iface_id;
  size_t payload_size = sizeof(brcmf_if_event);
  auto buf = std::make_unique<std::vector<uint8_t>>(payload_size);

  uint8_t* buffer_data = buf->data();
  struct brcmf_if_event* ifevent = reinterpret_cast<brcmf_if_event*>(buffer_data);

  ifevent->role = 1;

  if (HandleIfaceTblReq(add_iface, data, &iface_id, bsscfgidx) == ZX_OK) {
    if (add_iface) {
      ifevent->action = BRCMF_E_IF_ADD;
      ifevent->bsscfgidx = bsscfgidx;
    } else {
      ifevent->action = BRCMF_E_IF_DEL;
      ifevent->bsscfgidx = bsscfgidx;
    }
    ifevent->ifidx = iface_id;
    char ifname[IFNAMSIZ];
    sprintf(ifname, "wl0.%d", iface_id);
    SendEventToDriver(payload_size, std::move(buf), BRCMF_E_IF, BRCMF_E_STATUS_SUCCESS, iface_id,
                      ifname);
  } else {
    SendEventToDriver(payload_size, std::move(buf), BRCMF_E_IF, BRCMF_E_STATUS_ERROR, 0);
  }
  return ZX_OK;
}

// Handle association request from a client to the SoftAP interface
// ifidx is expected to be a valid index (allocated and configured as AP)
#define TWO_ZERO_LEN_TLVS_LEN (4)
void SimFirmware::HandleAssocReq(std::shared_ptr<const simulation::SimAssocReqFrame> frame) {
  auto buf = std::make_unique<std::vector<uint8_t>>(TWO_ZERO_LEN_TLVS_LEN);
  uint8_t* tlv_buf = buf->data();
  // The driver expects ssid and rsne in TLV format, just fake it for now
  *tlv_buf++ = WLAN_IE_TYPE_SSID;
  *tlv_buf++ = 0;
  *tlv_buf++ = WLAN_IE_TYPE_RSNE;
  *tlv_buf++ = 0;

  // Check client state
  auto client = FindClient(frame->src_addr_);
  if (client) {
    if (client->state == Client::ASSOCIATED) {
      // Client already associated, send a REASSOC_IND event to driver
      SendEventToDriver(TWO_ZERO_LEN_TLVS_LEN, std::move(buf), BRCMF_E_REASSOC_IND,
                        BRCMF_E_STATUS_SUCCESS, softap_ifidx_.value(), nullptr, 0, 0,
                        frame->src_addr_);
    } else if (client->state == Client::AUTHENTICATED) {
      // Indicate Assoc success to driver - by sendind ASSOC_IND
      SendEventToDriver(TWO_ZERO_LEN_TLVS_LEN, std::move(buf), BRCMF_E_ASSOC_IND,
                        BRCMF_E_STATUS_SUCCESS, softap_ifidx_.value(), nullptr, 0, 0,
                        frame->src_addr_);
      client->state = Client::ASSOCIATED;
    }
    simulation::SimAssocRespFrame assoc_resp_frame(frame->bssid_, frame->src_addr_,
                                                   wlan_ieee80211::StatusCode::SUCCESS);
    hw_.Tx(assoc_resp_frame);
    BRCMF_DBG(SIM, "Assoc done Num Clients : %lu",
              iface_tbl_[softap_ifidx_.value()].ap_config.clients.size());
  } else {
    // Client cannot start association before getting authenticated.
    // IEEE 802.11-2016 11.3.6.3 specifies that the SME issues a NOT_AUTHENTICATED in the
    // MLME-AUTHENTICATE.response message here.  Unfortunately, NOT_AUTHENTICATED is in the
    // ReasonCode namespace, and the generated MAC frame requires a StatusCode for the association
    // response frame.  This is a known fault in 802.11-2016, that may be fixed in the next
    // revision.  Fortunately for us, the NOT_AUTHENTICATED value of 9 in the ReasonCode namespace
    // falls in a reserved-unused value in the StatusCode namespace, so we will just return the
    // casted value as (presumably) existing APs may err in doing.
    simulation::SimAssocRespFrame assoc_resp_frame(
        frame->bssid_, frame->src_addr_,
        static_cast<wlan_ieee80211::StatusCode>(wlan_ieee80211::ReasonCode::NOT_AUTHENTICATED));
    hw_.Tx(assoc_resp_frame);
    BRCMF_DBG(SIM, "Assoc fail, should be authenticated first.");
  }
}

void SimFirmware::AssocInit(std::unique_ptr<AssocOpts> assoc_opts, wlan_channel_t& channel) {
  SetAssocState(AssocState::ASSOCIATING);
  assoc_state_.opts = std::move(assoc_opts);
  assoc_state_.num_attempts = 0;

  uint16_t chanspec = channel_to_chanspec(&d11_inf_, &channel);
  SetIFChanspec(kClientIfidx, chanspec);
  hw_.SetChannel(channel);
  hw_.EnableRx();
}

void SimFirmware::AssocScanResultSeen(const ScanResult& scan_result) {
  std::optional<cssid_t> scan_result_ssid;
  for (const auto& ie : scan_result.ies) {
    if (ie != nullptr && ie->IeType() == simulation::InformationElement::IE_TYPE_SSID) {
      auto ssid_ie = std::static_pointer_cast<simulation::SsidInformationElement>(ie);
      scan_result_ssid.emplace(ssid_ie->ssid_);
    }
  }
  // Check ssid filter
  if (scan_state_.opts->ssid && scan_result_ssid.has_value()) {
    ZX_ASSERT(scan_state_.opts->ssid->len <= sizeof(scan_state_.opts->ssid->data));
    if (scan_result_ssid->len != scan_state_.opts->ssid->len) {
      return;
    }
    if (std::memcmp(scan_result_ssid->data, scan_state_.opts->ssid->data, scan_result_ssid->len) !=
        0) {
      return;
    }
  }

  // Check bssid filter
  if ((scan_state_.opts->bssid) && (scan_result.bssid != *scan_state_.opts->bssid)) {
    return;
  }

  assoc_state_.scan_results.push_back(scan_result);
  // The correct probe response has been received, stop scanning.
  ScanComplete(BRCMF_E_STATUS_SUCCESS);
}

void SimFirmware::AssocScanDone(brcmf_fweh_event_status_t event_status) {
  ZX_ASSERT(assoc_state_.state == AssocState::SCANNING);

  // Operation fails if we don't have at least one scan result
  if ((event_status == BRCMF_E_STATUS_SUCCESS) && (assoc_state_.scan_results.size() == 0)) {
    event_status = BRCMF_E_STATUS_NO_NETWORKS;
  }

  if (event_status != BRCMF_E_STATUS_SUCCESS) {
    SendEventToDriver(0, nullptr, BRCMF_E_SET_SSID, event_status, kClientIfidx);
    SetAssocState(AssocState::NOT_ASSOCIATED);
    return;
  }

  // For now, just pick the first AP. The real firmware can select based on signal strength and
  // band, but since wlanstack makes its own decision, we won't bother to model that here for now.
  ScanResult& ap = assoc_state_.scan_results.front();

  auto assoc_opts = std::make_unique<AssocOpts>();
  assoc_opts->bssid = ap.bssid;
  if (scan_state_.opts->ssid)
    assoc_opts->ssid = scan_state_.opts->ssid.value();

  AssocInit(std::move(assoc_opts), ap.channel);
  // Send an event of the first scan result to driver when assoc scan is done.
  EscanResultSeen(ap);

  BRCMF_DBG(SIM, "Auth start after assoc scan");
  AuthStart();
}

void SimFirmware::AssocClearContext() {
  hw_.CancelCallback(assoc_state_.assoc_timer_id);
  AuthClearContext();
  SetAssocState(AssocState::NOT_ASSOCIATED);
  assoc_state_.opts = nullptr;
  assoc_state_.reassoc_opts = nullptr;
  assoc_state_.scan_results.clear();
  // Clear out the channel setting
  iface_tbl_[kClientIfidx].chanspec = 0;

  assoc_resp_ies_len_ = 0;
  memset(assoc_resp_ies_, 0, ASSOC_IES_MAX_LEN);
}

void SimFirmware::AuthClearContext() {
  hw_.CancelCallback(auth_state_.auth_timer_id);
  auth_state_.state = AuthState::NOT_AUTHENTICATED;
  auth_state_.sec_type = simulation::SEC_PROTO_TYPE_OPEN;
}

void SimFirmware::AssocHandleFailure(wlan_ieee80211::StatusCode status) {
  if (assoc_state_.state == AssocState::NOT_ASSOCIATED) {
    return;
  }
  if (assoc_state_.num_attempts >= assoc_max_retries_
      // The firmware has been observed to not retry when there is an
      // authentication challenge failure. Also when firmware is doing external supplicant SAE auth,
      // it won't retry, because the retry is controlled by external supplicant.
      || assoc_state_.state == AssocState::AUTHENTICATION_CHALLENGE_FAILURE ||
      auth_state_.sec_type == simulation::SEC_PROTO_TYPE_WPA3) {
    BRCMF_DBG(SIM, "Assoc failed. Send E_SET_SSID with failure");
    if (assoc_state_.num_attempts < assoc_max_retries_) {
      status = fuchsia::wlan::ieee80211::StatusCode::REJECTED_SEQUENCE_TIMEOUT;
    }
    SendEventToDriver(0, nullptr, BRCMF_E_ASSOC, BRCMF_E_STATUS_FAIL, kClientIfidx, nullptr, 0,
                      static_cast<uint32_t>(status), assoc_state_.opts->bssid);
    SendEventToDriver(0, nullptr, BRCMF_E_SET_SSID, BRCMF_E_STATUS_FAIL, kClientIfidx, nullptr, 0,
                      static_cast<uint32_t>(status));
    AssocClearContext();
  } else {
    assoc_state_.num_attempts++;
    auth_state_.state = AuthState::NOT_AUTHENTICATED;
    BRCMF_DBG(SIM, "Assoc failed. Try again..attempt: %d", assoc_state_.num_attempts);
    AuthStart();
  }
}

void SimFirmware::AuthStart() {
  BRCMF_DBG(SIM, "Auth Start");
  common::MacAddr srcAddr(GetMacAddr(kClientIfidx));
  common::MacAddr bssid(assoc_state_.opts->bssid);

  hw_.RequestCallback(std::bind(&SimFirmware::AssocHandleFailure, this,
                                wlan_ieee80211::StatusCode::REFUSED_REASON_UNSPECIFIED),
                      kAuthTimeout, &auth_state_.auth_timer_id);

  ZX_ASSERT(auth_state_.state == AuthState::NOT_AUTHENTICATED);
  simulation::SimAuthType auth_type = simulation::AUTH_TYPE_OPEN;
  switch (iface_tbl_[kClientIfidx].auth_type) {
    case BRCMF_AUTH_MODE_OPEN:
      // Sequence number start from 0, end with at most 3.
      auth_type = simulation::AUTH_TYPE_OPEN;
      break;
    case BRCMF_AUTH_MODE_AUTO:
      auth_type = simulation::AUTH_TYPE_SHARED_KEY;
      break;
    case BRCMF_AUTH_MODE_SAE:
      auth_type = simulation::AUTH_TYPE_SAE;
      break;
    default:
      ZX_ASSERT_MSG(false, "Auth type not supported.");
  }

  simulation::SimAuthFrame auth_req_frame(srcAddr, bssid, 1, auth_type,
                                          wlan_ieee80211::StatusCode::SUCCESS);

  if (iface_tbl_[kClientIfidx].wsec == WEP_ENABLED) {
    ZX_ASSERT(iface_tbl_[kClientIfidx].wpa_auth == WPA_AUTH_DISABLED);
    auth_state_.sec_type = simulation::SEC_PROTO_TYPE_WEP;
  }

  switch (iface_tbl_[kClientIfidx].wpa_auth) {
    case WPA_AUTH_PSK:
      ZX_ASSERT((iface_tbl_[kClientIfidx].wsec & (uint32_t)WSEC_NONE) == 0U);
      auth_state_.sec_type = simulation::SEC_PROTO_TYPE_WPA1;
      break;
    case WPA2_AUTH_PSK:
      ZX_ASSERT((iface_tbl_[kClientIfidx].wsec & (uint32_t)WSEC_NONE) == 0U);
      auth_state_.sec_type = simulation::SEC_PROTO_TYPE_WPA2;
      break;
    case WPA3_AUTH_SAE_PSK:
      ZX_ASSERT((iface_tbl_[kClientIfidx].wsec & (uint32_t)WSEC_NONE) == 0U);
      auth_state_.sec_type = simulation::SEC_PROTO_TYPE_WPA3;
      break;
    default:
      ZX_ASSERT_MSG(auth_state_.sec_type == simulation::SEC_PROTO_TYPE_WEP ||
                        auth_state_.sec_type == simulation::SEC_PROTO_TYPE_OPEN,
                    "Upper layer auth type not supported.");
      /* No action needed for WEP or OPEN*/
      break;
  }

  if (auth_state_.sec_type == simulation::SEC_PROTO_TYPE_WPA3) {
    auto buf = std::make_unique<std::vector<uint8_t>>(sizeof(brcmf_ext_auth));
    auto ext_auth_data = reinterpret_cast<brcmf_ext_auth*>(buf->data());

    // Set the data values, though driver only cares about the bssid.
    ext_auth_data->ssid.SSID_len = assoc_state_.opts->ssid.len;
    memcpy(ext_auth_data->ssid.SSID, assoc_state_.opts->ssid.data,
           wlan_ieee80211::MAX_SSID_BYTE_LEN);
    bssid.CopyTo(ext_auth_data->bssid);
    ext_auth_data->key_mgmt_suite = WPA3_AUTH_SAE_PSK;
    ext_auth_data->status = BRCMF_E_STATUS_SUCCESS;
    auth_state_.state = AuthState::EXPECTING_EXTERNAL_COMMIT;
    SendEventToDriver(sizeof(brcmf_ext_auth), std::move(buf), BRCMF_E_JOIN_START,
                      BRCMF_E_STATUS_SUCCESS, kClientIfidx);
  } else {
    auth_req_frame.sec_proto_type_ = auth_state_.sec_type;
    auth_state_.state = AuthState::EXPECTING_SECOND;
    hw_.Tx(auth_req_frame);
  }
}

// The authentication frame might be an authentication response for client iface, or it could also
// be an authentication req from a potential client for softap iface.
void SimFirmware::RxAuthFrame(std::shared_ptr<const simulation::SimAuthFrame> frame) {
  if (frame->sec_proto_type_ == simulation::SEC_PROTO_TYPE_WPA3) {
    ZX_ASSERT(frame->auth_type_ == simulation::AUTH_TYPE_SAE);
    RemoteUpdateExternalSaeStatus(frame->seq_num_, frame->status_, frame->payload_.data(),
                                  frame->payload_.size());
  } else {
    if (frame->seq_num_ == 1) {
      HandleAuthReq(frame);
    } else {
      HandleAuthResp(frame);
    }
  }
}

// Handling authentication request for potential client of softap iface.
void SimFirmware::HandleAuthReq(std::shared_ptr<const simulation::SimAuthFrame> frame) {
  if (softap_ifidx_ == std::nullopt) {
    BRCMF_DBG(SIM, "SoftAP iface has not been allocated or started.");
    return;
  }

  ZX_ASSERT_MSG(iface_tbl_[softap_ifidx_.value()].auth_type == BRCMF_AUTH_MODE_OPEN,
                "The only auth type we support now is BRCMF_AUTH_MODE_OPEN because softAP iface "
                "only support OPEN and WPA2.");
  // Refuse auth req with auth type other than AUTH_TYPE_OPEN.
  if (frame->auth_type_ != simulation::AUTH_TYPE_OPEN) {
    BRCMF_DBG(SIM, "SoftAP iface only support OPEN mode for authentication now.");
    // Send authentication response back
    simulation::SimAuthFrame auth_resp_frame(
        iface_tbl_[softap_ifidx_.value()].mac_addr, frame->src_addr_, 2, frame->auth_type_,
        wlan_ieee80211::StatusCode::REFUSED_REASON_UNSPECIFIED);
    hw_.Tx(auth_resp_frame);
    return;
  }

  auto client = std::make_shared<Client>(frame->src_addr_, Client::AUTHENTICATED);
  // Add the client to the list
  iface_tbl_[softap_ifidx_.value()].ap_config.clients.push_back(client);

  // Send authentication response back
  simulation::SimAuthFrame auth_resp_frame(iface_tbl_[softap_ifidx_.value()].mac_addr,
                                           frame->src_addr_, 2, simulation::AUTH_TYPE_OPEN,
                                           wlan_ieee80211::StatusCode::SUCCESS);
  hw_.Tx(auth_resp_frame);
  // AUTH_IND is a simple event with the source mac address included
  SendEventToDriver(0, nullptr, BRCMF_E_AUTH_IND, BRCMF_E_STATUS_SUCCESS, softap_ifidx_.value(),
                    nullptr, 0, 0, frame->src_addr_);
}

void SimFirmware::HandleAuthResp(std::shared_ptr<const simulation::SimAuthFrame> frame) {
  // If we are not expecting auth resp packets, ignore it,
  if (auth_state_.state == AuthState::NOT_AUTHENTICATED ||
      auth_state_.state == AuthState::AUTHENTICATED) {
    return;
  }
  // Ignore if this is not intended for us
  common::MacAddr mac_addr(GetMacAddr(kClientIfidx));
  if (frame->dst_addr_ != mac_addr) {
    return;
  }
  // Ignore if this is not from the bssid with which we were trying to authenticate
  if (frame->src_addr_ != assoc_state_.opts->bssid) {
    return;
  }

  // It should not be an auth req frame if its dst addr is a client
  if (frame->seq_num_ != 2 && frame->seq_num_ != 4) {
    return;
  }

  // Response received, cancel timer
  hw_.CancelCallback(auth_state_.auth_timer_id);

  if (iface_tbl_[kClientIfidx].auth_type == BRCMF_AUTH_MODE_OPEN) {
    ZX_ASSERT(auth_state_.state == AuthState::EXPECTING_SECOND);
    ZX_ASSERT(frame->seq_num_ == 2);
    if (frame->status_ != wlan_ieee80211::StatusCode::SUCCESS) {
      BRCMF_DBG(SIM, "Auth refused, Handle failure");
      AssocHandleFailure(frame->status_);
      return;
    }
    auth_state_.state = AuthState::AUTHENTICATED;
    // Remember the last auth'd bssid
    auth_state_.bssid = assoc_state_.opts->bssid;
    BRCMF_DBG(SIM, "Assoc Start after auth");
    AssocStart();
  } else {
    // When iface_tbl_[kClientIfidx].auth_type == BRCMF_AUTH_MODE_AUTO
    if (auth_state_.state == AuthState::EXPECTING_SECOND && frame->seq_num_ == 2) {
      // Retry with AUTH_TYPE_OPEN_SYSTEM when refused in AUTH_TYPE_SHARED_KEY mode
      if (frame->status_ != wlan_ieee80211::StatusCode::SUCCESS) {
        auth_state_.state = AuthState::NOT_AUTHENTICATED;
        iface_tbl_[kClientIfidx].auth_type = BRCMF_AUTH_MODE_OPEN;
        BRCMF_DBG(SIM, "Auth shared refused...try with OPEN");
        AuthStart();
        return;
      }

      // Challenge failure should not occur until the fourth frame.
      ZX_ASSERT(frame->status_ != wlan_ieee80211::StatusCode::CHALLENGE_FAILURE);

      // If we receive the second auth frame when we are expecting it, we send out the third one and
      // set another timer for it.
      hw_.RequestCallback(std::bind(&SimFirmware::AssocHandleFailure, this,
                                    wlan_ieee80211::StatusCode::REJECTED_SEQUENCE_TIMEOUT),
                          kAuthTimeout, &auth_state_.auth_timer_id);

      auth_state_.state = AuthState::EXPECTING_FOURTH;

      common::MacAddr srcAddr(GetMacAddr(kClientIfidx));
      common::MacAddr bssid(assoc_state_.opts->bssid);
      simulation::SimAuthFrame auth_req_frame(srcAddr, bssid, frame->seq_num_ + 1,
                                              simulation::AUTH_TYPE_SHARED_KEY,
                                              wlan_ieee80211::StatusCode::SUCCESS);
      auth_req_frame.sec_proto_type_ = auth_state_.sec_type;
      hw_.Tx(auth_req_frame);
    } else if (auth_state_.state == AuthState::EXPECTING_FOURTH && frame->seq_num_ == 4) {
      if (frame->status_ == wlan_ieee80211::StatusCode::CHALLENGE_FAILURE) {
        assoc_state_.state = AssocState::AUTHENTICATION_CHALLENGE_FAILURE;
        auth_state_.state = AuthState::NOT_AUTHENTICATED;
        BRCMF_DBG(SIM, "Auth shared challenge failure, Handle failure");
        SendEventToDriver(0, nullptr, BRCMF_E_AUTH, BRCMF_E_STATUS_FAIL, kClientIfidx, nullptr,
                          0,  // TODO: determine what the flags should be
                          static_cast<uint32_t>(frame->status_), frame->src_addr_);
        AssocHandleFailure(frame->status_);
        return;
      }

      // If we receive the fourth auth frame when we are expecting it, start association
      auth_state_.state = AuthState::AUTHENTICATED;
      // Remember the last auth'd bssid
      auth_state_.bssid = assoc_state_.opts->bssid;
      BRCMF_DBG(SIM, "Assoc Start after 4way handshake");
      AssocStart();
    }
  }
}

zx_status_t SimFirmware::RemoteUpdateExternalSaeStatus(uint16_t seq_num,
                                                       wlan_ieee80211::StatusCode status_code,
                                                       const uint8_t* sae_payload,
                                                       size_t text_len) {
  ZX_ASSERT(sae_payload != nullptr);
  if (seq_num != 1 && seq_num != 2) {
    BRCMF_ERR("Invalid sequence number in SAE auth frame.");
    return ZX_ERR_INVALID_ARGS;
  }

  if (seq_num == 1 && auth_state_.state != AuthState::EXPECTING_AP_COMMIT) {
    BRCMF_ERR("Unexpected COMMIT auth frame from AP.");
    return ZX_ERR_BAD_STATE;
  }

  if (seq_num == 2 && auth_state_.state != AuthState::EXPECTING_AP_CONFIRM) {
    BRCMF_ERR("Unexpected CONFIRM auth frame from AP.");
    return ZX_ERR_BAD_STATE;
  }

  // Frame received from AP, cancel timer.
  hw_.CancelCallback(auth_state_.auth_timer_id);

  // construct auth frame and send up to external supplicant.
  size_t buf_size = sizeof(wlan::Authentication) + text_len;
  auto buf = std::make_unique<std::vector<uint8_t>>(buf_size);
  auto pframe_hdr = reinterpret_cast<wlan::Authentication*>(buf->data());

  pframe_hdr->auth_txn_seq_number = seq_num;
  pframe_hdr->status_code = WLAN_AUTH_RESULT_SUCCESS;
  pframe_hdr->auth_algorithm_number = BRCMF_AUTH_MODE_SAE;
  memcpy(buf->data() + sizeof(wlan::Authentication), sae_payload, text_len);

  hw_.RequestCallback(std::bind(&SimFirmware::AssocHandleFailure, this,
                                wlan_ieee80211::StatusCode::REFUSED_REASON_UNSPECIFIED),
                      kAuthTimeout, &auth_state_.auth_timer_id);

  // Update state
  if (seq_num == 1)
    auth_state_.state = AuthState::EXPECTING_EXTERNAL_CONFIRM;
  else
    auth_state_.state = AuthState::EXPECTING_EXTERNAL_HANDSHAKE_RESP;

  SendEventToDriver(buf_size, std::move(buf), BRCMF_E_AUTH, BRCMF_E_STATUS_SUCCESS, kClientIfidx);

  return ZX_OK;
}

zx_status_t SimFirmware::LocalUpdateExternalSaeStatus(uint16_t seq_num,
                                                      wlan_ieee80211::StatusCode status_code,
                                                      const uint8_t* sae_payload, size_t text_len) {
  ZX_ASSERT(sae_payload != nullptr);
  if (seq_num != 1 && seq_num != 2) {
    BRCMF_ERR("Invalid sequence number in SAE auth frame.");
    return ZX_ERR_INVALID_ARGS;
  }

  common::MacAddr srcAddr(GetMacAddr(kClientIfidx));
  common::MacAddr bssid(assoc_state_.opts->bssid);
  // Create a template auth_req_frame, might be modified later.
  simulation::SimAuthFrame auth_req_frame(srcAddr, bssid, 1, simulation::AUTH_TYPE_SAE,
                                          wlan_ieee80211::StatusCode::SUCCESS);

  if (seq_num == 1 && auth_state_.state != AuthState::EXPECTING_EXTERNAL_COMMIT) {
    BRCMF_ERR("Unexpected COMMIT auth frame from external supplicant. seq_num: %u, state: %u",
              seq_num, auth_state_.state);
    return ZX_ERR_BAD_STATE;
  }

  if (seq_num == 2 && auth_state_.state != AuthState::EXPECTING_EXTERNAL_CONFIRM) {
    BRCMF_ERR("Unexpected CONFIRM auth frame from external supplicant. seq_num: %u, state: %u",
              seq_num, auth_state_.state);
    return ZX_ERR_BAD_STATE;
  }

  // Frame received from external supplicant, cancel timer.
  hw_.CancelCallback(auth_state_.auth_timer_id);

  auth_req_frame.status_ = status_code;
  auth_req_frame.seq_num_ = seq_num;
  auth_req_frame.AddChallengeText(cpp20::span(sae_payload, text_len));
  auth_req_frame.sec_proto_type_ = auth_state_.sec_type;

  hw_.RequestCallback(std::bind(&SimFirmware::AssocHandleFailure, this,
                                wlan_ieee80211::StatusCode::REJECTED_SEQUENCE_TIMEOUT),
                      kAuthTimeout, &auth_state_.auth_timer_id);

  hw_.Tx(auth_req_frame);

  // Update state
  if (seq_num == 1)
    auth_state_.state = AuthState::EXPECTING_AP_COMMIT;
  else
    auth_state_.state = AuthState::EXPECTING_AP_CONFIRM;

  return ZX_OK;
}

// Remove the client from the list. If found return true else false.
bool SimFirmware::FindAndRemoveClient(const common::MacAddr client_mac, bool motivation_deauth,
                                      wlan_ieee80211::ReasonCode deauth_reason) {
  if (softap_ifidx_ == std::nullopt) {
    BRCMF_ERR("SoftAP iface has not been allocated or started.");
    return false;
  }
  auto& clients = iface_tbl_[softap_ifidx_.value()].ap_config.clients;
  for (auto client : clients) {
    if (client->mac_addr == client_mac) {
      if (motivation_deauth) {
        // The removal is triggered by a deauth frame.
        if (client->state == Client::AUTHENTICATED) {
          // When this client is authenticated but not associated, only send up BRCMF_E_DEAUTH_IND
          // to driver.
          SendEventToDriver(0, nullptr, BRCMF_E_DEAUTH_IND, BRCMF_E_STATUS_SUCCESS,
                            softap_ifidx_.value(), nullptr, 0, static_cast<uint32_t>(deauth_reason),
                            client_mac);
        } else if (client->state == Client::ASSOCIATED) {
          // When this client is associated, send both BRCMF_E_DEAUTH_IND and BRCMF_E_DISASSOC_IND
          // events up to driver.
          SendEventToDriver(0, nullptr, BRCMF_E_DEAUTH_IND, BRCMF_E_STATUS_SUCCESS,
                            softap_ifidx_.value(), nullptr, 0, static_cast<uint32_t>(deauth_reason),
                            client_mac);
          SendEventToDriver(
              0, nullptr, BRCMF_E_DISASSOC_IND, BRCMF_E_STATUS_SUCCESS, softap_ifidx_.value(),
              nullptr, BRCMF_EVENT_MSG_LINK,
              static_cast<uint32_t>(wlan_ieee80211::ReasonCode::LEAVING_NETWORK_DISASSOC),
              client_mac);
        }
      } else {
        BRCMF_DBG(SIM, "deauth_reason is not used.");
        // The removal is triggered by a disassoc frame.
        SendEventToDriver(
            0, nullptr, BRCMF_E_DISASSOC_IND, BRCMF_E_STATUS_SUCCESS, softap_ifidx_.value(),
            nullptr, BRCMF_EVENT_MSG_LINK,
            static_cast<uint32_t>(wlan_ieee80211::ReasonCode::LEAVING_NETWORK_DISASSOC),
            client_mac);
      }

      clients.remove(client);
      return true;
    }
  }
  return false;
}

// Return true if client is in the assoc list else false
std::shared_ptr<SimFirmware::Client> SimFirmware::FindClient(const common::MacAddr client_mac) {
  if (softap_ifidx_ == std::nullopt) {
    BRCMF_ERR("SoftAP iface has not been allocated or started.");
    return std::shared_ptr<SimFirmware::Client>(nullptr);
  }

  auto& clients = iface_tbl_[softap_ifidx_.value()].ap_config.clients;

  for (auto& client : clients) {
    if (client->mac_addr == client_mac) {
      return client;
    }
  }

  return std::shared_ptr<SimFirmware::Client>(nullptr);
}

std::vector<brcmf_wsec_key_le> SimFirmware::GetKeyList(uint16_t ifidx) {
  return iface_tbl_[ifidx].wsec_key_list;
}

void SimFirmware::RxDeauthReq(std::shared_ptr<const simulation::SimDeauthFrame> frame) {
  BRCMF_DBG(SIM, "Deauth from %s for %s reason: %d", MACSTR(frame->src_addr_),
            MACSTR(frame->dst_addr_), static_cast<int>(frame->reason_));
  // First check if this is a deauth meant for a client associated to our SoftAP
  auto ifidx = GetIfidxByMac(frame->dst_addr_);
  if (ifidx == -1) {
    // Not meant for any of the valid IFs, ignore
    return;
  }
  if (!iface_tbl_[ifidx].ap_mode) {
    ZX_ASSERT_MSG(ifidx == kClientIfidx, "Not matching client iface.");
    // Not meant for the SoftAP. Check if it is meant for the client interface
    HandleDisconnectForClientIF(frame, auth_state_.bssid, frame->reason_);
    return;
  }
  // Remove the client from the list (if found)
  if (FindAndRemoveClient(frame->src_addr_, true, frame->reason_)) {
    BRCMF_DBG(SIM, "Deauth done Num Clients: %lu", iface_tbl_[ifidx].ap_config.clients.size());
    return;
  }
  BRCMF_DBG(SIM, "Deauth Client not found in List");
}

void SimFirmware::AssocStart() {
  BRCMF_DBG(SIM, "Assoc Start");
  common::MacAddr srcAddr(GetMacAddr(kClientIfidx));

  hw_.RequestCallback(std::bind(&SimFirmware::AssocHandleFailure, this,
                                wlan_ieee80211::StatusCode::REFUSED_REASON_UNSPECIFIED),
                      kAssocTimeout, &assoc_state_.assoc_timer_id);

  // We can't use assoc_state_.opts->bssid directly because it may get free'd during TxAssocReq
  // handling if a response is sent.
  common::MacAddr bssid(assoc_state_.opts->bssid);
  simulation::SimAssocReqFrame assoc_req_frame(srcAddr, bssid, assoc_state_.opts->ssid);
  hw_.Tx(assoc_req_frame);
}

// Get the index of the SoftAP IF based on Mac.
int16_t SimFirmware::GetIfidxByMac(const common::MacAddr& addr) {
  for (uint8_t i = 0; i < kMaxIfSupported; i++) {
    if (iface_tbl_[i].allocated && iface_tbl_[i].mac_addr == addr) {
      return i;
    }
  }
  return -1;
}

// Get channel of IF
wlan_channel_t SimFirmware::GetIfChannel(bool is_ap) {
  wlan_channel_t channel;

  // Get chanspec
  ZX_ASSERT_MSG(iface_tbl_[kClientIfidx].allocated, "The client iface is not allocated!");
  uint16_t chanspec = iface_tbl_[kClientIfidx].chanspec;
  ZX_ASSERT_MSG(chanspec != 0, "No chanspec assigned to client.");

  // convert to channel
  chanspec_to_channel(&d11_inf_, chanspec, &channel);
  return channel;
}

// This routine for now only handles Disassoc Request meant for the SoftAP IF.
void SimFirmware::RxDisassocReq(std::shared_ptr<const simulation::SimDisassocReqFrame> frame) {
  BRCMF_DBG(SIM, "Disassoc from %s for %s reason: %d", MACSTR(frame->src_addr_),
            MACSTR(frame->dst_addr_), static_cast<int>(frame->reason_));
  // First check if this is a disassoc meant for a client associated to our SoftAP
  auto ifidx = GetIfidxByMac(frame->dst_addr_);
  if (ifidx == -1) {
    // Not meant for any of the valid IFs, ignore
    return;
  }
  if (!iface_tbl_[ifidx].ap_mode) {
    ZX_ASSERT_MSG(ifidx == kClientIfidx, "Not matching client iface.");
    // Not meant for the SoftAP. Check if it is meant for the client interface
    HandleDisconnectForClientIF(frame, assoc_state_.opts->bssid, frame->reason_);
    return;
  }
  // Remove the client from the list (if found)
  if (FindAndRemoveClient(frame->src_addr_, false,
                          wlan_ieee80211::ReasonCode::UNSPECIFIED_REASON)) {
    BRCMF_DBG(SIM, "Disassoc done Num Clients: %lu", iface_tbl_[ifidx].ap_config.clients.size());
    return;
  }
  BRCMF_DBG(SIM, "Client not found in List");
}

// AssocResp and ReassocResp frame types contain capability info, which indicates BSS type.
static zx_status_t GetBssType(std::shared_ptr<const simulation::SimManagementFrame> frame,
                              bss_type_t* bss_type) {
  // IEEE Std 802.11-2016, 9.4.1.4 to determine bss type
  bool capIbss;
  bool capEss;
  switch (frame->MgmtFrameType()) {
    case simulation::SimManagementFrame::FRAME_TYPE_ASSOC_RESP: {
      auto assoc_resp = std::static_pointer_cast<const simulation::SimAssocRespFrame>(frame);
      capIbss = assoc_resp->capability_info_.ibss();
      capEss = assoc_resp->capability_info_.ess();
      break;
    }
    case simulation::SimManagementFrame::FRAME_TYPE_REASSOC_RESP: {
      auto reassoc_resp = std::static_pointer_cast<const simulation::SimReassocRespFrame>(frame);
      capIbss = reassoc_resp->capability_info_.ibss();
      capEss = reassoc_resp->capability_info_.ess();
      break;
    }
    default:
      return ZX_ERR_INVALID_ARGS;
  }

  if (capIbss && !capEss) {
    BRCMF_WARN("Non-infrastructure types not currently supported by sim-fw\n");
    *bss_type = BSS_TYPE_INDEPENDENT;
    return ZX_ERR_NOT_SUPPORTED;
  } else if (!capIbss && capEss) {
    *bss_type = BSS_TYPE_INFRASTRUCTURE;
  } else if (capIbss && capEss) {
    BRCMF_WARN("Non-infrastructure types not currently supported by sim-fw\n");
    *bss_type = BSS_TYPE_MESH;
    return ZX_ERR_NOT_SUPPORTED;
  } else {
    BRCMF_WARN("Station with impossible capability not being an ess or ibss found\n");
    return ZX_ERR_INVALID_ARGS;
  }
  return ZX_OK;
}

void SimFirmware::RxAssocResp(std::shared_ptr<const simulation::SimAssocRespFrame> frame) {
  // Ignore if we are not trying to associate
  if (assoc_state_.state != AssocState::ASSOCIATING) {
    return;
  }

  // Ignore if this is not intended for us
  if (GetIfidxByMac(frame->dst_addr_) == -1) {
    return;
  }

  // Ignore if this is not from the bssid with which we were trying to associate
  if (frame->src_addr_ != assoc_state_.opts->bssid) {
    return;
  }
  // Response received, cancel timer
  hw_.CancelCallback(assoc_state_.assoc_timer_id);
  if (frame->status_ == wlan_ieee80211::StatusCode::SUCCESS) {
    bss_type_t bss_type;
    if (GetBssType(frame, &bss_type) != ZX_OK) {
      ZX_ASSERT_MSG(false, "Sim firmware does not support BSS type\n");
    }
    assoc_state_.opts->bss_type = bss_type;

    // Set values that are returned for iovar "assoc_resp_ies"
    ZX_ASSERT_MSG(frame->raw_ies_.size() <= ASSOC_IES_MAX_LEN,
                  "Assoc resp frame IEs longer than ASSOC_IES_MAX_LEN: %lu > %d",
                  frame->raw_ies_.size(), ASSOC_IES_MAX_LEN);
    memcpy(assoc_resp_ies_, frame->raw_ies_.data(), frame->raw_ies_.size());
    assoc_resp_ies_len_ = frame->raw_ies_.size();

    BRCMF_DBG(SIM, "Assoc success, send events with a delay");
    // Send the ASSOC event with a delay
    SendEventToDriver(0, nullptr, BRCMF_E_ASSOC, BRCMF_E_STATUS_SUCCESS, kClientIfidx, nullptr, 0,
                      0, assoc_state_.opts->bssid, kAssocEventDelay);
    // Send the LINK event with a delay
    SendEventToDriver(0, nullptr, BRCMF_E_LINK, BRCMF_E_STATUS_SUCCESS, kClientIfidx, nullptr,
                      BRCMF_EVENT_MSG_LINK, 0, assoc_state_.opts->bssid, kLinkEventDelay);
    // Send the SSID event after a delay
    SendEventToDriver(0, nullptr, BRCMF_E_SET_SSID, BRCMF_E_STATUS_SUCCESS, kClientIfidx, nullptr,
                      0, 0, assoc_state_.opts->bssid, kSsidEventDelay);
    // Set the Assoc state only after E_ASSOC is sent to the driver.
    hw_.RequestCallback(std::bind(&SimFirmware::SetAssocState, this, AssocState::ASSOCIATED),
                        kAssocEventDelay);
    if (softap_ifidx_ != std::nullopt) {
      // Send the AP_STARTED event to the SoftAp IF after a delay. This event is sent to the
      // SoftAP IF in case its channel changed (to sync up to the client's channel).
      SendEventToDriver(0, nullptr, BRCMF_E_AP_STARTED, BRCMF_E_STATUS_SUCCESS,
                        softap_ifidx_.value(), nullptr, 0, 0, kZeroMac, kApStartedEventDelay);
    }
  } else {
    BRCMF_DBG(SIM, "Assoc refused, Handle failure");
    AssocHandleFailure(frame->status_);
  }
}

zx_status_t SimFirmware::ReassocToCurrentAp(
    std::shared_ptr<const simulation::SimReassocRespFrame> frame) {
  BRCMF_ERR("Reassociation to current AP is not supported in sim firmware");
  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t SimFirmware::ReassocToDifferentAp(
    std::shared_ptr<const simulation::SimReassocRespFrame> frame) {
  BRCMF_INFO("Reassociation to different AP");
  bss_type_t bss_type;
  if (GetBssType(frame, &bss_type) != ZX_OK) {
    ZX_ASSERT_MSG(false, "Sim firmware does not support BSS type\n");
    return ZX_ERR_NOT_SUPPORTED;
  }
  if (frame->status_ != wlan_ieee80211::StatusCode::SUCCESS) {
    return ZX_ERR_CONNECTION_REFUSED;
  }

  BRCMF_DBG(SIM, "Reassoc success, send events with a delay");
  assoc_state_.opts->bssid = assoc_state_.reassoc_opts->bssid;
  assoc_state_.opts->bss_type = bss_type;
  assoc_state_.reassoc_opts.reset();

  // Reassoc success implies auth success, and real firmware sends the AUTH event here.
  SendEventToDriver(0, nullptr, BRCMF_E_AUTH, BRCMF_E_STATUS_SUCCESS, kClientIfidx);
  // Send the REASSOC event with a delay.
  SendEventToDriver(0, nullptr, BRCMF_E_REASSOC, BRCMF_E_STATUS_SUCCESS, kClientIfidx, nullptr, 0,
                    0, assoc_state_.opts->bssid, kReassocEventDelay);
  // Send the LINK event with a delay.
  SendEventToDriver(0, nullptr, BRCMF_E_LINK, BRCMF_E_STATUS_SUCCESS, kClientIfidx, nullptr,
                    BRCMF_EVENT_MSG_LINK, 0, assoc_state_.opts->bssid, kLinkEventDelay);
  // Send the ROAM event with a delay.
  SendEventToDriver(0, nullptr, BRCMF_E_ROAM, BRCMF_E_STATUS_SUCCESS, kClientIfidx, nullptr, 0, 0,
                    {}, kRoamEventDelay);
  // Set the Assoc state only after REASSOC event is sent to the driver.
  hw_.RequestCallback([this] { SetAssocState(AssocState::ASSOCIATED); }, kReassocEventDelay);
  if (softap_ifidx_ != std::nullopt) {
    // Send the AP_STARTED event to the SoftAp IF after a delay. This event is sent to the
    // SoftAP IF in case its channel changed (to sync up to the client's channel).
    SendEventToDriver(0, nullptr, BRCMF_E_AP_STARTED, BRCMF_E_STATUS_SUCCESS, softap_ifidx_.value(),
                      nullptr, 0, 0, kZeroMac, kApStartedEventDelay);
  }
  return ZX_OK;
}

void SimFirmware::RxReassocResp(std::shared_ptr<const simulation::SimReassocRespFrame> frame) {
  // Ignore if STA is not trying to reassociate
  if (assoc_state_.state != AssocState::REASSOCIATING || assoc_state_.reassoc_opts == nullptr) {
    BRCMF_INFO("Received reassoc response, but STA is not reassociating");
    return;
  }

  // Ignore if frame is not intended for this STA
  if (GetIfidxByMac(frame->dst_addr_) == -1) {
    BRCMF_INFO("Received reassoc response, but response is not for this STA");
    return;
  }

  // Ignore if this is not from the bssid with which STA is trying to reassociate
  if (frame->src_addr_ != assoc_state_.reassoc_opts->bssid) {
    BRCMF_INFO("Received reassoc response, but not from target BSS");
    return;
  }
  // Response received, cancel timer
  hw_.CancelCallback(assoc_state_.reassoc_timer_id);

  zx_status_t reassoc_status;
  if (assoc_state_.opts->bssid == assoc_state_.reassoc_opts->bssid) {
    reassoc_status = ReassocToCurrentAp(frame);
  } else {
    reassoc_status = ReassocToDifferentAp(frame);
  }
  if (reassoc_status != ZX_OK) {
    BRCMF_DBG(SIM, "Reassoc refused, handle failure");
    ReassocHandleFailure(frame->status_);
  }
}

void SimFirmware::SetAssocState(AssocState::AssocStateName state) {
  if (state == AssocState::NOT_ASSOCIATED) {
    auth_state_.state = AuthState::NOT_AUTHENTICATED;
  }
  assoc_state_.state = state;
}

// Disassociate the Local Client (request coming in from the driver)
void SimFirmware::DisassocLocalClient(wlan_ieee80211::ReasonCode reason) {
  if (assoc_state_.state == AssocState::ASSOCIATED) {
    common::MacAddr srcAddr(GetMacAddr(kClientIfidx));
    common::MacAddr bssid(assoc_state_.opts->bssid);
    // Transmit the disassoc req and since there is no response for it, indicate disassoc done to
    // driver now
    simulation::SimDisassocReqFrame disassoc_req_frame(srcAddr, bssid, reason);
    hw_.Tx(disassoc_req_frame);
    SetStateToDisassociated(reason, true);
  } else if (auth_state_.state == AuthState::AUTHENTICATED) {
    common::MacAddr srcAddr(GetMacAddr(kClientIfidx));
    common::MacAddr bssid(assoc_state_.opts->bssid);
    // Transmit the deauth frame clear AP state.
    simulation::SimDeauthFrame deauth_req_frame(srcAddr, bssid, reason);
    hw_.Tx(deauth_req_frame);
  }

  AuthClearContext();
  AssocClearContext();
}

// Disassoc/deauth Request from FakeAP for the Client IF.
void SimFirmware::HandleDisconnectForClientIF(
    std::shared_ptr<const simulation::SimManagementFrame> frame, const common::MacAddr& bssid,
    wlan_ieee80211::ReasonCode reason) {
  // Ignore if this is not intended for us
  common::MacAddr mac_addr(iface_tbl_[kClientIfidx].mac_addr);
  if (frame->dst_addr_ != mac_addr) {
    return;
  }

  // Ignore if this is not from the bssid with which we are associated/authenticated
  if (frame->src_addr_ != bssid) {
    return;
  }

  if (frame->MgmtFrameType() == simulation::SimManagementFrame::FRAME_TYPE_DEAUTH) {
    // The client could receive a deauth even after disassociation. Notify the driver always
    SendEventToDriver(0, nullptr, BRCMF_E_DEAUTH_IND, BRCMF_E_STATUS_SUCCESS, kClientIfidx, 0, 0,
                      static_cast<uint32_t>(reason));
    if (auth_state_.state == AuthState::AUTHENTICATED) {
      AuthClearContext();
    }
    // DEAUTH implies disassoc, so continue
  }
  // disassoc
  if (assoc_state_.state != AssocState::ASSOCIATED) {
    // Already disassoc'd, nothing more to do.
    return;
  }

  SetStateToDisassociated(reason, false);
  AssocClearContext();
}

// precondition: was associated
void SimFirmware::SetStateToDisassociated(wlan_ieee80211::ReasonCode reason,
                                          bool locally_initiated) {
  // Disable beacon watchdog that triggers disconnect
  DisableBeaconWatchdog();
  // Send the appropriate event to driver.
  SendEventToDriver(0, nullptr, locally_initiated ? BRCMF_E_DISASSOC : BRCMF_E_DISASSOC_IND,
                    BRCMF_E_STATUS_SUCCESS, kClientIfidx, nullptr, 0, static_cast<uint32_t>(reason),
                    assoc_state_.opts->bssid, kDisassocEventDelay);
  SendEventToDriver(0, nullptr, BRCMF_E_LINK, BRCMF_E_STATUS_SUCCESS, kClientIfidx, nullptr, 0,
                    static_cast<uint32_t>(reason), assoc_state_.opts->bssid, kLinkEventDelay);
}

void SimFirmware::ReassocInit(std::unique_ptr<ReassocOpts> reassoc_opts, wlan_channel_t& channel) {
  if (assoc_state_.state != AssocState::ASSOCIATED) {
    BRCMF_WARN("Cannot reassociate because STA is not associated");
    return;
  }
  SetAssocState(AssocState::REASSOCIATING);
  assoc_state_.reassoc_opts = std::move(reassoc_opts);

  const uint16_t chanspec = channel_to_chanspec(&d11_inf_, &channel);
  SetIFChanspec(kClientIfidx, chanspec);
  hw_.SetChannel(channel);
  SendEventToDriver(0, nullptr, BRCMF_E_REASSOC, BRCMF_E_STATUS_ATTEMPT, kClientIfidx);
  SendEventToDriver(0, nullptr, BRCMF_E_ROAM_PREP, BRCMF_E_STATUS_SUCCESS, kClientIfidx);
}

void SimFirmware::ReassocStart() {
  BRCMF_DBG(SIM, "Reassoc Start");
  const common::MacAddr srcAddr(GetMacAddr(kClientIfidx));

  if (assoc_state_.state != AssocState::REASSOCIATING) {
    BRCMF_WARN("Cannot reassociate because STA left reassociating state");
    assoc_state_.reassoc_opts.reset();
    return;
  }

  hw_.RequestCallback(
      [this] {
        SimFirmware::ReassocHandleFailure(wlan_ieee80211::StatusCode::REFUSED_REASON_UNSPECIFIED);
      },
      kReassocTimeout, &assoc_state_.reassoc_timer_id);

  // We can't use reassoc_opts->bssid directly because it may get free'd during Tx
  // handling if a response is sent.
  const common::MacAddr bssid(assoc_state_.reassoc_opts->bssid);
  const simulation::SimReassocReqFrame reassoc_req_frame(srcAddr, bssid);
  hw_.Tx(reassoc_req_frame);
}

void SimFirmware::ReassocHandleFailure(::fuchsia::wlan::ieee80211::StatusCode status) {
  assoc_state_.reassoc_opts.reset();
  if (assoc_state_.state == AssocState::NOT_ASSOCIATED) {
    BRCMF_WARN("Reassoc failed, STA is not associated");
    return;
  }
  // TODO(fxbug.dev/111760) If real firmware retries reassoc, then retry here.
  BRCMF_DBG(SIM, "Reassoc failed.");
  SendEventToDriver(0, nullptr, BRCMF_E_REASSOC, BRCMF_E_STATUS_FAIL, kClientIfidx, nullptr, 0,
                    static_cast<uint32_t>(status), assoc_state_.reassoc_opts->bssid);
  AssocClearContext();
}

// Assoc Request from Client for the SoftAP IF
void SimFirmware::RxAssocReq(std::shared_ptr<const simulation::SimAssocReqFrame> frame) {
  BRCMF_DBG(SIM, "Assoc from %s for %s", MACSTR(frame->src_addr_), MACSTR(frame->bssid_));

  if (softap_ifidx_ == std::nullopt) {
    BRCMF_DBG(SIM,
              "Received an assoc request but the softap iface has not been allocated or started.");
    return;
  }

  if (std::memcmp(iface_tbl_[softap_ifidx_.value()].mac_addr.byte, frame->bssid_.byte, ETH_ALEN) ==
      0) {
    // ASSOC_IND contains some TLVs
    HandleAssocReq(frame);
  }
}

zx_status_t SimFirmware::HandleJoinRequest(const void* value, size_t value_len) {
  auto join_params = reinterpret_cast<const brcmf_ext_join_params_le*>(value);

  // Verify that the channel count is consistent with the size of the structure
  size_t max_channels =
      (value_len - offsetof(brcmf_ext_join_params_le, assoc_le.chanspec_list)) / sizeof(uint16_t);
  size_t num_channels = join_params->assoc_le.chanspec_num;
  if (max_channels < num_channels) {
    BRCMF_DBG(SIM, "Bad join request: message size (%zd) too short for %zd channels", value_len,
              num_channels);
    return ZX_ERR_INVALID_ARGS;
  }

  if (assoc_state_.state != AssocState::NOT_ASSOCIATED) {
    ZX_ASSERT_MSG(assoc_state_.state != AssocState::ASSOCIATED,
                  "Need to add support for automatically disassociating");

    BRCMF_DBG(SIM, "Attempt to associate while association already in progress");
    return ZX_ERR_BAD_STATE;
  }

  if (scan_state_.state == ScanState::SCANNING) {
    BRCMF_DBG(SIM, "Scan in progress, aborting scan.");
    ScanComplete(BRCMF_E_STATUS_NEWASSOC);
  }

  auto scan_opts = std::make_unique<ScanOpts>();

  // scan_opts->sync_id is unused, since we're not reporting our results back to the driver

  switch (join_params->scan_le.scan_type) {
    case BRCMF_SCANTYPE_DEFAULT:
      // Use the default
      scan_opts->is_active = !default_passive_scan_;
      break;
    case BRCMF_SCANTYPE_PASSIVE:
      scan_opts->is_active = false;
      break;
    case BRCMF_SCANTYPE_ACTIVE:
      scan_opts->is_active = true;
      break;
    default:
      return ZX_ERR_INVALID_ARGS;
  }

  // Specify the SSID filter, if applicable
  const struct brcmf_ssid_le* req_ssid = &join_params->ssid_le;
  ZX_ASSERT(wlan_ieee80211::MAX_SSID_BYTE_LEN == sizeof(scan_opts->ssid->data));
  if (req_ssid->SSID_len != 0) {
    cssid_t ssid;
    ssid.len = req_ssid->SSID_len;
    std::copy(&req_ssid->SSID[0], &req_ssid->SSID[wlan_ieee80211::MAX_SSID_BYTE_LEN], ssid.data);
    scan_opts->ssid = ssid;
  }

  // Specify BSSID filter, if applicable
  common::MacAddr bssid(join_params->assoc_le.bssid);
  if (!bssid.IsZero()) {
    scan_opts->bssid = bssid;
  }

  // Determine dwell time
  if (scan_opts->is_active) {
    uint32_t active_time = join_params->scan_le.active_time;
    if (active_time == static_cast<uint32_t>(-1)) {
      // Setting this to an arbitrary value for now.
      active_time = 100;
    }
    int32_t nprobes = (int32_t)join_params->scan_le.nprobes;
    scan_opts->active_scan_max_attempts = nprobes <= 0 ? 1 : nprobes;
    // Dwell time is equally split across the # of attempts
    scan_opts->dwell_time = zx::msec(active_time) / scan_opts->active_scan_max_attempts;
    BRCMF_DBG(SIM, "Join req - active scan: act time: %u nprobes: %d dwell: %zu", active_time,
              join_params->scan_le.nprobes, scan_opts->dwell_time.get());
  } else if (join_params->scan_le.passive_time == static_cast<uint32_t>(-1)) {
    // Use default passive time
    if (default_passive_time_ == static_cast<uint32_t>(-1)) {
      // If we hit this, we need to determine the default passive time
      ZX_ASSERT("Attempt to use default passive scan time, but it hasn't been set yet");
    }
    scan_opts->dwell_time = zx::msec(default_passive_time_);
  } else {
    scan_opts->dwell_time = zx::msec(join_params->scan_le.passive_time);
  }

  // Copy channels from request
  scan_opts->channels.resize(num_channels);
  const uint16_t* chanspecs = &join_params->assoc_le.chanspec_list[0];
  std::copy(&chanspecs[0], &chanspecs[num_channels], scan_opts->channels.data());

  scan_opts->on_result_fn =
      std::bind(&SimFirmware::AssocScanResultSeen, this, std::placeholders::_1);
  scan_opts->on_done_fn = std::bind(&SimFirmware::AssocScanDone, this, std::placeholders::_1);

  // Reset assoc state
  SetAssocState(AssocState::SCANNING);
  assoc_state_.scan_results.clear();

  zx_status_t status = ScanStart(std::move(scan_opts));
  if (status != ZX_OK) {
    BRCMF_DBG(SIM, "Failed to start scan: %s", zx_status_get_string(status));
    SetAssocState(AssocState::NOT_ASSOCIATED);
  }
  return status;
}

zx_status_t SimFirmware::SetIFChanspec(uint16_t ifidx, uint16_t chanspec) {
  if (ifidx < 0 || ifidx >= kMaxIfSupported || !iface_tbl_[ifidx].allocated) {
    return ZX_ERR_INVALID_ARGS;
  }

  if (iface_tbl_[ifidx].ap_mode) {
    // When it's set for softAP, and if there is a client with a chanspec
    if (!iface_tbl_[kClientIfidx].allocated || iface_tbl_[kClientIfidx].chanspec == 0) {
      // If no client is activated, just set the chanspec
      iface_tbl_[ifidx].chanspec = chanspec;
      return ZX_OK;
    }

    // When a new softAP iface is created, set the chanspec to client iface chanspec, ignore
    // the input.
    iface_tbl_[ifidx].chanspec = iface_tbl_[kClientIfidx].chanspec;
    return ZX_OK;
  } else {
    // If it's set for clients, change all chanspecs of existing ifaces into the same one(the one we
    // want to set).
    for (uint16_t i = 0; i < kMaxIfSupported; i++) {
      if (iface_tbl_[i].allocated) {
        // TODO(zhiyichen): If this operation change the chanspec for softAP iface, send out CSA
        // announcement when there is any client connecting to it.
        iface_tbl_[i].chanspec = chanspec;
      }
    }
  }
  return ZX_OK;
}

// Get the index of IF with the given bsscfgidx
int16_t SimFirmware::GetIfidxByBsscfgidx(int32_t bsscfgidx) {
  for (uint8_t i = 0; i < kMaxIfSupported; i++) {
    if (iface_tbl_[i].allocated && (bsscfgidx == iface_tbl_[i].bsscfgidx)) {
      return i;
    }
  }
  return -1;
}

// Stop the specified bss
zx_status_t SimFirmware::StopInterface(const int32_t bsscfgidx) {
  int16_t ifidx = GetIfidxByBsscfgidx(bsscfgidx);
  if (ifidx == -1)
    return ZX_ERR_IO;
  if (iface_tbl_[ifidx].ap_mode) {
    StopSoftAP(ifidx);
  } else {
    DisassocLocalClient(wlan_ieee80211::ReasonCode::LEAVING_NETWORK_DISASSOC);
  }
  iface_tbl_[ifidx].is_up = 0;
  return ZX_OK;
}

zx_status_t SimFirmware::IovarsSet(uint16_t ifidx, const char* name_buf, const void* value_buf,
                                   size_t value_len, bcme_status_t* fw_err) {
  uint8_t* value = (uint8_t*)value_buf;
  int32_t bsscfgidx = 0;
  char* name = (char*)name_buf;

  // If it is a bsscfg iovar, handle it accordingly (strip the prefix and extract the
  // bsscfgidx.
  const size_t bsscfg_prefix_len = strlen(BRCMF_FWIL_BSSCFG_PREFIX);
  if (!std::strncmp(name, BRCMF_FWIL_BSSCFG_PREFIX, bsscfg_prefix_len)) {
    bsscfgidx = *(reinterpret_cast<const int32_t*>(value));
    value += sizeof(bsscfgidx);
    ZX_ASSERT(value_len >= sizeof(bsscfgidx));
    value_len -= sizeof(bsscfgidx);
    if (!value_len)
      value = nullptr;
    name += bsscfg_prefix_len;
  }

  // If Error Injection is enabled return with the appropriate status right away
  zx_status_t status = ZX_OK;
  if (err_inj_.CheckIfErrInjIovarEnabled(name, &status, fw_err, nullptr, ifidx)) {
    return status;
  }

  auto iovar = iovar_table_.find(name);
  if (iovar == iovar_table_.end()) {
    BRCMF_ERR("Iovar -- %s is not found in iovar table.", name);
    return ZX_OK;
  }
  auto& impl = iovar->second;
  status = impl.Set(ifidx, bsscfgidx, value, value_len);
  if (status == ZX_ERR_IO_REFUSED) {
    *fw_err = BCME_BUFTOOSHORT;
  }
  return status;
}

zx_status_t SimFirmware::IovarsGet(uint16_t ifidx, const char* name, void* value_out,
                                   size_t value_len, bcme_status_t* fw_err) {
  zx_status_t status = ZX_OK;

  if (fw_err == nullptr) {
    BRCMF_ERR(
        "fw_err cannot be nullptr for IovarsGet() calls. IovarsGet() should not be called directly "
        "from firmware simulator tests.");
    return ZX_ERR_INTERNAL;
  } else {
    *fw_err = BCME_OK;
  }

  const std::vector<uint8_t>* err_inj_alt_value;
  if (err_inj_.CheckIfErrInjIovarEnabled(name, &status, fw_err, &err_inj_alt_value, ifidx)) {
    if (err_inj_alt_value != nullptr) {
      // Use provided replacement data
      size_t alt_data_size = err_inj_alt_value->size();
      memcpy(value_out, err_inj_alt_value->data(), std::min(alt_data_size, value_len));
      if (alt_data_size < value_len) {
        // Zero out any remaining bytes
        uint8_t* bytes_out = reinterpret_cast<uint8_t*>(value_out);
        memset(&bytes_out[alt_data_size], 0, value_len - alt_data_size);
      }
    } else {
      memset(value_out, 0, value_len);
    }

    return status;
  }

  auto iovar = iovar_table_.find(name);
  if (iovar == iovar_table_.end()) {
    BRCMF_ERR("Iovar -- %s is not found in iovar table.", name);
    memset(value_out, 0, value_len);
    return ZX_OK;
  }
  auto& impl = iovar->second;
  status = impl.Get(ifidx, value_out, value_len);
  if (status == ZX_ERR_IO_REFUSED) {
    // TODO(zhiyichen): If the buffer is too short, set return value to ZX_OK to return dcmd to
    // driver. Driver will ignore dcmd if the return value is not ZX_OK. This logic needs to be
    // fixed.
    status = ZX_OK;
    *fw_err = BCME_BUFTOOSHORT;
  }
  return status;
}

/* Iovar handler function definitions */
zx_status_t SimFirmware::IovarIfaceVarSet(SimIovarSetReq* req) {
  ZX_ASSERT_MSG(req->iftbl_offset.has_value(), "offset in sim_iface_entry_t not set");
  if (!iface_tbl_[req->ifidx].allocated) {
    return ZX_ERR_BAD_STATE;
  }
  char* base = (char*)&iface_tbl_[req->ifidx];
  memcpy(base + req->iftbl_offset.value(), req->value, req->value_len);
  return ZX_OK;
}

zx_status_t SimFirmware::IovarIfaceVarGet(SimIovarGetReq* req) {
  ZX_ASSERT_MSG(req->iftbl_offset.has_value(), "offset in sim_iface_entry_t not set");
  if (!iface_tbl_[req->ifidx].allocated) {
    return ZX_ERR_BAD_STATE;
  }
  char* base = (char*)&iface_tbl_[req->ifidx];
  memcpy(req->value, base + req->iftbl_offset.value(), req->value_len);
  return ZX_OK;
}

zx_status_t SimFirmware::IovarSet(SimIovarSetReq* req) {
  ZX_ASSERT_MSG(req->var_addr.has_value(), "Variable address is not set");
  memcpy(req->var_addr.value(), req->value, req->value_len);
  return ZX_OK;
}

zx_status_t SimFirmware::IovarGet(SimIovarGetReq* req) {
  ZX_ASSERT_MSG(req->var_addr.has_value(), "Variable address is not set");
  memcpy(req->value, req->var_addr.value(), req->value_len);
  return ZX_OK;
}

zx_status_t SimFirmware::IovarCurEtheraddrSet(SimIovarSetReq* req) {
  if (req->value_len != ETH_ALEN) {
    return ZX_ERR_INVALID_ARGS;
  }
  return SetMacAddr(req->ifidx, reinterpret_cast<const uint8_t*>(req->value));
}
zx_status_t SimFirmware::IovarCurEtheraddrGet(SimIovarGetReq* req) {
  if (iface_tbl_[req->ifidx].mac_addr_set)
    memcpy(req->value, iface_tbl_[req->ifidx].mac_addr.byte, ETH_ALEN);
  else
    memcpy(req->value, mac_addr_.data(), ETH_ALEN);
  return ZX_OK;
}

zx_status_t SimFirmware::IovarPfnMacaddrSet(SimIovarSetReq* req) {
  auto pfn_mac = reinterpret_cast<const brcmf_pno_macaddr_le*>(req->value);
  memcpy(pfn_mac_addr_.byte, pfn_mac->mac, ETH_ALEN);
  return ZX_OK;
}
zx_status_t SimFirmware::IovarPfnMacaddrGet(SimIovarGetReq* req) {
  memcpy(req->value, pfn_mac_addr_.byte, ETH_ALEN);
  return ZX_OK;
}

zx_status_t SimFirmware::IovarStbcTxSet(SimIovarSetReq* req) {
  auto stbc_tx = reinterpret_cast<const int32_t*>(req->value);
  if (*stbc_tx != -1 && *stbc_tx != 0 && *stbc_tx != 1) {
    BRCMF_ERR("stbc_tx: %d has to be 0, 1 or -1", *stbc_tx);
    return ZX_ERR_INVALID_ARGS;
  }
  if (txstreams_ == 1 && *stbc_tx == 1) {
    BRCMF_ERR("stbc_tx cannot be set to 1 when txstreams is 1");
    return ZX_ERR_INVALID_ARGS;
  }
  stbc_tx_ = *stbc_tx;
  BRCMF_DBG(SIM, "set stbc tx value: %u", stbc_tx_);
  return ZX_OK;
}

zx_status_t SimFirmware::IovarStbcTxGet(SimIovarGetReq* req) {
  BRCMF_DBG(SIM, "get stbc_tx value: %u", stbc_tx_);
  memcpy(req->value, &stbc_tx_, sizeof(stbc_tx_));
  return ZX_OK;
}

zx_status_t SimFirmware::IovarTxstreamsSet(SimIovarSetReq* req) {
  auto txstreams = reinterpret_cast<const uint32_t*>(req->value);
  if (*txstreams >= 1) {
    txstreams_ = *txstreams;
    BRCMF_DBG(SIM, "set txstreams value: %u", *txstreams);
    return ZX_OK;
  }
  BRCMF_ERR("txstreams: %d has to be atleast >= 1", *txstreams);
  return ZX_ERR_INVALID_ARGS;
}

zx_status_t SimFirmware::IovarTxstreamsGet(SimIovarGetReq* req) {
  BRCMF_DBG(SIM, "get txstreams value: %u", txstreams_);
  memcpy(req->value, &txstreams_, sizeof(txstreams_));
  return ZX_OK;
}

zx_status_t SimFirmware::IovarWsecKeySet(SimIovarSetReq* req) {
  auto wk_req = reinterpret_cast<const brcmf_wsec_key_le*>(req->value);
  // Ensure that Primary Key does not have a mac address (all zeros)
  if (wk_req->flags == BRCMF_PRIMARY_KEY) {
    ZX_ASSERT_MSG(std::memcmp(wk_req->ea, kZeroMac.byte, ETH_ALEN) == 0,
                  "Group Key Mac should be all zeros");
  }
  std::vector<brcmf_wsec_key_le>& key_list = iface_tbl_[req->ifidx].wsec_key_list;
  auto key_iter = std::find_if(key_list.begin(), key_list.end(),
                               [=](brcmf_wsec_key_le& k) { return k.index == wk_req->index; });
  // If the key with same index exists, override it, if not, add a new key.
  if (key_iter != key_list.end()) {
    *key_iter = *wk_req;
  } else {
    // Use the first key index as current key index, in real case it will only change by AP.
    if (key_list.empty())
      iface_tbl_[req->ifidx].cur_key_idx = wk_req->index;

    key_list.push_back(*wk_req);
  }
  return ZX_OK;
}
zx_status_t SimFirmware::IovarWsecKeyGet(SimIovarGetReq* req) {
  std::vector<brcmf_wsec_key_le>& key_list = iface_tbl_[req->ifidx].wsec_key_list;
  auto key_iter = std::find_if(key_list.begin(), key_list.end(), [=](brcmf_wsec_key_le& k) {
    return k.index == iface_tbl_[req->ifidx].cur_key_idx;
  });
  if (key_iter == key_list.end()) {
    return ZX_ERR_NOT_FOUND;
  }
  memcpy(req->value, &(*key_iter), sizeof(brcmf_wsec_key_le));
  return ZX_OK;
}

zx_status_t SimFirmware::IovarChanspecSet(SimIovarSetReq* req) {
  auto chanspec = reinterpret_cast<const uint16_t*>(req->value);
  // TODO(karthikrish) Add multi channel support in SIM Env. For now ensure all IFs use the same
  // channel
  return (SetIFChanspec(req->ifidx, *chanspec));
}

zx_status_t SimFirmware::IovarMpcSet(SimIovarSetReq* req) {
  auto mpc = reinterpret_cast<const uint32_t*>(req->value);
  // Ensure that mpc is never enabled when AP has been started
  if (softap_ifidx_ != std::nullopt) {
    // Ensure that mpc is 0 if the SoftAP has been started
    ZX_ASSERT_MSG(*mpc == 0, "mpc should be 0 when SoftAP is active");
  }
  mpc_ = *mpc;
  return ZX_OK;
}

zx_status_t SimFirmware::IovarInterfaceRemoveSet(SimIovarSetReq* req) {
  return HandleIfaceRequest(false, req->value, req->value_len, req->bsscfgidx);
}

zx_status_t SimFirmware::IovarSsidSet(SimIovarSetReq* req) {
  return HandleIfaceRequest(true, req->value, req->value_len, req->bsscfgidx);
}

zx_status_t SimFirmware::IovarEscanSet(SimIovarSetReq* req) {
  // For now scanning on softAP iface is not supported yet.
  if (req->ifidx != kClientIfidx) {
    BRCMF_ERR("Not scanning with client iface.");
    return ZX_ERR_INVALID_ARGS;
  }
  return HandleEscanRequest(reinterpret_cast<const brcmf_escan_params_le*>(req->value),
                            req->value_len);
}

zx_status_t SimFirmware::IovarJoinSet(SimIovarSetReq* req) {
  if (req->ifidx != kClientIfidx) {
    BRCMF_ERR("Not joining with client iface.");
    return ZX_ERR_INVALID_ARGS;
  }

  // This is a start point of authentication and association operation, make sure the client
  // iface is allocated.
  if (!iface_tbl_[kClientIfidx].allocated) {
    BRCMF_ERR("Client iface has not been allocated.");
    return ZX_ERR_BAD_STATE;
  }
  // Don't cast yet because last element is variable length
  return HandleJoinRequest(req->value, req->value_len);
}

zx_status_t SimFirmware::IovarBssSet(SimIovarSetReq* req) {
  auto bss_info = reinterpret_cast<const brcmf_bss_ctrl*>(req->value);
  // We do not know how non-zero value is handled in real FW.
  ZX_ASSERT(bss_info->value == 0);
  return StopInterface(bss_info->bsscfgidx);
}

zx_status_t SimFirmware::IovarAssocMgrCmdSet(SimIovarSetReq* req) {
  BRCMF_DBG(SIM, "Receive assoc_mgr_cmd in sim-fw.");
  ZX_ASSERT_MSG(req->ifidx == kClientIfidx, "SAE authentication only supported on client iface.");

  auto cmd = reinterpret_cast<const assoc_mgr_cmd_t*>(req->value);
  if (cmd->version != ASSOC_MGR_CURRENT_VERSION) {
    BRCMF_INFO("Version number doesn't match the current one, but ignoring it in sim-fw for now.");
  }

  switch (cmd->cmd) {
    case ASSOC_MGR_CMD_PAUSE_ON_EVT:
      if (cmd->params == ASSOC_MGR_PARAMS_PAUSE_EVENT_AUTH_RESP) {
        // When driver received JOIN_START event and successfully notified SME that the SAE
        // authentication should start by sending up handshake indication, it will send a
        // assoc_mgr_cmd with this parameter, telling firmware to expect the first authentication
        // frame from external supplicant. Here we only check whether the auth_state is correct.
        if (auth_state_.state != AuthState::EXPECTING_EXTERNAL_COMMIT) {
          BRCMF_ERR(
              "Incorrect state, START_AUTH event wasn't sent up to the driver, or the SAE "
              "process has been started. Ignoring the command and return.");
          return ZX_ERR_BAD_STATE;
        }
      } else if (cmd->params == ASSOC_MGR_PARAMS_EVENT_NONE) {
        // Driver is trying to start association after finishing SAE authentication.
        if (auth_state_.state != AuthState::EXPECTING_EXTERNAL_HANDSHAKE_RESP) {
          BRCMF_ERR(
              "Unexpected HANDSHAKE_RESP from external supplicant. Ignoring the command and "
              "return.");
          return ZX_ERR_BAD_STATE;
        }
        // Handshake resp received from external supplicant, cancel timer.
        hw_.CancelCallback(auth_state_.auth_timer_id);

        auth_state_.state = AuthState::AUTHENTICATED;
        AssocStart();
        return ZX_OK;
      }
      break;
    case ASSOC_MGR_CMD_ABORT_ASSOC:
      ZX_ASSERT_MSG(false, "Not supporting this command for assoc_mgr_cmd yet.");
      break;
    case ASSOC_MGR_CMD_SEND_AUTH: {
      ZX_ASSERT_MSG(req->ifidx == kClientIfidx,
                    "SAE authentication only supported on client iface.");

      auto sae_frame = reinterpret_cast<const brcmf_sae_auth_frame*>(&cmd->params);
      if (memcmp(sae_frame->mac_hdr.addr1.byte, assoc_state_.opts->bssid.byte, ETH_ALEN) ||
          memcmp(sae_frame->mac_hdr.addr3.byte, assoc_state_.opts->bssid.byte, ETH_ALEN)) {
        BRCMF_ERR(
            "Dest addr does not match in SAE frame from external supplicant. Ignoring frame.");
        return ZX_ERR_INVALID_ARGS;
      }

      // Authentication algorithm field must be SAE.
      if (sae_frame->auth_hdr.auth_algorithm_number != BRCMF_AUTH_MODE_SAE) {
        BRCMF_ERR("Authentication algorithm number does not match SAE algorithm number");
        return ZX_ERR_INVALID_ARGS;
      }

      size_t sae_payload_length = cmd->length - offsetof(struct brcmf_sae_auth_frame, sae_payload);
      if (LocalUpdateExternalSaeStatus(
              sae_frame->auth_hdr.auth_txn_seq_number,
              static_cast<wlan_ieee80211::StatusCode>(sae_frame->auth_hdr.status_code),
              sae_frame->sae_payload, sae_payload_length) != ZX_OK) {
        BRCMF_ERR("Update SAE status failed with auth frame from external supllicant.");
        return ZX_ERR_BAD_STATE;
      }
      break;
    }
    default:
      ZX_ASSERT_MSG(false, "Command type %u is not currently supported for assoc_mgr_cmd.",
                    cmd->cmd);
      break;
  }
  return ZX_OK;
}

zx_status_t SimFirmware::IovarCrashSet(SimIovarSetReq* req) {
  // No need to check the value of 'crash' iovar.
  ResetSimFirmware();
  return ZX_OK;
}

zx_status_t SimFirmware::IovarVerGet(SimIovarGetReq* req) {
  strlcpy(static_cast<char*>(req->value), kFirmwareVer, req->value_len);
  return ZX_OK;
}

zx_status_t SimFirmware::IovarRxchainGet(SimIovarGetReq* req) {
  if (!iface_tbl_[req->ifidx].allocated) {
    return ZX_ERR_BAD_STATE;
  }
  // rxchain 1 indicates antenna index 0.
  const uint32_t sim_rxchain = 1;
  memcpy(req->value, &sim_rxchain, sizeof(uint32_t));
  return ZX_OK;
}

zx_status_t SimFirmware::IovarSnrGet(SimIovarGetReq* req) {
  if (!iface_tbl_[req->ifidx].allocated) {
    return ZX_ERR_BAD_STATE;
  }
  int32_t sim_snr = 40;
  memcpy(req->value, &sim_snr, sizeof(sim_snr));
  return ZX_OK;
}

zx_status_t SimFirmware::IovarWstatsCountersGet(SimIovarGetReq* req) {
  if (!iface_tbl_[req->ifidx].allocated) {
    return ZX_ERR_BAD_STATE;
  }
  wl_wstats_cnt_t wstats_cnt = {
      .version = WSTATS_CNT_T_VERSION,
  };

  // Manually populate a few histograms.
  const uint16_t snr_db = 60;
  const uint32_t snr_num_frames = 50;
  wstats_cnt.rxsnr[snr_db] = snr_num_frames;
  const uint32_t noiseflr_dbm_index = 170;
  const uint32_t noiseflr_num_frames = 20;
  wstats_cnt.rxnoiseflr[noiseflr_dbm_index] = noiseflr_num_frames;
  const uint32_t rssi_dbm_index = 190;
  const uint32_t rssi_num_frames = 40;
  wstats_cnt.rxrssi[rssi_dbm_index] = rssi_num_frames;
  const uint32_t rate_index = 7;
  const uint32_t rate_num_frames = 80;
  wstats_cnt.rx11g[rate_index] = rate_num_frames;

  memcpy(req->value, &wstats_cnt, sizeof(wl_wstats_cnt_t));
  return ZX_OK;
}

zx_status_t SimFirmware::IovarAssocInfoGet(SimIovarGetReq* req) {
  auto result_ptr = static_cast<brcmf_cfg80211_assoc_ielen_le*>(req->value);
  result_ptr->req_len = 0;
  result_ptr->resp_len = assoc_resp_ies_len_;
  return ZX_OK;
}

zx_status_t SimFirmware::IovarAssocRespIesGet(SimIovarGetReq* req) {
  if (req->value_len < assoc_resp_ies_len_) {
    BRCMF_ERR("Buffer length is too small for the prepared assoc response ies.");
    return ZX_ERR_IO_REFUSED;
  }
  memcpy(req->value, assoc_resp_ies_, assoc_resp_ies_len_);
  return ZX_OK;
}

zx_status_t SimFirmware::IovarCapGet(SimIovarGetReq* req) {
  strlcpy(static_cast<char*>(req->value), kFirmwareCap, req->value_len);
  return ZX_OK;
}

zx_status_t SimFirmware::IovarNmodeGet(SimIovarGetReq* req) {
  // TODO: Provide means to simulate hardware without nmode.
  uint32_t* result_ptr = static_cast<uint32_t*>(req->value);
  *result_ptr = 1;
  return ZX_OK;
}

zx_status_t SimFirmware::IovarVhtModeGet(SimIovarGetReq* req) {
  // TODO: Provide means to simulate hardware without vhtmode.
  uint32_t* result_ptr = static_cast<uint32_t*>(req->value);
  *result_ptr = 1;
  return ZX_OK;
}

zx_status_t SimFirmware::IovarRrmGet(SimIovarGetReq* req) {
  // TODO: Provide means to simulate hardware without rrm.
  uint32_t* result_ptr = static_cast<uint32_t*>(req->value);
  *result_ptr = 1;
  return ZX_OK;
}

zx_status_t SimFirmware::IovarWmeAcStaGet(SimIovarGetReq* req) {
  // Note: the below mocked AC parameters are slightly different from default values
  //       because we set different values for each AC to make sure in the test that
  //       the right ACs are parsed.
  edcf_acparam_t params[4];

  // AC_BE
  params[0].aci = 4;              // aifsn
  params[0].ecw = 5 + (10 << 4);  // ecw_min + (ecw_max << 4)
  params[0].txop = 0;

  // AC_BK
  params[1].aci = 7 + (1 << 5);   // aifsn + (aci << 5)
  params[1].ecw = 6 + (11 << 4);  // ecw_min + (ecw_max << 4)
  params[1].txop = 0;

  // AC_VI
  params[2].aci = 3 + (2 << 5);  // aifsn + (aci << 5)
  params[2].ecw = 4 + (5 << 4);  // ecw_min + (ecw_max << 4)
  params[2].txop = 94;

  // AC_VO
  params[3].aci = 2 + (1 << 4) + (3 << 5);  // aifsn + (acm << 4) + (aci << 5)
  params[3].ecw = 2 + (4 << 4);             // ecw_min + (ecw_max << 4)
  params[3].txop = 47;

  memcpy(req->value, params, sizeof(params));
  return ZX_OK;
}

zx_status_t SimFirmware::IovarWmeApsdGet(SimIovarGetReq* req) {
  uint32_t* result_ptr = static_cast<uint32_t*>(req->value);
  *result_ptr = 1;
  return ZX_OK;
}

// If setting for the first time, save it as system mac address as well
zx_status_t SimFirmware::SetMacAddr(uint16_t ifidx, const uint8_t* mac_addr) {
  if (mac_addr_set_ == false) {
    BRCMF_DBG(SIM, "Setting system mac addr: " FMT_MAC, FMT_MAC_ARGS(mac_addr));
    memcpy(mac_addr_.data(), mac_addr, ETH_ALEN);
    memcpy(pfn_mac_addr_.byte, mac_addr, ETH_ALEN);
    mac_addr_set_ = true;
  }
  memcpy(iface_tbl_[ifidx].mac_addr.byte, mac_addr, ETH_ALEN);
  iface_tbl_[ifidx].mac_addr_set = true;

  BRCMF_DBG(SIM, "Setting mac addr ifidx: %d: " FMT_MAC, ifidx, FMT_MAC_ARGS(mac_addr));
  return ZX_OK;
}

common::MacAddr SimFirmware::GetMacAddr(uint16_t ifidx) {
  if (ifidx < kMaxIfSupported && iface_tbl_[ifidx].allocated && iface_tbl_[ifidx].mac_addr_set) {
    return iface_tbl_[ifidx].mac_addr;
  }
  ZX_ASSERT(mac_addr_set_);
  return common::MacAddr(mac_addr_);
}

zx_status_t SimFirmware::ScanStart(std::unique_ptr<ScanOpts> opts) {
  if (scan_state_.state != ScanState::STOPPED) {
    // Can't start a scan while another is in progress
    return ZX_ERR_NOT_SUPPORTED;
  }

  if (!iface_tbl_[kClientIfidx].allocated) {
    BRCMF_ERR("client iface has not been allocated.");
    return ZX_ERR_BAD_STATE;
  }

  // I believe in real firmware this will just search all channels. We need to define that set in
  // order for this functionality to work.
  ZX_ASSERT_MSG(opts->channels.size() >= 1,
                "No channels provided to escan start request - unsupported");

  // Configure state
  scan_state_.state = ScanState::SCANNING;
  scan_state_.opts = std::move(opts);
  scan_state_.channel_index = 0;

  // Start scan
  uint16_t chanspec = scan_state_.opts->channels[scan_state_.channel_index++];
  wlan_channel_t channel;
  chanspec_to_channel(&d11_inf_, chanspec, &channel);
  hw_.SetChannel(channel);

  // Do an active scan using random mac
  // TODO(fxbug.dev/89334): SSIDs in scan request are ignored
  if (scan_state_.opts->is_active) {
    scan_state_.active_scan_attempts = 1;
    simulation::SimProbeReqFrame probe_req_frame(pfn_mac_addr_);
    hw_.Tx(probe_req_frame);
  }
  hw_.EnableRx();

  hw_.RequestCallback(std::bind(&SimFirmware::ScanContinue, this), scan_state_.opts->dwell_time);
  return ZX_OK;
}

// Continue or stop scanning based on scan type.
// Active scan - continue with current channel until max attempts of probes sent
// or else switch to next channel
// Passive scan - switch to next channel
void SimFirmware::ScanContinue() {
  switch (scan_state_.state) {
    case ScanState::STOPPED:
      // We may see this event if a scan was cancelled -- just ignore it
      return;
    case ScanState::HOME:
      // We don't yet support intermittent scanning
      return;
    case ScanState::SCANNING:
      // If active scan, check and resend probe request if max attempts not reached.
      if (scan_state_.opts->is_active &&
          scan_state_.active_scan_attempts < scan_state_.opts->active_scan_max_attempts) {
        BRCMF_DBG(SIM, "active scan continue: %d att: %d max: %d", scan_state_.opts->is_active,
                  scan_state_.active_scan_attempts, scan_state_.opts->active_scan_max_attempts);
        // Note that the channel remains the same
        scan_state_.active_scan_attempts++;
        simulation::SimProbeReqFrame probe_req_frame(pfn_mac_addr_);
        hw_.Tx(probe_req_frame);
        hw_.RequestCallback(std::bind(&SimFirmware::ScanContinue, this),
                            scan_state_.opts->dwell_time);
        return;
      }

      if (scan_state_.channel_index >= scan_state_.opts->channels.size()) {
        ScanComplete(BRCMF_E_STATUS_SUCCESS);
      } else {
        // Scan next channel
        uint16_t chanspec = scan_state_.opts->channels[scan_state_.channel_index++];
        wlan_channel_t channel;
        chanspec_to_channel(&d11_inf_, chanspec, &channel);
        hw_.SetChannel(channel);
        BRCMF_DBG(SIM, "Continue scan - next chanspec: 0x%x", chanspec);
        if (scan_state_.opts->is_active) {
          scan_state_.active_scan_attempts = 1;
          simulation::SimProbeReqFrame probe_req_frame(pfn_mac_addr_);
          hw_.Tx(probe_req_frame);
        }
        hw_.RequestCallback(std::bind(&SimFirmware::ScanContinue, this),
                            scan_state_.opts->dwell_time);
      }
  }
}

// Clean up state after a scan request is finished
void SimFirmware::ScanComplete(brcmf_fweh_event_status_t status) {
  if (scan_state_.opts->is_active) {
    BRCMF_DBG(SIM, "Resetting pfn_mac_addr_ to system mac addr: " FMT_MAC,
              FMT_MAC_ARGS(mac_addr_.data()));
    memcpy(pfn_mac_addr_.byte, mac_addr_.data(), ETH_ALEN);
  }
  hw_.DisableRx();

  scan_state_.state = ScanState::STOPPED;
  // Restore the operating channel since Scan is done. This applies
  // only if the scan was started when the IF is already associated
  if (iface_tbl_[kClientIfidx].chanspec) {
    wlan_channel_t channel;
    chanspec_to_channel(&d11_inf_, iface_tbl_[kClientIfidx].chanspec, &channel);
    hw_.SetChannel(channel);
  }
  scan_state_.opts->on_done_fn(status);
  scan_state_.opts = nullptr;
}

// Send an event to the firmware notifying them that the scan has completed.
zx_status_t SimFirmware::HandleEscanRequest(const brcmf_escan_params_le* escan_params,
                                            size_t params_len) {
  if (escan_params->version != BRCMF_ESCAN_REQ_VERSION) {
    BRCMF_DBG(SIM, "Mismatched escan version (expected %d, saw %d) - ignoring request",
              BRCMF_ESCAN_REQ_VERSION, escan_params->version);
    return ZX_ERR_NOT_SUPPORTED;
  }

  switch (escan_params->action) {
    case WL_ESCAN_ACTION_START:
      return EscanStart(escan_params->sync_id, &escan_params->params_le,
                        params_len - offsetof(brcmf_escan_params_le, params_le));
    case WL_ESCAN_ACTION_CONTINUE:
      ZX_ASSERT_MSG(0, "Unimplemented escan option WL_ESCAN_ACTION_CONTINUE");
      return ZX_ERR_NOT_SUPPORTED;
    case WL_ESCAN_ACTION_ABORT:
      ZX_ASSERT_MSG(0, "Unimplemented escan option WL_ESCAN_ACTION_ABORT");
      return ZX_ERR_NOT_SUPPORTED;
    default:
      ZX_ASSERT_MSG(0, "Unrecognized escan option %d", escan_params->action);
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
      if (params->active_time == static_cast<uint32_t>(-1)) {
        BRCMF_ERR("No active scan time in parameter");
        return ZX_ERR_INVALID_ARGS;
      } else {
        int32_t nprobes = (int32_t)params->nprobes;
        scan_opts->active_scan_max_attempts = nprobes <= 0 ? 1 : nprobes;
        // Dwell time is equally split across the # of attempts
        scan_opts->dwell_time = zx::msec(params->active_time) / scan_opts->active_scan_max_attempts;
        BRCMF_DBG(SIM, "Active scan req: act time: %u nprobes: %d act time: %d dwell: %zu",
                  params->active_time, params->nprobes, params->active_time,
                  scan_opts->dwell_time.get());
      }
      break;
    case BRCMF_SCANTYPE_PASSIVE:
      scan_opts->is_active = false;
      // Determine dwell time. If specified in the request, use that value. Otherwise, if a default
      // dwell time has been specified, use that value. Otherwise, fail.
      if (params->passive_time == static_cast<uint32_t>(-1)) {
        if (default_passive_time_ == static_cast<uint32_t>(-1)) {
          BRCMF_ERR("Attempt to use default passive time, iovar hasn't been set yet");
          return ZX_ERR_INVALID_ARGS;
        }
        scan_opts->dwell_time = zx::msec(default_passive_time_);
      } else {
        scan_opts->dwell_time = zx::msec(params->passive_time);
      }
      break;
    default:
      BRCMF_DBG(SIM, "Invalid scan type requested: %d", params->scan_type);
      return ZX_ERR_INVALID_ARGS;
  }

  size_t num_channels = params->channel_num & BRCMF_SCAN_PARAMS_COUNT_MASK;

  // Configure state
  scan_opts->channels.resize(num_channels);
  std::copy(&params->channel_list[0], &params->channel_list[num_channels],
            scan_opts->channels.data());

  scan_opts->on_result_fn = std::bind(&SimFirmware::EscanResultSeen, this, std::placeholders::_1);
  scan_opts->on_done_fn = std::bind(&SimFirmware::EscanComplete, this, std::placeholders::_1);
  return ScanStart(std::move(scan_opts));
}

void SimFirmware::EscanComplete(brcmf_fweh_event_status_t event_status) {
  // The last field in a brcmf_escan_result_le is variable-length (although it isn't defined that
  // way). To generate a scan complete message, it has to have no BSS entries.
  size_t buf_len = offsetof(brcmf_escan_result_le, bss_info_le);
  auto buf = std::make_unique<std::vector<uint8_t>>(buf_len);

  // Assemble the result buffer. As per above, we are making a pointer to the escan_result, but we
  // MUST NOT reference the last field (bss_info_le), since it hasn't been allocated.
  auto result = reinterpret_cast<brcmf_escan_result_le*>(buf->data());
  result->buflen = buf_len;
  result->version = BRCMF_BSS_INFO_VERSION;
  result->sync_id = scan_state_.opts->sync_id;
  result->bss_count = 0;

  SendEventToDriver(buf_len, std::move(buf), BRCMF_E_ESCAN_RESULT, event_status, kClientIfidx);
}

void SimFirmware::Rx(std::shared_ptr<const simulation::SimFrame> frame,
                     std::shared_ptr<const simulation::WlanRxInfo> info) {
  if (frame->FrameType() == simulation::SimFrame::FRAME_TYPE_MGMT) {
    auto mgmt_frame = std::static_pointer_cast<const simulation::SimManagementFrame>(frame);
    RxMgmtFrame(mgmt_frame, info);
  } else if (frame->FrameType() == simulation::SimFrame::FRAME_TYPE_DATA) {
    auto data_frame = std::static_pointer_cast<const simulation::SimDataFrame>(frame);
    RxDataFrame(data_frame, info);
  }
}

void SimFirmware::RxMgmtFrame(std::shared_ptr<const simulation::SimManagementFrame> mgmt_frame,
                              std::shared_ptr<const simulation::WlanRxInfo> info) {
  switch (mgmt_frame->MgmtFrameType()) {
    case simulation::SimManagementFrame::FRAME_TYPE_BEACON: {
      auto beacon = std::static_pointer_cast<const simulation::SimBeaconFrame>(mgmt_frame);
      RxBeacon(info->channel, beacon, info->signal_strength);
      break;
    }

    case simulation::SimManagementFrame::FRAME_TYPE_PROBE_RESP: {
      auto probe_resp = std::static_pointer_cast<const simulation::SimProbeRespFrame>(mgmt_frame);
      RxProbeResp(info->channel, probe_resp, info->signal_strength);
      break;
    }

    case simulation::SimManagementFrame::FRAME_TYPE_ASSOC_REQ: {
      auto assoc_req = std::static_pointer_cast<const simulation::SimAssocReqFrame>(mgmt_frame);
      RxAssocReq(assoc_req);
      break;
    }

    case simulation::SimManagementFrame::FRAME_TYPE_ASSOC_RESP: {
      auto assoc_resp = std::static_pointer_cast<const simulation::SimAssocRespFrame>(mgmt_frame);
      RxAssocResp(assoc_resp);
      break;
    }

    case simulation::SimManagementFrame::FRAME_TYPE_DISASSOC_REQ: {
      auto disassoc_req =
          std::static_pointer_cast<const simulation::SimDisassocReqFrame>(mgmt_frame);
      RxDisassocReq(disassoc_req);
      break;
    }

    case simulation::SimManagementFrame::FRAME_TYPE_AUTH: {
      auto auth_resp = std::static_pointer_cast<const simulation::SimAuthFrame>(mgmt_frame);
      RxAuthFrame(auth_resp);
      break;
    }

    case simulation::SimManagementFrame::FRAME_TYPE_DEAUTH: {
      auto deauth_req = std::static_pointer_cast<const simulation::SimDeauthFrame>(mgmt_frame);
      RxDeauthReq(deauth_req);
      break;
    }

    case simulation::SimManagementFrame::FRAME_TYPE_REASSOC_RESP: {
      auto reassoc_resp =
          std::static_pointer_cast<const simulation::SimReassocRespFrame>(mgmt_frame);
      RxReassocResp(reassoc_resp);
      break;
    }

    default:
      break;
  }
}

bool SimFirmware::OffloadArpFrame(int16_t ifidx,
                                  std::shared_ptr<const simulation::SimDataFrame> data_frame) {
  // Feature is disabled for this interface
  if (arpoe_ == 0) {
    return false;
  }

  if (data_frame->payload_.size() < (sizeof(ethhdr) + sizeof(ether_arp))) {
    return false;
  }

  auto eth_hdr = reinterpret_cast<const ethhdr*>(data_frame->payload_.data());
  if (ntohs(eth_hdr->h_proto) != ETH_P_ARP) {
    return false;
  }

  auto arp_hdr = reinterpret_cast<const ether_arp*>(&data_frame->payload_.data()[sizeof(eth_hdr)]);
  uint16_t ar_op = ntohs(arp_hdr->ea_hdr.ar_op);

  if (ar_op == ARPOP_REQUEST) {
    // TODO: Actually construct the ARP reply, which would require us to sniff for IP addresses.
    // For now, not forwarding the packet to the host is enough.
    return (arp_ol_ & BRCMF_ARP_OL_AGENT) && (arp_ol_ & BRCMF_ARP_OL_PEER_AUTO_REPLY);
  }

  // TODO: Add support for ARP offloading of other commands
  ZX_ASSERT_MSG(0, "Support for ARP offloading (op = %d) unimplemented", ar_op);
  return false;
}

void SimFirmware::RxDataFrame(std::shared_ptr<const simulation::SimDataFrame> data_frame,
                              std::shared_ptr<const simulation::WlanRxInfo> info) {
  bool is_broadcast = (data_frame->addr1_ == common::kBcastMac);

  for (uint8_t idx = 0; idx < kMaxIfSupported; idx++) {
    if (!iface_tbl_[idx].allocated)
      continue;
    if (!(is_broadcast || (data_frame->addr1_ == iface_tbl_[idx].mac_addr)))
      continue;
    if (OffloadArpFrame(idx, data_frame))
      continue;
    if (idx == kClientIfidx && assoc_state_.state != AssocState::ASSOCIATED) {
      // Receiving a unicast packet before client is associated. Send a deauth event
      // to driver (in the real world we would have sent a deauth frame to the sender
      BRCMF_DBG(SIM, "Sending E_DEAUTH to driver");
      SendEventToDriver(0, nullptr, BRCMF_E_DEAUTH, BRCMF_E_STATUS_SUCCESS, idx, nullptr, 0,
                        BRCMF_E_REASON_UCAST_FROM_UNASSOC_STA);
      continue;
    }
    SendFrameToDriver(idx, data_frame->payload_.size(), data_frame->payload_, info);
  }
}

// Start or restart the beacon watchdog. This is a timeout event mirroring how the firmware can
// detect when a connection is lost from the lack of beacons received.
void SimFirmware::RestartBeaconWatchdog() {
  DisableBeaconWatchdog();
  assoc_state_.is_beacon_watchdog_active = true;
  hw_.RequestCallback(std::bind(&SimFirmware::HandleBeaconTimeout, this), zx::sec(beacon_timeout_),
                      &assoc_state_.beacon_watchdog_id_);
}

void SimFirmware::DisableBeaconWatchdog() {
  if (assoc_state_.is_beacon_watchdog_active) {
    hw_.CancelCallback(assoc_state_.beacon_watchdog_id_);
  }
}

void SimFirmware::HandleBeaconTimeout() {
  // Ignore if we are not associated
  if (assoc_state_.state != AssocState::ASSOCIATED) {
    return;
  }

  assoc_state_.is_beacon_watchdog_active = false;
  // Indicate to the driver that we're disassociating due to lost beacons
  SendEventToDriver(0, nullptr, BRCMF_E_LINK, BRCMF_E_STATUS_SUCCESS, kClientIfidx, 0,
                    BRCMF_E_REASON_LOW_RSSI);
  SendEventToDriver(0, nullptr, BRCMF_E_LINK, BRCMF_E_STATUS_SUCCESS, kClientIfidx, 0,
                    BRCMF_E_REASON_DEAUTH);
  AssocClearContext();
}

void SimFirmware::ConductChannelSwitch(const wlan_channel_t& dst_channel, uint8_t mode) {
  // Change fw and hw channel
  uint16_t chanspec;
  ZX_ASSERT_MSG(iface_tbl_[kClientIfidx].allocated, "No client found!");

  hw_.SetChannel(dst_channel);
  chanspec = channel_to_chanspec(&d11_inf_, &dst_channel);
  SetIFChanspec(kClientIfidx, chanspec);

  // Send up CSA event to driver
  auto buf = std::make_unique<std::vector<uint8_t>>(sizeof(uint8_t));
  *(buf->data()) = mode;
  SendEventToDriver(sizeof(uint8_t), std::move(buf), BRCMF_E_CSA_COMPLETE_IND,
                    BRCMF_E_STATUS_SUCCESS, kClientIfidx);

  // Clear state
  channel_switch_state_.state = ChannelSwitchState::HOME;
}

// static
int8_t SimFirmware::RssiDbmFromSignalStrength(double signal_strength) {
  // truncate signal strength to rssi unit
  if (signal_strength > INT8_MAX) {
    return INT8_MAX;
  } else if (signal_strength < INT8_MIN) {
    return INT8_MIN;
  }
  return signal_strength;
}

void SimFirmware::RxBeacon(const wlan_channel_t& channel,
                           std::shared_ptr<const simulation::SimBeaconFrame> frame,
                           double signal_strength) {
  if (scan_state_.state == ScanState::SCANNING && !scan_state_.opts->is_active) {
    int8_t rssi_dbm = RssiDbmFromSignalStrength(signal_strength);
    ScanResult scan_result = {
        .channel = channel, .bssid = frame->bssid_, .rssi_dbm = rssi_dbm, .ies = frame->IEs_};

    scan_result.bss_capability.set_val(frame->capability_info_.val());
    scan_state_.opts->on_result_fn(scan_result);
    // TODO(fxbug.dev/49350): Channel switch during scanning need to be supported.
  } else if (assoc_state_.state == AssocState::ASSOCIATED &&
             frame->bssid_ == assoc_state_.opts->bssid) {
    // if we're associated with this AP, start/restart the beacon watchdog
    RestartBeaconWatchdog();

    auto ie = frame->FindIe(simulation::InformationElement::IE_TYPE_CSA);
    if (ie == nullptr) {
      return;
    }
    // If CSA IE exists.
    auto csa_ie = std::static_pointer_cast<simulation::CsaInformationElement>(ie);

    // Get current chanspec of client ifidx and convert to channel.
    wlan_channel_t channel = GetIfChannel(false);

    zx::duration SwitchDelay = frame->interval_ * (int64_t)csa_ie->channel_switch_count_;

    if (channel_switch_state_.state == ChannelSwitchState::HOME) {
      // If the destination channel is the same as current channel, just ignore it.
      if (csa_ie->new_channel_number_ == channel.primary) {
        return;
      }

      channel.primary = csa_ie->new_channel_number_;
      channel_switch_state_.new_channel = csa_ie->new_channel_number_;

      channel_switch_state_.state = ChannelSwitchState::SWITCHING;
    } else {
      ZX_ASSERT(channel_switch_state_.state == ChannelSwitchState::SWITCHING);
      if (csa_ie->new_channel_number_ == channel_switch_state_.new_channel) {
        return;
      }

      // If the new channel is different from the previous dst channel, cancel callback.
      hw_.CancelCallback(channel_switch_state_.switch_timer_id);

      // If it's the same as current channel for this client before switching, just simply cancel
      // the switch event and clear state.
      if (csa_ie->new_channel_number_ == channel.primary) {
        channel_switch_state_.state = ChannelSwitchState::HOME;
        return;
      }

      // Schedule a new event when dst channel change.
      channel.primary = csa_ie->new_channel_number_;
    }

    hw_.RequestCallback(
        std::bind(&SimFirmware::ConductChannelSwitch, this, channel, csa_ie->channel_switch_mode_),
        SwitchDelay, &channel_switch_state_.switch_timer_id);
  }
}

void SimFirmware::RxProbeResp(const wlan_channel_t& channel,
                              std::shared_ptr<const simulation::SimProbeRespFrame> frame,
                              double signal_strength) {
  if (scan_state_.state != ScanState::SCANNING || !scan_state_.opts->is_active) {
    return;
  }

  int8_t rssi_dbm = SimFirmware::RssiDbmFromSignalStrength(signal_strength);
  ScanResult scan_result = {
      .channel = channel, .bssid = frame->src_addr_, .rssi_dbm = rssi_dbm, .ies = frame->IEs_};

  scan_result.bss_capability.set_val(frame->capability_info_.val());
  scan_state_.opts->on_result_fn(scan_result);
}

// Handle an Rx Beacon sent to us from the hardware, using it to fill in all of the fields in a
// brcmf_escan_result.
void SimFirmware::EscanResultSeen(const ScanResult& result_in) {
  std::vector<uint8_t> ie_buf;
  for (const auto& ie : result_in.ies) {
    if (ie == nullptr) {
      continue;
    }
    switch (ie->IeType()) {
      // TODO(fxbug.dev/89334): scan_state_.opts should contain SSIDs which would determine
      // whether to report this scan result or not.
      case simulation::InformationElement::IE_TYPE_SSID: {
        const auto ssid_ie = std::static_pointer_cast<simulation::SsidInformationElement>(ie);
        std::vector<uint8_t> current_ie_buf = ssid_ie->ToRawIe();
        ie_buf.insert(ie_buf.end(), current_ie_buf.begin(), current_ie_buf.end());
        break;
      }
      case simulation::InformationElement::IE_TYPE_CSA: {
        const auto csa_ie = std::static_pointer_cast<simulation::CsaInformationElement>(ie);
        std::vector<uint8_t> current_ie_buf = csa_ie->ToRawIe();
        ie_buf.insert(ie_buf.end(), current_ie_buf.begin(), current_ie_buf.end());
        break;
      }
      case simulation::InformationElement::IE_TYPE_WPA1:
        __FALLTHROUGH;
      case simulation::InformationElement::IE_TYPE_WPA2:
        __FALLTHROUGH;
      default:
        break;
    }
  }

  // scan_result_size includes all BSS info structures (each including IEs). Like the firmware, we
  // only send one result back at a time.
  size_t scan_result_size = roundup(sizeof(brcmf_escan_result_le) + ie_buf.size(), 4);

  auto buf = std::make_unique<std::vector<uint8_t>>(scan_result_size);

  uint8_t* buffer_data = buf->data();
  auto result_out = reinterpret_cast<brcmf_escan_result_le*>(buffer_data);
  result_out->buflen = scan_result_size;
  result_out->version = BRCMF_BSS_INFO_VERSION;
  result_out->sync_id = scan_state_.opts->sync_id;
  result_out->bss_count = 1;

  struct brcmf_bss_info_le* bss_info = &result_out->bss_info_le;
  bss_info->version = BRCMF_BSS_INFO_VERSION;

  // length of this record (includes IEs)
  bss_info->length = roundup(sizeof(brcmf_bss_info_le) + ie_buf.size(), 4);
  // channel
  bss_info->chanspec = channel_to_chanspec(&d11_inf_, &result_in.channel);
  // capability
  bss_info->capability = result_in.bss_capability.val();

  // ssid
  bss_info->SSID_len = 0;  // SSID will go into an IE

  // bssid
  ZX_ASSERT(sizeof(bss_info->BSSID) == common::kMacAddrLen);
  memcpy(bss_info->BSSID, result_in.bssid.byte, common::kMacAddrLen);

  // RSSI
  bss_info->RSSI = result_in.rssi_dbm;

  // IEs
  bss_info->ie_offset = sizeof(brcmf_bss_info_le);
  size_t ie_offset = sizeof(brcmf_escan_result_le);
  uint8_t* ie_data = &buffer_data[ie_offset];
  std::memcpy(ie_data, ie_buf.data(), ie_buf.size());

  bss_info->ie_length = ie_buf.size();

  // Wrap this in an event and send it back to the driver
  SendEventToDriver(scan_result_size, std::move(buf), BRCMF_E_ESCAN_RESULT, BRCMF_E_STATUS_PARTIAL,
                    kClientIfidx);
}

std::shared_ptr<std::vector<uint8_t>> SimFirmware::CreateEventBuffer(
    size_t requested_size, brcmf_event_msg_be** msg_out_be, size_t* payload_offset_out) {
  size_t total_size = sizeof(brcmf_event) + requested_size;
  size_t event_data_offset;

  // Note: events always encode the interface index into the event header and 0 into the BCDC
  // header.
  auto buf = CreateBcdcBuffer(0, total_size, &event_data_offset);

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

void SimFirmware::SendEventToDriver(size_t payload_size,
                                    std::shared_ptr<std::vector<uint8_t>> buffer_in,
                                    uint32_t event_type, uint32_t status, uint16_t ifidx,
                                    char* ifname, uint16_t flags, uint32_t reason,
                                    std::optional<common::MacAddr> addr,
                                    std::optional<zx::duration> delay) {
  BRCMF_DBG(SIM, "*****Sending Event: %d*****", event_type);
  brcmf_event_msg_be* msg_be;
  size_t payload_offset;
  // Assert if ifidx is not valid
  if (event_type != BRCMF_E_IF)
    ZX_ASSERT(ifidx < kMaxIfSupported && iface_tbl_[ifidx].allocated);

  auto buf = CreateEventBuffer(payload_size, &msg_be, &payload_offset);
  msg_be->flags = htobe16(flags);
  msg_be->event_type = htobe32(event_type);
  msg_be->status = htobe32(status);
  msg_be->reason = htobe32(reason);
  msg_be->auth_type = htobe32(iface_tbl_[ifidx].auth_type);
  msg_be->ifidx = ifidx;
  msg_be->bsscfgidx = iface_tbl_[ifidx].bsscfgidx;

  if (ifname)
    memcpy(msg_be->ifname, ifname, IFNAMSIZ);

  if (addr)
    memcpy(msg_be->addr, addr->byte, ETH_ALEN);

  if (payload_size != 0) {
    ZX_ASSERT(buffer_in != nullptr);
    uint8_t* buf_data = buf->data();
    memcpy(&buf_data[payload_offset], buffer_in->data(), payload_size);
  }

  if (delay && delay->get() > 0) {
    // Setup the callback and return.
    hw_.RequestCallback(std::bind(&brcmf_sim_rx_event, simdev_, buf), delay.value());
    return;
  } else {
    BRCMF_DBG(SIM, "Sending Event: %d", event_type);
    brcmf_sim_rx_event(simdev_, std::move(buf));
  }
}

void SimFirmware::SendFrameToDriver(uint16_t ifidx, size_t payload_size,
                                    const std::vector<uint8_t>& buffer_in,
                                    std::shared_ptr<const simulation::WlanRxInfo> info) {
  size_t header_offset;
  auto buf = CreateBcdcBuffer(ifidx, payload_size, &header_offset);

  if (payload_size != 0) {
    ZX_ASSERT(!buffer_in.empty());
    uint8_t* buf_data = buf->data();
    memcpy(&buf_data[header_offset], buffer_in.data(), payload_size);
  }

  brcmf_sim_rx_frame(simdev_, std::move(buf));
}

// This function schedules an event for brcmf_sim_reset() instead of directly calling this function,
// this is because this function will delete and recreated the sim-fw, so that sim-fw will release
// itself if this function is in the same call chain, which leads a segment fault.
void SimFirmware::ResetSimFirmware() {
  // The crash happens immediately after 'crash' iovar is received from firmware.
  hw_.RequestCallback(std::bind(&brcmf_sim_firmware_crash, simdev_), zx::sec(0));
}

void SimFirmware::convert_chanspec_to_channel(uint16_t chanspec, wlan_channel_t* channel) {
  chanspec_to_channel(&d11_inf_, chanspec, channel);
}
uint16_t SimFirmware::convert_channel_to_chanspec(wlan_channel_t* channel) {
  return channel_to_chanspec(&d11_inf_, channel);
}

}  // namespace wlan::brcmfmac
