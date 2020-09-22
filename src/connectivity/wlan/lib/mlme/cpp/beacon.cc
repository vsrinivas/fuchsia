// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <wlan/common/buffer_writer.h>
#include <wlan/common/channel.h>
#include <wlan/common/write_element.h>
#include <wlan/mlme/beacon.h>
#include <wlan/mlme/rates_elements.h>

namespace wlan {

static void WriteSsid(BufferWriter* w, const BeaconConfig& config) {
  if (config.ssid != nullptr) {
    common::WriteSsid(w, {config.ssid, config.ssid_len});
  }
}

static void WriteDsssParamSet(BufferWriter* w, const BeaconConfig& config) {
  common::WriteDsssParamSet(w, config.channel.primary);
}

static void WriteTim(BufferWriter* w, const PsCfg* ps_cfg, size_t* rel_tim_ele_offset) {
  if (!ps_cfg) {
    return;
  }

  // To get the TIM offset in frame, we have to count the header, fixed
  // parameters and tagged parameters before TIM is written.
  *rel_tim_ele_offset = w->WrittenBytes();

  size_t bitmap_len;
  uint8_t bitmap_offset;
  uint8_t pvb[kMaxTimBitmapLen];
  ps_cfg->GetTim()->WritePartialVirtualBitmap(pvb, sizeof(pvb), &bitmap_len, &bitmap_offset);

  TimHeader header;
  header.dtim_count = ps_cfg->dtim_count();
  header.dtim_period = ps_cfg->dtim_period();
  header.bmp_ctrl.set_offset(bitmap_offset);
  if (ps_cfg->IsDtim()) {
    header.bmp_ctrl.set_group_traffic_ind(ps_cfg->GetTim()->HasGroupTraffic());
  }
  common::WriteTim(w, header, {pvb, bitmap_len});
}

static void WriteCountry(BufferWriter* w, const BeaconConfig& config) {
  // TODO(fxbug.dev/29328): Read from dot11CountryString MIB
  const Country kCountry = {{'U', 'S', ' '}};

  std::vector<SubbandTriplet> subbands;

  // TODO(porce): Read from the AP's regulatory domain
  if (wlan::common::Is2Ghz(config.channel)) {
    subbands.push_back({1, 11, 36});
  } else {
    subbands.push_back({36, 4, 36});
    subbands.push_back({52, 4, 30});
    subbands.push_back({100, 12, 30});
    subbands.push_back({149, 5, 36});
  }

  common::WriteCountry(w, kCountry, subbands);
}

static void WriteHtCapabilities(BufferWriter* w, const BeaconConfig& config) {
  if (config.ht.ready) {
    auto h = BuildHtCapabilities(config.ht);
    common::WriteHtCapabilities(w, h);
  }
}

static void WriteHtOperation(BufferWriter* w, const BeaconConfig& config) {
  if (config.ht.ready) {
    HtOperation hto = BuildHtOperation(config.channel);
    common::WriteHtOperation(w, hto);
  }
}

static void WriteRsne(BufferWriter* w, const BeaconConfig& config) {
  if (config.rsne != nullptr) {
    w->Write({config.rsne, config.rsne_len});
  }
}

static void WriteMeshConfiguration(BufferWriter* w, const BeaconConfig& config) {
  if (config.mesh_config != nullptr) {
    common::WriteMeshConfiguration(w, *config.mesh_config);
  }
}

static void WriteMeshId(BufferWriter* w, const BeaconConfig& config) {
  if (config.mesh_id != nullptr) {
    common::WriteMeshId(w, {config.mesh_id, config.mesh_id_len});
  }
}

static void WriteElements(BufferWriter* w, const BeaconConfig& config, size_t* rel_tim_ele_offset) {
  RatesWriter rates_writer{config.rates};
  // TODO(hahnr): Query from hardware which IEs must be filled out here.
  WriteSsid(w, config);
  rates_writer.WriteSupportedRates(w);
  WriteDsssParamSet(w, config);
  WriteTim(w, config.ps_cfg, rel_tim_ele_offset);
  WriteCountry(w, config);
  rates_writer.WriteExtendedSupportedRates(w);
  WriteRsne(w, config);
  WriteHtCapabilities(w, config);
  WriteHtOperation(w, config);
  WriteMeshId(w, config);
  WriteMeshConfiguration(w, config);
}

template <typename T>
static void SetBssType(T* bcn, BssType bss_type) {
  // IEEE Std 802.11-2016, 9.4.1.4
  switch (bss_type) {
    case BssType::kInfrastructure:
      bcn->cap.set_ess(1);
      bcn->cap.set_ibss(0);
      break;
    case BssType::kIndependent:
      bcn->cap.set_ess(0);
      bcn->cap.set_ibss(1);
      break;
    case BssType::kMesh:
      bcn->cap.set_ess(0);
      bcn->cap.set_ibss(0);
      break;
  }
}

template <typename T>
static zx_status_t BuildBeaconOrProbeResponse(const BeaconConfig& config,
                                              const common::MacAddr& recv_addr,
                                              MgmtFrame<T>* buffer, size_t* tim_ele_offset) {
  constexpr size_t reserved_ie_len = 256;
  constexpr size_t max_frame_size =
      MgmtFrameHeader::max_len() + Beacon::max_len() + reserved_ie_len;
  auto packet = GetWlanPacket(max_frame_size);
  if (packet == nullptr) {
    return ZX_ERR_NO_RESOURCES;
  }

  BufferWriter w(*packet);
  auto mgmt_hdr = w.Write<MgmtFrameHeader>();
  mgmt_hdr->fc.set_type(FrameType::kManagement);
  mgmt_hdr->fc.set_subtype(T::Subtype());
  mgmt_hdr->addr1 = recv_addr;
  mgmt_hdr->addr2 = config.bssid;
  mgmt_hdr->addr3 = config.bssid;

  auto bcn = w.Write<Beacon>();
  bcn->beacon_interval = config.beacon_period;
  bcn->timestamp = config.timestamp;
  bcn->cap.set_privacy(config.rsne != nullptr);

  SetBssType(bcn, config.bss_type);
  bcn->cap.set_short_preamble(1);

  // Write elements.
  BufferWriter elem_w(w.RemainingBuffer());
  size_t rel_tim_ele_offset = SIZE_MAX;
  WriteElements(&elem_w, config, &rel_tim_ele_offset);

  // Update packet's final length and tx_info.
  packet->set_len(w.WrittenBytes() + elem_w.WrittenBytes());

  if (tim_ele_offset != nullptr) {
    if (rel_tim_ele_offset == SIZE_MAX) {
      // A tim element offset was requested but no element was written
      return ZX_ERR_INVALID_ARGS;
    }
    *tim_ele_offset = w.WrittenBytes() + rel_tim_ele_offset;
  }

  *buffer = MgmtFrame<T>(std::move(packet));
  return ZX_OK;
}

zx_status_t BuildBeacon(const BeaconConfig& config, MgmtFrame<Beacon>* buffer,
                        size_t* tim_ele_offset) {
  return BuildBeaconOrProbeResponse(config, common::kBcastMac, buffer, tim_ele_offset);
}

zx_status_t BuildProbeResponse(const BeaconConfig& config, const common::MacAddr& recv_addr,
                               MgmtFrame<ProbeResponse>* buffer) {
  return BuildBeaconOrProbeResponse(config, recv_addr, buffer, nullptr);
}

}  // namespace wlan
