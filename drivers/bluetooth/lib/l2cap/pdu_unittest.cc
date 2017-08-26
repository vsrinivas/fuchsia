// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "pdu.h"
#include "recombiner.h"

#include "gtest/gtest.h"

#include "garnet/drivers/bluetooth/lib/common/test_helpers.h"
#include "garnet/drivers/bluetooth/lib/hci/hci.h"
#include "garnet/drivers/bluetooth/lib/hci/packet.h"

namespace bluetooth {
namespace l2cap {
namespace {

template <typename... T>
hci::ACLDataPacketPtr PacketFromBytes(T... data) {
  auto bytes = common::CreateStaticByteBuffer(std::forward<T>(data)...);
  FXL_DCHECK(bytes.size() >= sizeof(hci::ACLDataHeader));

  auto packet =
      hci::ACLDataPacket::New(bytes.size() - sizeof(hci::ACLDataHeader));
  packet->mutable_view()->mutable_data().Write(bytes);
  packet->InitializeFromBuffer();

  return packet;
}

TEST(L2CAP_PduTest, Move) {
  Recombiner recombiner;

  // clang-format off

  auto packet0 = PacketFromBytes(
    // ACL data header
    0x01, 0x00, 0x08, 0x00,

    // Basic L2CAP header
    0x04, 0x00, 0xFF, 0xFF, 'T', 'e', 's', 't'
  );

  // clang-format on

  EXPECT_TRUE(recombiner.AddFragment(std::move(packet0)));

  PDU pdu;
  EXPECT_TRUE(recombiner.Release(&pdu));
  EXPECT_TRUE(pdu.is_valid());
  EXPECT_EQ(1u, pdu.fragment_count());

  common::StaticByteBuffer<4> pdu_data;

  // Read the entire PDU.
  EXPECT_EQ(4u, pdu.Copy(&pdu_data));
  EXPECT_EQ("Test", pdu_data.AsString());

  PDU move_cted(std::move(pdu));
  EXPECT_FALSE(pdu.is_valid());
  EXPECT_EQ(0u, pdu.fragment_count());
  EXPECT_TRUE(move_cted.is_valid());
  EXPECT_EQ(1u, move_cted.fragment_count());

  pdu_data.SetToZeros();
  EXPECT_EQ(4u, move_cted.Copy(&pdu_data));
  EXPECT_EQ("Test", pdu_data.AsString());

  PDU move_assigned = std::move(move_cted);
  EXPECT_FALSE(move_cted.is_valid());
  EXPECT_EQ(0u, move_cted.fragment_count());
  EXPECT_TRUE(move_assigned.is_valid());
  EXPECT_EQ(1u, move_assigned.fragment_count());

  pdu_data.SetToZeros();
  EXPECT_EQ(4u, move_assigned.Copy(&pdu_data));
  EXPECT_EQ("Test", pdu_data.AsString());
}

TEST(L2CAP_PduTest, ReleaseFragments) {
  Recombiner recombiner;

  // clang-format off

  auto packet0 = PacketFromBytes(
    // ACL data header
    0x01, 0x00, 0x08, 0x00,

    // Basic L2CAP header
    0x04, 0x00, 0xFF, 0xFF, 'T', 'e', 's', 't'
  );

  // clang-format on

  EXPECT_TRUE(recombiner.AddFragment(std::move(packet0)));

  PDU pdu;
  EXPECT_TRUE(recombiner.Release(&pdu));
  EXPECT_TRUE(pdu.is_valid());
  EXPECT_EQ(1u, pdu.fragment_count());

  auto fragments = pdu.ReleaseFragments();

  EXPECT_FALSE(pdu.is_valid());
  ASSERT_FALSE(fragments.is_empty());
  EXPECT_EQ(0u, pdu.fragment_count());

  // Directly count the elements in |fragments| to make sure the count is
  // correct.
  size_t count = 0;
  for (__UNUSED const auto& f : fragments)
    count++;
  EXPECT_EQ(1u, count);

  // Check that the fragment we got out is identical to the one we fed in.
  EXPECT_TRUE(
      common::ContainersEqual(common::CreateStaticByteBuffer(
                                  // ACL data header
                                  0x01, 0x00, 0x08, 0x00,

                                  // Basic L2CAP header
                                  0x04, 0x00, 0xFF, 0xFF, 'T', 'e', 's', 't'),
                              fragments.begin()->view().data()));
}

TEST(L2CAP_PduTest, ReadSingleFragment) {
  Recombiner recombiner;

  // clang-format off

  auto packet0 = PacketFromBytes(
    // ACL data header
    0x01, 0x00, 0x08, 0x00,

    // Basic L2CAP header
    0x04, 0x00, 0xFF, 0xFF, 'T', 'e', 's', 't'
  );

  // clang-format on

  EXPECT_TRUE(recombiner.AddFragment(std::move(packet0)));

  PDU pdu;
  EXPECT_TRUE(recombiner.Release(&pdu));
  EXPECT_TRUE(pdu.is_valid());

  common::StaticByteBuffer<4> pdu_data;

  // Read the entire PDU.
  EXPECT_EQ(4u, pdu.Copy(&pdu_data));
  EXPECT_EQ("Test", pdu_data.AsString());

  // Read 1 byte at offset 1.
  pdu_data.Fill('X');
  EXPECT_EQ(1u, pdu.Copy(&pdu_data, 1, 1));
  EXPECT_EQ("eXXX", pdu_data.AsString());

  // Read bytes starting at offset 2.
  pdu_data.Fill('X');
  EXPECT_EQ(2u, pdu.Copy(&pdu_data, 2));
  EXPECT_EQ("stXX", pdu_data.AsString());

  // Read bytes starting at offset 3.
  pdu_data.Fill('X');
  EXPECT_EQ(1u, pdu.Copy(&pdu_data, 3));
  EXPECT_EQ("tXXX", pdu_data.AsString());
}

TEST(L2CAP_PduTest, ReadMultipleFragments) {
  Recombiner recombiner;

  // clang-format off

  // Partial initial fragment
  auto packet0 = PacketFromBytes(
    // ACL data header (PBF: initial fragment)
    0x01, 0x00, 0x0A, 0x00,

    // Basic L2CAP header
    0x0F, 0x00, 0xFF, 0xFF, 'T', 'h', 'i', 's', ' ', 'i'
  );

  // Continuation fragment
  auto packet1 = PacketFromBytes(
    // ACL data header (PBF: continuing fragment)
    0x01, 0x10, 0x06, 0x00,

    // L2CAP PDU fragment
    's', ' ', 'a', ' ', 't', 'e'
  );

  // Continuation fragment
  auto packet2 = PacketFromBytes(
    // ACL data header (PBF: continuing fragment)
    0x01, 0x10, 0x02, 0x00,

    // L2CAP PDU fragment
    's', 't'
  );

  // Continuation fragment
  auto packet3 = PacketFromBytes(
    // ACL data header (PBF: continuing fragment)
    0x01, 0x10, 0x01, 0x00,

    // L2CAP PDU fragment
    '!'
  );

  EXPECT_TRUE(recombiner.AddFragment(std::move(packet0)));
  EXPECT_TRUE(recombiner.AddFragment(std::move(packet1)));
  EXPECT_TRUE(recombiner.AddFragment(std::move(packet2)));
  EXPECT_TRUE(recombiner.AddFragment(std::move(packet3)));

  PDU pdu;
  EXPECT_TRUE(recombiner.Release(&pdu));
  EXPECT_TRUE(pdu.is_valid());
  EXPECT_EQ(4u, pdu.fragment_count());

  common::StaticByteBuffer<15> pdu_data;

  // Read the entire PDU.
  EXPECT_EQ(15u, pdu.Copy(&pdu_data));
  EXPECT_EQ("This is a test!", pdu_data.AsString());

  // Read 1 byte at offset 1.
  pdu_data.Fill('X');
  EXPECT_EQ(1u, pdu.Copy(&pdu_data, 1, 1));
  EXPECT_EQ("hXXXXXXXXXXXXXX", pdu_data.AsString());

  // Read bytes starting at offset 2.
  pdu_data.Fill('X');
  EXPECT_EQ(13u, pdu.Copy(&pdu_data, 2));
  EXPECT_EQ("is is a test!XX", pdu_data.AsString());

  // Read bytes starting at the last octet of the first fragment.
  pdu_data.Fill('X');
  EXPECT_EQ(10u, pdu.Copy(&pdu_data, 5));
  EXPECT_EQ("is a test!XXXXX", pdu_data.AsString());

  // Read bytes starting at the first octet of the second fragment.
  pdu_data.Fill('X');
  EXPECT_EQ(9u, pdu.Copy(&pdu_data, 6));
  EXPECT_EQ("s a test!XXXXXX", pdu_data.AsString());

  // Read the very last octet from the last fragment.
  pdu_data.Fill('X');
  EXPECT_EQ(1u, pdu.Copy(&pdu_data, 14));
  EXPECT_EQ("!XXXXXXXXXXXXXX", pdu_data.AsString());

  // Partial read across multiple fragments
  pdu_data.Fill('X');
  EXPECT_EQ(8u, pdu.Copy(&pdu_data, 5, 8));
  EXPECT_EQ("is a tesXXXXXXX", pdu_data.AsString());
}

TEST(L2CAP_PduTest, ViewFirstFragment) {
  Recombiner recombiner;

  // clang-format off

  // Partial initial fragment
  auto packet0 = PacketFromBytes(
    // ACL data header (PBF: initial fragment)
    0x01, 0x00, 0x06, 0x00,

    // Basic L2CAP header
    0x04, 0x00, 0xFF, 0xFF, 'T', 'e'
  );

  // Continuation fragment
  auto packet1 = PacketFromBytes(
    // ACL data header (PBF: continuing fragment)
    0x01, 0x10, 0x02, 0x00,

    // L2CAP PDU fragment
    's', 't'
  );

  // clang-format on

  EXPECT_TRUE(recombiner.AddFragment(std::move(packet0)));
  EXPECT_TRUE(recombiner.AddFragment(std::move(packet1)));

  PDU pdu;
  EXPECT_TRUE(recombiner.Release(&pdu));
  ASSERT_TRUE(pdu.is_valid());

  auto view = pdu.ViewFirstFragment(1);
  EXPECT_EQ("T", view.AsString());

  // Passing a large number for size. |view| should not exceed the size of the
  // first fragment.
  view = pdu.ViewFirstFragment(100);
  EXPECT_EQ("Te", view.AsString());
}

TEST(L2CAP_PduTest, Reader) {
  Recombiner recombiner;

  // clang-format off

  // Partial initial fragment
  auto packet0 = PacketFromBytes(
    // ACL data header (PBF: initial fragment)
    0x01, 0x00, 0x09, 0x00,

    // Basic L2CAP header
    0x0F, 0x00, 0xFF, 0xFF, 'R', 'e', 'a', 'd', 'i'
  );

  // Continuation fragment
  auto packet1 = PacketFromBytes(
    // ACL data header (PBF: continuing fragment)
    0x01, 0x10, 0x06, 0x00,

    // L2CAP PDU fragment
    'n', 'g', ' ', 'p', 'a', 'c'
  );

  // Continuation fragment
  auto packet2 = PacketFromBytes(
    // ACL data header (PBF: continuing fragment)
    0x01, 0x10, 0x04, 0x00,

    // L2CAP PDU fragment
    'k', 'e', 't', 's'
  );

  // clang-format on

  EXPECT_TRUE(recombiner.AddFragment(std::move(packet0)));
  EXPECT_TRUE(recombiner.AddFragment(std::move(packet1)));
  EXPECT_TRUE(recombiner.AddFragment(std::move(packet2)));

  PDU pdu;
  EXPECT_TRUE(recombiner.Release(&pdu));
  ASSERT_TRUE(pdu.is_valid());

  PDU::Reader reader(&pdu);
  EXPECT_FALSE(reader.ReadNext(16, [](const auto&) {}));
  EXPECT_FALSE(reader.ReadNext(0, [](const auto&) {}));

  // Read the entire PDU.
  EXPECT_TRUE(reader.ReadNext(15, [](const auto& data) {
    EXPECT_EQ("Reading packets", data.AsString());
  }));

  reader = PDU::Reader(&pdu);

  // Read 4 bytes (no-copy).
  EXPECT_TRUE(reader.ReadNext(
      4, [](const auto& data) { EXPECT_EQ("Read", data.AsString()); }));

  // Read across fragment boundaries which will make a copy.
  EXPECT_TRUE(reader.ReadNext(
      4, [](const auto& data) { EXPECT_EQ("ing ", data.AsString()); }));

  // Read until the end of the current fragment (no-copy). The iterator should
  // point to the next fragment.
  EXPECT_TRUE(reader.ReadNext(
      3, [](const auto& data) { EXPECT_EQ("pac", data.AsString()); }));

  // Read all of the last fragment (no-copy).
  EXPECT_TRUE(reader.ReadNext(
      4, [](const auto& data) { EXPECT_EQ("kets", data.AsString()); }));
}

}  // namespace
}  // namespace l2cap
}  // namespace bluetooth
