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

#ifndef SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_BROADCOM_BRCMFMAC_SIM_SIM_FW_H_
#define SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_BROADCOM_BRCMFMAC_SIM_SIM_FW_H_

#include <stdint.h>
#include <sys/types.h>
#include <zircon/types.h>

#include <string>
#include <vector>

#include "src/connectivity/wlan/drivers/testing/lib/sim-env/sim-env.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/bcdc.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/brcmu_d11.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/core.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/sim/sim_hw.h"

namespace wlan::brcmfmac {

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

  struct ScanState {
    // HOME means listening to home channel between scan channels
    enum { STOPPED, SCANNING, HOME } state = STOPPED;

    // Unique scan identifier
    uint16_t sync_id;

    // Time per channel
    zx::duration dwell_time;

    // When a scan is in progress, the total number of channels being scanned
    std::unique_ptr<std::vector<uint16_t>> channels;

    // Next channel to scan (from channels)
    size_t channel_index;
  };

 public:
  SimFirmware() = delete;
  explicit SimFirmware(brcmf_simdev* simdev, simulation::Environment* env);

  void GetChipInfo(uint32_t* chip, uint32_t* chiprev);
  int32_t GetPM();

  // Firmware iovar accessors
  zx_status_t IovarsSet(const char* name, const void* value, size_t value_len);
  zx_status_t IovarsGet(const char* name, void* value_out, size_t value_len);
  zx_status_t HandleBssCfgSet(const char* name, const void* data, size_t value_len);

  // Bus operations: calls from driver
  zx_status_t BusPreinit();
  void BusStop();
  zx_status_t BusTxData(struct brcmf_netbuf* netbuf);
  zx_status_t BusTxCtl(unsigned char* msg, uint len);
  zx_status_t BusRxCtl(unsigned char* msg, uint len, int* rxlen_out);
  struct pktq* BusGetTxQueue();
  void BusWowlConfig(bool enabled);
  size_t BusGetRamsize();
  zx_status_t BusGetMemdump(void* data, size_t len);
  zx_status_t BusGetFwName(uint chip, uint chiprev, unsigned char* fw_name, size_t* fw_name_size);
  zx_status_t BusGetBootloaderMacAddr(uint8_t* mac_addr);

 private:
  /* This structure contains the variables related to an iface entry in SIM FW.
   * ssid - input from the driver indicating the ssid of the interface
   * ssid_len - length of the ssid field (excluding the null)
   * bsscfgidx - input from the driver indicating the bss index
   * allocated - maintained by SIM FW to indicate entry is allocated
   * iface_id - the iface id allocated by SIM FW - in this case always the array index of the table
   */
  typedef struct sim_iface_entry {
    char ssid[IEEE80211_SSID_LEN_MAX];
    uint8_t ssid_len;
    int32_t bsscfgidx;
    bool allocated;
    int8_t iface_id;
  } sim_iface_entry_t;

  // This value is specific to firmware, and drives some behavior (notably the interpretation
  // of the chanspec encoding).
  static constexpr uint32_t kIoType = BRCMU_D11AC_IOTYPE;

  // Default interface identification string
  static constexpr const char* kDefaultIfcName = "wl\x30";

  // Max number of interfaces supported
  static constexpr uint8_t kMaxIfSupported = 4;

  // BCDC interface
  std::unique_ptr<std::vector<uint8_t>> CreateBcdcBuffer(size_t requested_size, size_t* offset_out);
  zx_status_t BcdcVarOp(brcmf_proto_bcdc_dcmd* msg, uint8_t* data, size_t len, bool is_set);

  // Iovar handlers
  zx_status_t SetMacAddr(const uint8_t* mac_addr);
  zx_status_t HandleEscanRequest(const brcmf_escan_params_le* value, size_t value_len);
  zx_status_t HandleIfaceTblReq(const bool add_entry, const void* data, uint8_t* iface_id);
  zx_status_t HandleIfaceRequest(const bool add_iface, const void* data, const size_t len);

  // Escan operations
  zx_status_t EscanStart(uint16_t sync_id, const brcmf_scan_params_le* params, size_t params_len);
  void EscanNextChannel();
  void EscanComplete();

  // Handlers for events from hardware
  void RxBeacon(const wlan_channel_t& channel, const wlan_ssid_t& ssid,
                const common::MacAddr& bssid);

  // Allocate a buffer for an event (brcmf_event)
  std::unique_ptr<std::vector<uint8_t>> CreateEventBuffer(size_t requested_size,
                                                          brcmf_event_msg_be** msg_be,
                                                          size_t* offset_out);

  // Wrap the buffer in an event and send back to the driver over the bus
  void SendEventToDriver(std::unique_ptr<std::vector<uint8_t>> buffer);

  // This is the simulator object that represents the interface between the driver and the
  // firmware. We will use it to send back events.
  brcmf_simdev* simdev_;

  // Context for encoding/decoding chanspecs
  brcmu_d11inf d11_inf_;

  // Next message to pass back to a BCDC Rx Ctl request
  BcdcResponse bcdc_response_;

  // Simulated hardware state
  SimHardware hw_;

  // Internal firmware state variables
  std::array<uint8_t, ETH_ALEN> mac_addr_;
  ScanState scan_state_;
  uint32_t default_passive_time_ = -1;  // In ms. -1 indicates value has not been set.
  int32_t power_mode_ = -1;             // -1 indicates value has not been set.
  sim_iface_entry_t iface_tbl_[kMaxIfSupported];
  struct brcmf_fil_country_le country_code_;
};

}  // namespace wlan::brcmfmac

#endif  // SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_BROADCOM_BRCMFMAC_SIM_SIM_FW_H_
