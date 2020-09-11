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
#include "src/connectivity/wlan/drivers/testing/lib/sim-env/sim-sta-ifc.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/bcdc.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/bits.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/brcmu_d11.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/brcmu_wifi.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/cfg80211.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/core.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/fwil_types.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/sim/sim_errinj.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/sim/sim_hw.h"

namespace wlan::brcmfmac {

// The amount of time we will wait for an association response after an association request
constexpr zx::duration kAssocTimeout = zx::sec(1);
// The amount of time we will wait for an authentication response after an authentication request
constexpr zx::duration kAuthTimeout = zx::sec(1);
// The amount of time we will wait for a beacon from an associated device before disassociating
// Timing based off broadcom firmware black box testing
constexpr zx::duration kBeaconTimeout = zx::sec(5);
// Delay between receiving start AP request and sending E_LINK event
constexpr zx::duration kStartAPConfDelay = zx::msec(10);
// Delay before sending ASSOC event after client association
constexpr zx::duration kAssocEventDelay = zx::msec(10);
// Delay between events E_LINK and E_SSID.
constexpr zx::duration kSsidEventDelay = zx::msec(100);
// Delay in sending E_LINK event during assoc & disassoc.
constexpr zx::duration kLinkEventDelay = zx::msec(1);
// Delay in sending E_DISASSOC event during disassoc.
constexpr zx::duration kDisassocEventDelay = zx::msec(1);

// Size allocated to hold association frame IEs in SIM FW
#define ASSOC_IES_MAX_LEN 1000

class SimFirmware {
  class BcdcResponse {
   public:
    void Clear();

    // Copy data into buffer. This function will return ZX_ERR_INTERNAL
    // if len_ exceeds INT_MAX.
    zx_status_t Get(uint8_t* data, size_t len, int* rxlen_out);

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
    common::MacAddr bssid;
    wlan_ssid_t ssid;
    wlan_bss_type_t bss_type;
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
    bool is_beacon_watchdog_active = false;
    uint64_t beacon_watchdog_id_;
  };

  struct AuthState {
    enum {
      NOT_AUTHENTICATED,
      EXPECTING_SECOND,
      EXPECTING_FOURTH,
      AUTHENTICATED
    } state = NOT_AUTHENTICATED;

    uint64_t auth_timer_id;
    enum simulation::SimSecProtoType sec_type = simulation::SEC_PROTO_TYPE_OPEN;
    common::MacAddr bssid;
  };

  struct ChannelSwitchState {
    enum {
      HOME,
      // SWITCHING means the event for conducting channel switch has already been set.
      SWITCHING
    } state = HOME;

    uint8_t new_channel;
    uint64_t switch_timer_id;
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

  SimErrorInjector err_inj_;
  simulation::StationIfc* GetHardwareIfc();
  void GetChipInfo(uint32_t* chip, uint32_t* chiprev);
  int32_t GetPM();
  // Num of clients currently associated with the SoftAP IF
  uint16_t GetNumClients(uint16_t ifidx);

  // Firmware iovar accessors
  zx_status_t IovarsSet(uint16_t ifidx, const char* name, const void* value, size_t value_len);
  zx_status_t IovarsGet(uint16_t ifidx, const char* name, void* value_out, size_t value_len,
                        bcme_status_t* fw_err);

  // Firmware error injection related methods
  void ErrorInjectSetBit(size_t inject_bit);
  void ErrorInjectClearBit(size_t inject_bit);
  void ErrorInjectAllClear();

  // channel-chanspec helper functions
  void convert_chanspec_to_channel(uint16_t chanspec, wlan_channel_t* ch);
  uint16_t convert_channel_to_chanspec(wlan_channel_t* channel);

  // Bus operations: calls from driver
  zx_status_t BusPreinit();
  void BusStop();
  zx_status_t BusTxCtl(unsigned char* msg, uint len);
  zx_status_t BusTxData(struct brcmf_netbuf* netbuf);
  zx_status_t BusRxCtl(unsigned char* msg, uint len, int* rxlen_out);
  struct pktq* BusGetTxQueue();
  zx_status_t BusGetBootloaderMacAddr(uint8_t* mac_addr);
  void BusSetTimer(std::unique_ptr<std::function<void()>> fn, zx_duration_t delay,
                   uint64_t* id_out);
  void BusCancelTimer(uint64_t id);
  // This function returns the wsec_key_list for an iface to outside.
  std::vector<brcmf_wsec_key_le> GetKeyList(uint16_t ifidx);

 private:
  struct Client {
    // When we receive an authentication request of a client, if it's a reasonable request, we will
    // directly create the client with AUTHENTICATED state, and if it's not a reasonable request, we
    // will not record anything for this client, so the first state of a client is AUTHENTICATED, no
    // INIT or HOME state needed.
    enum State { AUTHENTICATED, ASSOCIATED };

    Client(common::MacAddr mac_addr, State state) : mac_addr(mac_addr), state(state) {}
    common::MacAddr mac_addr;
    State state;
  };
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
    std::list<std::shared_ptr<Client>> clients;
  };

  /* This structure contains the variables related to an iface entry in SIM FW.
   * mac_addr - Mac address of the interface
   * mac_addr_set - Flag indicating if the mac address is set
   * chanspec - The operating channel of this interface
   * bsscfgidx - input from the driver indicating the bss index
   * allocated - maintained by SIM FW to indicate entry is allocated
   * iface_id - the iface id allocated by SIM FW - in this case always the array index of the table
   * cur_key_idx - The index indicates which key we are using for this iface
   * wsec_key_list - Storing keys for this iface
   * ap_mode - is the iface in SoftAP(true) or Client(false) mode
   * ap_config - SoftAP specific config (set when interface is configured as SoftAP)
   * arpoe - is ARP offloading enabled on this interface
   * arp_ol - in what mode is ARP offloading operating (see BRCMF_ARP_OL_* flags)
   */

  typedef struct sim_iface_entry {
    common::MacAddr mac_addr;
    bool mac_addr_set;
    uint16_t chanspec;
    int32_t bsscfgidx;
    bool allocated;
    int8_t iface_id;
    uint32_t wsec = 0;
    uint32_t cur_key_idx = 0;
    std::vector<brcmf_wsec_key_le> wsec_key_list;
    uint32_t wpa_auth = 0;
    uint32_t tlv = 0;
    bool ap_mode;
    ApConfig ap_config;
    uint32_t arpoe = 0;
    uint32_t arp_ol = 0;
    uint16_t auth_type = BRCMF_AUTH_MODE_OPEN;
    uint32_t allmulti = 0;
  } sim_iface_entry_t;

  // This value is specific to firmware, and drives some behavior (notably the interpretation
  // of the chanspec encoding).
  static constexpr uint32_t kIoType = BRCMU_D11AC_IOTYPE;

  // Default interface identification string
  static constexpr const char* kDefaultIfcName = "wl\x30";

  // Max number of interfaces supported
  static constexpr uint8_t kMaxIfSupported = 4;

  // BCDC interface
  std::unique_ptr<std::vector<uint8_t>> CreateBcdcBuffer(int16_t ifidx, size_t requested_size,
                                                         size_t data_offset, size_t* offset_out);
  zx_status_t BcdcVarOp(uint16_t ifidx, brcmf_proto_bcdc_dcmd* msg, uint8_t* data, size_t len,
                        bool is_set);

  // Iovar handlers
  zx_status_t SetMacAddr(uint16_t ifidx, const uint8_t* mac_addr);
  common::MacAddr GetMacAddr(uint16_t ifidx);
  zx_status_t HandleEscanRequest(const brcmf_escan_params_le* value, size_t value_len);
  zx_status_t HandleIfaceTblReq(const bool add_entry, const void* data, uint8_t* iface_id,
                                int32_t bsscfgidx);
  zx_status_t HandleIfaceRequest(const bool add_iface, const void* data, const size_t len,
                                 int32_t bsscfgidx);
  zx_status_t HandleJoinRequest(const void* value, size_t value_len);
  void HandleAssocReq(std::shared_ptr<const simulation::SimAssocReqFrame> frame);
  void HandleDisconnectForClientIF(std::shared_ptr<const simulation::SimManagementFrame> frame,
                                   const common::MacAddr& bssid, uint32_t reason);
  void HandleAuthReq(std::shared_ptr<const simulation::SimAuthFrame> frame);
  void HandleAuthResp(std::shared_ptr<const simulation::SimAuthFrame> frame);
  // Generic scan operations
  zx_status_t ScanStart(std::unique_ptr<ScanOpts> opts);
  void ScanNextChannel();

  // Escan operations
  zx_status_t EscanStart(uint16_t sync_id, const brcmf_scan_params_le* params, size_t params_len);
  void EscanResultSeen(const ScanResult& scan_result);
  void EscanComplete();

  // Association operations
  void AssocInit(std::unique_ptr<AssocOpts> assoc_opts, wlan_channel_t& channel);
  void AssocScanResultSeen(const ScanResult& scan_result);
  void AssocScanDone();
  void AuthStart();  // Scan complete, start authentication process
  void AssocStart();
  void AssocClearContext();
  void AuthClearContext();
  void AssocHandleFailure();
  void AuthHandleFailure();
  void DisassocStart(brcmf_scb_val_le* scb_val);
  void DisassocLocalClient(uint32_t reason);
  void SetStateToDisassociated(uint32_t reason, bool locally_initiated);
  void RestartBeaconWatchdog();
  void DisableBeaconWatchdog();
  void HandleBeaconTimeout();

  // Handlers for events from hardware
  void Rx(std::shared_ptr<const simulation::SimFrame> frame,
          std::shared_ptr<const simulation::WlanRxInfo> info);

  // Perform in-firmware ARP processing, if enabled. Returns a value indicating if the frame was
  // completely offloaded (and doesn't need to be passed to the driver).
  bool OffloadArpFrame(int16_t ifidx, std::shared_ptr<const simulation::SimDataFrame> data_frame);

  void RxMgmtFrame(std::shared_ptr<const simulation::SimManagementFrame> mgmt_frame,
                   std::shared_ptr<const simulation::WlanRxInfo> info);
  void RxDataFrame(std::shared_ptr<const simulation::SimDataFrame> data_frame,
                   std::shared_ptr<const simulation::WlanRxInfo> info);
  void RxBeacon(const wlan_channel_t& channel,
                std::shared_ptr<const simulation::SimBeaconFrame> frame);
  void RxAssocResp(std::shared_ptr<const simulation::SimAssocRespFrame> frame);
  void RxDisassocReq(std::shared_ptr<const simulation::SimDisassocReqFrame> frame);
  void RxAssocReq(std::shared_ptr<const simulation::SimAssocReqFrame> frame);
  void RxProbeResp(const wlan_channel_t& channel,
                   std::shared_ptr<const simulation::SimProbeRespFrame> frame,
                   double signal_strength);
  void RxAuthFrame(std::shared_ptr<const simulation::SimAuthFrame> frame);

  // Handler for channel switch.
  void ConductChannelSwitch(const wlan_channel_t& dst_channel, uint8_t mode);
  void RxDeauthReq(std::shared_ptr<const simulation::SimDeauthFrame> frame);

  void StopSoftAP(uint16_t ifidx);
  // Allocate a buffer for an event (brcmf_event)
  std::shared_ptr<std::vector<uint8_t>> CreateEventBuffer(size_t requested_size,
                                                          brcmf_event_msg_be** msg_be,
                                                          size_t* offset_out);

  // Wrap the buffer in an event and send back to the driver over the bus
  void SendEventToDriver(size_t payload_size, std::shared_ptr<std::vector<uint8_t>> buffer_in,
                         uint32_t event_type, uint32_t status, uint16_t ifidx,
                         char* ifname = nullptr, uint16_t flags = 0, uint32_t reason = 0,
                         std::optional<common::MacAddr> addr = {},
                         std::optional<zx::duration> delay = {});

  // Send received frame over the bus to the driver
  void SendFrameToDriver(uint16_t ifidx, size_t payload_size, const std::vector<uint8_t>& buffer_in,
                         std::shared_ptr<const simulation::WlanRxInfo> info);

  // Get the idx of the SoftAP IF based on Mac address
  int16_t GetIfidxByMac(const common::MacAddr& addr);

  // Get the channel of IF the parameter indicates whether we need to find softAP ifidx or client
  // ifidx.
  wlan_channel_t GetIfChannel(bool is_ap);

  // Get IF idx of matching bsscfgidx.
  int16_t GetIfidxByBsscfgidx(int32_t bsscfgidx);

  zx_status_t SetIFChanspec(uint16_t ifidx, uint16_t chanspec);
  // motivation_auth means whether this function is triggered by receiving disauth frame, the
  // value "true" means it is triggered by a deauth frame, and "false" means ut's triggered by a
  // disassoc frame.
  bool FindAndRemoveClient(const common::MacAddr client_mac, bool motivation_deauth,
                           uint16_t deauth_reason = 0);
  std::shared_ptr<Client> FindClient(const common::MacAddr client_mac);
  void ScheduleLinkEvent(zx::duration when, uint16_t ifidx);
  void SendAPStartLinkEvent(uint16_t ifidx);
  zx_status_t StopInterface(const int32_t bsscfgidx);

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

  // The two ifidxs we support for now, the client ifidx is always 0(according to real firmware),
  // and softap_ifidx is not fixed. here if the value of it is std::optnull, that means the softap
  // iface has not been allocated or has not been started yet.
  const uint16_t kClientIfidx = 0;
  std::optional<uint16_t> softap_ifidx_;

  // States for client iface
  ScanState scan_state_;
  AssocState assoc_state_;
  AuthState auth_state_;
  ChannelSwitchState channel_switch_state_;

  // Internal firmware state variables
  std::array<uint8_t, ETH_ALEN> mac_addr_;
  bool mac_addr_set_ = false;
  common::MacAddr pfn_mac_addr_;
  bool default_passive_scan_ = true;
  uint32_t default_passive_time_ = -1;  // In ms. -1 indicates value has not been set.
  int32_t power_mode_ = -1;             // -1 indicates value has not been set.
  struct brcmf_fil_country_le country_code_;
  uint32_t assoc_max_retries_ = 0;
  bool dev_is_up_ = false;
  uint32_t mpc_ = 1;  // Read FW appears to be setting this to 1 by default.
  zx::duration beacon_timeout_ = kBeaconTimeout;
  std::atomic<unsigned long> error_inject_bits_ = 0;
  uint8_t assoc_resp_ies_[ASSOC_IES_MAX_LEN];
  size_t assoc_resp_ies_len_ = 0;
};

}  // namespace wlan::brcmfmac

#endif  // SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_BROADCOM_BRCMFMAC_SIM_SIM_FW_H_
