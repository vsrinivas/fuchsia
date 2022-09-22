// Copyright (c) 2022 The Fuchsia Authors
//
// Permission to use, copy, modify, and/or distribute this software for any purpose with or without
// fee is hereby granted, provided that the above copyright notice and this permission notice
// appear in all copies.
//
// THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS
// SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE
// AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
// WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT,
// NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE
// OF THIS SOFTWARE.

#include "src/connectivity/wlan/drivers/third_party/nxp/nxpfmac/ies.h"

#include <zxtest/zxtest.h>

namespace {

using wlan::nxpfmac::Ie;
using wlan::nxpfmac::IeIterator;
using wlan::nxpfmac::IeView;

struct IeHdr {
  uint8_t type;
  uint8_t len;
};

struct FooIe {
  static constexpr uint8_t kIeType = 0x01;
  FooIe() : hdr{kIeType, sizeof(data)} {}
  IeHdr hdr;
  uint8_t data[12];
};

struct BarIe {
  static constexpr uint8_t kIeType = 0x02;
  BarIe() : hdr{kIeType, sizeof(data)} {}
  IeHdr hdr;
  uint8_t data[4];
};

struct BazIe {
  static constexpr uint8_t kIeType = 0x03;
  BazIe() : hdr{kIeType, sizeof(data)} {}
  IeHdr hdr;
  uint8_t data[4];
};

TEST(IesTest, IsValidIeRange) {
  const struct {
    FooIe foo;
    BarIe bar;
    BazIe baz;
  } test_ies;
  auto ies = reinterpret_cast<const uint8_t*>(&test_ies);

  ASSERT_TRUE(wlan::nxpfmac::is_valid_ie_range(ies, sizeof(test_ies)));
  // Ranges that are too short are not valid.
  ASSERT_FALSE(wlan::nxpfmac::is_valid_ie_range(ies, sizeof(test_ies) - 1));
  // Ranges that are too long are not valid either.
  ASSERT_FALSE(wlan::nxpfmac::is_valid_ie_range(ies, sizeof(test_ies) + 1));
}

TEST(IesTest, ConstructIeView) { ASSERT_NO_FATAL_FAILURE(IeView(nullptr, 0)); }

TEST(IesTest, IeViewGet) {
  const struct {
    FooIe foo;
    BarIe bar;
  } test_ies;

  ASSERT_EQ(sizeof(FooIe::data), test_ies.foo.hdr.len);
  ASSERT_EQ(sizeof(BarIe::data), test_ies.bar.hdr.len);

  const IeView view(reinterpret_cast<const uint8_t*>(&test_ies), sizeof(test_ies));

  // Verify that the view contains a foo IE
  auto foo = view.get(FooIe::kIeType);
  ASSERT_TRUE(foo.has_value());
  Ie ie = foo.value();
  ASSERT_EQ(FooIe::kIeType, ie.type());
  ASSERT_EQ(sizeof(FooIe::data), ie.size());
  ASSERT_EQ(sizeof(FooIe), ie.raw_size());
  ASSERT_EQ(test_ies.foo.data, ie.data());
  ASSERT_EQ(reinterpret_cast<const uint8_t*>(&test_ies.foo), ie.raw_data());
  const FooIe* foo_ptr = view.get_as<FooIe>(FooIe::kIeType);
  ASSERT_NOT_NULL(foo_ptr);
  ASSERT_EQ(&test_ies.foo, foo_ptr);

  // And that it contains a bar IE
  auto bar = view.get(BarIe::kIeType);
  ASSERT_TRUE(bar.has_value());
  ie = bar.value();
  ASSERT_EQ(reinterpret_cast<const uint8_t*>(&test_ies.bar), ie.raw_data());
  ASSERT_EQ(BarIe::kIeType, ie.type());
  ASSERT_EQ(sizeof(BarIe::data), ie.size());
  ASSERT_EQ(sizeof(BarIe), ie.raw_size());
  ASSERT_EQ(test_ies.bar.data, ie.data());
  const BarIe* bar_ptr = view.get_as<BarIe>(BarIe::kIeType);
  ASSERT_NOT_NULL(bar_ptr);
  ASSERT_EQ(&test_ies.bar, bar_ptr);

  // But it does not contain a baz IE
  ASSERT_FALSE(view.get(BazIe::kIeType).has_value());
}

TEST(IesTest, IeWithInvalidLength) {
  // Verify that IEs that do not correctly fit into the provided length are ignored.

  // Local structs can't have static data members so place this constant outside the struct.
  constexpr uint8_t kSuspiciousIeType = 0xaa;
  struct SuspiciousIe {
    IeHdr hdr;
    uint8_t data[32];
  };

  const struct {
    FooIe foo;
    BarIe bar;
    // The length here is way too big for what the Ie holds.
    SuspiciousIe invalid{.hdr{.type = kSuspiciousIeType, .len = 0xA0}};
  } test_ies;

  const IeView view(reinterpret_cast<const uint8_t*>(&test_ies), sizeof(test_ies));

  ASSERT_TRUE(view.get(FooIe::kIeType).has_value());
  ASSERT_TRUE(view.get(BarIe::kIeType).has_value());
  // This IE should not appear in the view, it cannot be safely accessed.
  ASSERT_FALSE(view.get(kSuspiciousIeType).has_value());
}

TEST(IesTest, IteratorTest) {
  const struct {
    BazIe baz;
    FooIe foo;
    BarIe bar;
  } test_ies;
  const uint8_t* data = reinterpret_cast<const uint8_t*>(&test_ies);

  const IeView view(data, sizeof(test_ies));

  IeIterator it = view.begin();
  ASSERT_NE(it, view.end());
  ASSERT_EQ(BazIe::kIeType, it->type());
  ASSERT_EQ(test_ies.baz.data, (*it).data());
  ASSERT_EQ(reinterpret_cast<const uint8_t*>(&test_ies.baz), it->raw_data());

  ++it;
  ASSERT_NE(it, view.end());
  ASSERT_EQ(FooIe::kIeType, (*it).type());
  ASSERT_EQ(test_ies.foo.data, it->data());
  ASSERT_EQ(reinterpret_cast<const uint8_t*>(&test_ies.foo), it->raw_data());

  ++it;
  ASSERT_NE(it, view.end());
  ASSERT_EQ(BarIe::kIeType, it->type());
  ASSERT_EQ(test_ies.bar.data, it->data());
  ASSERT_EQ(reinterpret_cast<const uint8_t*>(&test_ies.bar), (*it).raw_data());

  ++it;
  ASSERT_EQ(it, view.end());
}

}  // namespace
