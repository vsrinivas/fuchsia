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

#include "device_inspect.h"

#include <zircon/status.h>

namespace wlan::brcmfmac {

void DeviceInspect::AllocTimers(struct brcmf_bus* bus_if, async_dispatcher_t* dispatcher) {
  timer_hr_ = std::make_unique<Timer>(bus_if, dispatcher,
                                      std::bind(&DeviceInspect::TimerHrCallback, this), true);
}

void DeviceInspect::DeallocTimers() { timer_hr_.reset(); }

void DeviceInspect::StartTimers() { timer_hr_->Start(ZX_HOUR(1)); }

void DeviceInspect::TimerHrCallback() {
  tx_qfull_24hrs_.SlideWindow();
  conn_metrics_.success_24hrs.SlideWindow();
  conn_metrics_.no_network_fail_24hrs.SlideWindow();
  conn_metrics_.auth_fail_24hrs.SlideWindow();
  conn_metrics_.other_fail_24hrs.SlideWindow();
}

zx_status_t DeviceInspect::InitMetrics() {
  zx_status_t status = ZX_OK;
  tx_qfull_ = root_.CreateUint("tx_qfull", 0);
  if ((status = tx_qfull_24hrs_.Init(&root_, 24, "tx_qfull_24hrs", 0)) != ZX_OK) {
    return status;
  }

  conn_metrics_.root = root_.CreateChild("connection-metrics");
  conn_metrics_.success = conn_metrics_.root.CreateUint("success", 0);
  if ((status = conn_metrics_.success_24hrs.Init(&conn_metrics_.root, 24, "success_24hrs", 0)) !=
      ZX_OK) {
    return status;
  }
  conn_metrics_.no_network_fail = conn_metrics_.root.CreateUint("no_network_fail", 0);
  if ((status = conn_metrics_.no_network_fail_24hrs.Init(&conn_metrics_.root, 24,
                                                         "no_network_fail_24hrs", 0)) != ZX_OK) {
    return status;
  }
  conn_metrics_.auth_fail = conn_metrics_.root.CreateUint("auth_fail", 0);
  if ((status = conn_metrics_.auth_fail_24hrs.Init(&conn_metrics_.root, 24, "auth_fail_24hrs",
                                                   0)) != ZX_OK) {
    return status;
  }
  conn_metrics_.other_fail = conn_metrics_.root.CreateUint("other_fail", 0);
  if ((status = conn_metrics_.other_fail_24hrs.Init(&conn_metrics_.root, 24, "other_fail_24hrs",
                                                    0)) != ZX_OK) {
    return status;
  }

  arp_frame_metrics_.root = root_.CreateChild("arp-req-frames");

  return status;
}

zx_status_t DeviceInspect::Start(struct brcmf_bus* bus_if, async_dispatcher_t* dispatcher) {
  zx_status_t status = ZX_OK;
  root_ = inspector_.GetRoot().CreateChild("brcmfmac-phy");
  if ((status = InitMetrics()) != ZX_OK) {
    BRCMF_ERR("Failed to initialize metrics err: %s", zx_status_get_string(status));
    return status;
  }

  AllocTimers(bus_if, dispatcher);

  // Starting periodic timer can cause certain sim-tests to run indefinitely.
  // Therefore, we start timers only for inspect related tests by calling
  // StartTimers().
  if (brcmf_bus_get_bus_type(bus_if) != BRCMF_BUS_TYPE_SIM) {
    StartTimers();
  }

  return ZX_OK;
}

void DeviceInspect::LogTxQueueFull() {
  tx_qfull_.Add(1);
  tx_qfull_24hrs_.Add(1);
}

void DeviceInspect::LogConnSuccess() {
  conn_metrics_.success.Add(1);
  conn_metrics_.success_24hrs.Add(1);
}
void DeviceInspect::LogConnAuthFail() {
  conn_metrics_.auth_fail.Add(1);
  conn_metrics_.auth_fail_24hrs.Add(1);
}
void DeviceInspect::LogConnNoNetworkFail() {
  conn_metrics_.no_network_fail.Add(1);
  conn_metrics_.no_network_fail_24hrs.Add(1);
}
void DeviceInspect::LogConnOtherFail() {
  conn_metrics_.other_fail.Add(1);
  conn_metrics_.other_fail_24hrs.Add(1);
}

void DeviceInspect::LogArpRequestFrame(zx_time_t time, const uint8_t* frame, size_t frame_size) {
  uint16_t& local_count = arp_frame_metrics_.local_unique_arp_request_count;
  std::vector<uint8_t> arp_frame_vec(frame, frame + frame_size);

  // Create a new node for each unique ARP Request frame.
  arp_frame_metrics_.arp_request_frame_roots.push_back(
      arp_frame_metrics_.root.CreateChild(std::to_string(local_count)));

  // Create ARP Request frame info base on the new node.
  auto current_node = arp_frame_metrics_.arp_request_frame_roots.rbegin();
  arp_frame_metrics_.arp_req_frame_nodes.push_back(
      {.timestamp = current_node->CreateInt("timestamp", time),
       .frame_byte_vec = current_node->CreateByteVector("frame_data", arp_frame_vec)});

  // Pop the frame node after push, so the maximum number of frames could be in the metrics is
  // kMaxArpRequestFrameSize + 1.
  if (local_count >= ArpFrameMetrics::kMaxArpRequestFrameCount) {
    arp_frame_metrics_.arp_req_frame_nodes.pop_front();
    arp_frame_metrics_.arp_request_frame_roots.pop_front();
  }

  local_count++;
}

}  // namespace wlan::brcmfmac
