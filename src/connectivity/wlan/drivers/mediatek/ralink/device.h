// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_WLAN_DRIVERS_MEDIATEK_RALINK_DEVICE_H_
#define SRC_CONNECTIVITY_WLAN_DRIVERS_MEDIATEK_RALINK_DEVICE_H_

#include <fuchsia/wlan/device/cpp/fidl.h>
#include <lib/fit/function.h>
#include <lib/zx/time.h>
#include <zircon/compiler.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>
#include <unordered_map>
#include <vector>

#include <bitmap/raw-bitmap.h>
#include <bitmap/storage.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/hw/wlan/wlaninfo.h>
#include <ddk/protocol/usb.h>
#include <ddk/protocol/wlanphyimpl.h>
#include <ddktl/protocol/wlan/mac.h>
#include <usb/usb.h>
#include <wlan/common/macaddr.h>
#include <wlan/protocol/mac.h>

namespace ralink {

// WCID 255 is reserved for no/unknown peer address.
static constexpr size_t kMaxValidWcid = 254;
// WCID = 255 for addresses which are not known to the hardware.
constexpr uint8_t kWcidUnknown = 255;

template <uint16_t A>
class Register;
template <uint8_t A>
class BbpRegister;
template <uint8_t A>
class RfcsrRegister;
template <uint16_t A>
class EepromField;
enum KeyMode : uint8_t;
enum KeyType : uint8_t;
struct BulkoutAggregation;
class TxStatFifo;
class TxStatFifoExt;

class Device {
 public:
  Device(zx_device_t* device, usb_protocol_t usb, uint8_t bulk_in, std::vector<uint8_t>&& bulk_out,
         size_t parent_req_size);
  ~Device();

  zx_status_t Bind();

  // ddk device methods
  void PhyUnbind();
  void PhyRelease();
  void MacUnbind();
  void MacRelease();

  // ddk wlanphy_impl_protocol_ops
  zx_status_t Query(wlanphy_impl_info_t* info);
  zx_status_t CreateIface(const wlanphy_impl_create_iface_req_t* req, uint16_t* out_iface_id);
  zx_status_t DestroyIface(uint16_t id);
  zx_status_t SetCountry(const wlanphy_country_t* country);

  // ddk wlanmac_protocol_ops methods
  zx_status_t WlanmacQuery(uint32_t options, wlanmac_info_t* info);
  zx_status_t WlanmacStart(const wlanmac_ifc_protocol_t* ifc, zx_handle_t* out_sme_channel);
  void WlanmacStop();
  zx_status_t WlanmacQueueTx(uint32_t options, wlan_tx_packet_t* pkt);
  zx_status_t WlanmacSetChannel(uint32_t options, const wlan_channel_t* chan);
  zx_status_t WlanmacConfigureBss(uint32_t options, const wlan_bss_config_t* config);
  zx_status_t WlanmacEnableBeaconing(uint32_t options, bool enabled);
  zx_status_t WlanmacConfigureBeacon(uint32_t options, const wlan_tx_packet_t* pkt);
  zx_status_t WlanmacSetKey(uint32_t options, const wlan_key_config_t* key_config);
  zx_status_t WlanmacClearAssoc(uint32_t options, const uint8_t* bssid, size_t bssid_len);

  zx_status_t Query(wlan_info_t* info);

 private:
  struct TxCalibrationValues {
    uint8_t gain_cal_tx0 = 0;
    uint8_t phase_cal_tx0 = 0;
    uint8_t gain_cal_tx1 = 0;
    uint8_t phase_cal_tx1 = 0;
  };

  // RF register values defined per channel number
  struct RfVal {
    RfVal() : channel(0), N(0), R(0), K(0), mod(0) {}

    RfVal(int channel, uint32_t N, uint32_t R, uint32_t K)
        : channel(channel), N(N), R(R), K(K), mod(0) {}

    RfVal(int channel, uint32_t N, uint32_t R, uint32_t K, uint32_t mod)
        : channel(channel), N(N), R(R), K(K), mod(mod) {}

    int channel;
    uint32_t N;
    uint32_t R;
    uint32_t K;
    uint32_t mod;

    TxCalibrationValues cal_values;

    int8_t default_power1 = 0;
    int8_t default_power2 = 0;
    int8_t default_power3 = 0;
  };

  struct RegInitValue {
    RegInitValue(uint8_t addr, uint8_t val) : addr(addr), val(val) {}
    uint8_t addr;
    uint8_t val;
  };

  // Configure RfVal tables
  zx_status_t InitializeRfVal();

  zx_status_t AddPhyDevice();
  zx_status_t AddMacDevice();

  // read and write general registers
  zx_status_t ReadRegister(uint16_t offset, uint32_t* value);
  template <uint16_t A>
  zx_status_t ReadRegister(Register<A>* reg);
  zx_status_t WriteRegister(uint16_t offset, uint32_t value);
  template <uint16_t A>
  zx_status_t WriteRegister(const Register<A>& reg);

  // read and write the eeprom
  zx_status_t ReadEeprom();
  zx_status_t ReadEepromField(uint16_t addr, uint16_t* value);
  zx_status_t ReadEepromByte(uint16_t addr, uint8_t* value);
  template <uint16_t A>
  zx_status_t ReadEepromField(EepromField<A>* field);
  template <uint16_t A>
  zx_status_t WriteEepromField(const EepromField<A>& field);
  zx_status_t ValidateEeprom();

  // read and write baseband processor registers
  zx_status_t ReadBbp(uint8_t addr, uint8_t* val);
  template <uint8_t A>
  zx_status_t ReadBbp(BbpRegister<A>* reg);
  zx_status_t WriteBbp(uint8_t addr, uint8_t val);
  template <uint8_t A>
  zx_status_t WriteBbp(const BbpRegister<A>& reg);
  zx_status_t WriteBbpGroup(const std::vector<RegInitValue>& regs);
  zx_status_t WaitForBbp();

  // write glrt registers
  zx_status_t WriteGlrt(uint8_t addr, uint8_t val);
  zx_status_t WriteGlrtGroup(const std::vector<RegInitValue>& regs);
  zx_status_t WriteGlrtBlock(uint8_t values[], size_t size, size_t offset);

  // read and write rf registers
  zx_status_t ReadRfcsr(uint8_t addr, uint8_t* val);
  template <uint8_t A>
  zx_status_t ReadRfcsr(RfcsrRegister<A>* reg);
  zx_status_t WriteRfcsr(uint8_t addr, uint8_t val);
  template <uint8_t A>
  zx_status_t WriteRfcsr(const RfcsrRegister<A>& reg);
  zx_status_t WriteRfcsrGroup(const std::vector<RegInitValue>& regs);

  // send a command to the MCU
  zx_status_t McuCommand(uint8_t command, uint8_t token, uint8_t arg0, uint8_t arg1);

  // hardware encryption
  uint8_t DeriveSharedKeyIndex(uint8_t bss_idx, uint8_t key_idx);
  KeyMode MapIeeeCipherSuiteToKeyMode(const uint8_t cipher_oui[3], uint8_t cipher_type);
  zx_status_t WriteKey(const uint8_t key[], size_t key_len, uint16_t offset, KeyMode mode);
  zx_status_t WritePairwiseKey(uint8_t wcid, const uint8_t key[], size_t key_len, KeyMode mode);
  zx_status_t WriteSharedKey(uint8_t skey, const uint8_t key[], size_t key_len, KeyMode mode);
  zx_status_t WriteSharedKeyMode(uint8_t skey, KeyMode mode);
  zx_status_t ResetIvEiv(uint8_t wcid, uint8_t key_id, KeyMode mode);
  zx_status_t WriteWcid(uint8_t wcid, const uint8_t mac[]);
  zx_status_t WriteWcidAttribute(uint8_t bss_idx, uint8_t wcid, KeyMode mode, KeyType type);
  // resets all security aspects for a given WCID and shared key as well as their keys.
  zx_status_t ResetWcid(uint8_t wcid, uint8_t skey, uint8_t key_type);
  // Returns the WCID associated with the given address or none, if the address is not yet
  // registered.
  std::optional<uint8_t> GetWcid(const wlan::common::MacAddr& addr);
  // Returns ZX_OK if the peer was added or was already added.
  zx_status_t AddPeer(const wlan::common::MacAddr& addr, uint8_t* out_wcid);
  // Returns ZX_OK if the peer was removed.
  // Returns ZX_ERR_NOT_FOUND if the peer was not found.
  // Returns other error code otherwise.
  zx_status_t RemovePeer(const wlan::common::MacAddr& addr);

  // interrupt routines
  zx_status_t OnTxReportInterruptTimer();
  zx_status_t OnTbttInterruptTimer();
  zx_status_t InterruptWorker();

  // initialization functions
  zx_status_t LoadFirmware();
  zx_status_t EnableRadio();
  zx_status_t StartInterruptPolling();
  void StopInterruptPolling();
  zx_status_t InitRegisters();
  zx_status_t InitBbp();
  zx_status_t InitBbp5592();
  zx_status_t InitBbp5390();
  zx_status_t InitRfcsr();

  zx_status_t DetectAutoRun(bool* autorun);
  zx_status_t DisableWpdma();
  zx_status_t WaitForMacCsr();
  zx_status_t SetRxFilter();
  zx_status_t AdjustFreqOffset();
  zx_status_t NormalModeSetup();
  zx_status_t StartQueues();
  zx_status_t StopRxQueue();
  zx_status_t SetupInterface();

  zx_status_t LookupRfVal(const wlan_channel_t& chan, RfVal* rf_val);
  zx_status_t ConfigureChannel(const wlan_channel_t& chan);
  zx_status_t ConfigureChannel5390(const wlan_channel_t& chan);
  zx_status_t ConfigureChannel5592(const wlan_channel_t& chan);

  uint8_t GetEirpRegUpperBound(const wlan_channel_t& chan);
  uint8_t GetPerChainTxPower(const wlan_channel_t& chan, uint8_t eirp_target);

  zx_status_t ConfigureTxPower(const wlan_channel_t& chan);

  template <typename R, typename Predicate>
  zx_status_t BusyWait(R* reg, Predicate pred, zx::duration delay = kDefaultBusyWait);

  zx_status_t SetBss(const uint8_t* bssid);

  void HandleRxComplete(usb_request_t* request);
  void HandleTxComplete(usb_request_t* request);

  struct TxStatsFifoEntry;
  TxStatsFifoEntry RemoveTxStatsFifoEntry(int packet_id) __TA_REQUIRES(lock_);
  int AddTxStatsFifoEntry(const wlan_tx_packet_t& wlan_pkt);
  zx_status_t BuildTxStatusReport(const TxStatsFifoEntry& entry, const TxStatFifo& stat_fifo,
                                  const TxStatFifoExt& stat_fifo_ext, wlan_tx_status* report)
      __TA_REQUIRES(lock_);

  zx_status_t FillAggregation(BulkoutAggregation* aggr, const wlan_tx_packet_t* wlan_pkt,
                              int packet_id, size_t aggr_payload_len);

  zx::duration RemainingTbttTime();
  zx_status_t EnableHwBcn(bool active);

  static void ReadRequestComplete(void* ctx, usb_request_t* request);
  static void WriteRequestComplete(void* ctx, usb_request_t* request);

  void DumpLengths(const wlan_tx_packet_t& wlan_pkt, BulkoutAggregation* aggr, usb_request_t* req);
  size_t GetMpduLen(const wlan_tx_packet_t& wlan_pkt);
  size_t GetTxwiLen();
  size_t GetBulkoutAggrTailLen();
  size_t GetUsbReqLen(const wlan_tx_packet_t& wlan_pkt);
  size_t GetBulkoutAggrPayloadLen(const wlan_tx_packet_t& wlan_pkt);
  uint8_t GetRxAckPolicy(const wlan_tx_packet_t& wlan_pkt);
  size_t WriteBulkout(uint8_t* dest, const wlan_tx_packet_t& wlan_pkt);
  size_t GetL2PadLen(const wlan_tx_packet_t& wlan_pkt);
  zx_device_t* parent_ = nullptr;
  zx_device_t* zxdev_ = nullptr;
  zx_device_t* wlanmac_dev_ __TA_GUARDED(lock_) = nullptr;
  usb_protocol_t usb_;
  size_t parent_req_size_ = 0;

  uint8_t rx_endpt_ = 0;
  std::vector<uint8_t> tx_endpts_;

  std::mutex lock_;
  enum { PHY_RUNNING, PHY_DESTROYING } phy_state_ __TA_GUARDED(lock_) = PHY_RUNNING;
  enum {
    IFC_NONE,
    IFC_CREATING,
    IFC_RUNNING,
    IFC_DESTROYING
  } iface_state_ __TA_GUARDED(lock_) = IFC_NONE;
  ddk::WlanmacIfcProtocolClient wlanmac_proxy_ __TA_GUARDED(lock_);

  constexpr static size_t kEepromSize = 0x0100;
  std::array<uint16_t, kEepromSize> eeprom_ = {};

  constexpr static zx::duration kDefaultBusyWait = zx::usec(100);

  // constants read out of the device
  uint16_t rt_type_ = 0;
  uint16_t rt_rev_ = 0;
  uint16_t rf_type_ = 0;

  uint8_t mac_addr_[ETH_MAC_SIZE];

  bool has_external_lna_2g_ = false;
  bool has_external_lna_5g_ = false;

  uint8_t tx_path_ = 0;
  uint8_t rx_path_ = 0;

  uint8_t antenna_diversity_ = 0;

  // key: 20MHz channel number
  // val: RF parameters for the channel
  std::map<int, RfVal> rf_vals_;

  // cfg_chan_ is what is configured (from higher layers)
  // TODO(porce): Define oper_chan_ to read from the registers directly
  wlan_channel_t cfg_chan_ = wlan_channel_t{
      .primary = 0,
      .cbw = WLAN_CHANNEL_BANDWIDTH__20,
  };
  uint16_t lna_gain_ = 0;
  uint8_t bg_rssi_offset_[3] = {};
  uint8_t bssid_[ETH_MAC_SIZE];

  std::vector<usb_request_t*> free_write_reqs_ __TA_GUARDED(lock_);
  uint16_t iface_id_ __TA_GUARDED(lock_) = 0;
  uint16_t iface_role_ __TA_GUARDED(lock_) = 0;
  zx::channel iface_sme_channel_ __TA_GUARDED(lock_) = {};

  constexpr static zx::duration kDisconnectQuiescePeriod = zx::msec(10);
  zx::time last_disconnect_time_ = {};
  uint8_t last_disconnect_bssid_[ETH_MAC_SIZE] = {0};

  // TX statistics reporting
  struct TxStatsFifoEntry {
    // Destination mac address, or addr1 in packet header.
    uint8_t peer_addr[ETH_MAC_SIZE] = {};
    // DDK index to uniquely identify a tx vector.
    tx_vec_idx_t tx_vector_idx = WLAN_TX_VECTOR_IDX_INVALID;
    // True iff this FIFO entry is in use.
    bool in_use = false;
  };
  static constexpr int kTxStatsFifoSize = 16;
  TxStatsFifoEntry tx_stats_fifo_[kTxStatsFifoSize] __TA_GUARDED(lock_);
  int tx_stats_fifo_counter_ __TA_GUARDED(lock_) = 0;
  size_t tx_status_report_pending_ __TA_GUARDED(lock_) = 0;

  // Thread which periodically reads interrupt registers.
  // Required because the device doesn't support USB interrupt endpoints.
  std::thread interrupt_thrd_;
  zx::port interrupt_port_;

  // Timer which handles asynchronous TX reports.
  zx::timer async_tx_interrupt_timer_;

  // Timer which handles TBTT interrupts for 802.11 beacon frames.
  zx::timer tbtt_interrupt_timer_;

  // Bitmap indicating available WCID slots.
  bitmap::RawBitmapGeneric<bitmap::FixedStorage<kMaxValidWcid>> used_wcid_bitmap_;
  // Map from mac address to WCID.
  std::unordered_map<wlan::common::MacAddr, uint8_t, wlan::common::MacAddrHasher> addr_wcid_map_;
};

}  // namespace ralink

#endif  // SRC_CONNECTIVITY_WLAN_DRIVERS_MEDIATEK_RALINK_DEVICE_H_
