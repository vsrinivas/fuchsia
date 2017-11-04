// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/drivers/bluetooth/lib/gatt/local_service_manager.h"

#include "gtest/gtest.h"

#include "garnet/drivers/bluetooth/lib/common/test_helpers.h"
#include "garnet/drivers/bluetooth/lib/gatt/gatt.h"

namespace btlib {
namespace gatt {
namespace {

constexpr common::UUID kTestType16((uint16_t)0xdead);
constexpr common::UUID kTestType32((uint32_t)0xdeadbeef);

// The first characteristic value attribute of the first service has handle
// number 3.
constexpr att::Handle kFirstChrcValueHandle = 0x0003;

// The first descroptor of the first characteristic of the first service has
// handle number 4.
constexpr att::Handle kFirstDescrHandle = 0x0004;

void NopReadHandler(IdType, IdType, uint16_t, const ReadResponder&) {}

void NopWriteHandler(IdType,
                     IdType,
                     uint16_t,
                     const common::ByteBuffer&,
                     const WriteResponder&) {}

TEST(GATT_LocalServiceManagerTest, EmptyService) {
  LocalServiceManager mgr;

  auto service = std::make_unique<Service>(true /* primary */, kTestType16);
  auto id1 =
      mgr.RegisterService(std::move(service), NopReadHandler, NopWriteHandler);
  EXPECT_NE(0u, id1);

  service = std::make_unique<Service>(false /* primary */, kTestType32);
  auto id2 =
      mgr.RegisterService(std::move(service), NopReadHandler, NopWriteHandler);
  EXPECT_NE(0u, id2);

  EXPECT_EQ(2u, mgr.database()->groupings().size());

  auto iter = mgr.database()->groupings().begin();

  EXPECT_TRUE(iter->complete());
  EXPECT_EQ(1u, iter->attributes().size());
  EXPECT_TRUE(iter->active());
  EXPECT_EQ(0x0001, iter->start_handle());
  EXPECT_EQ(0x0001, iter->end_handle());
  EXPECT_EQ(types::kPrimaryService, iter->group_type());
  EXPECT_TRUE(common::ContainersEqual(
      common::CreateStaticByteBuffer(0xad, 0xde), iter->decl_value()));

  iter++;

  EXPECT_TRUE(iter->complete());
  EXPECT_EQ(1u, iter->attributes().size());
  EXPECT_TRUE(iter->active());
  EXPECT_EQ(0x0002, iter->start_handle());
  EXPECT_EQ(0x0002, iter->end_handle());
  EXPECT_EQ(types::kSecondaryService, iter->group_type());
  EXPECT_TRUE(common::ContainersEqual(
      common::CreateStaticByteBuffer(0xFB, 0x34, 0x9B, 0x5F, 0x80, 0x00, 0x00,
                                     0x80, 0x00, 0x10, 0x00, 0x00, 0xef, 0xbe,
                                     0xad, 0xde),
      iter->decl_value()));
}

TEST(GATT_LocalServiceManagerTest, UnregisterService) {
  LocalServiceManager mgr;

  auto service = std::make_unique<Service>(true /* primary */, kTestType16);
  auto id1 =
      mgr.RegisterService(std::move(service), NopReadHandler, NopWriteHandler);
  EXPECT_NE(0u, id1);
  EXPECT_EQ(1u, mgr.database()->groupings().size());

  // Unknown id
  EXPECT_FALSE(mgr.UnregisterService(id1 + 1));

  // Success
  EXPECT_TRUE(mgr.UnregisterService(id1));
  EXPECT_TRUE(mgr.database()->groupings().empty());

  // |id1| becomes unknown
  EXPECT_FALSE(mgr.UnregisterService(id1));
}

TEST(GATT_LocalServiceManagerTest, RegisterCharacteristic) {
  LocalServiceManager mgr;

  constexpr IdType kChrcId = 0;
  constexpr uint8_t kChrcProps = Property::kRead;
  constexpr common::UUID kTestChrcType((uint16_t)0xabcd);
  const att::AccessRequirements kReadReqs(true, true, true);
  const att::AccessRequirements kWriteReqs;

  auto service = std::make_unique<Service>(true /* primary */, kTestType16);
  service->AddCharacteristic(std::make_unique<Characteristic>(
      kChrcId, kTestChrcType, kChrcProps, 0, kReadReqs, kWriteReqs));
  auto id1 =
      mgr.RegisterService(std::move(service), NopReadHandler, NopWriteHandler);
  EXPECT_NE(0u, id1);

  ASSERT_EQ(1u, mgr.database()->groupings().size());
  const auto& grouping = mgr.database()->groupings().front();
  EXPECT_TRUE(grouping.complete());

  const auto& attrs = grouping.attributes();
  ASSERT_EQ(3u, attrs.size());

  att::Handle srvc_handle = attrs[0].handle();
  EXPECT_EQ(att::kHandleMin, srvc_handle);

  // Characteristic declaration
  EXPECT_EQ(srvc_handle + 1, attrs[1].handle());
  EXPECT_EQ(types::kCharacteristicDeclaration, attrs[1].type());
  EXPECT_EQ(att::AccessRequirements(false, false, false), attrs[1].read_reqs());
  EXPECT_EQ(att::AccessRequirements(), attrs[1].write_reqs());
  EXPECT_TRUE(attrs[1].value());

  // clang-format off
  const auto kDeclValue = common::CreateStaticByteBuffer(
      0x02,        // properties
      0x03, 0x00,  // value handle
      0xcd, 0xab   // UUID
  );
  // clang-format on
  EXPECT_TRUE(common::ContainersEqual(kDeclValue, *attrs[1].value()));

  // Characteristic value
  EXPECT_EQ(srvc_handle + 2, attrs[2].handle());
  EXPECT_EQ(kTestChrcType, attrs[2].type());
  EXPECT_EQ(kReadReqs, attrs[2].read_reqs());
  EXPECT_EQ(kWriteReqs, attrs[2].write_reqs());

  // This value is dynamic.
  EXPECT_FALSE(attrs[2].value());
}

TEST(GATT_LocalServiceManagerTest, RegisterCharacteristic32) {
  LocalServiceManager mgr;

  constexpr IdType kChrcId = 0;
  constexpr uint8_t kChrcProps = Property::kRead;
  constexpr common::UUID kTestChrcType((uint32_t)0xdeadbeef);
  const att::AccessRequirements kReadReqs(true, true, true);
  const att::AccessRequirements kWriteReqs;

  auto service = std::make_unique<Service>(true /* primary */, kTestType16);
  service->AddCharacteristic(std::make_unique<Characteristic>(
      kChrcId, kTestChrcType, kChrcProps, 0, kReadReqs, kWriteReqs));
  auto id1 =
      mgr.RegisterService(std::move(service), NopReadHandler, NopWriteHandler);
  EXPECT_NE(0u, id1);

  ASSERT_EQ(1u, mgr.database()->groupings().size());
  const auto& grouping = mgr.database()->groupings().front();
  EXPECT_TRUE(grouping.complete());

  const auto& attrs = grouping.attributes();
  ASSERT_EQ(3u, attrs.size());

  att::Handle srvc_handle = attrs[0].handle();
  EXPECT_EQ(att::kHandleMin, srvc_handle);

  // Characteristic declaration
  EXPECT_EQ(srvc_handle + 1, attrs[1].handle());
  EXPECT_EQ(types::kCharacteristicDeclaration, attrs[1].type());
  EXPECT_EQ(att::AccessRequirements(false, false, false), attrs[1].read_reqs());
  EXPECT_EQ(att::AccessRequirements(), attrs[1].write_reqs());
  EXPECT_TRUE(attrs[1].value());

  const auto kDeclValue = common::CreateStaticByteBuffer(
      0x02,        // properties
      0x03, 0x00,  // value handle

      // The 32-bit UUID will be stored as 128-bit
      0xFB, 0x34, 0x9B, 0x5F, 0x80, 0x00, 0x00, 0x80, 0x00, 0x10, 0x00, 0x00,
      0xef, 0xbe, 0xad, 0xde);
  EXPECT_TRUE(common::ContainersEqual(kDeclValue, *attrs[1].value()));

  // Characteristic value
  EXPECT_EQ(srvc_handle + 2, attrs[2].handle());
  EXPECT_EQ(kTestChrcType, attrs[2].type());
  EXPECT_EQ(kReadReqs, attrs[2].read_reqs());
  EXPECT_EQ(kWriteReqs, attrs[2].write_reqs());

  // This value is dynamic.
  EXPECT_FALSE(attrs[2].value());
}

TEST(GATT_LocalServiceManagerTest, RegisterCharacteristic128) {
  LocalServiceManager mgr;

  constexpr IdType kChrcId = 0;
  constexpr uint8_t kChrcProps = Property::kRead;
  common::UUID kTestChrcType;
  EXPECT_TRUE(common::StringToUuid("00112233-4455-6677-8899-AABBCCDDEEFF",
                                   &kTestChrcType));
  const att::AccessRequirements kReadReqs(true, true, true);
  const att::AccessRequirements kWriteReqs;

  auto service = std::make_unique<Service>(true /* primary */, kTestType16);
  service->AddCharacteristic(std::make_unique<Characteristic>(
      kChrcId, kTestChrcType, kChrcProps, 0, kReadReqs, kWriteReqs));
  auto id1 =
      mgr.RegisterService(std::move(service), NopReadHandler, NopWriteHandler);
  EXPECT_NE(0u, id1);

  ASSERT_EQ(1u, mgr.database()->groupings().size());
  const auto& grouping = mgr.database()->groupings().front();
  EXPECT_TRUE(grouping.complete());

  const auto& attrs = grouping.attributes();
  ASSERT_EQ(3u, attrs.size());

  att::Handle srvc_handle = attrs[0].handle();
  EXPECT_EQ(att::kHandleMin, srvc_handle);

  // Characteristic declaration
  EXPECT_EQ(srvc_handle + 1, attrs[1].handle());
  EXPECT_EQ(types::kCharacteristicDeclaration, attrs[1].type());
  EXPECT_EQ(att::AccessRequirements(false, false, false), attrs[1].read_reqs());
  EXPECT_EQ(att::AccessRequirements(), attrs[1].write_reqs());
  EXPECT_TRUE(attrs[1].value());

  const auto kDeclValue = common::CreateStaticByteBuffer(
      0x02,        // properties
      0x03, 0x00,  // value handle

      // 128-bit UUID
      0xFF, 0xEE, 0xDD, 0xCC, 0xBB, 0xAA, 0x99, 0x88, 0x77, 0x66, 0x55, 0x44,
      0x33, 0x22, 0x11, 0x00);
  EXPECT_TRUE(common::ContainersEqual(kDeclValue, *attrs[1].value()));

  // Characteristic value
  EXPECT_EQ(srvc_handle + 2, attrs[2].handle());
  EXPECT_EQ(kTestChrcType, attrs[2].type());
  EXPECT_EQ(kReadReqs, attrs[2].read_reqs());
  EXPECT_EQ(kWriteReqs, attrs[2].write_reqs());

  // This value is dynamic.
  EXPECT_FALSE(attrs[2].value());
}

TEST(GATT_LocalServiceManagerTest, RegisterCharacteristicSorted) {
  LocalServiceManager mgr;
  const att::AccessRequirements kReadReqs, kWriteReqs;

  constexpr common::UUID kType16((uint16_t)0xbeef);
  constexpr common::UUID kType128((uint32_t)0xdeadbeef);

  constexpr IdType kChrcId0 = 0;
  constexpr uint8_t kChrcProps0 = 0;
  constexpr IdType kChrcId1 = 1;
  constexpr uint8_t kChrcProps1 = 1;
  constexpr IdType kChrcId2 = 2;
  constexpr uint8_t kChrcProps2 = 2;
  constexpr IdType kChrcId3 = 3;
  constexpr uint8_t kChrcProps3 = 3;

  auto service = std::make_unique<Service>(true /* primary */, kTestType16);
  service->AddCharacteristic(std::make_unique<Characteristic>(
      kChrcId0, kType128, kChrcProps0, 0, kReadReqs, kWriteReqs));
  service->AddCharacteristic(std::make_unique<Characteristic>(
      kChrcId1, kType16, kChrcProps1, 0, kReadReqs, kWriteReqs));
  service->AddCharacteristic(std::make_unique<Characteristic>(
      kChrcId2, kType128, kChrcProps2, 0, kReadReqs, kWriteReqs));
  service->AddCharacteristic(std::make_unique<Characteristic>(
      kChrcId3, kType16, kChrcProps3, 0, kReadReqs, kWriteReqs));
  auto id1 =
      mgr.RegisterService(std::move(service), NopReadHandler, NopWriteHandler);
  EXPECT_NE(0u, id1);

  ASSERT_EQ(1u, mgr.database()->groupings().size());
  const auto& grouping = mgr.database()->groupings().front();
  EXPECT_TRUE(grouping.complete());

  const auto& attrs = grouping.attributes();
  ASSERT_EQ(9u, attrs.size());

  // The declaration attributes should be sorted by service type (16-bit UUIDs
  // first).
  ASSERT_TRUE(attrs[1].value());
  EXPECT_EQ(kChrcProps1, (*attrs[1].value())[0]);
  ASSERT_TRUE(attrs[3].value());
  EXPECT_EQ(kChrcProps3, (*attrs[3].value())[0]);
  ASSERT_TRUE(attrs[5].value());
  EXPECT_EQ(kChrcProps0, (*attrs[5].value())[0]);
  ASSERT_TRUE(attrs[7].value());
  EXPECT_EQ(kChrcProps2, (*attrs[7].value())[0]);
}

TEST(GATT_LocalServiceManagerTest, RegisterDescriptor) {
  LocalServiceManager mgr;
  const att::AccessRequirements kReadReqs, kWriteReqs;

  constexpr common::UUID kChrcType16((uint16_t)0x1234);
  constexpr common::UUID kDescType16((uint16_t)0x5678);

  auto service = std::make_unique<Service>(true /* primary */, kTestType16);
  auto chrc = std::make_unique<Characteristic>(0, kChrcType16, 0, 0, kReadReqs,
                                               kWriteReqs);
  chrc->AddDescriptor(
      std::make_unique<Descriptor>(1, kDescType16, kReadReqs, kWriteReqs));
  service->AddCharacteristic(std::move(chrc));

  EXPECT_NE(0u, mgr.RegisterService(std::move(service), NopReadHandler,
                                    NopWriteHandler));

  ASSERT_EQ(1u, mgr.database()->groupings().size());
  const auto& grouping = mgr.database()->groupings().front();
  EXPECT_TRUE(grouping.complete());

  const auto& attrs = grouping.attributes();
  ASSERT_EQ(4u, attrs.size());
  EXPECT_EQ(types::kCharacteristicDeclaration, attrs[1].type());
  EXPECT_EQ(kChrcType16, attrs[2].type());
  EXPECT_EQ(kDescType16, attrs[3].type());
  EXPECT_FALSE(attrs[3].value());
}

TEST(GATT_LocalServiceManagerTest, DuplicateChrcIds) {
  LocalServiceManager mgr;
  const att::AccessRequirements kReadReqs, kWriteReqs;

  constexpr common::UUID kChrcType16((uint16_t)0x1234);

  auto service = std::make_unique<Service>(true /* primary */, kTestType16);

  // Use same characteristic ID twice.
  service->AddCharacteristic(std::make_unique<Characteristic>(
      0, kChrcType16, 0, 0, kReadReqs, kWriteReqs));
  service->AddCharacteristic(std::make_unique<Characteristic>(
      0, kChrcType16, 0, 0, kReadReqs, kWriteReqs));

  EXPECT_EQ(0u, mgr.RegisterService(std::move(service), NopReadHandler,
                                    NopWriteHandler));
}

TEST(GATT_LocalServiceManagerTest, DuplicateDescIds) {
  LocalServiceManager mgr;
  const att::AccessRequirements kReadReqs, kWriteReqs;

  constexpr common::UUID kChrcType16((uint16_t)0x1234);
  constexpr common::UUID kDescType16((uint16_t)0x5678);

  auto service = std::make_unique<Service>(true /* primary */, kTestType16);

  // Use same descriptor ID twice.
  auto chrc = std::make_unique<Characteristic>(0, kChrcType16, 0, 0, kReadReqs,
                                               kWriteReqs);
  chrc->AddDescriptor(
      std::make_unique<Descriptor>(1, kDescType16, kReadReqs, kWriteReqs));
  chrc->AddDescriptor(
      std::make_unique<Descriptor>(1, kDescType16, kReadReqs, kWriteReqs));
  service->AddCharacteristic(std::move(chrc));

  EXPECT_EQ(0u, mgr.RegisterService(std::move(service), NopReadHandler,
                                    NopWriteHandler));
}

TEST(GATT_LocalServiceManagerTest, DuplicateChrcAndDescIds) {
  LocalServiceManager mgr;
  const att::AccessRequirements kReadReqs, kWriteReqs;

  constexpr common::UUID kChrcType16((uint16_t)0x1234);
  constexpr common::UUID kDescType16((uint16_t)0x5678);

  auto service = std::make_unique<Service>(true /* primary */, kTestType16);

  // Use same descriptor ID twice.
  auto chrc = std::make_unique<Characteristic>(0, kChrcType16, 0, 0, kReadReqs,
                                               kWriteReqs);
  chrc->AddDescriptor(
      std::make_unique<Descriptor>(0, kDescType16, kReadReqs, kWriteReqs));
  service->AddCharacteristic(std::move(chrc));

  EXPECT_EQ(0u, mgr.RegisterService(std::move(service), NopReadHandler,
                                    NopWriteHandler));
}

TEST(GATT_LocalServiceManagerTest, ReadCharacteristicNoReadPermission) {
  LocalServiceManager mgr;
  const att::AccessRequirements kReadReqs, kWriteReqs;
  constexpr common::UUID kChrcType16((uint16_t)0x1234);
  constexpr IdType kChrcId = 5;

  auto service = std::make_unique<Service>(true /* primary */, kTestType16);
  service->AddCharacteristic(std::make_unique<Characteristic>(
      kChrcId, kChrcType16, Property::kRead, 0, kReadReqs, kWriteReqs));

  bool called = false;
  auto read_cb = [&called](auto, auto, auto, auto&) { called = true; };

  EXPECT_NE(0u,
            mgr.RegisterService(std::move(service), read_cb, NopWriteHandler));

  auto* attr = mgr.database()->FindAttribute(kFirstChrcValueHandle);
  ASSERT_TRUE(attr);
  EXPECT_EQ(kChrcType16, attr->type());

  bool result_called = false;
  auto result_cb = [&result_called](auto, const auto&) {
    result_called = true;
  };

  EXPECT_FALSE(attr->ReadAsync(0, result_cb));
  EXPECT_FALSE(called);
  EXPECT_FALSE(result_called);
}

TEST(GATT_LocalServiceManagerTest, ReadCharacteristicNoReadProperty) {
  LocalServiceManager mgr;
  constexpr common::UUID kChrcType16((uint16_t)0x1234);
  constexpr IdType kChrcId = 5;

  // Characteristic is readable but doesn't have the "read" property.
  const att::AccessRequirements kReadReqs(false, false, false);
  const att::AccessRequirements kWriteReqs;

  auto service = std::make_unique<Service>(true /* primary */, kTestType16);
  service->AddCharacteristic(std::make_unique<Characteristic>(
      kChrcId, kChrcType16, 0, 0, kReadReqs, kWriteReqs));

  bool called = false;
  auto read_cb = [&called](auto, auto, auto, auto&) { called = true; };

  EXPECT_NE(0u,
            mgr.RegisterService(std::move(service), read_cb, NopWriteHandler));

  auto* attr = mgr.database()->FindAttribute(kFirstChrcValueHandle);
  ASSERT_TRUE(attr);
  EXPECT_EQ(kChrcType16, attr->type());

  att::ErrorCode ecode = att::ErrorCode::kNoError;
  auto result_cb = [&ecode](auto code, const auto&) { ecode = code; };

  EXPECT_TRUE(attr->ReadAsync(0, result_cb));

  // The error should be handled internally and not reach |read_cb|.
  EXPECT_FALSE(called);
  EXPECT_EQ(att::ErrorCode::kReadNotPermitted, ecode);
}

TEST(GATT_LocalServiceManagerTest, ReadCharacteristic) {
  LocalServiceManager mgr;
  constexpr common::UUID kChrcType16((uint16_t)0x1234);
  constexpr IdType kChrcId = 5;
  constexpr uint16_t kOffset = 10;

  const auto kTestValue = common::CreateStaticByteBuffer('f', 'o', 'o');

  const att::AccessRequirements kReadReqs(false, false, false);
  const att::AccessRequirements kWriteReqs;

  auto service = std::make_unique<Service>(true /* primary */, kTestType16);
  service->AddCharacteristic(std::make_unique<Characteristic>(
      kChrcId, kChrcType16, Property::kRead, 0, kReadReqs, kWriteReqs));

  bool called = false;
  IdType svc_id;
  auto read_cb = [&](auto cb_svc_id, auto id, auto offset,
                     const auto& responder) {
    called = true;
    EXPECT_EQ(svc_id, cb_svc_id);
    EXPECT_EQ(kChrcId, id);
    EXPECT_EQ(kOffset, offset);
    responder(att::ErrorCode::kNoError, kTestValue);
  };

  svc_id = mgr.RegisterService(std::move(service), read_cb, NopWriteHandler);
  ASSERT_NE(0u, svc_id);

  auto* attr = mgr.database()->FindAttribute(kFirstChrcValueHandle);
  ASSERT_TRUE(attr);
  EXPECT_EQ(kChrcType16, attr->type());

  att::ErrorCode ecode = att::ErrorCode::kUnlikelyError;
  auto result_cb = [&ecode, &kTestValue](auto code, const auto& value) {
    ecode = code;
    EXPECT_TRUE(common::ContainersEqual(kTestValue, value));
  };

  EXPECT_TRUE(attr->ReadAsync(kOffset, result_cb));

  EXPECT_TRUE(called);
  EXPECT_EQ(att::ErrorCode::kNoError, ecode);
}

TEST(GATT_LocalServiceManagerTest, WriteCharacteristicNoWritePermission) {
  LocalServiceManager mgr;
  const att::AccessRequirements kReadReqs, kWriteReqs;
  constexpr common::UUID kChrcType16((uint16_t)0x1234);
  constexpr IdType kChrcId = 5;
  const common::BufferView kTestValue;

  auto service = std::make_unique<Service>(true /* primary */, kTestType16);
  service->AddCharacteristic(std::make_unique<Characteristic>(
      kChrcId, kChrcType16, Property::kWrite, 0, kReadReqs, kWriteReqs));

  bool called = false;
  auto write_cb = [&called](auto, auto, auto, auto&, auto&) { called = true; };

  EXPECT_NE(0u,
            mgr.RegisterService(std::move(service), NopReadHandler, write_cb));

  auto* attr = mgr.database()->FindAttribute(kFirstChrcValueHandle);
  ASSERT_TRUE(attr);
  EXPECT_EQ(kChrcType16, attr->type());

  bool result_called = false;
  auto result_cb = [&result_called](auto) { result_called = true; };

  EXPECT_FALSE(attr->WriteAsync(0, kTestValue, result_cb));
  EXPECT_FALSE(called);
  EXPECT_FALSE(result_called);
}

TEST(GATT_LocalServiceManagerTest, WriteCharacteristicNoWriteProperty) {
  LocalServiceManager mgr;
  constexpr common::UUID kChrcType16((uint16_t)0x1234);
  constexpr IdType kChrcId = 5;
  const common::BufferView kTestValue;

  const att::AccessRequirements kReadReqs;
  const att::AccessRequirements kWriteReqs(false, false, false);

  auto service = std::make_unique<Service>(true /* primary */, kTestType16);
  service->AddCharacteristic(std::make_unique<Characteristic>(
      kChrcId, kChrcType16, 0, 0, kReadReqs, kWriteReqs));

  bool called = false;
  auto write_cb = [&called](auto, auto, auto, auto&, auto&) { called = true; };

  EXPECT_NE(0u,
            mgr.RegisterService(std::move(service), NopReadHandler, write_cb));

  auto* attr = mgr.database()->FindAttribute(kFirstChrcValueHandle);
  ASSERT_TRUE(attr);
  EXPECT_EQ(kChrcType16, attr->type());

  att::ErrorCode ecode = att::ErrorCode::kNoError;
  auto result_cb = [&ecode](auto code) { ecode = code; };

  EXPECT_TRUE(attr->WriteAsync(0, kTestValue, result_cb));

  // The error should be handled internally and not reach |write_cb|.
  EXPECT_FALSE(called);
  EXPECT_EQ(att::ErrorCode::kWriteNotPermitted, ecode);
}

TEST(GATT_LocalServiceManagerTest, WriteCharacteristic) {
  LocalServiceManager mgr;
  constexpr common::UUID kChrcType16((uint16_t)0x1234);
  constexpr IdType kChrcId = 5;
  constexpr uint16_t kOffset = 10;

  const auto kTestValue = common::CreateStaticByteBuffer('f', 'o', 'o');

  const att::AccessRequirements kReadReqs;
  const att::AccessRequirements kWriteReqs(false, false, false);

  auto service = std::make_unique<Service>(true /* primary */, kTestType16);
  service->AddCharacteristic(std::make_unique<Characteristic>(
      kChrcId, kChrcType16, Property::kWrite, 0, kReadReqs, kWriteReqs));

  bool called = false;
  IdType svc_id;
  auto write_cb = [&](auto cb_svc_id, auto id, auto offset, const auto& value,
                      const auto& responder) {
    called = true;
    EXPECT_EQ(svc_id, cb_svc_id);
    EXPECT_EQ(kChrcId, id);
    EXPECT_EQ(kOffset, offset);
    EXPECT_TRUE(common::ContainersEqual(kTestValue, value));
    responder(att::ErrorCode::kNoError);
  };

  svc_id = mgr.RegisterService(std::move(service), NopReadHandler, write_cb);
  ASSERT_NE(0u, svc_id);

  auto* attr = mgr.database()->FindAttribute(kFirstChrcValueHandle);
  ASSERT_TRUE(attr);
  EXPECT_EQ(kChrcType16, attr->type());

  att::ErrorCode ecode = att::ErrorCode::kUnlikelyError;
  auto result_cb = [&ecode](auto code) { ecode = code; };

  EXPECT_TRUE(attr->WriteAsync(kOffset, kTestValue, result_cb));

  EXPECT_TRUE(called);
  EXPECT_EQ(att::ErrorCode::kNoError, ecode);
}

TEST(GATT_LocalServiceManagerTest, ReadDescriptorNoReadPermission) {
  LocalServiceManager mgr;
  const att::AccessRequirements kReadReqs, kWriteReqs;
  constexpr common::UUID kChrcType16((uint16_t)0x1234);
  constexpr common::UUID kDescType16((uint16_t)0x5678);
  constexpr IdType kChrcId = 0;
  constexpr IdType kDescId = 1;

  auto service = std::make_unique<Service>(true /* primary */, kTestType16);
  auto chrc = std::make_unique<Characteristic>(kChrcId, kChrcType16, 0, 0,
                                               kReadReqs, kWriteReqs);
  chrc->AddDescriptor(std::make_unique<Descriptor>(kDescId, kDescType16,
                                                   kReadReqs, kWriteReqs));
  service->AddCharacteristic(std::move(chrc));

  bool called = false;
  auto read_cb = [&called](auto, auto, auto, auto&) { called = true; };

  EXPECT_NE(0u,
            mgr.RegisterService(std::move(service), read_cb, NopWriteHandler));

  auto* attr = mgr.database()->FindAttribute(kFirstDescrHandle);
  ASSERT_TRUE(attr);
  EXPECT_EQ(kDescType16, attr->type());

  bool result_called = false;
  auto result_cb = [&result_called](auto, const auto&) {
    result_called = true;
  };

  EXPECT_FALSE(attr->ReadAsync(0, result_cb));
  EXPECT_FALSE(called);
  EXPECT_FALSE(result_called);
}

TEST(GATT_LocalServiceManagerTest, ReadDescriptor) {
  LocalServiceManager mgr;
  constexpr common::UUID kChrcType16((uint16_t)0x1234);
  constexpr common::UUID kDescType16((uint16_t)0x5678);
  constexpr IdType kChrcId = 0;
  constexpr IdType kDescId = 1;
  constexpr uint16_t kOffset = 10;

  const auto kTestValue = common::CreateStaticByteBuffer('f', 'o', 'o');

  const att::AccessRequirements kReadReqs(false, false, false);
  const att::AccessRequirements kWriteReqs;

  auto service = std::make_unique<Service>(true /* primary */, kTestType16);
  auto chrc = std::make_unique<Characteristic>(kChrcId, kChrcType16, 0, 0,
                                               kReadReqs, kWriteReqs);
  chrc->AddDescriptor(std::make_unique<Descriptor>(kDescId, kDescType16,
                                                   kReadReqs, kWriteReqs));
  service->AddCharacteristic(std::move(chrc));

  bool called = false;
  IdType svc_id;
  auto read_cb = [&](auto cb_svc_id, auto id, auto offset,
                     const auto& responder) {
    called = true;
    EXPECT_EQ(svc_id, cb_svc_id);
    EXPECT_EQ(kDescId, id);
    EXPECT_EQ(kOffset, offset);
    responder(att::ErrorCode::kNoError, kTestValue);
  };

  svc_id = mgr.RegisterService(std::move(service), read_cb, NopWriteHandler);
  ASSERT_NE(0u, svc_id);

  auto* attr = mgr.database()->FindAttribute(kFirstDescrHandle);
  ASSERT_TRUE(attr);
  EXPECT_EQ(kDescType16, attr->type());

  att::ErrorCode ecode = att::ErrorCode::kUnlikelyError;
  auto result_cb = [&ecode, &kTestValue](auto code, const auto& value) {
    ecode = code;
    EXPECT_TRUE(common::ContainersEqual(kTestValue, value));
  };

  EXPECT_TRUE(attr->ReadAsync(kOffset, result_cb));

  EXPECT_TRUE(called);
  EXPECT_EQ(att::ErrorCode::kNoError, ecode);
}

TEST(GATT_LocalServiceManagerTest, WriteDescriptorNoWritePermission) {
  LocalServiceManager mgr;
  const att::AccessRequirements kReadReqs, kWriteReqs;
  constexpr common::UUID kChrcType16((uint16_t)0x1234);
  constexpr common::UUID kDescType16((uint16_t)0x5678);
  constexpr IdType kChrcId = 0;
  constexpr IdType kDescId = 1;
  const common::BufferView kTestValue;

  auto service = std::make_unique<Service>(true /* primary */, kTestType16);
  auto chrc = std::make_unique<Characteristic>(kChrcId, kChrcType16, 0, 0,
                                               kReadReqs, kWriteReqs);
  chrc->AddDescriptor(std::make_unique<Descriptor>(kDescId, kDescType16,
                                                   kReadReqs, kWriteReqs));
  service->AddCharacteristic(std::move(chrc));

  bool called = false;
  auto write_cb = [&called](auto, auto, auto, auto&, auto&) { called = true; };

  EXPECT_NE(0u,
            mgr.RegisterService(std::move(service), NopReadHandler, write_cb));

  auto* attr = mgr.database()->FindAttribute(kFirstDescrHandle);
  ASSERT_TRUE(attr);
  EXPECT_EQ(kDescType16, attr->type());

  bool result_called = false;
  auto result_cb = [&result_called](auto) { result_called = true; };

  EXPECT_FALSE(attr->WriteAsync(0, kTestValue, result_cb));
  EXPECT_FALSE(called);
  EXPECT_FALSE(result_called);
}

TEST(GATT_LocalServiceManagerTest, WriteDescriptor) {
  LocalServiceManager mgr;
  constexpr common::UUID kChrcType16((uint16_t)0x1234);
  constexpr common::UUID kDescType16((uint16_t)0x5678);
  constexpr IdType kChrcId = 0;
  constexpr IdType kDescId = 1;
  constexpr uint16_t kOffset = 10;

  const auto kTestValue = common::CreateStaticByteBuffer('f', 'o', 'o');

  const att::AccessRequirements kReadReqs;
  const att::AccessRequirements kWriteReqs(false, false, false);

  auto service = std::make_unique<Service>(true /* primary */, kTestType16);
  auto chrc = std::make_unique<Characteristic>(kChrcId, kChrcType16, 0, 0,
                                               kReadReqs, kWriteReqs);
  chrc->AddDescriptor(std::make_unique<Descriptor>(kDescId, kDescType16,
                                                   kReadReqs, kWriteReqs));
  service->AddCharacteristic(std::move(chrc));

  bool called = false;
  IdType svc_id;
  auto write_cb = [&](auto cb_svc_id, auto id, auto offset, const auto& value,
                      const auto& responder) {
    called = true;
    EXPECT_EQ(svc_id, cb_svc_id);
    EXPECT_EQ(kDescId, id);
    EXPECT_EQ(kOffset, offset);
    EXPECT_TRUE(common::ContainersEqual(kTestValue, value));
    responder(att::ErrorCode::kNoError);
  };

  svc_id = mgr.RegisterService(std::move(service), NopReadHandler, write_cb);
  ASSERT_NE(0u, svc_id);

  auto* attr = mgr.database()->FindAttribute(kFirstDescrHandle);
  ASSERT_TRUE(attr);
  EXPECT_EQ(kDescType16, attr->type());

  att::ErrorCode ecode = att::ErrorCode::kUnlikelyError;
  auto result_cb = [&ecode](auto code) { ecode = code; };

  EXPECT_TRUE(attr->WriteAsync(kOffset, kTestValue, result_cb));

  EXPECT_TRUE(called);
  EXPECT_EQ(att::ErrorCode::kNoError, ecode);
}

}  // namespace
}  // namespace gatt
}  // namespace btlib
