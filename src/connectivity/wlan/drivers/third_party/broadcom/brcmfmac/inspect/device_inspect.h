// Copyright (c) 2020 The Fuchsia Authors
//
// Permission to use, copy, modify, and/or distribute this software for any purpose with or without
// fee is hereby granted, provided that the above copyright notice and this permission notice
// appear in all copies.
//
// THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS
// SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE
// AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
// WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT,
// NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE
// OF THIS SOFTWARE.

#ifndef SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_BROADCOM_BRCMFMAC_INSPECT_DEVICE_INSPECT_H_
#define SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_BROADCOM_BRCMFMAC_INSPECT_DEVICE_INSPECT_H_

#include <lib/inspect/cpp/inspect.h>

#include <list>
#include <memory>

#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/inspect/windowed_uintproperty.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/timer.h"

namespace wlan::brcmfmac {

// TODO (fxbug.dev/70331) - Avoid duplicates for windowed and non-windowed property.
struct DeviceConnMetrics {
  inspect::Node root;
  inspect::UintProperty success;
  WindowedUintProperty success_24hrs;
  inspect::UintProperty no_network_fail;
  WindowedUintProperty no_network_fail_24hrs;
  inspect::UintProperty auth_fail;
  WindowedUintProperty auth_fail_24hrs;
  inspect::UintProperty other_fail;
  WindowedUintProperty other_fail_24hrs;
};

struct ArpReqFrameInfo {
  inspect::IntProperty timestamp;
  inspect::ByteVectorProperty frame_byte_vec;
};

struct ArpFrameMetrics {
  inspect::Node root;
  constexpr static size_t kMaxArpRequestFrameCount = 10;
  uint16_t local_unique_arp_request_count;
  std::list<inspect::Node> arp_request_frame_roots;
  std::list<ArpReqFrameInfo> arp_req_frame_nodes;
};

class DeviceInspect {
 public:
  zx_status_t Start(struct brcmf_bus* bus_if, async_dispatcher_t* dispatcher);
  void Stop() { DeallocTimers(); }
  zx::vmo GetVmo() { return inspector_.DuplicateVmo(); }
  inspect::Inspector GetInspector() { return inspector_; }

  // Exposed for use in sim-tests.
  void StartTimers();

  // Metrics APIs
  void LogTxQueueFull();
  void LogConnSuccess();
  void LogConnNoNetworkFail();
  void LogConnAuthFail();
  void LogConnOtherFail();
  void LogArpRequestFrame(zx_time_t time, const uint8_t* frame, size_t frame_size);

 private:
  void AllocTimers(struct brcmf_bus* bus_if, async_dispatcher_t* dispatcher);
  void DeallocTimers();

  inspect::Inspector inspector_;
  inspect::Node root_;
  std::unique_ptr<Timer> timer_hr_;
  void TimerHrCallback();
  zx_status_t InitMetrics();

  // Metrics
  DeviceConnMetrics conn_metrics_;
  ArpFrameMetrics arp_frame_metrics_;
  inspect::UintProperty tx_qfull_;
  WindowedUintProperty tx_qfull_24hrs_;
};

}  // namespace wlan::brcmfmac

#endif  // SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_BROADCOM_BRCMFMAC_INSPECT_DEVICE_INSPECT_H_
