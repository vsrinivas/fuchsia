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

#include <optional>
#include <string>
#include <vector>

#include "src/connectivity/wlan/drivers/testing/lib/sim-env/sim-env.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/bcdc.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/brcmu_d11.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/brcmu_wifi.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/core.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/fwil_types.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/sim/sim_hw.h"

namespace wlan::brcmfmac {

// The amount of time we will wait for an association response after an association request
constexpr zx::duration kAssocTimeout = zx::sec(1);
// The amount of time we will wait for an authentication response after an authentication request
constexpr zx::duration kAuthTimeout = zx::sec(1);

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

  struct ScanResult {
    wlan_channel_t channel;
    wlan_ssid_t ssid;
    common::MacAddr bssid;
    wlan::CapabilityInfo bss_capability;
    int8_t rssi_dbm;
  };

  using ScanResultHandler = std::function<void(const ScanResult& scan_result)>;
  using ScanDoneHandler = std::function<void()>;

  struct ScanOpts {
    // Unique scan identifier
    uint16_t sync_id;

    bool is_active;

    // Optional filters
    std::optional<wlan_ssid_t> ssid;
    std::optional<common::MacAddr> bssid;

    // Time per channel
    zx::duration dwell_time;

    // When a scan is in progress, the total number of channels being scanned
    std::vector<uint16_t> channels;

    // Function to call when we receive a beacon while scanning
    ScanResultHandler on_result_fn;

    // Function to call when we have finished scanning
    ScanDoneHandler on_done_fn;
  };

  struct AssocOpts {
    wlan_channel_t channel;
    common::MacAddr bssid;
  };

 public:
  struct ScanState {
    // HOME means listening to home channel between scan channels
    enum { STOPPED, SCANNING, HOME } state = STOPPED;
    std::unique_ptr<ScanOpts> opts;
    // Next channel to scan (from channels)
    size_t channel_index;
    // The interface idx on which the scan is being done
    uint16_t ifidx;
  };

  struct AssocState {
    enum {
      NOT_ASSOCIATED,
      SCANNING,
      ASSOCIATING,
      ASSOCIATED,
    } state = NOT_ASSOCIATED;

    std::unique_ptr<AssocOpts> opts;
    // Results seen during pre-assoc scan
    std::list<ScanResult> scan_results;
    // Unique id of timer event used to timeout an association request
    uint64_t assoc_timer_id;
    // Association attempt number
    uint8_t num_attempts;
    // The interface idx on which the assoc is being done
    uint16_t ifidx;
  };

  struct AuthState {
    enum {
      NOT_AUTHENTICATED,
      EXPECTING_SECOND,
      EXPECTING_FOURTH,
      AUTHENTICATED
    } state = NOT_AUTHENTICATED;

    uint16_t auth_type;

    uint64_t auth_timer_id;
    wlan::CapabilityInfo bss_capability;
  };

  struct PacketBuf {
    std::unique_ptr<uint8_t[]> data;
    uint32_t len;
    // this is just to remember the coming netbuf's allocated_size, this, if the usage of
    // brmcf_netbuf is removed, this can be removed as well.
    uint32_t allocated_size_of_buf_in;
  };

  SimFirmware() = delete;
  explicit SimFirmware(brcmf_simdev* simdev, simulation::Environment* env);
  ~SimFirmware();

  void GetChipInfo(uint32_t* chip, uint32_t* chiprev);
  int32_t GetPM();
  // Num of clients currently associated with the SoftAP IF
  uint16_t GetNumClients(uint16_t ifidx);

  // Firmware iovar accessors
  zx_status_t IovarsSet(uint16_t ifidx, const char* name, const void* value, size_t value_len);
  zx_status_t IovarsGet(uint16_t ifidx, const char* name, void* value_out, size_t value_len);
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

  // Need to be verified by sim_test.
  struct PacketBuf last_pkt_buf_;

 private:
  /* SoftAP specific config
   * infra_mode - If AP is operating in Infra mode
   * beacon_period - Beacon period
   * dtim_period - DTIM period
   * ssid - ssid - non-zero length indicates AP start else AP stop
   * ap_started - indicates if ap has been started or stopped
   * clients - List of associated clients (mac address)
   */
  struct ApConfig {
    uint32_t infra_mode;
    uint32_t beacon_period;
    uint32_t dtim_period;
    brcmf_ssid_le ssid;
    bool ap_started;
    std::list<common::MacAddr> clients;
  };

  /* This structure contains the variables related to an iface entry in SIM FW.
   * mac_addr - Mac address of the interface
   * mac_addr_set - Flag indicating if the mac address is set
   * chanspec - The operating channel of this interface
   * bsscfgidx - input from the driver indicating the bss index
   * allocated - maintained by SIM FW to indicate entry is allocated
   * iface_id - the iface id allocated by SIM FW - in this case always the array index of the table
   * ap_mode - is the iface in SoftAP(true) or Client(false) mode
   * ap_config - SoftAP specific config (set when interface is configured as SoftAP)
   */

  typedef struct sim_iface_entry {
    common::MacAddr mac_addr;
    bool mac_addr_set;
    uint16_t chanspec;
    int32_t bsscfgidx;
    bool allocated;
    int8_t iface_id;
    bool ap_mode;
    ApConfig ap_config;
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
  zx_status_t BcdcVarOp(uint16_t ifidx, brcmf_proto_bcdc_dcmd* msg, uint8_t* data, size_t len,
                        bool is_set);

  // Iovar handlers
  zx_status_t SetMacAddr(uint16_t ifidx, const uint8_t* mac_addr);
  zx_status_t HandleEscanRequest(const brcmf_escan_params_le* value, size_t value_len,
                                 const uint16_t ifidx);
  zx_status_t HandleIfaceTblReq(const bool add_entry, const void* data, uint8_t* iface_id);
  zx_status_t HandleIfaceRequest(const bool add_iface, const void* data, const size_t len);
  zx_status_t HandleJoinRequest(const void* value, size_t value_len, const uint16_t ifidx);
  zx_status_t HandleAssocReq(uint16_t ifidx, const common::MacAddr& src_mac);
  void HandleDisassocForClientIF(const common::MacAddr& src, const common::MacAddr& dst,
                                 const uint16_t reason);

  // Generic scan operations
  zx_status_t ScanStart(std::unique_ptr<ScanOpts> opts, const uint16_t ifidx);
  void ScanNextChannel();

  // Escan operations
  zx_status_t EscanStart(uint16_t sync_id, const brcmf_scan_params_le* params, size_t params_len,
                         const uint16_t ifidx);
  void EscanResultSeen(const ScanResult& scan_result);
  void EscanComplete();

  // Association operations
  void AssocScanResultSeen(const ScanResult& scan_result);
  void AssocScanDone();
  void AuthStart();  // Scan complete, start authentication process
  void AssocStart();
  void AssocClearContext();
  void AssocHandleFailure();
  void AuthHandleFailure();
  void DisassocStart(brcmf_scb_val_le* scb_val);
  void DisassocLocalClient(brcmf_scb_val_le* scb_val);

  // Handlers for events from hardware
  void Rx(const simulation::SimFrame* frame, simulation::WlanRxInfo& info);

  void RxMgmtFrame(const simulation::SimManagementFrame* mgmt_frame, simulation::WlanRxInfo& info);

  void RxBeacon(const wlan_channel_t& channel, const simulation::SimBeaconFrame* frame);
  void RxAssocResp(const simulation::SimAssocRespFrame* frame);
  void RxDisassocReq(const simulation::SimDisassocReqFrame* frame);
  void RxAssocReq(const simulation::SimAssocReqFrame* frame);
  void RxProbeResp(const wlan_channel_t& channel, const simulation::SimProbeRespFrame* frame,
                   double signal_strength);
  void RxAuthResp(const simulation::SimAuthFrame* frame);

  // Allocate a buffer for an event (brcmf_event)
  std::unique_ptr<std::vector<uint8_t>> CreateEventBuffer(size_t requested_size,
                                                          brcmf_event_msg_be** msg_be,
                                                          size_t* offset_out);

  // Wrap the buffer in an event and send back to the driver over the bus
  void SendEventToDriver(std::unique_ptr<std::vector<uint8_t>> buffer);

  // Send an event without a payload back to the driver
  void SendSimpleEventToDriver(uint32_t event_type, uint32_t status, uint16_t ifidx,
                               uint16_t flags = 0, uint32_t reason = 0,
                               std::optional<common::MacAddr> addr = {});

  // Get the idx of the SoftAP IF based on Mac address
  int16_t GetSoftAPIfidx(const common::MacAddr& addr);

  // Get the idx of the SoftAP IF
  int16_t GetSoftAPIfidx();

  zx_status_t SetIFChanspec(uint16_t ifidx, uint16_t chanspec);
  // This is the simulator object that represents the interface between the driver and the
  // firmware. We will use it to send back events.
  brcmf_simdev* simdev_;

  // Context for encoding/decoding chanspecs
  brcmu_d11inf d11_inf_;

  // Next message to pass back to a BCDC Rx Ctl request
  BcdcResponse bcdc_response_;

  // Simulated hardware state
  SimHardware hw_;

  // Interface table made up of IF entries. Each entry is analogous to an IF
  // created in the driver (see the comments above for the contents of each
  // entry). Interface specific config/parameters are stored in this table
  sim_iface_entry_t iface_tbl_[kMaxIfSupported] = {};

  // Internal firmware state variables
  std::array<uint8_t, ETH_ALEN> mac_addr_;
  bool mac_addr_set_ = false;
  common::MacAddr pfn_mac_addr_;
  ScanState scan_state_;
  AssocState assoc_state_;
  AuthState auth_state_;
  bool default_passive_scan_ = true;
  uint32_t default_passive_time_ = -1;  // In ms. -1 indicates value has not been set.
  int32_t power_mode_ = -1;             // -1 indicates value has not been set.
  struct brcmf_fil_country_le country_code_;
  uint32_t assoc_max_retries_ = 0;
  bool dev_is_up_ = false;
  uint32_t mpc_ = 1;  // Read FW appears to be setting this to 1 by default.
  uint32_t wsec_ = 0;
  struct brcmf_wsec_key_le wsec_key_;
  uint32_t wpa_auth_ = 0;
};

}  // namespace wlan::brcmfmac

#endif  // SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_BROADCOM_BRCMFMAC_SIM_SIM_FW_H_
