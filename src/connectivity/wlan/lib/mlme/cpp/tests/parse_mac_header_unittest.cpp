// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gtest/gtest.h>
#include <wlan/common/parse_mac_header.h>

#include "test_utils.h"

namespace wlan {
namespace common {

TEST(ParseDataFrameHeader, Minimal) {
    // clang-format off
    const uint8_t data[] = {
        0x08, 0x00, // fc: non-qos data, 3-address, no ht ctl
        0x00, 0x00, // duration
        0x11, 0x11, 0x11, 0x11, 0x11, 0x11, // addr1
        0x22, 0x22, 0x22, 0x22, 0x22, 0x22, // addr2
        0x33, 0x33, 0x33, 0x33, 0x33, 0x33, // addr3
        0x00, 0x00, // seq ctl
    };
    // clang-format on
    BufferReader r(data);
    auto parsed = ParseDataFrameHeader(&r);
    ASSERT_TRUE(parsed);
    EXPECT_EQ(0u, r.RemainingBytes());
    EXPECT_EQ(data, reinterpret_cast<const uint8_t*>(parsed->fixed));
    EXPECT_EQ(MacAddr("11:11:11:11:11:11"), parsed->fixed->addr1);
    EXPECT_EQ(nullptr, parsed->addr4);
    EXPECT_EQ(nullptr, parsed->qos_ctrl);
    EXPECT_EQ(nullptr, parsed->ht_ctrl);
}

TEST(ParseDataFrameHeader, Full) {
    // clang-format off
    const uint8_t data[] = {
        0x88, 0x83, // fc: non-qos data, 4-address, ht ctl
        0x00, 0x00, // duration
        0x11, 0x11, 0x11, 0x11, 0x11, 0x11, // addr1
        0x22, 0x22, 0x22, 0x22, 0x22, 0x22, // addr2
        0x33, 0x33, 0x33, 0x33, 0x33, 0x33, // addr3
        0x00, 0x00, // seq ctl
        0x44, 0x44, 0x44, 0x44, 0x44, 0x44, // addr4
        0x55, 0x66, // qos ctl
        0x77, 0x88, 0x99, 0xaa, // ht ctl
    };
    // clang-format on
    BufferReader r(data);
    auto parsed = ParseDataFrameHeader(&r);
    ASSERT_TRUE(parsed);
    EXPECT_EQ(0u, r.RemainingBytes());
    EXPECT_EQ(data, reinterpret_cast<const uint8_t*>(parsed->fixed));
    EXPECT_EQ(MacAddr("11:11:11:11:11:11"), parsed->fixed->addr1);

    ASSERT_NE(nullptr, parsed->addr4);
    EXPECT_EQ(MacAddr("44:44:44:44:44:44"), *parsed->addr4);

    ASSERT_NE(nullptr, parsed->qos_ctrl);
    EXPECT_EQ(0x6655u, parsed->qos_ctrl->val());

    ASSERT_NE(nullptr, parsed->ht_ctrl);
    EXPECT_EQ(0xaa998877u, parsed->ht_ctrl->val());
}

TEST(ParseDataFrameHeader, FixedPartTooShort) {
    // clang-format off
    const uint8_t data[] = {
        0x08, 0x00, // fc: non-qos data, 3-address, no ht ctl
        0x00, 0x00, // duration
        0x11, 0x11, 0x11, 0x11, 0x11, 0x11, // addr1
        0x22, 0x22, 0x22, 0x22, 0x22, 0x22, // addr2
        0x33, 0x33, 0x33, 0x33, 0x33, 0x33, // addr3
        0x00, // one byte missing seq ctl
    };
    // clang-format on
    BufferReader r(data);
    auto parsed = ParseDataFrameHeader(&r);
    ASSERT_FALSE(parsed);
}

TEST(ParseDataFrameHeader, Addr4TooShort) {
    // clang-format off
    const uint8_t data[] = {
        0x88, 0x83, // fc: non-qos data, 4-address, ht ctl
        0x00, 0x00, // duration
        0x11, 0x11, 0x11, 0x11, 0x11, 0x11, // addr1
        0x22, 0x22, 0x22, 0x22, 0x22, 0x22, // addr2
        0x33, 0x33, 0x33, 0x33, 0x33, 0x33, // addr3
        0x00, 0x00, // seq ctl
        0x44, 0x44, 0x44, 0x44, 0x44, // one byte missing from addr4
    };
    // clang-format on
    BufferReader r(data);
    auto parsed = ParseDataFrameHeader(&r);
    ASSERT_FALSE(parsed);
}

TEST(ParseDataFrameHeader, QosControlTooShort) {
    // clang-format off
    const uint8_t data[] = {
        0x88, 0x83, // fc: non-qos data, 4-address, ht ctl
        0x00, 0x00, // duration
        0x11, 0x11, 0x11, 0x11, 0x11, 0x11, // addr1
        0x22, 0x22, 0x22, 0x22, 0x22, 0x22, // addr2
        0x33, 0x33, 0x33, 0x33, 0x33, 0x33, // addr3
        0x00, 0x00, // seq ctl
        0x44, 0x44, 0x44, 0x44, 0x44, 0x44, // addr4
        0x55, // one byte missing from qos ctl
    };
    // clang-format on
    BufferReader r(data);
    auto parsed = ParseDataFrameHeader(&r);
    ASSERT_FALSE(parsed);
}

TEST(ParseDataFrameHeader, HtControlTooShort) {
    // clang-format off
    const uint8_t data[] = {
        0x88, 0x83, // fc: non-qos data, 4-address, ht ctl
        0x00, 0x00, // duration
        0x11, 0x11, 0x11, 0x11, 0x11, 0x11, // addr1
        0x22, 0x22, 0x22, 0x22, 0x22, 0x22, // addr2
        0x33, 0x33, 0x33, 0x33, 0x33, 0x33, // addr3
        0x00, 0x00, // seq ctl
        0x44, 0x44, 0x44, 0x44, 0x44, 0x44, // addr4
        0x55, 0x66, // qos ctl
        0x77, 0x88, 0x99, // one byte missing from ht ctl
    };
    // clang-format on
    BufferReader r(data);
    auto parsed = ParseDataFrameHeader(&r);
    ASSERT_FALSE(parsed);
}

TEST(ParseMeshDataHeader, NoAddrExt) {
    // clang-format off
    const uint8_t data[] = {
        0x88, 0x02, // fc: qos data, 3-address, no ht ctl
        0x00, 0x00, // duration
        0x11, 0x11, 0x11, 0x11, 0x11, 0x11, // addr1
        0x22, 0x22, 0x22, 0x22, 0x22, 0x22, // addr2
        0x33, 0x33, 0x33, 0x33, 0x33, 0x33, // addr3
        0x00, 0x00, // seq ctl
        0x00, 0x01, // qos ctl: mesh control present
        // Mesh control
        0x00, // flags: no addr extension
        0x20, // ttl
        0xaa, 0xbb, 0xcc, 0xdd, // seq
        // LLC header
        0xaa, 0xaa, 0x03, // dsap ssap ctrl
        0x00, 0x00, 0x00, // oui
        0x12, 0x34, // protocol id
    };
    // clang-format on
    BufferReader r(data);
    auto parsed = ParseMeshDataHeader(&r);
    ASSERT_TRUE(parsed);

    EXPECT_EQ(data, reinterpret_cast<const uint8_t*>(parsed->mac_header.fixed));
    EXPECT_NE(nullptr, parsed->mac_header.qos_ctrl);
    EXPECT_EQ(nullptr, parsed->mac_header.ht_ctrl);

    EXPECT_EQ(0xddccbbaau, parsed->mesh_ctrl->seq);
    EXPECT_EQ(0u, parsed->addr_ext.size());
    EXPECT_EQ(0x3412u, parsed->llc->protocol_id_be);
    EXPECT_EQ(0u, r.RemainingBytes());
}

TEST(ParseMeshDataHeader, Addr4Ext) {
    // clang-format off
    const uint8_t data[] = {
        0x88, 0x02, // fc: qos data, 3-address, no ht ctl
        0x00, 0x00, // duration
        0x11, 0x11, 0x11, 0x11, 0x11, 0x11, // addr1
        0x22, 0x22, 0x22, 0x22, 0x22, 0x22, // addr2
        0x33, 0x33, 0x33, 0x33, 0x33, 0x33, // addr3
        0x00, 0x00, // seq ctl
        0x00, 0x01, // qos ctl: mesh control present
        // Mesh control
        0x01, // flags: addr4 extension
        0x20, // ttl
        0xaa, 0xbb, 0xcc, 0xdd, // seq
        0x44, 0x44, 0x44, 0x44, 0x44, 0x44, // addr4 extension
        // LLC header
        0xaa, 0xaa, 0x03, // dsap ssap ctrl
        0x00, 0x00, 0x00, // oui
        0x12, 0x34, // protocol id
    };
    // clang-format on
    BufferReader r(data);
    auto parsed = ParseMeshDataHeader(&r);
    ASSERT_TRUE(parsed);

    EXPECT_EQ(data, reinterpret_cast<const uint8_t*>(parsed->mac_header.fixed));
    EXPECT_NE(nullptr, parsed->mac_header.qos_ctrl);
    EXPECT_EQ(nullptr, parsed->mac_header.ht_ctrl);

    EXPECT_RANGES_EQ(std::vector<MacAddr>({MacAddr("44:44:44:44:44:44")}), parsed->addr_ext);
    EXPECT_EQ(0xddccbbaau, parsed->mesh_ctrl->seq);
    EXPECT_EQ(0x3412u, parsed->llc->protocol_id_be);
    EXPECT_EQ(0u, r.RemainingBytes());
}

TEST(ParseMeshDataHeader, Addr56Ext) {
    // clang-format off
    const uint8_t data[] = {
        0x88, 0x03, // fc: qos data, 4-address, no ht ctl
        0x00, 0x00, // duration
        0x11, 0x11, 0x11, 0x11, 0x11, 0x11, // addr1
        0x22, 0x22, 0x22, 0x22, 0x22, 0x22, // addr2
        0x33, 0x33, 0x33, 0x33, 0x33, 0x33, // addr3
        0x00, 0x00, // seq ctl
        0x44, 0x44, 0x44, 0x44, 0x44, 0x44, // addr4
        0x00, 0x01, // qos ctl: mesh control present
        // Mesh control
        0x02, // flags: addr56 extension
        0x20, // ttl
        0xaa, 0xbb, 0xcc, 0xdd, // seq
        0x55, 0x55, 0x55, 0x55, 0x55, 0x55, // addr5
        0x66, 0x66, 0x66, 0x66, 0x66, 0x66, // addr6
        // LLC header
        0xaa, 0xaa, 0x03, // dsap ssap ctrl
        0x00, 0x00, 0x00, // oui
        0x12, 0x34, // protocol id
    };
    // clang-format on
    BufferReader r(data);
    auto parsed = ParseMeshDataHeader(&r);
    ASSERT_TRUE(parsed);

    EXPECT_EQ(data, reinterpret_cast<const uint8_t*>(parsed->mac_header.fixed));
    EXPECT_NE(nullptr, parsed->mac_header.qos_ctrl);
    EXPECT_EQ(nullptr, parsed->mac_header.ht_ctrl);

    EXPECT_RANGES_EQ(
        std::vector<MacAddr>({MacAddr("55:55:55:55:55:55"), MacAddr("66:66:66:66:66:66")}),
        parsed->addr_ext);
    EXPECT_EQ(0xddccbbaau, parsed->mesh_ctrl->seq);
    EXPECT_EQ(0x3412u, parsed->llc->protocol_id_be);
    EXPECT_EQ(0u, r.RemainingBytes());
}

TEST(ParseMeshDataHeader, TooShort_MacHeader) {
    const uint8_t data[] = {0x88, 0x02};
    BufferReader r(data);
    auto parsed = ParseMeshDataHeader(&r);
    ASSERT_FALSE(parsed);
}

TEST(ParseMeshDataHeader, TooShort_MeshControl) {
    // clang-format off
    const uint8_t data[] = {
        0x88, 0x02, // fc: qos data, 3-address, no ht ctl
        0x00, 0x00, // duration
        0x11, 0x11, 0x11, 0x11, 0x11, 0x11, // addr1
        0x22, 0x22, 0x22, 0x22, 0x22, 0x22, // addr2
        0x33, 0x33, 0x33, 0x33, 0x33, 0x33, // addr3
        0x00, 0x00, // seq ctl
        0x00, 0x01, // qos ctl: mesh control present
        // Mesh control
        0x00, // flags: no addr extension
        0x20, // ttl
        0xaa, 0xbb, 0xcc, // one byte missing from seq
    };
    // clang-format on
    BufferReader r(data);
    auto parsed = ParseMeshDataHeader(&r);
    ASSERT_FALSE(parsed);
}

TEST(ParseMeshDataHeader, TooShort_AddrExt) {
    // clang-format off
    const uint8_t data[] = {
        0x88, 0x03, // fc: qos data, 4-address, no ht ctl
        0x00, 0x00, // duration
        0x11, 0x11, 0x11, 0x11, 0x11, 0x11, // addr1
        0x22, 0x22, 0x22, 0x22, 0x22, 0x22, // addr2
        0x33, 0x33, 0x33, 0x33, 0x33, 0x33, // addr3
        0x00, 0x00, // seq ctl
        0x44, 0x44, 0x44, 0x44, 0x44, 0x44, // addr4
        0x00, 0x01, // qos ctl: mesh control present
        // Mesh control
        0x02, // flags: addr56 extension
        0x20, // ttl
        0xaa, 0xbb, 0xcc, 0xdd, // seq
        0x55, 0x55, 0x55, 0x55, 0x55, 0x55, // addr5
        0x66, 0x66, 0x66, 0x66, 0x66, // one byte missing from addr6
    };
    // clang-format on
    BufferReader r(data);
    auto parsed = ParseMeshDataHeader(&r);
    ASSERT_FALSE(parsed);
}

TEST(ParseMeshDataHeader, TooShort_Llc) {
    // clang-format off
    const uint8_t data[] = {
        0x88, 0x02, // fc: qos data, 3-address, no ht ctl
        0x00, 0x00, // duration
        0x11, 0x11, 0x11, 0x11, 0x11, 0x11, // addr1
        0x22, 0x22, 0x22, 0x22, 0x22, 0x22, // addr2
        0x33, 0x33, 0x33, 0x33, 0x33, 0x33, // addr3
        0x00, 0x00, // seq ctl
        0x00, 0x01, // qos ctl: mesh control present
        // Mesh control
        0x00, // flags: no addr extension
        0x20, // ttl
        0xaa, 0xbb, 0xcc, 0xdd, // seq
        // LLC header
        0xaa, 0xaa, 0x03, // dsap ssap ctrl
        0x00, 0x00, 0x00, // oui
        0x12, // one byte missing from protocol id
    };
    // clang-format on
    BufferReader r(data);
    auto parsed = ParseMeshDataHeader(&r);
    ASSERT_FALSE(parsed);
}

TEST(ParseMeshDataHeader, MissingQosBit) {
    // clang-format off
    const uint8_t data[] = {
        0x08, 0x02, // fc: non-qos data, 3-address, no ht ctl
        0x00, 0x00, // duration
        0x11, 0x11, 0x11, 0x11, 0x11, 0x11, // addr1
        0x22, 0x22, 0x22, 0x22, 0x22, 0x22, // addr2
        0x33, 0x33, 0x33, 0x33, 0x33, 0x33, // addr3
        0x00, 0x00, // seq ctl
        0x00, 0x01, // qos ctl: mesh control present
        // Mesh control
        0x00, // flags: no addr extension
        0x20, // ttl
        0xaa, 0xbb, 0xcc, 0xdd, // seq
        // LLC header
        0xaa, 0xaa, 0x03, // dsap ssap ctrl
        0x00, 0x00, 0x00, // oui
        0x12, 0x34, // protocol id
    };
    // clang-format on
    BufferReader r(data);
    auto parsed = ParseMeshDataHeader(&r);
    ASSERT_FALSE(parsed);
}

TEST(ParseMeshDataHeader, MissingMeshControlPresentBit) {
    // clang-format off
    const uint8_t data[] = {
        0x88, 0x02, // fc: non-qos data, 3-address, no ht ctl
        0x00, 0x00, // duration
        0x11, 0x11, 0x11, 0x11, 0x11, 0x11, // addr1
        0x22, 0x22, 0x22, 0x22, 0x22, 0x22, // addr2
        0x33, 0x33, 0x33, 0x33, 0x33, 0x33, // addr3
        0x00, 0x00, // seq ctl
        0x00, 0x00, // qos ctl: no mesh control
        // Mesh control
        0x00, // flags: no addr extension
        0x20, // ttl
        0xaa, 0xbb, 0xcc, 0xdd, // seq
        // LLC header
        0xaa, 0xaa, 0x03, // dsap ssap ctrl
        0x00, 0x00, 0x00, // oui
        0x12, 0x34, // protocol id
    };
    // clang-format on
    BufferReader r(data);
    auto parsed = ParseMeshDataHeader(&r);
    ASSERT_FALSE(parsed);
}

TEST(ParseMeshDataHeader, InvalidAddrExt) {
    // clang-format off
    const uint8_t data[] = {
        0x88, 0x02, // fc: non-qos data, 3-address, no ht ctl
        0x00, 0x00, // duration
        0x11, 0x11, 0x11, 0x11, 0x11, 0x11, // addr1
        0x22, 0x22, 0x22, 0x22, 0x22, 0x22, // addr2
        0x33, 0x33, 0x33, 0x33, 0x33, 0x33, // addr3
        0x00, 0x00, // seq ctl
        0x00, 0x01, // qos ctl: mesh control present
        // Mesh control
        0x03, // flags: invalid addr extension
        0x20, // ttl
        0xaa, 0xbb, 0xcc, 0xdd, // seq
        // LLC header
        0xaa, 0xaa, 0x03, // dsap ssap ctrl
        0x00, 0x00, 0x00, // oui
        0x12, 0x34, // protocol id
        // A bunch of bytes to make sure we don't fail because of a length check
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    };
    // clang-format on
    BufferReader r(data);
    auto parsed = ParseMeshDataHeader(&r);
    ASSERT_FALSE(parsed);
}

}  // namespace common
}  // namespace wlan
