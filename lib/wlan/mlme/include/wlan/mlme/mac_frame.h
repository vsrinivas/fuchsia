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
using UnknownBody = uint8_t;

// TODO(hahnr): Remove once frame owns Packet.
template <typename Header, typename Body> class ImmutableFrame {
   public:
    ImmutableFrame(fbl::unique_ptr<Packet> pkt) : pkt_(fbl::move(pkt)) {
        ZX_DEBUG_ASSERT(pkt_ != nullptr);
    }

    const Header* hdr() const {
        ZX_DEBUG_ASSERT(!IsTaken());
        if (IsTaken()) { return nullptr; }

        auto hdr = pkt_->field<Header>(0);
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

    size_t body_len() const {
        ZX_DEBUG_ASSERT(!IsTaken());
        if (IsTaken()) { return 0; }

        size_t offset = body_offset<Header>();
        ZX_DEBUG_ASSERT(offset <= pkt_->len());
        if (pkt_->len() < offset) { return 0; }
        return pkt_->len() - offset;
    }

    size_t len() const {
        ZX_DEBUG_ASSERT(!IsTaken());
        if (IsTaken()) { return 0; }

        return pkt_->len();
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
        static_assert(CanCarryRxInfo<Header>(), "frame does not carry wlan_rx_info_t");

        ZX_DEBUG_ASSERT(has_rx_info());
        if (IsTaken()) { return nullptr; }

        return pkt_->ctrl_data<wlan_rx_info_t>();
    }

    bool HasValidLen() const {
        ZX_DEBUG_ASSERT(!IsTaken());
        if (IsTaken()) { return false; }

        if (pkt_->field<Header>(0) == nullptr) { return false; }
        return pkt_->field<Body>(body_offset<Header>()) != nullptr;
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

    // If the frame can carry wlan_rx_info_t, it might use padding. In that case, check for, and
    // account for padding when computing the body's offset.
    template <typename H>
    typename std::enable_if<CanCarryRxInfo<H>(), size_t>::type body_offset() const {
        size_t offset = hdr()->len();
        if (has_rx_info() && rx_info()->rx_flags & WLAN_RX_INFO_FLAGS_FRAME_BODY_PADDING_4) {
            offset = align<4>(offset);
        }
        return offset;
    }

    // If the frame cannot carry wlan_rx_info_t there won't be padding specified and the body's
    // offset can easily be computed.
    template <typename H>
    typename std::enable_if<!CanCarryRxInfo<H>(), size_t>::type body_offset() const {
        return hdr()->len();
    }

    fbl::unique_ptr<Packet> pkt_;
};

template <typename Header, typename Body> class Frame {
   public:
    Frame(Header* hdr, Body* body, size_t body_len) : hdr_(hdr), body_(body), body_len_(body_len) {}

    Header* hdr() { return hdr_; }
    Body* body() { return body_; }
    size_t body_len() const { return body_len_; }
    void set_body_len(size_t body_len) { body_len_ = body_len; }

   private:
    Header* hdr_;
    Body* body_;
    size_t body_len_;
};

// Frame which contains a known header but unknown payload.
template <typename Header> using BaseFrame = Frame<Header, NilHeader>;
template <typename Header> using ImmutableBaseFrame = ImmutableFrame<Header, NilHeader>;

template <typename T> using MgmtFrame = Frame<MgmtFrameHeader, T>;
template <typename T> using ImmutableMgmtFrame = ImmutableFrame<MgmtFrameHeader, T>;

template <typename T> using CtrlFrame = Frame<T, NilHeader>;
template <typename T> using ImmutableCtrlFrame = ImmutableFrame<T, NilHeader>;

template <typename T> using DataFrame = Frame<DataFrameHeader, T>;
template <typename T> using ImmutableDataFrame = ImmutableFrame<DataFrameHeader, T>;

// TODO(hahnr): This isn't a great location for these definitions.
using aid_t = size_t;
static constexpr aid_t kGroupAdressedAid = 0;
static constexpr aid_t kMaxBssClients = 2008;
static constexpr aid_t kUnknownAid = kMaxBssClients + 1;

template <typename Body>
MgmtFrame<Body> BuildMgmtFrame(fbl::unique_ptr<Packet>* packet, size_t body_payload_len = 0,
                               bool has_ht_ctrl = false);

zx_status_t FillTxInfo(fbl::unique_ptr<Packet>* packet, const MgmtFrameHeader& hdr);

seq_t NextSeqNo(const MgmtFrameHeader& hdr, Sequence* seq);
seq_t NextSeqNo(const MgmtFrameHeader& hdr, uint8_t aci, Sequence* seq);
seq_t NextSeqNo(const DataFrameHeader& hdr, Sequence* seq);

void SetSeqNo(MgmtFrameHeader* hdr, Sequence* seq);
void SetSeqNo(MgmtFrameHeader* hdr, uint8_t aci, Sequence* seq);
void SetSeqNo(DataFrameHeader* hdr, Sequence* seq);
}  // namespace wlan
