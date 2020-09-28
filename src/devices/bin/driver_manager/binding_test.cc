// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/binding.h>
#include <fbl/algorithm.h>
#include <fbl/ref_counted.h>
#include <fbl/ref_ptr.h>
#include <zxtest/zxtest.h>

#include "binding_internal.h"

namespace {

class MockDevice : public fbl::RefCounted<MockDevice> {
 public:
  MockDevice(fbl::RefPtr<MockDevice> parent, const zx_device_prop_t* props, size_t props_count,
             uint32_t protocol_id)
      : parent_(std::move(parent)), protocol_id_(protocol_id) {
    fbl::Array<zx_device_prop_t> props_array(new zx_device_prop_t[props_count], props_count);
    if (props != nullptr) {
      memcpy(props_array.data(), props, sizeof(props[0]) * props_count);
    } else {
      ASSERT_EQ(props_count, 0);
    }
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

  const fbl::RefPtr<MockDevice>& parent() { return parent_; }
  const fbl::Array<const zx_device_prop_t>& props() const { return props_; }
  const zx_device_prop_t* topo_prop() const { return topo_prop_; }
  uint32_t protocol_id() const { return protocol_id_; }

 private:
  const fbl::RefPtr<MockDevice> parent_;
  fbl::Array<const zx_device_prop_t> props_;
  const zx_device_prop_t* topo_prop_ = nullptr;
  uint32_t protocol_id_ = 0;
};

using internal::Match;
using internal::MatchParts;

template <size_t N>
fbl::Array<const zx_bind_inst_t> MakeBindProgram(const zx_bind_inst_t (&insts)[N]) {
  fbl::Array<zx_bind_inst_t> array(new zx_bind_inst_t[N], N);
  memcpy(array.data(), insts, N * sizeof(insts[0]));
  return array;
}

TEST(BindingTestCase, CompositeMatchZeroParts) {
  auto device = fbl::MakeRefCounted<MockDevice>(nullptr, nullptr, 0, 0);

  Match match = MatchParts(device, nullptr, 0);
  ASSERT_EQ(match, Match::None);
}

TEST(BindingTestCase, CompositeMatchOnePartOneDeviceFail) {
  constexpr uint32_t kProtocolId = 1;
  auto device = fbl::MakeRefCounted<MockDevice>(nullptr, nullptr, 0, kProtocolId);

  auto part = MakeBindProgram({
      BI_MATCH_IF(EQ, BIND_PROTOCOL, 2),
  });
  FragmentPartDescriptor parts[] = {
      {std::move(part)},
  };

  Match match = MatchParts(device, parts, std::size(parts));
  ASSERT_EQ(match, Match::None);
}

TEST(BindingTestCase, CompositeMatchOnePartOneDeviceSucceed) {
  constexpr uint32_t kProtocolId = 1;
  auto device = fbl::MakeRefCounted<MockDevice>(nullptr, nullptr, 0, kProtocolId);

  auto part = MakeBindProgram({
      BI_MATCH_IF(EQ, BIND_PROTOCOL, 1),
  });
  FragmentPartDescriptor parts[] = {
      {std::move(part)},
  };

  Match match = MatchParts(device, parts, std::size(parts));
  ASSERT_EQ(match, Match::One);
}

TEST(BindingTestCase, CompositeMatchTwoPartOneDevice) {
  constexpr uint32_t kProtocolId = 1;
  auto device = fbl::MakeRefCounted<MockDevice>(nullptr, nullptr, 0, kProtocolId);

  // Both parts can match the only device, but only one part is allowed to
  // match to a given device.
  auto part1 = MakeBindProgram({
      BI_MATCH_IF(EQ, BIND_PROTOCOL, 1),
  });
  auto part2 = MakeBindProgram({
      BI_MATCH_IF(EQ, BIND_PROTOCOL, 1),
  });
  FragmentPartDescriptor parts[] = {
      {std::move(part1)},
      {std::move(part2)},
  };

  Match match = MatchParts(device, parts, std::size(parts));
  ASSERT_EQ(match, Match::None);
}

TEST(BindingTestCase, CompositeMatchZeroPartsTwoDevices) {
  fbl::RefPtr<MockDevice> devices[] = {
      fbl::MakeRefCounted<MockDevice>(nullptr, nullptr, 0, 0),
      fbl::MakeRefCounted<MockDevice>(devices[0], nullptr, 0, 0),
  };

  Match match = MatchParts(devices[std::size(devices) - 1], nullptr, 0);
  ASSERT_EQ(match, Match::None);
}

TEST(BindingTestCase, CompositeMatchOnePartTwoDevices) {
  constexpr uint32_t kProtocolId = 1;
  fbl::RefPtr<MockDevice> devices[] = {
      fbl::MakeRefCounted<MockDevice>(nullptr, nullptr, 0, kProtocolId),
      fbl::MakeRefCounted<MockDevice>(devices[0], nullptr, 0, kProtocolId),
  };

  // This program matches both devices
  auto part = MakeBindProgram({
      BI_MATCH_IF(EQ, BIND_PROTOCOL, kProtocolId),
  });
  FragmentPartDescriptor parts[] = {
      {std::move(part)},
  };

  Match match = MatchParts(devices[std::size(devices) - 1], parts, std::size(parts));
  ASSERT_EQ(match, Match::None);
}

TEST(BindingTestCase, CompositeMatchTwoPartsTwoDevicesFail) {
  constexpr uint32_t kProtocolId1 = 1;
  constexpr uint32_t kProtocolId2 = 2;
  fbl::RefPtr<MockDevice> devices[] = {
      fbl::MakeRefCounted<MockDevice>(nullptr, nullptr, 0, kProtocolId1),
      fbl::MakeRefCounted<MockDevice>(devices[0], nullptr, 0, kProtocolId2),
  };

  auto part1 = MakeBindProgram({
      BI_MATCH_IF(EQ, BIND_PROTOCOL, kProtocolId1),
  });
  auto part2 = MakeBindProgram({
      BI_MATCH_IF(EQ, BIND_PROTOCOL, kProtocolId2),
  });
  FragmentPartDescriptor parts[] = {
      // First entry should match the root, but this rule matches leaf
      {std::move(part2)},
      // Last entry should match the leaf, but this rule matches root
      {std::move(part1)},
  };

  Match match = MatchParts(devices[std::size(devices) - 1], parts, std::size(parts));
  ASSERT_EQ(match, Match::None);
}

TEST(BindingTestCase, CompositeMatchTwoPartsTwoDevicesSucceed) {
  constexpr uint32_t kProtocolId1 = 1;
  constexpr uint32_t kProtocolId2 = 2;
  fbl::RefPtr<MockDevice> devices[] = {
      fbl::MakeRefCounted<MockDevice>(nullptr, nullptr, 0, kProtocolId1),
      fbl::MakeRefCounted<MockDevice>(devices[0], nullptr, 0, kProtocolId2),
  };

  auto part1 = MakeBindProgram({
      BI_MATCH_IF(EQ, BIND_PROTOCOL, kProtocolId1),
  });
  auto part2 = MakeBindProgram({
      BI_MATCH_IF(EQ, BIND_PROTOCOL, kProtocolId2),
  });
  FragmentPartDescriptor parts[] = {
      {std::move(part1)},
      {std::move(part2)},
  };

  Match match = MatchParts(devices[std::size(devices) - 1], parts, std::size(parts));
  ASSERT_EQ(match, Match::One);
}

TEST(BindingTestCase, CompositeMatchThreePartsTwoDevices) {
  constexpr uint32_t kProtocolId1 = 1;
  constexpr uint32_t kProtocolId2 = 2;
  fbl::RefPtr<MockDevice> devices[] = {
      fbl::MakeRefCounted<MockDevice>(nullptr, nullptr, 0, kProtocolId1),
      fbl::MakeRefCounted<MockDevice>(devices[0], nullptr, 0, kProtocolId2),
  };

  auto part1 = MakeBindProgram({
      BI_MATCH_IF(EQ, BIND_PROTOCOL, kProtocolId1),
  });
  auto part2 = MakeBindProgram({
      BI_MATCH_IF(EQ, BIND_PROTOCOL, kProtocolId2),
  });
  auto part3 = MakeBindProgram({
      BI_MATCH_IF(EQ, BIND_PROTOCOL, kProtocolId2),
  });
  FragmentPartDescriptor parts[] = {
      {std::move(part1)},
      {std::move(part2)},
      {std::move(part3)},
  };

  Match match = MatchParts(devices[std::size(devices) - 1], parts, std::size(parts));
  ASSERT_EQ(match, Match::None);
}

TEST(BindingTestCase, CompositeMatchTwoPartsThreeDevicesNoMidTopoFail1) {
  // No topological property on the middle device
  zx_device_prop_t mid_props[] = {
      {BIND_PCI_DID, 0, 1234},
  };

  constexpr uint32_t kProtocolId1 = 1;
  constexpr uint32_t kProtocolId2 = 2;
  constexpr uint32_t kProtocolId3 = 3;
  fbl::RefPtr<MockDevice> devices[] = {
      fbl::MakeRefCounted<MockDevice>(nullptr, nullptr, 0, kProtocolId1),
      fbl::MakeRefCounted<MockDevice>(devices[0], mid_props, std::size(mid_props), kProtocolId2),
      fbl::MakeRefCounted<MockDevice>(devices[1], nullptr, 0, kProtocolId3),
  };

  auto part1 = MakeBindProgram({
      BI_MATCH_IF(EQ, BIND_PROTOCOL, kProtocolId1),
  });
  auto part2 = MakeBindProgram({
      BI_MATCH_IF(EQ, BIND_PROTOCOL, kProtocolId2),
  });
  FragmentPartDescriptor parts[] = {
      {std::move(part1)},
      // This matches the middle device, not the leaf
      {std::move(part2)},
  };

  Match match = MatchParts(devices[std::size(devices) - 1], parts, std::size(parts));
  ASSERT_EQ(match, Match::None);
}

TEST(BindingTestCase, CompositeMatchTwoPartsThreeDevicesNoMidTopoFail2) {
  // No topological property on the middle device
  zx_device_prop_t mid_props[] = {
      {BIND_PCI_DID, 0, 1234},
  };

  constexpr uint32_t kProtocolId1 = 1;
  constexpr uint32_t kProtocolId2 = 2;
  constexpr uint32_t kProtocolId3 = 3;
  fbl::RefPtr<MockDevice> devices[] = {
      fbl::MakeRefCounted<MockDevice>(nullptr, nullptr, 0, kProtocolId1),
      fbl::MakeRefCounted<MockDevice>(devices[0], mid_props, std::size(mid_props), kProtocolId2),
      fbl::MakeRefCounted<MockDevice>(devices[1], nullptr, 0, kProtocolId3),
  };

  auto part1 = MakeBindProgram({
      BI_MATCH_IF(EQ, BIND_PROTOCOL, kProtocolId2),
  });
  auto part2 = MakeBindProgram({
      BI_MATCH_IF(EQ, BIND_PROTOCOL, kProtocolId3),
  });
  FragmentPartDescriptor parts[] = {
      // This matches the middle device, not the root
      {std::move(part1)},
      {std::move(part2)},
  };

  Match match = MatchParts(devices[std::size(devices) - 1], parts, std::size(parts));
  ASSERT_EQ(match, Match::None);
}

TEST(BindingTestCase, CompositeMatchTwoPartsThreeDevicesNoMidTopoSuccess) {
  // No topological property on the middle device
  zx_device_prop_t mid_props[] = {
      {BIND_PCI_DID, 0, 1234},
  };

  constexpr uint32_t kProtocolId1 = 1;
  constexpr uint32_t kProtocolId2 = 2;
  constexpr uint32_t kProtocolId3 = 3;
  fbl::RefPtr<MockDevice> devices[] = {
      fbl::MakeRefCounted<MockDevice>(nullptr, nullptr, 0, kProtocolId1),
      fbl::MakeRefCounted<MockDevice>(devices[0], mid_props, std::size(mid_props), kProtocolId2),
      fbl::MakeRefCounted<MockDevice>(devices[1], nullptr, 0, kProtocolId3),
  };

  auto part1 = MakeBindProgram({
      BI_MATCH_IF(EQ, BIND_PROTOCOL, kProtocolId1),
  });
  auto part2 = MakeBindProgram({
      BI_MATCH_IF(EQ, BIND_PROTOCOL, kProtocolId3),
  });
  FragmentPartDescriptor parts[] = {
      {std::move(part1)},
      {std::move(part2)},
  };

  Match match = MatchParts(devices[std::size(devices) - 1], parts, std::size(parts));
  ASSERT_EQ(match, Match::One);
}

TEST(BindingTestCase, CompositeMatchTwoPartsThreeDevicesMidTopo) {
  // Topological property on the middle device
  zx_device_prop_t mid_props[] = {
      {BIND_PCI_DID, 0, 1234},
      {BIND_TOPO_PCI, 0, BIND_TOPO_PCI_PACK(0, 0, 0)},
  };

  constexpr uint32_t kProtocolId1 = 1;
  constexpr uint32_t kProtocolId2 = 2;
  constexpr uint32_t kProtocolId3 = 3;
  fbl::RefPtr<MockDevice> devices[] = {
      fbl::MakeRefCounted<MockDevice>(nullptr, nullptr, 0, kProtocolId1),
      fbl::MakeRefCounted<MockDevice>(devices[0], mid_props, std::size(mid_props), kProtocolId2),
      fbl::MakeRefCounted<MockDevice>(devices[1], nullptr, 0, kProtocolId3),
  };

  auto part1 = MakeBindProgram({
      BI_MATCH_IF(EQ, BIND_PROTOCOL, kProtocolId1),
  });
  auto part2 = MakeBindProgram({
      BI_MATCH_IF(EQ, BIND_PROTOCOL, kProtocolId3),
  });
  FragmentPartDescriptor parts[] = {
      {std::move(part1)},
      // We need to match on the topological node, but we don't have a rule
      // for it.
      {std::move(part2)},
  };

  Match match = MatchParts(devices[std::size(devices) - 1], parts, std::size(parts));
  ASSERT_EQ(match, Match::None);
}

TEST(BindingTestCase, CompositeMatchThreePartsThreeDevicesMidTopo) {
  // Topological property on the middle device
  zx_device_prop_t mid_props[] = {
      {BIND_PCI_DID, 0, 1234},
      {BIND_TOPO_PCI, 0, BIND_TOPO_PCI_PACK(0, 0, 0)},
  };

  constexpr uint32_t kProtocolId1 = 1;
  constexpr uint32_t kProtocolId2 = 2;
  constexpr uint32_t kProtocolId3 = 3;
  fbl::RefPtr<MockDevice> devices[] = {
      fbl::MakeRefCounted<MockDevice>(nullptr, nullptr, 0, kProtocolId1),
      fbl::MakeRefCounted<MockDevice>(devices[0], mid_props, std::size(mid_props), kProtocolId2),
      fbl::MakeRefCounted<MockDevice>(devices[1], nullptr, 0, kProtocolId3),
  };

  auto part1 = MakeBindProgram({
      BI_MATCH_IF(EQ, BIND_PROTOCOL, kProtocolId1),
  });
  auto part2 = MakeBindProgram({
      BI_MATCH_IF(EQ, BIND_TOPO_PCI, BIND_TOPO_PCI_PACK(0, 0, 0)),
  });
  auto part3 = MakeBindProgram({
      BI_MATCH_IF(EQ, BIND_PROTOCOL, kProtocolId3),
  });
  FragmentPartDescriptor parts[] = {
      {std::move(part1)},
      {std::move(part2)},
      {std::move(part3)},
  };

  Match match = MatchParts(devices[std::size(devices) - 1], parts, std::size(parts));
  ASSERT_EQ(match, Match::One);
}

TEST(BindingTestCase, CompositeMatchTwoPartsFourDevicesOneTopo) {
  // Topological property on the middle device
  zx_device_prop_t mid_props[] = {
      {BIND_PCI_DID, 0, 1234},
      {BIND_TOPO_PCI, 0, BIND_TOPO_PCI_PACK(0, 0, 0)},
  };

  constexpr uint32_t kProtocolId1 = 1;
  constexpr uint32_t kProtocolId2 = 2;
  constexpr uint32_t kProtocolId3 = 3;
  constexpr uint32_t kProtocolId4 = 4;
  fbl::RefPtr<MockDevice> devices[] = {
      fbl::MakeRefCounted<MockDevice>(nullptr, nullptr, 0, kProtocolId1),
      fbl::MakeRefCounted<MockDevice>(devices[0], mid_props, std::size(mid_props), kProtocolId2),
      fbl::MakeRefCounted<MockDevice>(devices[1], nullptr, 0, kProtocolId3),
      fbl::MakeRefCounted<MockDevice>(devices[2], nullptr, 0, kProtocolId4),
  };

  auto part1 = MakeBindProgram({
      BI_MATCH_IF(EQ, BIND_PROTOCOL, kProtocolId1),
  });
  auto part2 = MakeBindProgram({
      BI_MATCH_IF(EQ, BIND_PROTOCOL, kProtocolId4),
  });
  FragmentPartDescriptor parts[] = {
      {std::move(part1)},
      {std::move(part2)},
  };

  Match match = MatchParts(devices[std::size(devices) - 1], parts, std::size(parts));
  ASSERT_EQ(match, Match::None);
}

TEST(BindingTestCase, CompositeMatchThreePartsFourDevicesOneTopo) {
  // Topological property on the middle device
  zx_device_prop_t mid_props[] = {
      {BIND_PCI_DID, 0, 1234},
      {BIND_TOPO_PCI, 0, BIND_TOPO_PCI_PACK(0, 0, 0)},
  };

  constexpr uint32_t kProtocolId1 = 1;
  constexpr uint32_t kProtocolId2 = 2;
  constexpr uint32_t kProtocolId3 = 3;
  constexpr uint32_t kProtocolId4 = 4;
  fbl::RefPtr<MockDevice> devices[] = {
      fbl::MakeRefCounted<MockDevice>(nullptr, nullptr, 0, kProtocolId1),
      fbl::MakeRefCounted<MockDevice>(devices[0], mid_props, std::size(mid_props), kProtocolId2),
      fbl::MakeRefCounted<MockDevice>(devices[1], nullptr, 0, kProtocolId3),
      fbl::MakeRefCounted<MockDevice>(devices[2], nullptr, 0, kProtocolId4),
  };

  auto part1 = MakeBindProgram({
      BI_MATCH_IF(EQ, BIND_PROTOCOL, kProtocolId1),
  });
  auto part2 = MakeBindProgram({
      BI_MATCH_IF(EQ, BIND_TOPO_PCI, BIND_TOPO_PCI_PACK(0, 0, 0)),
  });
  auto part3 = MakeBindProgram({
      BI_MATCH_IF(EQ, BIND_PROTOCOL, kProtocolId4),
  });
  FragmentPartDescriptor parts[] = {
      {std::move(part1)},
      {std::move(part2)},
      {std::move(part3)},
  };

  Match match = MatchParts(devices[std::size(devices) - 1], parts, std::size(parts));
  ASSERT_EQ(match, Match::One);
}

TEST(BindingTestCase, CompositeMatchFourPartsFourDevicesOneTopo) {
  // Topological property on the middle device
  zx_device_prop_t mid_props[] = {
      {BIND_PCI_DID, 0, 1234},
      {BIND_TOPO_PCI, 0, BIND_TOPO_PCI_PACK(0, 0, 0)},
  };

  constexpr uint32_t kProtocolId1 = 1;
  constexpr uint32_t kProtocolId2 = 2;
  constexpr uint32_t kProtocolId3 = 3;
  constexpr uint32_t kProtocolId4 = 4;
  fbl::RefPtr<MockDevice> devices[] = {
      fbl::MakeRefCounted<MockDevice>(nullptr, nullptr, 0, kProtocolId1),
      fbl::MakeRefCounted<MockDevice>(devices[0], mid_props, std::size(mid_props), kProtocolId2),
      fbl::MakeRefCounted<MockDevice>(devices[1], nullptr, 0, kProtocolId3),
      fbl::MakeRefCounted<MockDevice>(devices[2], nullptr, 0, kProtocolId4),
  };

  auto part1 = MakeBindProgram({
      BI_MATCH_IF(EQ, BIND_PROTOCOL, kProtocolId1),
  });
  auto part2 = MakeBindProgram({
      BI_MATCH_IF(EQ, BIND_TOPO_PCI, BIND_TOPO_PCI_PACK(0, 0, 0)),
  });
  auto part3 = MakeBindProgram({
      BI_MATCH_IF(EQ, BIND_PROTOCOL, kProtocolId3),
  });
  auto part4 = MakeBindProgram({
      BI_MATCH_IF(EQ, BIND_PROTOCOL, kProtocolId4),
  });
  FragmentPartDescriptor parts[] = {
      {std::move(part1)},
      {std::move(part2)},
      {std::move(part3)},
      {std::move(part4)},
  };

  Match match = MatchParts(devices[std::size(devices) - 1], parts, std::size(parts));
  ASSERT_EQ(match, Match::One);
}

TEST(BindingTestCase, CompositeMatchThreePartsFourDevicesAmbiguous) {
  constexpr uint32_t kProtocolId1 = 1;
  constexpr uint32_t kProtocolId2 = 2;
  constexpr uint32_t kProtocolId3 = 3;
  fbl::RefPtr<MockDevice> devices[] = {
      fbl::MakeRefCounted<MockDevice>(nullptr, nullptr, 0, kProtocolId1),
      fbl::MakeRefCounted<MockDevice>(devices[0], nullptr, 0, kProtocolId2),
      fbl::MakeRefCounted<MockDevice>(devices[1], nullptr, 0, kProtocolId2),
      fbl::MakeRefCounted<MockDevice>(devices[2], nullptr, 0, kProtocolId3),
  };

  auto part1 = MakeBindProgram({
      BI_MATCH_IF(EQ, BIND_PROTOCOL, kProtocolId1),
  });
  auto part2 = MakeBindProgram({
      BI_MATCH_IF(EQ, BIND_PROTOCOL, kProtocolId2),
  });
  auto part3 = MakeBindProgram({
      BI_MATCH_IF(EQ, BIND_PROTOCOL, kProtocolId3),
  });
  FragmentPartDescriptor parts[] = {
      {std::move(part1)},
      // This matches both of the inner devices.
      {std::move(part2)},
      {std::move(part3)},
  };

  Match match = MatchParts(devices[std::size(devices) - 1], parts, std::size(parts));
  ASSERT_EQ(match, Match::Many);
}

TEST(BindingTestCase, CompositeMatchThreePartsFourDevicesAmbiguousAgainstLeaf) {
  constexpr uint32_t kProtocolId1 = 1;
  constexpr uint32_t kProtocolId2 = 2;
  constexpr uint32_t kProtocolId3 = 3;
  fbl::RefPtr<MockDevice> devices[] = {
      fbl::MakeRefCounted<MockDevice>(nullptr, nullptr, 0, kProtocolId1),
      fbl::MakeRefCounted<MockDevice>(devices[0], nullptr, 0, kProtocolId2),
      fbl::MakeRefCounted<MockDevice>(devices[1], nullptr, 0, kProtocolId3),
      fbl::MakeRefCounted<MockDevice>(devices[2], nullptr, 0, kProtocolId3),
  };

  auto part1 = MakeBindProgram({
      BI_MATCH_IF(EQ, BIND_PROTOCOL, kProtocolId1),
  });
  auto part2 = MakeBindProgram({
      BI_MATCH_IF(EQ, BIND_PROTOCOL, kProtocolId2),
  });
  auto part3 = MakeBindProgram({
      BI_MATCH_IF(EQ, BIND_PROTOCOL, kProtocolId3),
  });
  FragmentPartDescriptor parts[] = {
      {std::move(part1)},
      {std::move(part2)},
      // This matches the leaf and its parent, but is not considered ambiguous
      // since we force the match to the leaf
      {std::move(part3)},
  };

  Match match = MatchParts(devices[std::size(devices) - 1], parts, std::size(parts));
  ASSERT_EQ(match, Match::One);
}

TEST(BindingTestCase, CompositeMatchThreePartsFourDevicesAmbiguousAgainstRoot) {
  constexpr uint32_t kProtocolId1 = 1;
  constexpr uint32_t kProtocolId2 = 2;
  constexpr uint32_t kProtocolId3 = 3;
  fbl::RefPtr<MockDevice> devices[] = {
      fbl::MakeRefCounted<MockDevice>(nullptr, nullptr, 0, kProtocolId1),
      fbl::MakeRefCounted<MockDevice>(devices[0], nullptr, 0, kProtocolId1),
      fbl::MakeRefCounted<MockDevice>(devices[1], nullptr, 0, kProtocolId2),
      fbl::MakeRefCounted<MockDevice>(devices[2], nullptr, 0, kProtocolId3),
  };

  auto part1 = MakeBindProgram({
      BI_MATCH_IF(EQ, BIND_PROTOCOL, kProtocolId1),
  });
  auto part2 = MakeBindProgram({
      BI_MATCH_IF(EQ, BIND_PROTOCOL, kProtocolId2),
  });
  auto part3 = MakeBindProgram({
      BI_MATCH_IF(EQ, BIND_PROTOCOL, kProtocolId3),
  });
  FragmentPartDescriptor parts[] = {
      // This matches the root and its immediate child, but is not considered
      // ambiguous isnce we force the match to the root
      {std::move(part1)},
      {std::move(part2)},
      {std::move(part3)},
  };

  Match match = MatchParts(devices[std::size(devices) - 1], parts, std::size(parts));
  ASSERT_EQ(match, Match::One);
}

TEST(BindingTestCase, CompositeMatchComplexTopology) {
  zx_device_prop_t props1[] = {
      {BIND_TOPO_PCI, 0, BIND_TOPO_PCI_PACK(0, 0, 0)},
  };
  zx_device_prop_t props2[] = {
      {BIND_TOPO_PCI, 0, BIND_TOPO_PCI_PACK(1, 0, 0)},
  };
  zx_device_prop_t props3[] = {
      {BIND_TOPO_I2C, 0, BIND_TOPO_I2C_PACK(0x12)},
  };

  constexpr uint32_t kProtocolId = 1;
  fbl::RefPtr<MockDevice> devices[] = {
      fbl::MakeRefCounted<MockDevice>(nullptr, nullptr, 0, 0),
      fbl::MakeRefCounted<MockDevice>(devices[0], props1, std::size(props1), 0),
      fbl::MakeRefCounted<MockDevice>(devices[1], nullptr, 0, 0),
      fbl::MakeRefCounted<MockDevice>(devices[2], props2, std::size(props2), 0),
      fbl::MakeRefCounted<MockDevice>(devices[3], nullptr, 0, 0),
      fbl::MakeRefCounted<MockDevice>(devices[4], nullptr, 0, 0),
      fbl::MakeRefCounted<MockDevice>(devices[5], props3, std::size(props3), 0),
      fbl::MakeRefCounted<MockDevice>(devices[6], nullptr, 0, 0),
      fbl::MakeRefCounted<MockDevice>(devices[7], nullptr, 0, kProtocolId),
  };

  auto part1 = MakeBindProgram({
      BI_MATCH(),
  });
  auto part2 = MakeBindProgram({
      BI_MATCH_IF(EQ, BIND_TOPO_PCI, BIND_TOPO_PCI_PACK(0, 0, 0)),
  });
  auto part3 = MakeBindProgram({
      BI_MATCH_IF(EQ, BIND_TOPO_PCI, BIND_TOPO_PCI_PACK(1, 0, 0)),
  });
  auto part4 = MakeBindProgram({
      BI_MATCH_IF(EQ, BIND_TOPO_I2C, BIND_TOPO_I2C_PACK(0x12)),
  });
  auto part5 = MakeBindProgram({
      BI_MATCH_IF(EQ, BIND_PROTOCOL, kProtocolId),
  });
  FragmentPartDescriptor parts[] = {
      {std::move(part1)}, {std::move(part2)}, {std::move(part3)},
      {std::move(part4)}, {std::move(part5)},
  };

  Match match = MatchParts(devices[std::size(devices) - 1], parts, std::size(parts));
  ASSERT_EQ(match, Match::One);
}

TEST(BindingTestCase, CompositeMatchComplexAmbiguity) {
  constexpr uint32_t kProtocolId1 = 1;
  constexpr uint32_t kProtocolId2 = 2;
  constexpr uint32_t kProtocolId3 = 3;
  fbl::RefPtr<MockDevice> devices[] = {
      fbl::MakeRefCounted<MockDevice>(nullptr, nullptr, 0, kProtocolId1),
      fbl::MakeRefCounted<MockDevice>(devices[0], nullptr, 0, kProtocolId2),
      fbl::MakeRefCounted<MockDevice>(devices[1], nullptr, 0, kProtocolId2),
      fbl::MakeRefCounted<MockDevice>(devices[2], nullptr, 0, kProtocolId2),
      fbl::MakeRefCounted<MockDevice>(devices[3], nullptr, 0, kProtocolId3),
  };

  auto part1 = MakeBindProgram({
      BI_MATCH_IF(EQ, BIND_PROTOCOL, kProtocolId1),
  });
  auto part2 = MakeBindProgram({
      BI_MATCH_IF(EQ, BIND_PROTOCOL, kProtocolId2),
  });
  auto part3 = MakeBindProgram({
      BI_MATCH(),
  });
  auto part4 = MakeBindProgram({
      BI_MATCH_IF(EQ, BIND_PROTOCOL, kProtocolId3),
  });
  FragmentPartDescriptor parts[] = {
      {std::move(part1)},
      // parts 2 and 3 can match ancestors 1 and 2 or 2 and 3.
      {std::move(part2)},
      {std::move(part3)},
      {std::move(part4)},
  };

  Match match = MatchParts(devices[std::size(devices) - 1], parts, std::size(parts));
  ASSERT_EQ(match, Match::Many);
}

}  // namespace
