// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <wlan/common/mac_frame.h>
#include <wlan/common/action_frame.h>
#include <wlan/mlme/packet.h>
#include <wlan/protocol/mac.h>

#include <zircon/types.h>

namespace wlan {

struct UnknownBody : public EmptyHdr {
    uint8_t data[];
} __PACKED;

template <typename H>
struct is_mac_hdr {
    static constexpr bool value = std::is_same<H, MgmtFrameHeader>::value ||
                                  std::is_same<H, DataFrameHeader>::value ||
                                  std::is_same<H, CtrlFrameHdr>::value;
};

namespace internal {

template <unsigned int N, typename T> T align(T t) {
    static_assert(N > 1 && !(N & (N - 1)), "alignment must be with a power of 2");
    return (t + (N - 1)) & ~(N - 1);
}

template <typename H> bool is_valid_mac_hdr(const uint8_t* buf, size_t len) {
    if (len < sizeof(FrameControl)) { return false; }

    auto fc = reinterpret_cast<const FrameControl*>(buf);
    return fc->type() == H::Type();
}

template <typename H, typename B> struct FrameTypeValidator {};

template <typename B> struct MacSubtypeValidator {
    static bool is_valid(const FrameControl& fc) { return fc.subtype() == B::Subtype(); }
};

template <> struct MacSubtypeValidator<UnknownBody> {
    static bool is_valid(const FrameControl& fc) { return true; }
};

template <> struct MacSubtypeValidator<NullDataHdr> {
    static bool is_valid(const FrameControl& fc) {
        return fc.subtype() == DataSubtype::kNull || fc.subtype() == DataSubtype::kQosnull;
    }
};

template <> struct MacSubtypeValidator<LlcHeader> {
    static bool is_valid(const FrameControl& fc) {
        return fc.subtype() == DataSubtype::kDataSubtype || fc.subtype() == DataSubtype::kQosdata;
    }
};

template <typename H, typename B> bool is_valid_mac_frame(const uint8_t* buf, size_t len) {
    if (len < sizeof(FrameControl)) { return false; }

    auto fc = reinterpret_cast<const FrameControl*>(buf);
    return fc->type() == H::Type() && MacSubtypeValidator<B>::is_valid(*fc);
}

template<typename B>
struct FrameTypeValidator<MgmtFrameHeader, B> {
    static bool is_valid(const uint8_t* buf, size_t len) {
        return is_valid_mac_frame<MgmtFrameHeader, B>(buf, len);
    }
};

template<typename B>
struct FrameTypeValidator<DataFrameHeader, B> {
    static bool is_valid(const uint8_t* buf, size_t len) {
        return is_valid_mac_frame<DataFrameHeader, B>(buf, len);
    }
};

template<typename B>
struct FrameTypeValidator<CtrlFrameHdr, B> {
    static bool is_valid(const uint8_t* buf, size_t len) {
        return is_valid_mac_frame<CtrlFrameHdr, B>(buf, len);
    }
};

template<typename B>
struct FrameTypeValidator<ActionFrame, B> {
    static bool is_valid(const uint8_t* buf, size_t len) {
        if (len < sizeof(ActionFrame)) { return false; }

        auto hdr = reinterpret_cast<const ActionFrame*>(buf);
        return hdr->category == B::ActionCategory();
    }
};

template<typename B>
struct FrameTypeValidator<ActionFrameBlockAck, B> {
    static bool is_valid(const uint8_t* buf, size_t len) {
        if (len < sizeof(ActionFrameBlockAck)) { return false; }

        auto hdr = reinterpret_cast<const ActionFrameBlockAck*>(buf);
        return hdr->action == B::BlockAckAction();
    }
};

}  // namespace internal

typedef size_t(*add_padding_func)(size_t);

template<typename H>
add_padding_func get_packet_padding_func(const Packet* pkt) {
    auto rx = pkt->ctrl_data<wlan_rx_info_t>();
    if (is_mac_hdr<H>::value && rx != nullptr &&
        rx->rx_flags & WLAN_RX_INFO_FLAGS_FRAME_BODY_PADDING_4) {
        return internal::align<4>;
    }
    return [](size_t v) { return v; };
}

// Check if the given buffer is long enough to hold a header of type H.
// Note: The expected length of the header can be variable and depends on the content of the buffer.
template<typename H>
bool is_valid_hdr_length(const uint8_t* buf, size_t len) {
    if (buf == nullptr) { return false; }

    if (std::is_base_of<EmptyHdr, H>::value) { return true; }

    if (len < sizeof(H)) { return false; }
    auto hdr = reinterpret_cast<const H*>(buf);
    ZX_DEBUG_ASSERT(hdr->len() >= sizeof(H));
    return len >= hdr->len();
}

template <typename H, typename B>
bool is_valid_frame_length(const uint8_t* buf, size_t len, add_padding_func padding) {
    if (!is_valid_hdr_length<H>(buf, len)) { return false; }

    ZX_DEBUG_ASSERT(len >= sizeof(H));
    auto hdr = reinterpret_cast<const H*>(buf);
    size_t body_offset = padding(hdr->len());
    if (body_offset > len) { return false; }

    return is_valid_hdr_length<B>(buf + body_offset, len - body_offset);
}

template <typename H, typename B> bool is_valid_frame_length(const Packet* pkt, size_t offset) {
    if (offset > pkt->len()) { return false; }

    const uint8_t* buf = pkt->data() + offset;
    size_t len = pkt->len() - offset;
    auto padding = get_packet_padding_func<H>(pkt);
    return is_valid_frame_length<H, B>(buf, len, padding);
}

template<typename H, typename B>
bool is_valid_frame_type(const uint8_t* buf, size_t len) {
    return internal::FrameTypeValidator<H, B>::is_valid(buf, len);
}

template <typename H, typename B> bool is_valid_frame_type(const Packet* pkt, size_t offset) {
    if (offset > pkt->len()) { return false; }

    const uint8_t* buf = pkt->data() + offset;
    size_t len = pkt->len() - offset;
    auto padding = get_packet_padding_func<H>(pkt);
    return is_valid_frame_type<H, B>(buf, len);
}

}  // namespace wlan
