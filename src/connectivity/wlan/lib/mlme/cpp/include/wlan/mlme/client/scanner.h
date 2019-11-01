// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_WLAN_LIB_MLME_CPP_INCLUDE_WLAN_MLME_CLIENT_SCANNER_H_
#define SRC_CONNECTIVITY_WLAN_LIB_MLME_CPP_INCLUDE_WLAN_MLME_CLIENT_SCANNER_H_

#include <fuchsia/wlan/mlme/cpp/fidl.h>
#include <lib/zx/time.h>
#include <zircon/types.h>

#include <memory>
#include <unordered_map>

#include <wlan/mlme/client/bss.h>
#include <wlan/mlme/client/channel_scheduler.h>
#include <wlan/mlme/rust_utils.h>
#include <wlan/mlme/timer_manager.h>
#include <wlan/protocol/mac.h>

namespace wlan {

class Clock;
class DeviceInterface;
class Packet;
class Timer;
template <typename T>
class MlmeMsg;

class Scanner {
 public:
  Scanner(DeviceInterface* device, ChannelScheduler* chan_sched,
          TimerManager<TimeoutTarget>* timer_mgr);
  virtual ~Scanner() {}

  zx_status_t Start(const MlmeMsg<::fuchsia::wlan::mlme::ScanRequest>& req);
  void Reset();

  bool IsRunning() const;
  wlan_channel_t ScanChannel() const;

  void ScheduleTimeout(zx::time deadline);
  void HandleTimeout();
  void CancelTimeout();

  zx_status_t HandleMlmeScanReq(const MlmeMsg<::fuchsia::wlan::mlme::ScanRequest>& req);
  void HandleBeacon(const MgmtFrameView<Beacon>& frame);
  void HandleProbeResponse(const MgmtFrameView<ProbeResponse>& frame);
  void HandleHwScanAborted();
  void HandleHwScanComplete();

 private:
  struct OffChannelHandlerImpl : OffChannelHandler {
    Scanner* scanner_;

    explicit OffChannelHandlerImpl(Scanner* scanner) : scanner_(scanner) {}

    virtual void BeginOffChannelTime() override;
    virtual void HandleOffChannelFrame(std::unique_ptr<Packet>) override;
    virtual bool EndOffChannelTime(bool interrupted, OffChannelRequest* next_req) override;
  };

  zx_status_t StartHwScan();

  bool ShouldDropMgmtFrame(const MgmtFrameHeader& hdr);
  void ProcessBeaconOrProbeResponse(const common::MacAddr bssid, const Beacon& beacon,
                                    fbl::Span<const uint8_t> ie_chain,
                                    const wlan_rx_info_t* rx_info);
  void SendProbeRequest(wlan_channel_t channel);
  OffChannelRequest CreateOffChannelRequest();
  void SendResultsAndReset();

  OffChannelHandlerImpl off_channel_handler_;
  DeviceInterface* device_;
  ::fuchsia::wlan::mlme::ScanRequestPtr req_ = nullptr;

  size_t channel_index_ = 0;

  std::unordered_map<uint64_t, Bss> current_bss_;
  ChannelScheduler* chan_sched_;
  TimerManager<TimeoutTarget>* timer_mgr_;
  SequenceManager seq_mgr_;
  TimeoutId timeout_;
};

}  // namespace wlan

#endif  // SRC_CONNECTIVITY_WLAN_LIB_MLME_CPP_INCLUDE_WLAN_MLME_CLIENT_SCANNER_H_
