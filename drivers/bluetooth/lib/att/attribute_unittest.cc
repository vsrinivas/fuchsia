// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "attribute.h"

#include "garnet/drivers/bluetooth/lib/common/test_helpers.h"
#include "gtest/gtest.h"

namespace bluetooth {
namespace att {
namespace {

constexpr Handle kTestHandle = 0x0001;
constexpr common::UUID kTestType((uint16_t)0x2800);
const auto kTestValue = common::CreateStaticByteBuffer('t', 'e', 's', 't');

TEST(ATT_AttributeTest, AccessRequirementsDefault) {
  AccessRequirements reqs;
  EXPECT_FALSE(reqs.allowed());
  EXPECT_FALSE(reqs.encryption_required());
  EXPECT_FALSE(reqs.authentication_required());
  EXPECT_FALSE(reqs.authorization_required());
}

TEST(ATT_AttributeTest, AccessRequirements) {
  AccessRequirements reqs1(true, false, false);
  EXPECT_TRUE(reqs1.allowed());
  EXPECT_TRUE(reqs1.encryption_required());
  EXPECT_FALSE(reqs1.authentication_required());
  EXPECT_FALSE(reqs1.authorization_required());

  AccessRequirements reqs2(false, true, false);
  EXPECT_TRUE(reqs2.allowed());
  EXPECT_FALSE(reqs2.encryption_required());
  EXPECT_TRUE(reqs2.authentication_required());
  EXPECT_FALSE(reqs2.authorization_required());

  AccessRequirements reqs3(false, false, true);
  EXPECT_TRUE(reqs3.allowed());
  EXPECT_FALSE(reqs3.encryption_required());
  EXPECT_FALSE(reqs3.authentication_required());
  EXPECT_TRUE(reqs3.authorization_required());

  AccessRequirements reqs4(false, false, false);
  EXPECT_TRUE(reqs4.allowed());
  EXPECT_FALSE(reqs4.encryption_required());
  EXPECT_FALSE(reqs4.authentication_required());
  EXPECT_FALSE(reqs4.authorization_required());
}

TEST(ATT_AttributeTest, Default) {
  Attribute attr;
  EXPECT_FALSE(attr.is_initialized());
  EXPECT_EQ(kInvalidHandle, attr.handle());
  EXPECT_FALSE(attr.value());
  EXPECT_FALSE(attr.ReadAsync(0, [](auto, const auto&) {}));
  EXPECT_FALSE(attr.WriteAsync(0, common::BufferView(), [](auto) {}));
}

TEST(ATT_AttributeTest, Attribute) {
  Attribute attr(kTestHandle, kTestType,
                 AccessRequirements(false, false, false), AccessRequirements());
  EXPECT_TRUE(attr.is_initialized());
  EXPECT_EQ(kTestHandle, attr.handle());
  EXPECT_EQ(kTestType, attr.type());

  // Cached value
  EXPECT_FALSE(attr.value());
  attr.SetValue(kTestValue);
  ASSERT_TRUE(attr.value());
  EXPECT_TRUE(common::ContainersEqual(kTestValue, *attr.value()));
}

TEST(ATT_AttributeTest, ReadAsyncReadNotAllowed) {
  Attribute attr(kTestHandle, kTestType, AccessRequirements(),
                 AccessRequirements());
  EXPECT_FALSE(attr.ReadAsync(0, [](auto, const auto&) {}));
}

TEST(ATT_AttributeTest, ReadAsyncReadNoHandler) {
  Attribute attr(kTestHandle, kTestType,
                 AccessRequirements(false, false, false),  // read (no security)
                 AccessRequirements());                    // write not allowed
  EXPECT_FALSE(attr.ReadAsync(0, [](auto, const auto&) {}));
}

TEST(ATT_AttributeTest, ReadAsync) {
  constexpr uint16_t kTestOffset = 5;
  constexpr ErrorCode kTestStatus = ErrorCode::kNoError;

  Attribute attr(kTestHandle, kTestType,
                 AccessRequirements(false, false, false), AccessRequirements());

  bool callback_called = false;
  auto callback = [&](ErrorCode status, const auto& value) {
    EXPECT_EQ(kTestStatus, status);
    EXPECT_TRUE(common::ContainersEqual(
        common::CreateStaticByteBuffer('h', 'i'), value));
    callback_called = true;
  };

  auto handler = [&](Handle handle, uint16_t offset, const auto& result_cb) {
    EXPECT_EQ(kTestHandle, handle);
    EXPECT_EQ(kTestOffset, offset);
    EXPECT_TRUE(result_cb);

    result_cb(kTestStatus, common::CreateStaticByteBuffer('h', 'i'));
  };

  attr.set_read_handler(handler);
  EXPECT_TRUE(attr.ReadAsync(kTestOffset, callback));
  EXPECT_TRUE(callback_called);
}

TEST(ATT_AttributeTest, WriteAsyncWriteNotAllowed) {
  Attribute attr(kTestHandle, kTestType,
                 AccessRequirements(),                      // read not allowed
                 AccessRequirements(false, false, false));  // write no security
  EXPECT_FALSE(attr.WriteAsync(0, common::BufferView(), [](auto) {}));
}

TEST(ATT_AttributeTest, WriteAsyncWriteNoHandler) {
  Attribute attr(kTestHandle, kTestType, AccessRequirements(),
                 AccessRequirements(false, false, false));
  EXPECT_FALSE(attr.WriteAsync(0, common::BufferView(), [](auto) {}));
}

TEST(ATT_AttributeTest, WriteAsync) {
  constexpr uint16_t kTestOffset = 5;
  constexpr ErrorCode kTestStatus = ErrorCode::kNoError;

  Attribute attr(kTestHandle, kTestType, AccessRequirements(),
                 AccessRequirements(false, false, false));

  bool callback_called = false;
  auto callback = [&](ErrorCode status) {
    EXPECT_EQ(kTestStatus, status);
    callback_called = true;
  };

  auto handler = [&](Handle handle, uint16_t offset, const auto& value,
                     const auto& result_cb) {
    EXPECT_EQ(kTestHandle, handle);
    EXPECT_EQ(kTestOffset, offset);
    EXPECT_TRUE(common::ContainersEqual(
        common::CreateStaticByteBuffer('h', 'i'), value));
    EXPECT_TRUE(result_cb);

    result_cb(kTestStatus);
  };

  attr.set_write_handler(handler);
  EXPECT_TRUE(attr.WriteAsync(
      kTestOffset, common::CreateStaticByteBuffer('h', 'i'), callback));
  EXPECT_TRUE(callback_called);
}

TEST(ATT_AttributeTest, GroupingDeclAttr) {
  constexpr size_t kAttrCount = 0u;

  AttributeGrouping group(kTestType, kTestHandle, kAttrCount, kTestValue);

  // The grouping consists of just the group declaration and is thus complete.
  EXPECT_TRUE(group.complete());
  EXPECT_EQ(kTestType, group.group_type());
  EXPECT_EQ(kTestHandle, group.start_handle());
  EXPECT_EQ(kTestHandle, group.end_handle());
  EXPECT_EQ(1u, group.attributes().size());

  // The grouping is already complete.
  EXPECT_FALSE(group.AddAttribute(kTestType, AccessRequirements(),
                                  AccessRequirements()));

  const Attribute& decl_attr = group.attributes()[0];
  EXPECT_EQ(kTestHandle, decl_attr.handle());
  EXPECT_EQ(kTestType, decl_attr.type());
  ASSERT_TRUE(decl_attr.value());
  EXPECT_TRUE(common::ContainersEqual(kTestValue, *decl_attr.value()));
  EXPECT_TRUE(decl_attr.read_reqs().allowed());
  EXPECT_FALSE(decl_attr.read_reqs().encryption_required());
  EXPECT_FALSE(decl_attr.read_reqs().authentication_required());
  EXPECT_FALSE(decl_attr.read_reqs().authorization_required());
  EXPECT_FALSE(decl_attr.write_reqs().allowed());
}

TEST(ATT_AttributeTest, GroupingAddAttribute) {
  constexpr size_t kAttrCount = 2;
  constexpr common::UUID kTestType1((uint16_t)0x0001);
  constexpr common::UUID kTestType2((uint16_t)0x0002);
  constexpr common::UUID kTestType3((uint16_t)0x0003);

  AttributeGrouping group(kTestType, kTestHandle, kAttrCount, kTestValue);
  EXPECT_FALSE(group.complete());
  EXPECT_EQ(kTestHandle, group.start_handle());
  EXPECT_EQ(kTestHandle + kAttrCount, group.end_handle());

  Attribute* attr = group.AddAttribute(kTestType1, AccessRequirements(),
                                       AccessRequirements());
  ASSERT_TRUE(attr);
  EXPECT_EQ(kTestType1, attr->type());
  EXPECT_EQ(kTestHandle + 1, attr->handle());

  // The group is not complete until |kAttrCount| attributes have been added.
  EXPECT_FALSE(group.complete());
  EXPECT_EQ(2u, group.attributes().size());

  attr = group.AddAttribute(kTestType2, AccessRequirements(),
                            AccessRequirements());
  ASSERT_TRUE(attr);
  EXPECT_EQ(kTestType2, attr->type());

  EXPECT_TRUE(group.complete());
  EXPECT_EQ(group.end_handle(), attr->handle());
  EXPECT_EQ(3u, group.attributes().size());

  EXPECT_FALSE(group.AddAttribute(kTestType3, AccessRequirements(),
                                  AccessRequirements()));
}

}  // namespace
}  // namespace att
}  // namespace bluetooth
