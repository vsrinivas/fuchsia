// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <wlan/mlme/sequence.h>

#include <fbl/type_support.h>
#include <fbl/unique_ptr.h>
#include <lib/zx/time.h>
#include <wlan/common/bitfield.h>
#include <wlan/common/mac_frame.h>
#include <wlan/common/macaddr.h>
#include <wlan/mlme/packet.h>
#include <zircon/compiler.h>
#include <zircon/types.h>

#include <cstdint>

namespace wlan {

namespace {
template <unsigned int N, typename T> T align(T t) {
    static_assert(N > 1 && !(N & (N - 1)), "alignment must be with a power of 2");
    return (t + (N - 1)) & ~(N - 1);
}
}  // namespace

using NilHeader = uint8_t[0];
struct UnknownBody {
    uint8_t data[];
} __PACKED;

// TODO(hahnr): Remove once frame owns Packet.
template <typename Header, typename Body = UnknownBody> class Frame {
   public:
    explicit Frame(fbl::unique_ptr<Packet> pkt) : pkt_(fbl::move(pkt)) {
        ZX_DEBUG_ASSERT(pkt_ != nullptr);
    }

    Frame(size_t offset, fbl::unique_ptr<Packet> pkt) : pkt_(fbl::move(pkt)) {
        ZX_DEBUG_ASSERT(pkt_ != nullptr);
        data_offset_ = offset;
    }

    Frame() : pkt_(nullptr) {}

    const Header* hdr() const {
        ZX_DEBUG_ASSERT(!IsTaken());
        if (IsTaken()) { return nullptr; }

        auto hdr = pkt_->field<Header>(data_offset_);
        ZX_DEBUG_ASSERT(hdr != nullptr);
        return hdr;
    }

    Header* hdr() {
        ZX_DEBUG_ASSERT(!IsTaken());
        if (IsTaken()) { return nullptr; }

        auto hdr = pkt_->mut_field<Header>(data_offset_);
        ZX_DEBUG_ASSERT(hdr != nullptr);
        return hdr;
    }

    const Body* body() const {
        ZX_DEBUG_ASSERT(!IsTaken());
        if (IsTaken()) { return nullptr; }

        auto body = pkt_->field<Body>(body_offset<Header>());
        ZX_DEBUG_ASSERT(body != nullptr);
        return body;
    }

    Body* body() {
        ZX_DEBUG_ASSERT(!IsTaken());
        if (IsTaken()) { return nullptr; }

        auto body = pkt_->mut_field<Body>(body_offset<Header>());
        ZX_DEBUG_ASSERT(body != nullptr);
        return body;
    }

    size_t body_len() const {
        ZX_DEBUG_ASSERT(!IsTaken());
        if (IsTaken()) { return 0; }

        size_t offset = body_offset<Header>();
        ZX_DEBUG_ASSERT(offset <= pkt_->len());
        if (pkt_->len() < offset) { return 0; }
        return pkt_->len() - offset;
    }

    zx_status_t set_body_len(size_t len) {
        ZX_DEBUG_ASSERT(!IsTaken());
        if (IsTaken()) { return ZX_ERR_NO_RESOURCES; }
        ZX_DEBUG_ASSERT(len <= pkt_->len());

        return pkt_->set_len(body_offset<Header>() + len);
    }

    size_t len() const {
        ZX_DEBUG_ASSERT(!IsTaken());
        if (IsTaken()) { return 0; }

        if (pkt_->len() < data_offset_) { return 0; }
        return pkt_->len() - data_offset_;
    }

    bool has_rx_info() const {
        ZX_DEBUG_ASSERT(!IsTaken());
        // Only Data, Mgmt and Ctrl frames can carry an rx_info field.
        if (!CanCarryRxInfo<Header>()) { return false; }
        if (IsTaken()) { return false; }

        return pkt_->has_ctrl_data<wlan_rx_info_t>();
    }

    const wlan_rx_info_t* rx_info() const {
        // Only Data, Mgmt and Ctrl frames can carry an rx_info field.
        // Throw when trying to access this data with any other frame type.
        static_assert(CanCarryRxInfo<Header>(), "only MAC frame can carry rx_info");

        ZX_DEBUG_ASSERT(has_rx_info());
        if (IsTaken()) { return nullptr; }

        return pkt_->ctrl_data<wlan_rx_info_t>();
    }

    zx_status_t FillTxInfo() {
        static_assert(CanCarryTxInfo<Header>(), "only MAC frame can carry tx_info");

        wlan_tx_info_t txinfo = {
            // Outgoing management frame
            .tx_flags = 0x0,
            .valid_fields = WLAN_TX_INFO_VALID_PHY | WLAN_TX_INFO_VALID_CHAN_WIDTH,
            .phy = WLAN_PHY_OFDM,  // Always
            .cbw = CBW20,          // Use CBW20 always
        };

        // TODO(porce): Imeplement rate selection.
        auto fc = pkt_->field<FrameControl>(0);
        switch (fc->subtype()) {
        default:
            txinfo.valid_fields |= WLAN_TX_INFO_VALID_MCS;
            // txinfo.data_rate = 12;  // 6 Mbps, one of the basic rates.
            txinfo.mcs = 0x3;  // TODO(NET-645): Choose an optimal MCS
            break;
        }

        pkt_->CopyCtrlFrom(txinfo);
        return ZX_OK;
    }

    bool HasValidLen() const {
        if (IsTaken()) { return false; }

        if (pkt_->field<Header>(data_offset_) == nullptr) { return false; }
        return pkt_->field<Body>(body_offset<Header>()) != nullptr;
    }

    // Consumes the frame and returns its body as a typed frame.
    // By default, the current frame's Body will be the next frame's Header, and
    // a `uint8_t[]` will be used for the next frame's body.
    // The current frame will be considered `taken` after this call.
    template <typename NextH = Body, typename NextB = UnknownBody>
    Frame<NextH, NextB> NextFrame() {
        size_t offset = body_offset<Header>();
        return Frame<NextH, NextB>(offset, take());
    }

    // Returns the Frame's underlying Packet. The Frame will no longer own the Packet and
    // will be `empty` from that moment on and should no longer be used.
    fbl::unique_ptr<Packet> take() {
        ZX_DEBUG_ASSERT(!IsTaken());
        return fbl::move(pkt_);
    }

    // `true` if the frame was 'taken', `false` otherwise.
    bool IsTaken() const { return pkt_ == nullptr; }

   private:
    template <typename T> static constexpr bool CanCarryRxInfo() {
        constexpr bool is_data_frame = std::is_same<T, DataFrameHeader>::value;
        constexpr bool is_mgmt_frame = std::is_same<T, MgmtFrameHeader>::value;
        constexpr bool is_ctrl_frame = std::is_base_of<CtrlFrameIdentifier, T>::value;
        return is_data_frame || is_mgmt_frame || is_ctrl_frame;
    }

    template <typename T> static constexpr bool CanCarryTxInfo() { return CanCarryRxInfo<T>(); }

    // If the frame carries wlan_rx_info_t, it might use padding. In that case, check for, and
    // account for padding when computing the body's offset.
    template <typename H>
    typename std::enable_if<CanCarryRxInfo<H>(), size_t>::type body_offset() const {
        // TODO(hahnr): Similar to wlan_rx_info_t, we might have to take additional padding for
        // the tx path into account.

        size_t offset = hdr()->len();
        if (has_rx_info() && rx_info()->rx_flags & WLAN_RX_INFO_FLAGS_FRAME_BODY_PADDING_4) {
            offset = align<4>(offset);
        }
        return data_offset_ + offset;
    }

    // If the frame cannot carry wlan_rx_info_t there won't be padding specified and the body's
    // offset can easily be computed.
    template <typename H>
    typename std::enable_if<!CanCarryRxInfo<H>(), size_t>::type body_offset() const {
        return data_offset_ + hdr()->len();
    }

    size_t data_offset_ = 0;
    fbl::unique_ptr<Packet> pkt_;
};

// Frame which contains a known header but unknown payload.
using EthFrame = Frame<EthernetII>;
template <typename T> using MgmtFrame = Frame<MgmtFrameHeader, T>;
template <typename T> using CtrlFrame = Frame<T, NilHeader>;
template <typename T> using DataFrame = Frame<DataFrameHeader, T>;

// TODO(hahnr): This isn't a great location for these definitions.
using aid_t = size_t;
static constexpr aid_t kGroupAdressedAid = 0;
static constexpr aid_t kMaxBssClients = 2008;
static constexpr aid_t kUnknownAid = kMaxBssClients + 1;

template <typename Body>
zx_status_t BuildMgmtFrame(MgmtFrame<Body>* frame, size_t body_payload_len = 0,
                           bool has_ht_ctrl = false);

zx_status_t FillTxInfo(fbl::unique_ptr<Packet>* packet, const MgmtFrameHeader& hdr);

seq_t NextSeqNo(const MgmtFrameHeader& hdr, Sequence* seq);
seq_t NextSeqNo(const MgmtFrameHeader& hdr, uint8_t aci, Sequence* seq);
seq_t NextSeqNo(const DataFrameHeader& hdr, Sequence* seq);

void SetSeqNo(MgmtFrameHeader* hdr, Sequence* seq);
void SetSeqNo(MgmtFrameHeader* hdr, uint8_t aci, Sequence* seq);
void SetSeqNo(DataFrameHeader* hdr, Sequence* seq);
}  // namespace wlan
