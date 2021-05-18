// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/ddk/binding.h>

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
  }

  MockDevice& operator=(const MockDevice&) = delete;
  MockDevice& operator=(MockDevice&&) = delete;
  MockDevice(const MockDevice&) = delete;
  MockDevice(MockDevice&&) = delete;

  const fbl::RefPtr<MockDevice>& parent() { return parent_; }
  const fbl::Array<const zx_device_prop_t>& props() const { return props_; }
  uint32_t protocol_id() const { return protocol_id_; }

 private:
  const fbl::RefPtr<MockDevice> parent_;
  fbl::Array<const zx_device_prop_t> props_;
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
