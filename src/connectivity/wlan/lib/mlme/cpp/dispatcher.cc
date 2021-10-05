// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/wlan/internal/cpp/fidl.h>
#include <fuchsia/wlan/minstrel/cpp/fidl.h>
#include <fuchsia/wlan/mlme/cpp/fidl.h>
#include <zircon/fidl.h>
#include <zircon/status.h>
#include <zircon/types.h>

#include <atomic>
#include <cinttypes>
#include <cstring>
#include <memory>
#include <sstream>

#include <ddk/hw/wlan/wlaninfo/c/banjo.h>
#include <wlan/common/band.h>
#include <wlan/common/channel.h>
#include <wlan/common/mac_frame.h>
#include <wlan/common/stats.h>
#include <wlan/mlme/ap/ap_mlme.h>
#include <wlan/mlme/client/client_mlme.h>
#include <wlan/mlme/device_interface.h>
#include <wlan/mlme/dispatcher.h>
#include <wlan/mlme/mac_frame.h>
#include <wlan/mlme/packet.h>
#include <wlan/mlme/service.h>
#include <wlan/protocol/mac.h>

namespace wlan {

namespace wlan_common = ::fuchsia::wlan::common;
namespace wlan_internal = ::fuchsia::wlan::internal;
namespace wlan_mlme = ::fuchsia::wlan::mlme;
namespace wlan_minstrel = ::fuchsia::wlan::minstrel;
namespace wlan_stats = ::fuchsia::wlan::stats;

Dispatcher::Dispatcher(DeviceInterface* device, std::unique_ptr<Mlme> mlme)
    : device_(device), mlme_(std::move(mlme)) {
  debugfn();
  ZX_ASSERT(mlme_ != nullptr);
}

Dispatcher::~Dispatcher() {}

zx_status_t Dispatcher::HandlePacket(std::unique_ptr<Packet> packet) {
  debugfn();

  ZX_DEBUG_ASSERT(packet != nullptr);
  ZX_DEBUG_ASSERT(packet->peer() != Packet::Peer::kUnknown);

  WLAN_STATS_INC(any_packet.in);

  // If there is no active MLME, block all packets but service ones.
  // MLME-JOIN.request and MLME-START.request implicitly select a mode and
  // initialize the MLME. DEVICE_QUERY.request is used to obtain device
  // capabilities.

  auto service_msg = (packet->peer() == Packet::Peer::kService);
  if (mlme_ == nullptr && !service_msg) {
    WLAN_STATS_INC(any_packet.drop);
    return ZX_OK;
  }

  WLAN_STATS_INC(any_packet.out);

  zx_status_t status = ZX_OK;
  switch (packet->peer()) {
    case Packet::Peer::kEthernet:
      status = mlme_->HandleFramePacket(std::move(packet));
      break;
    case Packet::Peer::kWlan:
      if (auto fc = packet->field<FrameControl>(0)) {
        switch (fc->type()) {
          case FrameType::kManagement:
            WLAN_STATS_INC(mgmt_frame.in);
            break;
          case FrameType::kControl:
            WLAN_STATS_INC(ctrl_frame.in);
            break;
          case FrameType::kData:
            WLAN_STATS_INC(data_frame.in);
            break;
          default:
            break;
        }

        status = mlme_->HandleFramePacket(std::move(packet));
      }
      break;
    default:
      break;
  }

  return status;
}

zx_status_t Dispatcher::HandlePortPacket(uint64_t key) {
  debugfn();
  ZX_DEBUG_ASSERT(ToPortKeyType(key) == PortKeyType::kMlme);

  ObjectId id(ToPortKeyId(key));
  switch (id.subtype()) {
    case to_enum_type(ObjectSubtype::kTimer): {
      auto status = mlme_->HandleTimeout(id);
      if (status == ZX_ERR_NOT_SUPPORTED) {
        warnf("unknown MLME timer target: %u\n", id.target());
      }
      break;
    }
    default:
      warnf("unknown MLME event subtype: %u\n", id.subtype());
  }
  return ZX_OK;
}

zx_status_t Dispatcher::HandleAnyMlmeMessage(cpp20::span<uint8_t> span) {
  debugfn();
  // Attempt to process encoded message in MLME.
  auto hdr = FromBytes<fidl_message_header_t>(span);
  if (hdr == nullptr) {
    errorf("short mlme message, len=%zu\n", span.size());
    return ZX_OK;
  }
  uint64_t ordinal = hdr->ordinal;
  debughdr("service packet txid=%u ordinal=%lu\n", hdr->txid, ordinal);

  return mlme_->HandleEncodedMlmeMsg(span);
}

void Dispatcher::HwIndication(uint32_t ind) {
  debugfn();
  mlme_->HwIndication(ind);
}

void Dispatcher::HwScanComplete(uint8_t result_code) {
  debugfn();
  mlme_->HwScanComplete(result_code);
}

void Dispatcher::ResetStats() {
  stats_.Reset();
  if (mlme_) {
    mlme_->ResetMlmeStats();
  }
}

wlan_mlme::StatsQueryResponse Dispatcher::GetStatsToFidl() const {
  wlan_mlme::StatsQueryResponse stats_response;
  stats_response.stats.dispatcher_stats = stats_.ToFidl();
  auto mlme_stats = mlme_->GetMlmeStats();
  if (!mlme_stats.has_invalid_tag()) {
    stats_response.stats.mlme_stats =
        std::make_unique<wlan_stats::MlmeStats>(mlme_->GetMlmeStats());
  }
  return stats_response;
}

}  // namespace wlan
