// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <wlan/common/element_splitter.h>
#include <wlan/common/mac_frame.h>
#include <wlan/common/parse_element.h>
#include <wlan/common/perr_destination_parser.h>
#include <wlan/common/write_element.h>
#include <wlan/mlme/mac_header_writer.h>
#include <wlan/mlme/mesh/hwmp.h>
#include <zircon/status.h>

namespace wlan {

static constexpr size_t kInitialTtl = 32;
static constexpr size_t kMaxHwmpFrameSize = 2048;
static constexpr size_t kDot11MeshHWMPactivePathTimeoutTu = 5000;
static constexpr size_t kDot11MeshHWMPmaxPREQretries = 3;
static constexpr size_t kDot11MeshHWMPpreqMinIntervalTu = 100;
static constexpr bool kDot11MeshHWMPtargetOnly = true;
static constexpr size_t kDot11MeshHWMPperrMinIntervalTu = 100;

HwmpState::HwmpState(fbl::unique_ptr<Timer> timer)
    : our_hwmp_seqno(0),
      timer_mgr(std::move(timer)),
      perr_rate_limiter(WLAN_TU(kDot11MeshHWMPperrMinIntervalTu), 1) {}

// According to IEEE Std 802.11-2016, 14.10.8.3
bool HwmpSeqnoLessThan(uint32_t a, uint32_t b) {
  // Perform subtraction mod 2^32.
  //
  // If a <= b, then d = abs(a - b).
  // If a > b, then d = 2^32 - abs(a - b).
  // In either case, 'd' is the distance on the circle that one needs to walk
  // if starting from 'a', finishing at 'b' and going in the positive direction.
  //
  // 'a' precedes 'b' iff this walk distance 'd' is non-zero and is less than
  // half the length of the circle. Note there is an edge case where 'a' and 'b'
  // are exactly 2^31 apart (i.e., diametrically opposed on the circle). We will
  // return false in that case.
  uint32_t d = b - a;
  // Equivalent to d != 0 && d < (1u << 31);
  return static_cast<int32_t>(d) > 0;
}

static uint32_t MaxHwmpSeqno(uint32_t a, uint32_t b) {
  return HwmpSeqnoLessThan(a, b) ? b : a;
}

// See IEEE Std 802.11-2016, 14.10.8.4
static bool ShouldUpdatePathToRemoteNode(const MeshPath* path,
                                         uint32_t remote_hwmp_seqno,
                                         uint32_t total_metric) {
  // No known path: definitely create one
  if (!path) {
    return true;
  }

  // Update if we got a more recent HWMP sequence number
  if (path->hwmp_seqno &&
      HwmpSeqnoLessThan(*path->hwmp_seqno, remote_hwmp_seqno)) {
    return true;
  }

  // If the sequence number is unknown or exactly matches what we received,
  // update the path if the metric improved
  if (!path->hwmp_seqno || *path->hwmp_seqno == remote_hwmp_seqno) {
    return path->metric > total_metric;
  }

  return false;
}

// See IEEE Std 802.11-2016, 14.10.8.4
//
// "Remote" node is either the originator or the target
// (for PREQ and PREP elements, respectively)
//
// Returns the mesh path to the remote node if it was updated and nullptr
// otherwise.
static const MeshPath* UpdateForwardingInfo(
    PathTable* path_table, HwmpState* state,
    const common::MacAddr& transmitter_addr, const common::MacAddr& remote_addr,
    uint32_t remote_hwmp_seqno, uint32_t metric, uint32_t last_hop_metric,
    unsigned hop_count, uint32_t lifetime_tu) {
  zx::time expiration = state->timer_mgr.Now() + WLAN_TU(lifetime_tu);
  const MeshPath* ret = nullptr;

  // First, update the path information for the originator/destination node
  // (see the last two bullet points in 14.10.8.4)
  auto path = path_table->GetPath(remote_addr);
  if (ShouldUpdatePathToRemoteNode(path, remote_hwmp_seqno,
                                   metric + last_hop_metric)) {
    zx::time old_expiration = path ? path->expiration_time : zx::time{};
    // See Table 14-9, columns titled 'Forwarding information for originator
    // mesh STA' and 'Forwarding information for target mesh STA'.
    ret = path_table->AddOrUpdatePath(
        remote_addr,
        MeshPath{
            .next_hop = transmitter_addr,
            .hwmp_seqno = {remote_hwmp_seqno},
            .expiration_time = std::max(old_expiration, expiration),
            .metric = metric + last_hop_metric,
            .hop_count = hop_count + 1,
        });
  }

  // Update the path information for the transmitter if it is different from the
  // originator/destination (see the first bullet point in 14.10.8.4).
  if (transmitter_addr != remote_addr) {
    auto path = path_table->GetPath(transmitter_addr);
    if (!path || path->metric > last_hop_metric) {
      zx::time old_expiration = path ? path->expiration_time : zx::time{};
      auto path_seqno = path ? path->hwmp_seqno : std::optional<uint32_t>{};

      path_table->AddOrUpdatePath(
          transmitter_addr,
          MeshPath{
              .next_hop = transmitter_addr,
              .hwmp_seqno = path_seqno,
              .expiration_time = std::max(old_expiration, expiration),
              .metric = last_hop_metric,
              .hop_count = 1,
          });
    }
  }

  return ret;
}

struct SeqnoAndMetric {
  uint32_t hwmp_seqno;
  uint32_t metric;

  static SeqnoAndMetric FromState(const HwmpState* state) {
    return {.hwmp_seqno = state->our_hwmp_seqno, .metric = 0};
  }

  static SeqnoAndMetric FromPath(const MeshPath* path) {
    ZX_ASSERT(path->hwmp_seqno.has_value());
    return {.hwmp_seqno = *path->hwmp_seqno, .metric = path->metric};
  }
};

// See IEEE Std 802.11-2016, 14.10.10.3, Cases A and C
static fbl::unique_ptr<Packet> MakeOriginalPrep(
    const MacHeaderWriter& header_writer,
    const common::MacAddr& preq_transmitter_addr,
    const PreqPerTarget& per_target, const common::ParsedPreq& preq,
    const MeshPath& path_to_originator, SeqnoAndMetric target_info) {
  auto packet = GetWlanPacket(kMaxHwmpFrameSize);
  if (!packet) {
    return {};
  }
  BufferWriter w(*packet);
  header_writer.WriteMeshMgmtHeader(&w, kAction, preq_transmitter_addr);
  w.Write<ActionFrame>()->category = action::kMesh;
  w.Write<MeshActionHeader>()->mesh_action = action::kHwmpMeshPathSelection;
  common::WritePrep(&w,
                    {
                        .flags = {},
                        .hop_count = 0,
                        .element_ttl = kInitialTtl,
                        .target_addr = per_target.target_addr,
                        .target_hwmp_seqno = target_info.hwmp_seqno,
                    },
                    nullptr,  // For now, we don't support external addresses in
                              // path selection
                    {
                        .lifetime = preq.middle->lifetime,
                        .metric = target_info.metric,
                        .originator_addr = preq.header->originator_addr,
                        // Safe because this can only be called after updating
                        // the path to the originator
                        .originator_hwmp_seqno = *path_to_originator.hwmp_seqno,
                    });
  packet->set_len(w.WrittenBytes());
  return packet;
}

// See IEEE Std 802.11-2016, 14.10.9.3, Case E
static fbl::unique_ptr<Packet> MakeForwardedPreq(
    const MacHeaderWriter& mac_header_writer,
    const common::ParsedPreq& incoming_preq, uint32_t last_hop_metric,
    Span<const PreqPerTarget> to_forward) {
  auto packet = GetWlanPacket(kMaxHwmpFrameSize);
  if (!packet) {
    return {};
  }
  BufferWriter w(*packet);
  mac_header_writer.WriteMeshMgmtHeader(&w, kAction, common::kBcastMac);
  w.Write<ActionFrame>()->category = action::kMesh;
  w.Write<MeshActionHeader>()->mesh_action = action::kHwmpMeshPathSelection;
  common::WritePreq(
      &w,
      {
          .flags = incoming_preq.header->flags,
          .hop_count =
              static_cast<uint8_t>(incoming_preq.header->hop_count + 1),
          .element_ttl =
              static_cast<uint8_t>(incoming_preq.header->element_ttl - 1),
          .path_discovery_id = incoming_preq.header->path_discovery_id,
          .originator_addr = incoming_preq.header->originator_addr,
          .originator_hwmp_seqno = incoming_preq.header->originator_hwmp_seqno,
      },
      incoming_preq.originator_external_addr,
      {
          .lifetime = incoming_preq.middle->lifetime,
          .metric = incoming_preq.middle->metric + last_hop_metric,
          .target_count = static_cast<uint8_t>(to_forward.size()),
      },
      to_forward);
  packet->set_len(w.WrittenBytes());
  return packet;
}

// See IEEE Std 802.11-2016, 14.10.9.4.3
static void HandlePreq(const common::MacAddr& preq_transmitter_addr,
                       const common::MacAddr& self_addr,
                       const common::ParsedPreq& preq, uint32_t last_hop_metric,
                       const MacHeaderWriter& mac_header_writer,
                       HwmpState* state, PathTable* path_table,
                       PacketQueue* packets_to_tx) {
  // The spec (IEEE Std 802.11-2016, 14.10.9.4.2) suggests that we entirely
  // throw out the PREQ if it doesn't meet the "acceptance criteria",
  // e.g. if there is no known path to the target. This seems wrong: we could
  // still use the information in the PREQ to update the paths to the
  // transmitter and the originator. Also, the sentence refers to "THE" target
  // address of the PREQ element, whereas in fact a single PREQ element may
  // contain several target addresses.
  auto path_to_originator = UpdateForwardingInfo(
      path_table, state, preq_transmitter_addr, preq.header->originator_addr,
      preq.header->originator_hwmp_seqno, preq.middle->metric, last_hop_metric,
      preq.header->hop_count, preq.middle->lifetime);
  // The spec also suggests another case when we need to handle the PREQ:
  // the sequence number in the PREQ matches what we have cached for the target,
  // but we haven't seen the (originator_addr, path_discovery_id) pair yet.
  // It is unclear yet whether this is really necessary. To implement this,
  // we would need to cache (originator_addr, path_discovery_id) pairs, which
  // could be avoided otherwise.
  if (path_to_originator == nullptr) {
    return;
  }

  PreqPerTarget to_forward[kPreqMaxTargets];
  size_t num_to_forward = 0;

  for (auto t : preq.per_target) {
    if (t.target_addr == self_addr) {
      // TODO(gbonik): Also check if we are a proxy of target_addr

      // See IEEE Std 802.11-2016, 14.10.8.3, second bullet point
      state->our_hwmp_seqno =
          MaxHwmpSeqno(state->our_hwmp_seqno, t.target_hwmp_seqno) + 1;
      if (auto packet = MakeOriginalPrep(
              mac_header_writer, preq_transmitter_addr, t, preq,
              *path_to_originator, SeqnoAndMetric::FromState(state))) {
        packets_to_tx->Enqueue(std::move(packet));
      }
    } else {
      bool replied = false;
      if (!t.flags.target_only()) {
        auto path_to_target = path_table->GetPath(t.target_addr);
        if (path_to_target != nullptr &&
            path_to_target->hwmp_seqno.has_value() &&
            path_to_target->expiration_time >= state->timer_mgr.Now()) {
          auto packet = MakeOriginalPrep(
              mac_header_writer, preq_transmitter_addr, t, preq,
              *path_to_originator, SeqnoAndMetric::FromPath(path_to_target));
          if (packet) {
            packets_to_tx->Enqueue(std::move(packet));
          }
          replied = true;
        }
      }

      if (preq.header->element_ttl > 1 &&
          num_to_forward < countof(to_forward)) {
        to_forward[num_to_forward] = t;
        if (replied) {
          // See IEEE Std 802.11-2016, 14.10.9.3, case E2: since we already
          // replied to the PREQ, we need to set 'Target Only' to true so that
          // other intermediate nodes do not reply.
          to_forward[num_to_forward].flags.set_target_only(true);
        }
        num_to_forward += 1;
      }
    }
    // TODO(gbonik): update proxy info if address extension is present (case
    // (a)/(b))
    // TODO: Also update precursors if we decide to store them one day (case
    // (d))
    // TODO(gobnik): reply to proactive (broadcast) PREQs (case (e))
  }

  if (num_to_forward > 0) {
    if (auto packet =
            MakeForwardedPreq(mac_header_writer, preq, last_hop_metric,
                              {to_forward, num_to_forward})) {
      packets_to_tx->Enqueue(std::move(packet));
    }
  }
}

// See IEEE Std 802.11-2016, 14.10.10.3, Case B
static fbl::unique_ptr<Packet> MakeForwardedPrep(
    const MacHeaderWriter& mac_header_writer,
    const common::ParsedPrep& incoming_prep, uint32_t last_hop_metric,
    const MeshPath& path_to_originator) {
  auto packet = GetWlanPacket(kMaxHwmpFrameSize);
  if (!packet) {
    return {};
  }
  BufferWriter w(*packet);
  mac_header_writer.WriteMeshMgmtHeader(&w, kAction,
                                        path_to_originator.next_hop);
  w.Write<ActionFrame>()->category = action::kMesh;
  w.Write<MeshActionHeader>()->mesh_action = action::kHwmpMeshPathSelection;
  common::WritePrep(
      &w,
      {
          .flags = incoming_prep.header->flags,
          .hop_count =
              static_cast<uint8_t>(incoming_prep.header->hop_count + 1),
          .element_ttl =
              static_cast<uint8_t>(incoming_prep.header->element_ttl - 1),
          .target_addr = incoming_prep.header->target_addr,
          .target_hwmp_seqno = incoming_prep.header->target_hwmp_seqno,
      },
      incoming_prep.target_external_addr,
      {
          .lifetime = incoming_prep.tail->lifetime,
          .metric = incoming_prep.tail->metric + last_hop_metric,
          .originator_addr = incoming_prep.tail->originator_addr,
          .originator_hwmp_seqno = incoming_prep.tail->originator_hwmp_seqno,
      });
  packet->set_len(w.WrittenBytes());
  return packet;
}

// See IEEE Std 802.11-2016, 14.10.10.4.3
static void HandlePrep(const common::MacAddr& prep_transmitter_addr,
                       const common::MacAddr& self_addr,
                       const common::ParsedPrep& prep, uint32_t last_hop_metric,
                       const MacHeaderWriter& mac_header_writer,
                       HwmpState* state, PathTable* path_table,
                       PacketQueue* packets_to_tx) {
  auto path_to_target = UpdateForwardingInfo(
      path_table, state, prep_transmitter_addr, prep.header->target_addr,
      prep.header->target_hwmp_seqno, prep.tail->metric, last_hop_metric,
      prep.header->hop_count, prep.tail->lifetime);
  if (path_to_target == nullptr) {
    return;
  }

  auto it = state->state_by_target.find(prep.header->target_addr.ToU64());
  if (it != state->state_by_target.end()) {
    // Path established successfully: cancel the retry timer
    state->timer_mgr.Cancel(it->second.next_attempt);
    state->state_by_target.erase(it);
  }

  if (prep.tail->originator_addr != self_addr && prep.header->element_ttl > 1) {
    if (auto path_to_originator =
            path_table->GetPath(prep.tail->originator_addr)) {
      if (auto packet = MakeForwardedPrep(
              mac_header_writer, prep, last_hop_metric, *path_to_originator)) {
        packets_to_tx->Enqueue(std::move(packet));
      }
    }
  }
  // TODO(gbonik): update proxy info if address extension is present (case
  // (b)/(c))
  // TODO: Also update precursors if we decide to store them one day (case (d))
}

// See IEEE Std 802.11-2016, 14.10.11.4.3
static bool ShouldInvalidatePathByPerr(
    const common::MacAddr& perr_transmitter_addr,
    const common::ParsedPerrDestination& perr_dest, const PathTable* path_table,
    uint32_t* out_hwmp_seqno) {
  using Rc = ::fuchsia::wlan::mlme::ReasonCode;

  // PERR elements with an external address can only invalidate proxy
  // information, not forwarding information. An element with reason code set to
  // NO_FORWARDING_INFORMATION or DESTINATION_UNREACHABLE is not allowed to have
  // an external address. See IEEE Std 802.11-2016, 14.10.11.3 for the list of
  // valid PERR contents.
  if (perr_dest.ext_addr != nullptr) {
    return false;
  }

  auto path = path_table->GetPath(perr_dest.header->dest_addr);
  if (path == nullptr) {
    return false;
  }
  if (path->next_hop != perr_transmitter_addr) {
    return false;
  }

  // Case (b)
  // It is unfortunate that the spec suggests using 0 to indicate an unknown
  // HWMP sequence value, instead of the "USN" flag like in PREQ. Zero is
  // otherwise a perfectly normal value of the HWMP sequence number which could
  // occur with the 32-bit counter rollover.
  if (perr_dest.tail->reason_code ==
          to_enum_type(Rc::MESH_PATH_ERROR_NO_FORWARDING_INFORMATION) &&
      perr_dest.header->hwmp_seqno == 0) {
    if (path->hwmp_seqno.has_value()) {
      *out_hwmp_seqno = *path->hwmp_seqno + 1;
    } else {
      *out_hwmp_seqno = 0;
    }
    return true;
  }

  // Case (c)
  if (perr_dest.tail->reason_code ==
          to_enum_type(Rc::MESH_PATH_ERROR_DESTINATION_UNREACHABLE) ||
      perr_dest.tail->reason_code ==
          to_enum_type(Rc::MESH_PATH_ERROR_NO_FORWARDING_INFORMATION)) {
    if (!path->hwmp_seqno.has_value() ||
        HwmpSeqnoLessThan(*path->hwmp_seqno, perr_dest.header->hwmp_seqno)) {
      *out_hwmp_seqno = perr_dest.header->hwmp_seqno;
      return true;
    }
  }

  return false;
}

static void WriteForwardedPerrDestination(
    const common::ParsedPerrDestination& incoming, uint32_t new_hwmp_seqno,
    BufferWriter* writer) {
  writer->WriteValue<PerrPerDestinationHeader>({
      .flags = incoming.header->flags,
      .dest_addr = incoming.header->dest_addr,
      .hwmp_seqno = new_hwmp_seqno,
  });
  if (incoming.ext_addr != nullptr) {
    writer->WriteValue<common::MacAddr>(*incoming.ext_addr);
  }
  writer->WriteValue<PerrPerDestinationTail>(*incoming.tail);
}

// See IEEE Std 802.11-2016, 14.10.11.3, Case D
static fbl::unique_ptr<Packet> MakeForwardedPerr(
    const MacHeaderWriter& mac_header_writer,
    const common::ParsedPerr& incoming_perr, uint8_t num_destinations,
    Span<const uint8_t> destinations) {
  auto packet = GetWlanPacket(kMaxHwmpFrameSize);
  if (!packet) {
    return {};
  }
  BufferWriter w(*packet);
  // Note that we deviate from the standard here, which suggests maintaining a
  // list of precursors for each path and sending individually-addressed
  // management frames to all precursors of the invalidated paths. This seems
  // impractical and would only take more air time. See 14.10.7, "PERR element
  // individually addressed [Case D]".
  mac_header_writer.WriteMeshMgmtHeader(&w, kAction, common::kBcastMac);
  w.Write<ActionFrame>()->category = action::kMesh;
  w.Write<MeshActionHeader>()->mesh_action = action::kHwmpMeshPathSelection;
  common::WritePerr(&w,
                    {
                        .element_ttl = static_cast<uint8_t>(
                            incoming_perr.header->element_ttl - 1),
                        .num_destinations = num_destinations,
                    },
                    destinations);
  packet->set_len(w.WrittenBytes());
  return packet;
}

static void HandlePerr(const common::MacAddr& perr_transmitter_addr,
                       const common::ParsedPerr& perr,
                       const MacHeaderWriter& mac_header_writer,
                       HwmpState* state, PathTable* path_table,
                       PacketQueue* packets_to_tx) {
  if (perr.header->num_destinations > kPerrMaxDestinations) {
    return;
  }

  // Buffer for constructing the "destinations" part of the forwarded PERR
  uint8_t to_forward_buf[kPerrMaxDestinations * kPerrMaxDestinationSize];
  BufferWriter to_forward(to_forward_buf);
  size_t num_dests_to_forward = 0;

  // Paths to be removed from the table
  common::MacAddr dests_to_invalidate[kPerrMaxDestinations];
  size_t num_dests_to_invalidate = 0;

  common::PerrDestinationParser parser(perr.destinations);
  for (size_t i = 0; i < perr.header->num_destinations; ++i) {
    auto dest = parser.Next();
    if (!dest.has_value()) {
      return;
    }

    uint32_t hwmp_seqno;
    if (ShouldInvalidatePathByPerr(perr_transmitter_addr, *dest, path_table,
                                   &hwmp_seqno)) {
      dests_to_invalidate[num_dests_to_invalidate++] = dest->header->dest_addr;
      WriteForwardedPerrDestination(*dest, hwmp_seqno, &to_forward);
      ++num_dests_to_forward;
    }

    // TODO(gbonik): invalidate proxy info. See IEEE Std
    // 802.11-2016, 14.10.11.4.3, case (d)
  }

  // Drop the element without processing if it has extra garbage at the end
  if (parser.ExtraBytesLeft()) {
    return;
  }

  // Remove invalidated paths
  for (size_t i = 0; i < num_dests_to_invalidate; ++i) {
    path_table->RemovePath(dests_to_invalidate[i]);
  }

  // Forward the frame
  if (num_dests_to_forward > 0 && perr.header->element_ttl > 1 &&
      state->perr_rate_limiter.RecordEvent(state->timer_mgr.Now())) {
    if (auto packet =
            MakeForwardedPerr(mac_header_writer, perr, num_dests_to_forward,
                              to_forward.WrittenData())) {
      packets_to_tx->Enqueue(std::move(packet));
    }
  }
}

PacketQueue HandleHwmpAction(Span<const uint8_t> elements,
                             const common::MacAddr& action_transmitter_addr,
                             const common::MacAddr& self_addr,
                             uint32_t last_hop_metric,
                             const MacHeaderWriter& mac_header_writer,
                             HwmpState* state, PathTable* path_table) {
  PacketQueue ret;
  for (auto [id, raw_body] : common::ElementSplitter(elements)) {
    switch (id) {
      case element_id::kPreq:
        if (auto preq = common::ParsePreq(raw_body)) {
          HandlePreq(action_transmitter_addr, self_addr, *preq, last_hop_metric,
                     mac_header_writer, state, path_table, &ret);
        }
        break;
      case element_id::kPrep:
        if (auto prep = common::ParsePrep(raw_body)) {
          HandlePrep(action_transmitter_addr, self_addr, *prep, last_hop_metric,
                     mac_header_writer, state, path_table, &ret);
        }
        break;
      case element_id::kPerr:
        if (auto perr = common::ParsePerr(raw_body)) {
          HandlePerr(action_transmitter_addr, *perr, mac_header_writer, state,
                     path_table, &ret);
        }
        break;
      default:
        break;
    }
  }
  return ret;
}

// IEEE Std 802.11-2016, 14.10.9.3, case A, Table 14-10
static fbl::unique_ptr<Packet> MakeOriginalPreq(
    const common::MacAddr& target_addr, const common::MacAddr& self_addr,
    const MacHeaderWriter& mac_header_writer, uint32_t our_hwmp_seqno,
    uint32_t path_discovery_id, const PathTable& path_table) {
  auto packet = GetWlanPacket(kMaxHwmpFrameSize);
  if (!packet) {
    return {};
  }

  BufferWriter w(*packet);
  // IEEE Std 802.11-2016, 14.10.7, case A
  mac_header_writer.WriteMeshMgmtHeader(&w, kAction, common::kBcastMac);
  w.Write<ActionFrame>()->category = action::kMesh;
  w.Write<MeshActionHeader>()->mesh_action = action::kHwmpMeshPathSelection;

  const MeshPath* path_to_target = path_table.GetPath(target_addr);
  auto target_seqno =
      path_to_target ? path_to_target->hwmp_seqno : std::optional<uint32_t>();

  PreqPerTargetFlags target_flags;
  target_flags.set_target_only(kDot11MeshHWMPtargetOnly);
  target_flags.set_usn(!target_seqno);

  PreqPerTarget per_target = {
      .flags = target_flags,
      .target_addr = target_addr,
      .target_hwmp_seqno = target_seqno.value_or(0),
  };

  common::WritePreq(&w,
                    {
                        .flags = {},
                        .hop_count = 0,
                        .element_ttl = kInitialTtl,
                        .path_discovery_id = path_discovery_id,
                        .originator_addr = self_addr,
                        .originator_hwmp_seqno = our_hwmp_seqno,
                    },
                    nullptr,
                    {
                        .lifetime = kDot11MeshHWMPactivePathTimeoutTu,
                        .metric = 0,
                        .target_count = 1,
                    },
                    {&per_target, 1});

  packet->set_len(w.WrittenBytes());
  return packet;
}

zx_status_t EmitOriginalPreq(const common::MacAddr& target_addr,
                             HwmpState::TargetState* target_state,
                             const common::MacAddr& self_addr,
                             const MacHeaderWriter& mac_header_writer,
                             HwmpState* state, const PathTable& path_table,
                             PacketQueue* packets_to_tx) {
  zx_status_t status = state->timer_mgr.Schedule(
      state->timer_mgr.Now() + WLAN_TU(kDot11MeshHWMPpreqMinIntervalTu),
      {target_addr}, &target_state->next_attempt);
  if (status != ZX_OK) {
    errorf("[hwmp] failed to schedule a timer: %s\n",
           zx_status_get_string(status));
    return ZX_ERR_INTERNAL;
  }
  uint32_t our_hwmp_seqno = ++state->our_hwmp_seqno;
  uint32_t path_discovery_id = ++state->next_path_discovery_id;
  target_state->attempts_left -= 1;
  if (auto packet =
          MakeOriginalPreq(target_addr, self_addr, mac_header_writer,
                           our_hwmp_seqno, path_discovery_id, path_table)) {
    packets_to_tx->Enqueue(std::move(packet));
  }
  return ZX_OK;
}

zx_status_t InitiatePathDiscovery(const common::MacAddr& target_addr,
                                  const common::MacAddr& self_addr,
                                  const MacHeaderWriter& mac_header_writer,
                                  HwmpState* state, const PathTable& path_table,
                                  PacketQueue* packets_to_tx) {
  auto it = state->state_by_target.find(target_addr.ToU64());
  if (it != state->state_by_target.end()) {
    it->second.attempts_left = kDot11MeshHWMPmaxPREQretries;
    return ZX_OK;
  }

  it = state->state_by_target
           .emplace(target_addr.ToU64(),
                    HwmpState::TargetState{{}, kDot11MeshHWMPmaxPREQretries})
           .first;
  zx_status_t status =
      EmitOriginalPreq(target_addr, &it->second, self_addr, mac_header_writer,
                       state, path_table, packets_to_tx);
  if (status != ZX_OK) {
    state->state_by_target.erase(it);
  }
  return status;
}

zx_status_t HandleHwmpTimeout(const common::MacAddr& self_addr,
                              const MacHeaderWriter& mac_header_writer,
                              HwmpState* state, const PathTable& path_table,
                              PacketQueue* packets_to_tx) {
  return state->timer_mgr.HandleTimeout([&](auto now, auto event,
                                            auto timeout_id) {
    auto it = state->state_by_target.find(event.addr.ToU64());
    if (it == state->state_by_target.end() ||
        it->second.next_attempt != timeout_id) {
      return;
    }

    if (it->second.attempts_left == 0) {
      // Failed to discover the path after the maximum number of attempts. Clear
      // the state.
      state->state_by_target.erase(it);
      return;
    }

    zx_status_t status =
        EmitOriginalPreq(event.addr, &it->second, self_addr, mac_header_writer,
                         state, path_table, packets_to_tx);
    if (status != ZX_OK) {
      errorf(
          "[hwmp] failed to schedule a path request frame transmission: %s\n",
          zx_status_get_string(status));
    }
  });
}

// IEEE Std 802.11-2016, Case B
PacketQueue OnMissingForwardingPath(const common::MacAddr& peer_to_notify,
                                    const common::MacAddr& missing_destination,
                                    const MacHeaderWriter& mac_header_writer,
                                    HwmpState* state) {
  auto packet = GetWlanPacket(kMaxHwmpFrameSize);
  if (!packet) {
    return {};
  }

  if (!state->perr_rate_limiter.RecordEvent(state->timer_mgr.Now())) {
    return {};
  }

  uint8_t destination_buf[kPerrMaxDestinationSize];
  BufferWriter destination_writer(destination_buf);
  destination_writer.WriteValue<PerrPerDestinationHeader>({
      .flags = {},
      .dest_addr = missing_destination,
      .hwmp_seqno = 0,
  });
  destination_writer.WriteValue<PerrPerDestinationTail>(
      {.reason_code = static_cast<uint16_t>(
           fuchsia::wlan::mlme::ReasonCode::
               MESH_PATH_ERROR_NO_FORWARDING_INFORMATION)});

  BufferWriter w(*packet);
  mac_header_writer.WriteMeshMgmtHeader(&w, kAction, peer_to_notify);
  w.Write<ActionFrame>()->category = action::kMesh;
  w.Write<MeshActionHeader>()->mesh_action = action::kHwmpMeshPathSelection;
  common::WritePerr(&w,
                    {
                        .element_ttl = kInitialTtl,
                        .num_destinations = 1,
                    },
                    destination_writer.WrittenData());
  packet->set_len(w.WrittenBytes());

  PacketQueue packets_to_tx;
  packets_to_tx.Enqueue(std::move(packet));
  return packets_to_tx;
}

}  // namespace wlan
