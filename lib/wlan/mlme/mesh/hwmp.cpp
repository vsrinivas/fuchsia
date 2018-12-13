// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <wlan/common/element_splitter.h>
#include <wlan/common/mac_frame.h>
#include <wlan/common/parse_element.h>
#include <wlan/common/write_element.h>
#include <wlan/mlme/mac_header_writer.h>
#include <wlan/mlme/mesh/hwmp.h>

namespace wlan {

static constexpr size_t kInitialTtl = 32;
static constexpr size_t kMaxHwmpFrameSize = 2048;

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
static bool ShouldUpdatePathToRemoteNode(const MeshPath* path, uint32_t remote_hwmp_seqno,
                                         uint32_t total_metric) {
    // No known path: definitely create one
    if (!path) { return true; }

    // Update if we got a more recent HWMP sequence number
    if (path->hwmp_seqno && HwmpSeqnoLessThan(*path->hwmp_seqno, remote_hwmp_seqno)) {
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
// Returns the mesh path to the remote node if it was updated and nullptr otherwise.
static const MeshPath* UpdateForwardingInfo(PathTable* path_table, HwmpState* state,
                                            const common::MacAddr& transmitter_addr,
                                            const common::MacAddr& remote_addr,
                                            uint32_t remote_hwmp_seqno, uint32_t metric,
                                            uint32_t last_hop_metric, unsigned hop_count,
                                            uint32_t lifetime_tu) {
    zx::time expiration = state->timer_mgr.Now() + WLAN_TU(lifetime_tu);
    const MeshPath* ret = nullptr;

    // First, update the path information for the originator/destination node
    // (see the last two bullet points in 14.10.8.4)
    auto path = path_table->GetPath(remote_addr);
    if (ShouldUpdatePathToRemoteNode(path, remote_hwmp_seqno, metric + last_hop_metric)) {
        zx::time old_expiration = path ? path->expiration_time : zx::time{};
        // See Table 14-9, columns titled 'Forwarding information for originator mesh STA'
        // and 'Forwarding information for target mesh STA'.
        ret = path_table->AddOrUpdatePath(
            remote_addr, MeshPath{
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
        if (!path || path->metric > metric + last_hop_metric) {
            zx::time old_expiration = path ? path->expiration_time : zx::time{};
            auto path_seqno = path ? path->hwmp_seqno : std::optional<uint32_t>{};

            path_table->AddOrUpdatePath(transmitter_addr,
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

// See IEEE Std 802.11-2016, 14.10.10.3, Case A
static fbl::unique_ptr<Packet> MakeOriginalPrep(const MacHeaderWriter& header_writer,
                                                const common::MacAddr& preq_transmitter_addr,
                                                const common::MacAddr& self_addr,
                                                const common::ParsedPreq& preq,
                                                const MeshPath& path_to_originator,
                                                const HwmpState& state) {
    auto packet = GetWlanPacket(kMaxHwmpFrameSize);
    if (!packet) { return {}; }
    BufferWriter w(*packet);
    header_writer.WriteMeshMgmtHeader(&w, kAction, preq_transmitter_addr);
    w.Write<ActionFrame>()->category = action::kMesh;
    w.Write<MeshActionHeader>()->mesh_action = action::kHwmpMeshPathSelection;
    common::WritePrep(
        &w,
        {
            .flags = {},
            .hop_count = 0,
            .element_ttl = kInitialTtl,
            .target_addr = self_addr,
            .target_hwmp_seqno = state.our_hwmp_seqno,
        },
        nullptr, // For now, we don't support external addresses in path selection
        {
            .lifetime = preq.middle->lifetime,
            .metric = 0,
            .originator_addr = preq.header->originator_addr,
            // Safe because this can only be called after updating the path to the originator
            .originator_hwmp_seqno = *path_to_originator.hwmp_seqno,
        });
    packet->set_len(w.WrittenBytes());
    return packet;
}

// See IEEE Std 802.11-2016, 14.10.9.4.3
static void HandlePreq(const common::MacAddr& preq_transmitter_addr,
                       const common::MacAddr& self_addr, const common::ParsedPreq& preq,
                       uint32_t last_hop_metric, const MacHeaderWriter& mac_header_writer,
                       HwmpState* state, PathTable* path_table, PacketQueue* packets_to_tx) {
    // The spec (IEEE Std 802.11-2016, 14.10.9.4.2) suggests that we entirely
    // throw out the PREQ if it doesn't meet the "acceptance criteria",
    // e.g. if there is no known path to the target. This seems wrong: we could
    // still use the information in the PREQ to update the paths to the transmitter
    // and the originator. Also, the sentence refers to "THE" target address of the
    // PREQ element, whereas in fact a single PREQ element may contain several
    // target addresses.
    auto path_to_originator =
        UpdateForwardingInfo(path_table, state, preq_transmitter_addr, preq.header->originator_addr,
                             preq.header->originator_hwmp_seqno, preq.middle->metric,
                             last_hop_metric, preq.header->hop_count, preq.middle->lifetime);
    // The spec also suggests another case when we need to handle the PREQ:
    // the sequence number in the PREQ matches what we have cached for the target,
    // but we haven't seen the (originator_addr, path_discovery_id) pair yet.
    // It is unclear yet whether this is really necessary. To implement this,
    // we would need to cache (originator_addr, path_disovery_id) pairs, which
    // could be avoided otherwise.
    if (path_to_originator == nullptr) { return; }
    for (auto t : preq.per_target) {
        if (t.target_addr == self_addr) {
            // TODO(gbonik): Also check if we are a proxy of target_addr

            // See IEEE Std 802.11-2016, 14.10.8.3, second bullet point
            state->our_hwmp_seqno = MaxHwmpSeqno(state->our_hwmp_seqno, t.target_hwmp_seqno) + 1;

            if (auto prep = MakeOriginalPrep(mac_header_writer, preq_transmitter_addr, self_addr,
                                             preq, *path_to_originator, *state)) {
                packets_to_tx->Enqueue(std::move(prep));
            }
        }
        // TODO(gbonik): update proxy info if address extension is present (case (a)/(b))
        // TODO(gbonik): reply if target_only=false and we know a path to the target (case (c))
        // TODO: Also update precursors if we decide to store them one day (case (d))
        // TODO(gobnik): reply to proactive (broadcast) PREQs (case (e))
        // TODO(gbonik): propagate the PREQ (case (f))
    }
}

PacketQueue HandleHwmpAction(Span<const uint8_t> elements,
                             const common::MacAddr& action_transmitter_addr,
                             const common::MacAddr& self_addr, uint32_t last_hop_metric,
                             const MacHeaderWriter& mac_header_writer, HwmpState* state,
                             PathTable* path_table) {
    PacketQueue ret;
    for (auto [id, raw_body] : common::ElementSplitter(elements)) {
        switch (id) {
        case element_id::kPreq:
            if (auto preq = common::ParsePreq(raw_body)) {
                HandlePreq(action_transmitter_addr, self_addr, *preq, last_hop_metric,
                           mac_header_writer, state, path_table, &ret);
            }
            break;
        default:
            break;
        }
    }
    return ret;
}

}  // namespace wlan
