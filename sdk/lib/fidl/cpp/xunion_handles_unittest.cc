// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fidl/cpp/comparison.h>
#include <lib/fidl/cpp/test/test_util.h>
#include <zircon/status.h>
#include <zircon/types.h>

#include <vector>

#include <fidl/test/unionmigration/cpp/fidl.h>
#include <gtest/gtest.h>

namespace {

class HandleChecker {
 public:
  HandleChecker() = default;

  size_t size() const { return events_.size(); }

  void AddEvent(zx_handle_t event) {
    zx_handle_t new_event;
    ASSERT_EQ(zx_handle_duplicate(event, ZX_RIGHT_SAME_RIGHTS, &new_event), ZX_OK);
    events_.emplace_back(zx::event(new_event));
  }

  void CheckEvents() {
    for (size_t i = 0; i < events_.size(); ++i) {
      zx_info_handle_count_t info = {};
      auto status =
          events_[i].get_info(ZX_INFO_HANDLE_COUNT, &info, sizeof(info), nullptr, nullptr);
      ZX_ASSERT(status == ZX_OK);
      EXPECT_EQ(info.handle_count, 1U) << "Handle not freed " << (i + 1) << '/' << events_.size();
    }
  }

 private:
  std::vector<zx::event> events_;
};

}  // namespace

// TODO(fxbug.dev/60033): These tests can be moved to GIDL once handles are
// supported on host
namespace fidl {
namespace {

// Tests that decoding unknown handles for a non-resource type fails, and closes
// handles.
TEST(XUnion, UnknownHandlesValue) {
  using fidl::test::unionmigration::BasicXUnionStruct;

  std::vector<uint8_t> bytes = {
      0x11, 0xba, 0x5e, 0xba, 0x00, 0x00, 0x00, 0x00,  // invalid ordinal + padding
      0x08, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00,  // envelope: 8 bytes, 3 handles
      0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,  // envelope: data is present
      0xde, 0xad, 0xbe, 0xef, 0x5c, 0xa1, 0xab, 0x1e,  // fake out-of-line dat
  };

  zx_handle_t h1, h2, h3;
  ASSERT_EQ(ZX_OK, zx_event_create(0, &h1));
  ASSERT_EQ(ZX_OK, zx_event_create(0, &h2));
  ASSERT_EQ(ZX_OK, zx_event_create(0, &h3));
  std::vector<zx_handle_t> handles = {h1, h2, h3};

  HandleChecker checker;
  checker.AddEvent(h1);
  checker.AddEvent(h2);
  checker.AddEvent(h3);

  Message message(BytePart(bytes.data(), bytes.size(), bytes.size()),
                  HandlePart(handles.data(), handles.size(), handles.size()));
  const char* error;
  auto status = message.Decode(BasicXUnionStruct::FidlType, &error);
  ASSERT_EQ(status, ZX_ERR_INVALID_ARGS);
  ASSERT_STREQ(error, "received unknown handles for a non-resource type");

  checker.CheckEvents();
}

// Tests that decoding unknown bytes for a resource type succeeds
TEST(XUnion, UnknownBytesResource) {
  using fidl::test::unionmigration::BasicResourceXUnion;
  using fidl::test::unionmigration::BasicResourceXUnionStruct;

  std::vector<uint8_t> bytes = {
      0x11, 0xba, 0x5e, 0xba, 0x00, 0x00, 0x00, 0x00,  // invalid ordinal + padding
      0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // envelope: 8 bytes, 0 handles
      0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,  // envelope: data is present
      0xde, 0xad, 0xbe, 0xef, 0x5c, 0xa1, 0xab, 0x1e,  // fake out-of-line dat
  };

  Message message(BytePart(bytes.data(), bytes.size(), bytes.size()), HandlePart());
  auto status = message.Decode(BasicResourceXUnionStruct::FidlType, nullptr);
  ASSERT_EQ(status, ZX_OK);

  fidl::Decoder decoder(std::move(message));
  BasicResourceXUnionStruct result;
  BasicResourceXUnionStruct::Decode(&decoder, &result, 0);

  const BasicResourceXUnion& xu = result.val;
  auto actual_bytes = xu.UnknownBytes();
  auto unknown_data = std::vector<uint8_t>(bytes.cbegin() + sizeof(fidl_xunion_t), bytes.cend());
  EXPECT_TRUE(fidl::test::util::cmp_payload(actual_bytes->data(), actual_bytes->size(),
                                            unknown_data.data(), unknown_data.size()));
}

// Tests that decoding unknown bytes for a value type succeeds
TEST(XUnion, UnknownBytesValue) {
  using fidl::test::unionmigration::BasicXUnion;
  using fidl::test::unionmigration::BasicXUnionStruct;

  std::vector<uint8_t> bytes = {
      0x11, 0xba, 0x5e, 0xba, 0x00, 0x00, 0x00, 0x00,  // invalid ordinal + padding
      0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // envelope: 8 bytes, 0 handles
      0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,  // envelope: data is present
      0xde, 0xad, 0xbe, 0xef, 0x5c, 0xa1, 0xab, 0x1e,  // fake out-of-line dat
  };

  Message message(BytePart(bytes.data(), bytes.size(), bytes.size()), HandlePart());
  auto status = message.Decode(BasicXUnionStruct::FidlType, nullptr);
  ASSERT_EQ(status, ZX_OK);

  fidl::Decoder decoder(std::move(message));
  BasicXUnionStruct result;
  BasicXUnionStruct::Decode(&decoder, &result, 0);

  const BasicXUnion& xu = result.val;
  auto actual_bytes = xu.UnknownBytes();
  auto unknown_data = std::vector<uint8_t>(bytes.cbegin() + sizeof(fidl_xunion_t), bytes.cend());
  EXPECT_TRUE(fidl::test::util::cmp_payload(actual_bytes->data(), actual_bytes->size(),
                                            unknown_data.data(), unknown_data.size()));
}

// Tests that decoding and re-encoding unknown handles for a resource type succeeds
TEST(XUnion, UnknownHandlesResource) {
  using fidl::test::unionmigration::BasicResourceXUnion;
  using fidl::test::unionmigration::BasicResourceXUnionStruct;

  std::vector<uint8_t> bytes = {
      0x11, 0xba, 0x5e, 0xba, 0x00, 0x00, 0x00, 0x00,  // invalid ordinal + padding
      0x08, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00,  // envelope: 8 bytes, 3 handles
      0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,  // envelope: data is present
      0xde, 0xad, 0xbe, 0xef, 0x5c, 0xa1, 0xab, 0x1e,  // fake out-of-line dat
  };
  auto unknown_data = std::vector<uint8_t>(bytes.cbegin() + sizeof(fidl_xunion_t), bytes.cend());

  zx_handle_t h1, h2, h3;
  ASSERT_EQ(ZX_OK, zx_event_create(0, &h1));
  ASSERT_EQ(ZX_OK, zx_event_create(0, &h2));
  ASSERT_EQ(ZX_OK, zx_event_create(0, &h3));
  std::vector<zx_handle_t> handles = {h1, h2, h3};

  auto result = test::util::DecodedBytes<BasicResourceXUnionStruct>(bytes, handles);
  const BasicResourceXUnion& xu = result.val;

  // compare
  auto actual_bytes = xu.UnknownBytes();
  EXPECT_TRUE(test::util::cmp_payload(actual_bytes->data(), actual_bytes->size(),
                                      unknown_data.data(), unknown_data.size()));

  auto actual_handles = xu.UnknownHandles();
  ASSERT_EQ(actual_handles->size(), 3ul);
  for (int i = 0; i < 3; ++i) {
    EXPECT_EQ(handles[i], (*actual_handles)[i].get());
  }

  EXPECT_TRUE(test::util::ValueToBytes(std::move(result), bytes, handles));
}

}  // namespace
}  // namespace fidl
