// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fbl/algorithm.h>
#include <unittest/unittest.h>
#include <zircon/driver/binding.h>

#include "binding-internal.h"

namespace {

class MockDevice {
public:
    MockDevice(MockDevice* parent, const zx_device_prop_t* props, size_t props_count,
               uint32_t protocol_id)
            : parent_(parent), protocol_id_(protocol_id) {
        fbl::Array<zx_device_prop_t> props_array(new zx_device_prop_t[props_count], props_count);
        memcpy(props_array.get(), props, sizeof(props[0]) * props_count);
        props_ = std::move(props_array);
        for (const auto& prop : props_) {
            if (prop.id >= BIND_TOPO_START && prop.id <= BIND_TOPO_END) {
                topo_prop_ = &prop;
                break;
            }
        }
    }

    MockDevice& operator=(const MockDevice&) = delete;
    MockDevice& operator=(MockDevice&&) = delete;
    MockDevice(const MockDevice&) = delete;
    MockDevice(MockDevice&&) = delete;

    MockDevice* parent() { return parent_; }
    const fbl::Array<const zx_device_prop_t>& props() const { return props_; }
    const zx_device_prop_t* topo_prop() const { return topo_prop_; }
    uint32_t protocol_id() const { return protocol_id_; }

private:
    MockDevice* parent_ = nullptr;
    fbl::Array<const zx_device_prop_t> props_;
    const zx_device_prop_t* topo_prop_ = nullptr;
    uint32_t protocol_id_ = 0;
};

using devmgr::internal::DeviceComponentPartDescriptor;
using devmgr::internal::Match;
using devmgr::internal::MatchParts;

bool composite_match_zero_parts() {
    BEGIN_TEST;

    MockDevice device(nullptr, nullptr, 0, 0);

    Match match = MatchParts(&device, nullptr, 0);
    ASSERT_EQ(match, Match::None);

    END_TEST;
}

bool composite_match_one_part_one_device_fail() {
    BEGIN_TEST;

    constexpr uint32_t kProtocolId = 1;
    MockDevice device(nullptr, nullptr, 0, kProtocolId);

    zx_bind_inst_t part1[] = {
        BI_MATCH_IF(EQ, BIND_PROTOCOL, 2),
    };
    DeviceComponentPartDescriptor parts[] = {
        { fbl::count_of(part1), part1 },
    };

    Match match = MatchParts(&device, parts, fbl::count_of(parts));
    ASSERT_EQ(match, Match::None);

    END_TEST;
}

bool composite_match_one_part_one_device_succeed() {
    BEGIN_TEST;

    constexpr uint32_t kProtocolId = 1;
    MockDevice device(nullptr, nullptr, 0, kProtocolId);

    zx_bind_inst_t part1[] = {
        BI_MATCH_IF(EQ, BIND_PROTOCOL, 1),
    };
    DeviceComponentPartDescriptor parts[] = {
        { fbl::count_of(part1), part1 },
    };

    Match match = MatchParts(&device, parts, fbl::count_of(parts));
    ASSERT_EQ(match, Match::One);

    END_TEST;
}

bool composite_match_two_part_one_device() {
    BEGIN_TEST;

    constexpr uint32_t kProtocolId = 1;
    MockDevice device(nullptr, nullptr, 0, kProtocolId);

    // Both parts can match the only device, but only one part is allowed to
    // match to a given device.
    zx_bind_inst_t part[] = {
        BI_MATCH_IF(EQ, BIND_PROTOCOL, 1),
    };
    DeviceComponentPartDescriptor parts[] = {
        { fbl::count_of(part), part },
        { fbl::count_of(part), part },
    };

    Match match = MatchParts(&device, parts, fbl::count_of(parts));
    ASSERT_EQ(match, Match::None);

    END_TEST;
}

bool composite_match_zero_parts_two_devices() {
    BEGIN_TEST;

    MockDevice devices[] = {
        MockDevice(nullptr, nullptr, 0, 0),
        MockDevice(&devices[0], nullptr, 0, 0),
    };

    Match match = MatchParts(&devices[fbl::count_of(devices) - 1], nullptr, 0);
    ASSERT_EQ(match, Match::None);

    END_TEST;
}

bool composite_match_one_part_two_devices() {
    BEGIN_TEST;

    constexpr uint32_t kProtocolId = 1;
    MockDevice devices[] = {
        MockDevice(nullptr, nullptr, 0, kProtocolId),
        MockDevice(&devices[0], nullptr, 0, kProtocolId),
    };

    // This program matches both devices
    zx_bind_inst_t part[] = {
        BI_MATCH_IF(EQ, BIND_PROTOCOL, kProtocolId),
    };
    DeviceComponentPartDescriptor parts[] = {
        { fbl::count_of(part), part },
    };

    Match match = MatchParts(&devices[fbl::count_of(devices) - 1], parts, fbl::count_of(parts));
    ASSERT_EQ(match, Match::None);

    END_TEST;
}

bool composite_match_two_parts_two_devices_fail() {
    BEGIN_TEST;

    constexpr uint32_t kProtocolId1 = 1;
    constexpr uint32_t kProtocolId2 = 2;
    MockDevice devices[] = {
        MockDevice(nullptr, nullptr, 0, kProtocolId1),
        MockDevice(&devices[0], nullptr, 0, kProtocolId2),
    };

    zx_bind_inst_t part1[] = {
        BI_MATCH_IF(EQ, BIND_PROTOCOL, kProtocolId1),
    };
    zx_bind_inst_t part2[] = {
        BI_MATCH_IF(EQ, BIND_PROTOCOL, kProtocolId2),
    };
    DeviceComponentPartDescriptor parts[] = {
        // First entry should match the root, but this rule matches leaf
        { fbl::count_of(part2), part2 },
        // Last entry should match the leaf, but this rule matches root
        { fbl::count_of(part1), part1 },
    };

    Match match = MatchParts(&devices[fbl::count_of(devices) - 1], parts, fbl::count_of(parts));
    ASSERT_EQ(match, Match::None);

    END_TEST;
}

bool composite_match_two_parts_two_devices_succeed() {
    BEGIN_TEST;

    constexpr uint32_t kProtocolId1 = 1;
    constexpr uint32_t kProtocolId2 = 2;
    MockDevice devices[] = {
        MockDevice(nullptr, nullptr, 0, kProtocolId1),
        MockDevice(&devices[0], nullptr, 0, kProtocolId2),
    };

    zx_bind_inst_t part1[] = {
        BI_MATCH_IF(EQ, BIND_PROTOCOL, kProtocolId1),
    };
    zx_bind_inst_t part2[] = {
        BI_MATCH_IF(EQ, BIND_PROTOCOL, kProtocolId2),
    };
    DeviceComponentPartDescriptor parts[] = {
        { fbl::count_of(part1), part1 },
        { fbl::count_of(part2), part2 },
    };

    Match match = MatchParts(&devices[fbl::count_of(devices) - 1], parts, fbl::count_of(parts));
    ASSERT_EQ(match, Match::One);

    END_TEST;
}

bool composite_match_three_parts_two_devices() {
    BEGIN_TEST;

    constexpr uint32_t kProtocolId1 = 1;
    constexpr uint32_t kProtocolId2 = 2;
    MockDevice devices[] = {
        MockDevice(nullptr, nullptr, 0, kProtocolId1),
        MockDevice(&devices[0], nullptr, 0, kProtocolId2),
    };

    zx_bind_inst_t part1[] = {
        BI_MATCH_IF(EQ, BIND_PROTOCOL, kProtocolId1),
    };
    zx_bind_inst_t part2[] = {
        BI_MATCH_IF(EQ, BIND_PROTOCOL, kProtocolId2),
    };
    DeviceComponentPartDescriptor parts[] = {
        { fbl::count_of(part1), part1 },
        { fbl::count_of(part2), part2 },
        { fbl::count_of(part2), part2 },
    };

    Match match = MatchParts(&devices[fbl::count_of(devices) - 1], parts, fbl::count_of(parts));
    ASSERT_EQ(match, Match::None);

    END_TEST;
}

bool composite_match_two_parts_three_devices_no_mid_topo_fail1() {
    BEGIN_TEST;

    // No topological property on the middle device
    zx_device_prop_t mid_props[] ={
        { BIND_PCI_DID, 0, 1234 },
    };

    constexpr uint32_t kProtocolId1 = 1;
    constexpr uint32_t kProtocolId2 = 2;
    constexpr uint32_t kProtocolId3 = 3;
    MockDevice devices[] = {
        MockDevice(nullptr, nullptr, 0, kProtocolId1),
        MockDevice(&devices[0], mid_props, fbl::count_of(mid_props), kProtocolId2),
        MockDevice(&devices[1], nullptr, 0, kProtocolId3),
    };

    zx_bind_inst_t part1[] = {
        BI_MATCH_IF(EQ, BIND_PROTOCOL, kProtocolId1),
    };
    zx_bind_inst_t part2[] = {
        BI_MATCH_IF(EQ, BIND_PROTOCOL, kProtocolId2),
    };
    DeviceComponentPartDescriptor parts[] = {
        { fbl::count_of(part1), part1 },
        // This matches the middle device, not the leaf
        { fbl::count_of(part2), part2 },
    };

    Match match = MatchParts(&devices[fbl::count_of(devices) - 1], parts, fbl::count_of(parts));
    ASSERT_EQ(match, Match::None);

    END_TEST;
}

bool composite_match_two_parts_three_devices_no_mid_topo_fail2() {
    BEGIN_TEST;

    // No topological property on the middle device
    zx_device_prop_t mid_props[] ={
        { BIND_PCI_DID, 0, 1234 },
    };

    constexpr uint32_t kProtocolId1 = 1;
    constexpr uint32_t kProtocolId2 = 2;
    constexpr uint32_t kProtocolId3 = 3;
    MockDevice devices[] = {
        MockDevice(nullptr, nullptr, 0, kProtocolId1),
        MockDevice(&devices[0], mid_props, fbl::count_of(mid_props), kProtocolId2),
        MockDevice(&devices[1], nullptr, 0, kProtocolId3),
    };

    zx_bind_inst_t part1[] = {
        BI_MATCH_IF(EQ, BIND_PROTOCOL, kProtocolId2),
    };
    zx_bind_inst_t part2[] = {
        BI_MATCH_IF(EQ, BIND_PROTOCOL, kProtocolId3),
    };
    DeviceComponentPartDescriptor parts[] = {
        // This matches the middle device, not the root
        { fbl::count_of(part1), part1 },
        { fbl::count_of(part2), part2 },
    };

    Match match = MatchParts(&devices[fbl::count_of(devices) - 1], parts, fbl::count_of(parts));
    ASSERT_EQ(match, Match::None);

    END_TEST;
}

bool composite_match_two_parts_three_devices_no_mid_topo_success() {
    BEGIN_TEST;

    // No topological property on the middle device
    zx_device_prop_t mid_props[] ={
        { BIND_PCI_DID, 0, 1234 },
    };

    constexpr uint32_t kProtocolId1 = 1;
    constexpr uint32_t kProtocolId2 = 2;
    constexpr uint32_t kProtocolId3 = 3;
    MockDevice devices[] = {
        MockDevice(nullptr, nullptr, 0, kProtocolId1),
        MockDevice(&devices[0], mid_props, fbl::count_of(mid_props), kProtocolId2),
        MockDevice(&devices[1], nullptr, 0, kProtocolId3),
    };

    zx_bind_inst_t part1[] = {
        BI_MATCH_IF(EQ, BIND_PROTOCOL, kProtocolId1),
    };
    zx_bind_inst_t part2[] = {
        BI_MATCH_IF(EQ, BIND_PROTOCOL, kProtocolId3),
    };
    DeviceComponentPartDescriptor parts[] = {
        { fbl::count_of(part1), part1 },
        { fbl::count_of(part2), part2 },
    };

    Match match = MatchParts(&devices[fbl::count_of(devices) - 1], parts, fbl::count_of(parts));
    ASSERT_EQ(match, Match::One);

    END_TEST;
}

bool composite_match_two_parts_three_devices_mid_topo() {
    BEGIN_TEST;

    // Topological property on the middle device
    zx_device_prop_t mid_props[] = {
        { BIND_PCI_DID, 0, 1234 },
        { BIND_TOPO_PCI, 0, BIND_TOPO_PCI_PACK(0, 0, 0) },
    };

    constexpr uint32_t kProtocolId1 = 1;
    constexpr uint32_t kProtocolId2 = 2;
    constexpr uint32_t kProtocolId3 = 3;
    MockDevice devices[] = {
        MockDevice(nullptr, nullptr, 0, kProtocolId1),
        MockDevice(&devices[0], mid_props, fbl::count_of(mid_props), kProtocolId2),
        MockDevice(&devices[1], nullptr, 0, kProtocolId3),
    };

    zx_bind_inst_t part1[] = {
        BI_MATCH_IF(EQ, BIND_PROTOCOL, kProtocolId1),
    };
    zx_bind_inst_t part2[] = {
        BI_MATCH_IF(EQ, BIND_PROTOCOL, kProtocolId3),
    };
    DeviceComponentPartDescriptor parts[] = {
        { fbl::count_of(part1), part1 },
        // We need to match on the topological node, but we don't have a rule
        // for it.
        { fbl::count_of(part2), part2 },
    };

    Match match = MatchParts(&devices[fbl::count_of(devices) - 1], parts, fbl::count_of(parts));
    ASSERT_EQ(match, Match::None);

    END_TEST;
}

bool composite_match_three_parts_three_devices_mid_topo() {
    BEGIN_TEST;

    // Topological property on the middle device
    zx_device_prop_t mid_props[] = {
        { BIND_PCI_DID, 0, 1234 },
        { BIND_TOPO_PCI, 0, BIND_TOPO_PCI_PACK(0, 0, 0) },
    };

    constexpr uint32_t kProtocolId1 = 1;
    constexpr uint32_t kProtocolId2 = 2;
    constexpr uint32_t kProtocolId3 = 3;
    MockDevice devices[] = {
        MockDevice(nullptr, nullptr, 0, kProtocolId1),
        MockDevice(&devices[0], mid_props, fbl::count_of(mid_props), kProtocolId2),
        MockDevice(&devices[1], nullptr, 0, kProtocolId3),
    };

    zx_bind_inst_t part1[] = {
        BI_MATCH_IF(EQ, BIND_PROTOCOL, kProtocolId1),
    };
    zx_bind_inst_t part2[] = {
        BI_MATCH_IF(EQ, BIND_TOPO_PCI, BIND_TOPO_PCI_PACK(0, 0, 0)),
    };
    zx_bind_inst_t part3[] = {
        BI_MATCH_IF(EQ, BIND_PROTOCOL, kProtocolId3),
    };
    DeviceComponentPartDescriptor parts[] = {
        { fbl::count_of(part1), part1 },
        { fbl::count_of(part2), part2 },
        { fbl::count_of(part3), part3 },
    };

    Match match = MatchParts(&devices[fbl::count_of(devices) - 1], parts, fbl::count_of(parts));
    ASSERT_EQ(match, Match::One);

    END_TEST;
}

bool composite_match_two_parts_four_devices_one_topo() {
    BEGIN_TEST;

    // Topological property on the middle device
    zx_device_prop_t mid_props[] = {
        { BIND_PCI_DID, 0, 1234 },
        { BIND_TOPO_PCI, 0, BIND_TOPO_PCI_PACK(0, 0, 0) },
    };

    constexpr uint32_t kProtocolId1 = 1;
    constexpr uint32_t kProtocolId2 = 2;
    constexpr uint32_t kProtocolId3 = 3;
    constexpr uint32_t kProtocolId4 = 4;
    MockDevice devices[] = {
        MockDevice(nullptr, nullptr, 0, kProtocolId1),
        MockDevice(&devices[0], mid_props, fbl::count_of(mid_props), kProtocolId2),
        MockDevice(&devices[1], nullptr, 0, kProtocolId3),
        MockDevice(&devices[2], nullptr, 0, kProtocolId4),
    };

    zx_bind_inst_t part1[] = {
        BI_MATCH_IF(EQ, BIND_PROTOCOL, kProtocolId1),
    };
    zx_bind_inst_t part2[] = {
        BI_MATCH_IF(EQ, BIND_PROTOCOL, kProtocolId4),
    };
    DeviceComponentPartDescriptor parts[] = {
        { fbl::count_of(part1), part1 },
        { fbl::count_of(part2), part2 },
    };

    Match match = MatchParts(&devices[fbl::count_of(devices) - 1], parts, fbl::count_of(parts));
    ASSERT_EQ(match, Match::None);

    END_TEST;
}

bool composite_match_three_parts_four_devices_one_topo() {
    BEGIN_TEST;

    // Topological property on the middle device
    zx_device_prop_t mid_props[] = {
        { BIND_PCI_DID, 0, 1234 },
        { BIND_TOPO_PCI, 0, BIND_TOPO_PCI_PACK(0, 0, 0) },
    };

    constexpr uint32_t kProtocolId1 = 1;
    constexpr uint32_t kProtocolId2 = 2;
    constexpr uint32_t kProtocolId3 = 3;
    constexpr uint32_t kProtocolId4 = 4;
    MockDevice devices[] = {
        MockDevice(nullptr, nullptr, 0, kProtocolId1),
        MockDevice(&devices[0], mid_props, fbl::count_of(mid_props), kProtocolId2),
        MockDevice(&devices[1], nullptr, 0, kProtocolId3),
        MockDevice(&devices[2], nullptr, 0, kProtocolId4),
    };

    zx_bind_inst_t part1[] = {
        BI_MATCH_IF(EQ, BIND_PROTOCOL, kProtocolId1),
    };
    zx_bind_inst_t part2[] = {
        BI_MATCH_IF(EQ, BIND_TOPO_PCI, BIND_TOPO_PCI_PACK(0, 0, 0)),
    };
    zx_bind_inst_t part3[] = {
        BI_MATCH_IF(EQ, BIND_PROTOCOL, kProtocolId4),
    };
    DeviceComponentPartDescriptor parts[] = {
        { fbl::count_of(part1), part1 },
        { fbl::count_of(part2), part2 },
        { fbl::count_of(part3), part3 },
    };

    Match match = MatchParts(&devices[fbl::count_of(devices) - 1], parts, fbl::count_of(parts));
    ASSERT_EQ(match, Match::One);

    END_TEST;
}

bool composite_match_four_parts_four_devices_one_topo() {
    BEGIN_TEST;

    // Topological property on the middle device
    zx_device_prop_t mid_props[] = {
        { BIND_PCI_DID, 0, 1234 },
        { BIND_TOPO_PCI, 0, BIND_TOPO_PCI_PACK(0, 0, 0) },
    };

    constexpr uint32_t kProtocolId1 = 1;
    constexpr uint32_t kProtocolId2 = 2;
    constexpr uint32_t kProtocolId3 = 3;
    constexpr uint32_t kProtocolId4 = 4;
    MockDevice devices[] = {
        MockDevice(nullptr, nullptr, 0, kProtocolId1),
        MockDevice(&devices[0], mid_props, fbl::count_of(mid_props), kProtocolId2),
        MockDevice(&devices[1], nullptr, 0, kProtocolId3),
        MockDevice(&devices[2], nullptr, 0, kProtocolId4),
    };

    zx_bind_inst_t part1[] = {
        BI_MATCH_IF(EQ, BIND_PROTOCOL, kProtocolId1),
    };
    zx_bind_inst_t part2[] = {
        BI_MATCH_IF(EQ, BIND_TOPO_PCI, BIND_TOPO_PCI_PACK(0, 0, 0)),
    };
    zx_bind_inst_t part3[] = {
        BI_MATCH_IF(EQ, BIND_PROTOCOL, kProtocolId3),
    };
    zx_bind_inst_t part4[] = {
        BI_MATCH_IF(EQ, BIND_PROTOCOL, kProtocolId4),
    };
    DeviceComponentPartDescriptor parts[] = {
        { fbl::count_of(part1), part1 },
        { fbl::count_of(part2), part2 },
        { fbl::count_of(part3), part3 },
        { fbl::count_of(part4), part4 },
    };

    Match match = MatchParts(&devices[fbl::count_of(devices) - 1], parts, fbl::count_of(parts));
    ASSERT_EQ(match, Match::One);

    END_TEST;
}

bool composite_match_three_parts_four_devices_ambiguous() {
    BEGIN_TEST;

    constexpr uint32_t kProtocolId1 = 1;
    constexpr uint32_t kProtocolId2 = 2;
    constexpr uint32_t kProtocolId3 = 3;
    MockDevice devices[] = {
        MockDevice(nullptr, nullptr, 0, kProtocolId1),
        MockDevice(&devices[0], nullptr, 0, kProtocolId2),
        MockDevice(&devices[1], nullptr, 0, kProtocolId2),
        MockDevice(&devices[2], nullptr, 0, kProtocolId3),
    };

    zx_bind_inst_t part1[] = {
        BI_MATCH_IF(EQ, BIND_PROTOCOL, kProtocolId1),
    };
    zx_bind_inst_t part2[] = {
        BI_MATCH_IF(EQ, BIND_PROTOCOL, kProtocolId2),
    };
    zx_bind_inst_t part3[] = {
        BI_MATCH_IF(EQ, BIND_PROTOCOL, kProtocolId3),
    };
    DeviceComponentPartDescriptor parts[] = {
        { fbl::count_of(part1), part1 },
        // This matches both of the inner devices.
        { fbl::count_of(part2), part2 },
        { fbl::count_of(part3), part3 },
    };

    Match match = MatchParts(&devices[fbl::count_of(devices) - 1], parts, fbl::count_of(parts));
    ASSERT_EQ(match, Match::Many);

    END_TEST;
}

bool composite_match_three_parts_four_devices_ambiguous_against_leaf() {
    BEGIN_TEST;

    constexpr uint32_t kProtocolId1 = 1;
    constexpr uint32_t kProtocolId2 = 2;
    constexpr uint32_t kProtocolId3 = 3;
    MockDevice devices[] = {
        MockDevice(nullptr, nullptr, 0, kProtocolId1),
        MockDevice(&devices[0], nullptr, 0, kProtocolId2),
        MockDevice(&devices[1], nullptr, 0, kProtocolId3),
        MockDevice(&devices[2], nullptr, 0, kProtocolId3),
    };

    zx_bind_inst_t part1[] = {
        BI_MATCH_IF(EQ, BIND_PROTOCOL, kProtocolId1),
    };
    zx_bind_inst_t part2[] = {
        BI_MATCH_IF(EQ, BIND_PROTOCOL, kProtocolId2),
    };
    zx_bind_inst_t part3[] = {
        BI_MATCH_IF(EQ, BIND_PROTOCOL, kProtocolId3),
    };
    DeviceComponentPartDescriptor parts[] = {
        { fbl::count_of(part1), part1 },
        { fbl::count_of(part2), part2 },
        // This matches the leaf and its parent, but is not considered ambiguous
        // since we force the match to the leaf
        { fbl::count_of(part3), part3 },
    };

    Match match = MatchParts(&devices[fbl::count_of(devices) - 1], parts, fbl::count_of(parts));
    ASSERT_EQ(match, Match::One);

    END_TEST;
}

bool composite_match_three_parts_four_devices_ambiguous_against_root() {
    BEGIN_TEST;

    constexpr uint32_t kProtocolId1 = 1;
    constexpr uint32_t kProtocolId2 = 2;
    constexpr uint32_t kProtocolId3 = 3;
    MockDevice devices[] = {
        MockDevice(nullptr, nullptr, 0, kProtocolId1),
        MockDevice(&devices[0], nullptr, 0, kProtocolId1),
        MockDevice(&devices[1], nullptr, 0, kProtocolId2),
        MockDevice(&devices[2], nullptr, 0, kProtocolId3),
    };

    zx_bind_inst_t part1[] = {
        BI_MATCH_IF(EQ, BIND_PROTOCOL, kProtocolId1),
    };
    zx_bind_inst_t part2[] = {
        BI_MATCH_IF(EQ, BIND_PROTOCOL, kProtocolId2),
    };
    zx_bind_inst_t part3[] = {
        BI_MATCH_IF(EQ, BIND_PROTOCOL, kProtocolId3),
    };
    DeviceComponentPartDescriptor parts[] = {
        // This matches the root and its immediate child, but is not considered
        // ambiguous isnce we force the match to the root
        { fbl::count_of(part1), part1 },
        { fbl::count_of(part2), part2 },
        { fbl::count_of(part3), part3 },
    };

    Match match = MatchParts(&devices[fbl::count_of(devices) - 1], parts, fbl::count_of(parts));
    ASSERT_EQ(match, Match::One);

    END_TEST;
}

bool composite_match_complex_topology() {
    BEGIN_TEST;

    zx_device_prop_t props1[] = {
        { BIND_TOPO_PCI, 0, BIND_TOPO_PCI_PACK(0, 0, 0) },
    };
    zx_device_prop_t props2[] = {
        { BIND_TOPO_PCI, 0, BIND_TOPO_PCI_PACK(1, 0, 0) },
    };
    zx_device_prop_t props3[] = {
        { BIND_TOPO_I2C, 0, BIND_TOPO_I2C_PACK(0x12) },
    };

    constexpr uint32_t kProtocolId = 1;
    MockDevice devices[] = {
        MockDevice(nullptr, nullptr, 0, 0),
        MockDevice(&devices[0], props1, fbl::count_of(props1), 0),
        MockDevice(&devices[1], nullptr, 0, 0),
        MockDevice(&devices[2], props2, fbl::count_of(props2), 0),
        MockDevice(&devices[3], nullptr, 0, 0),
        MockDevice(&devices[4], nullptr, 0, 0),
        MockDevice(&devices[5], props3, fbl::count_of(props3), 0),
        MockDevice(&devices[6], nullptr, 0, 0),
        MockDevice(&devices[7], nullptr, 0, kProtocolId),
    };

    zx_bind_inst_t part1[] = {
        BI_MATCH(),
    };
    zx_bind_inst_t part2[] = {
        BI_MATCH_IF(EQ, BIND_TOPO_PCI, BIND_TOPO_PCI_PACK(0, 0, 0)),
    };
    zx_bind_inst_t part3[] = {
        BI_MATCH_IF(EQ, BIND_TOPO_PCI, BIND_TOPO_PCI_PACK(1, 0, 0)),
    };
    zx_bind_inst_t part4[] = {
        BI_MATCH_IF(EQ, BIND_TOPO_I2C, BIND_TOPO_I2C_PACK(0x12)),
    };
    zx_bind_inst_t part5[] = {
        BI_MATCH_IF(EQ, BIND_PROTOCOL, kProtocolId),
    };
    DeviceComponentPartDescriptor parts[] = {
        { fbl::count_of(part1), part1 },
        { fbl::count_of(part2), part2 },
        { fbl::count_of(part3), part3 },
        { fbl::count_of(part4), part4 },
        { fbl::count_of(part5), part5 },
    };

    Match match = MatchParts(&devices[fbl::count_of(devices) - 1], parts, fbl::count_of(parts));
    ASSERT_EQ(match, Match::One);

    END_TEST;
}

bool composite_match_complex_ambiguity() {
    BEGIN_TEST;

    constexpr uint32_t kProtocolId1 = 1;
    constexpr uint32_t kProtocolId2 = 2;
    constexpr uint32_t kProtocolId3 = 3;
    MockDevice devices[] = {
        MockDevice(nullptr, nullptr, 0, kProtocolId1),
        MockDevice(&devices[0], nullptr, 0, kProtocolId2),
        MockDevice(&devices[1], nullptr, 0, kProtocolId2),
        MockDevice(&devices[2], nullptr, 0, kProtocolId2),
        MockDevice(&devices[3], nullptr, 0, kProtocolId3),
    };

    zx_bind_inst_t part1[] = {
        BI_MATCH_IF(EQ, BIND_PROTOCOL, kProtocolId1),
    };
    zx_bind_inst_t part2[] = {
        BI_MATCH_IF(EQ, BIND_PROTOCOL, kProtocolId2),
    };
    zx_bind_inst_t part3[] = {
        BI_MATCH(),
    };
    zx_bind_inst_t part4[] = {
        BI_MATCH_IF(EQ, BIND_PROTOCOL, kProtocolId3),
    };
    DeviceComponentPartDescriptor parts[] = {
        { fbl::count_of(part1), part1 },
        // parts 2 and 3 can match ancestors 1 and 2 or 2 and 3.
        { fbl::count_of(part2), part2 },
        { fbl::count_of(part3), part3 },
        { fbl::count_of(part4), part4 },
    };

    Match match = MatchParts(&devices[fbl::count_of(devices) - 1], parts, fbl::count_of(parts));
    ASSERT_EQ(match, Match::Many);

    END_TEST;
}

} // namespace

BEGIN_TEST_CASE(binding_tests)
RUN_TEST(composite_match_zero_parts)
RUN_TEST(composite_match_one_part_one_device_fail)
RUN_TEST(composite_match_one_part_one_device_succeed)
RUN_TEST(composite_match_two_part_one_device)

RUN_TEST(composite_match_zero_parts_two_devices)
RUN_TEST(composite_match_one_part_two_devices)
RUN_TEST(composite_match_two_parts_two_devices_fail)
RUN_TEST(composite_match_two_parts_two_devices_succeed)
RUN_TEST(composite_match_three_parts_two_devices)

RUN_TEST(composite_match_two_parts_three_devices_no_mid_topo_success)
RUN_TEST(composite_match_two_parts_three_devices_no_mid_topo_fail1)
RUN_TEST(composite_match_two_parts_three_devices_no_mid_topo_fail2)
RUN_TEST(composite_match_two_parts_three_devices_mid_topo)
RUN_TEST(composite_match_three_parts_three_devices_mid_topo)

RUN_TEST(composite_match_two_parts_four_devices_one_topo)
RUN_TEST(composite_match_three_parts_four_devices_one_topo)
RUN_TEST(composite_match_four_parts_four_devices_one_topo)
RUN_TEST(composite_match_three_parts_four_devices_ambiguous)
RUN_TEST(composite_match_three_parts_four_devices_ambiguous_against_leaf)
RUN_TEST(composite_match_three_parts_four_devices_ambiguous_against_root)

RUN_TEST(composite_match_complex_topology)
RUN_TEST(composite_match_complex_ambiguity)

END_TEST_CASE(binding_tests)
