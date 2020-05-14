// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "i2c-child.h"

#include <lib/fake_ddk/fake_ddk.h>

#include <ddk/binding.h>
#include <zxtest/zxtest.h>

#include "i2c-bus.h"

namespace i2c {

constexpr uint8_t kTestWrite0 = 0x99;
constexpr uint8_t kTestWrite1 = 0x88;
constexpr uint8_t kTestWrite2 = 0x77;
constexpr uint8_t kTestRead0 = 0x12;
constexpr uint8_t kTestRead1 = 0x34;
constexpr uint8_t kTestRead2 = 0x56;

class I2cChildTest : public I2cChild {
 public:
  I2cChildTest(zx_device_t* parent, fbl::RefPtr<I2cBus> bus, uint16_t address)
      : I2cChild(parent, std::move(bus), address) {
    ddk_proto_id_ = ZX_PROTOCOL_I2C;
    ZX_ASSERT(DdkAdd("Test-device") == ZX_OK);
  }
};

TEST(I2cChildTest, Write3BytesOnce) {
  fake_ddk::Bind tester;
  class I2cBusTest : public I2cBus {
   public:
    explicit I2cBusTest(ddk::I2cImplProtocolClient i2c, uint32_t bus_id)
        : I2cBus(fake_ddk::kFakeParent, i2c, bus_id) {}
    void Transact(uint16_t address, const i2c_op_t* op_list, size_t op_count,
                  i2c_transact_callback callback, void* cookie) override {
      if (op_count != 1) {
        callback(cookie, ZX_ERR_INTERNAL, nullptr, 0);
        return;
      }
      auto p0 = static_cast<uint8_t*>(const_cast<void*>(op_list[0].data_buffer));
      if (p0[0] != kTestWrite0 || p0[1] != kTestWrite1 || p0[2] != kTestWrite2 ||
          op_list[0].data_size != 3 || op_list[0].is_read != false || op_list[0].stop != true) {
        callback(cookie, ZX_ERR_INTERNAL, nullptr, 0);
        return;
      }
      callback(cookie, ZX_OK, nullptr, 0);
    }
  };
  ddk::I2cImplProtocolClient i2c = {};
  I2cChildTest server(fake_ddk::kFakeParent, fbl::AdoptRef<I2cBus>(new I2cBusTest(i2c, 0)), 0);
  llcpp::fuchsia::hardware::i2c::Device2::SyncClient client_wrap(std::move(tester.FidlClient()));

  bool is_write[] = {true};  // 1 write segment.
  fidl::VectorView<bool> segments_is_write(fidl::unowned_ptr(is_write), countof(is_write));

  // 3 bytes in 1 write segment.
  size_t n_write_bytes = 3;
  auto write_buffer = std::make_unique<uint8_t[]>(n_write_bytes);
  write_buffer[0] = kTestWrite0;
  write_buffer[1] = kTestWrite1;
  write_buffer[2] = kTestWrite2;
  fidl::VectorView<uint8_t> write_segment(fidl::unowned_ptr(write_buffer.get()), n_write_bytes);

  auto read = client_wrap.Transfer(std::move(segments_is_write),
                                   fidl::VectorView<fidl::VectorView<uint8_t>>(
                                       fidl::unowned_ptr(&write_segment), 1),  // 1 write segment.
                                   fidl::VectorView<uint8_t>(nullptr, 0));     // No reads.
  ASSERT_OK(read.status());
  ASSERT_FALSE(read->result.is_err());
}

TEST(I2cChildTest, Read3BytesOnce) {
  fake_ddk::Bind tester;
  class I2cBusTest : public I2cBus {
   public:
    explicit I2cBusTest(ddk::I2cImplProtocolClient i2c, uint32_t bus_id)
        : I2cBus(fake_ddk::kFakeParent, i2c, bus_id) {}
    void Transact(uint16_t address, const i2c_op_t* op_list, size_t op_count,
                  i2c_transact_callback callback, void* cookie) override {
      if (op_count != 1) {
        callback(cookie, ZX_ERR_INTERNAL, nullptr, 0);
        return;
      }
      if (op_list[0].data_size != 3 || op_list[0].is_read != true || op_list[0].stop != true) {
        callback(cookie, ZX_ERR_INTERNAL, nullptr, 0);
        return;
      }
      uint8_t reply0 = kTestRead0;
      uint8_t reply1 = kTestRead1;
      uint8_t reply2 = kTestRead2;
      i2c_op_t replies[3] = {
          {&reply0, 1, true, false},
          {&reply1, 1, true, false},
          {&reply2, 1, true, false},
      };
      callback(cookie, ZX_OK, replies, countof(replies));
    }
  };
  ddk::I2cImplProtocolClient i2c = {};
  I2cChildTest server(fake_ddk::kFakeParent, fbl::AdoptRef<I2cBus>(new I2cBusTest(i2c, 0)), 0);
  llcpp::fuchsia::hardware::i2c::Device2::SyncClient client_wrap(std::move(tester.FidlClient()));

  bool is_write[] = {false};  // 1 read segment.
  fidl::VectorView<bool> segments_is_write(fidl::unowned_ptr(is_write), countof(is_write));

  // 1 read segment expecting 3 bytes.
  constexpr size_t n_reads = 1;
  auto read_lengths = std::make_unique<uint8_t[]>(n_reads);
  read_lengths[0] = 3;  // 3 bytes to read in 1 segment.

  auto read =
      client_wrap.Transfer(std::move(segments_is_write),
                           fidl::VectorView<fidl::VectorView<uint8_t>>(nullptr, 0),  // No writes.
                           fidl::VectorView<uint8_t>(fidl::unowned_ptr(read_lengths.get()),
                                                     n_reads));  // 1 read segment.
  ASSERT_OK(read.status());
  ASSERT_FALSE(read->result.is_err());

  auto& read_data = read->result.response().read_segments_data;
  ASSERT_EQ(read_data[0].data()[0], kTestRead0);
  ASSERT_EQ(read_data[1].data()[0], kTestRead1);
  ASSERT_EQ(read_data[2].data()[0], kTestRead2);
}

TEST(I2cChildTest, Write1ByteOnceRead1Byte3Times) {
  fake_ddk::Bind tester;
  class I2cBusTest : public I2cBus {
   public:
    explicit I2cBusTest(ddk::I2cImplProtocolClient i2c, uint32_t bus_id)
        : I2cBus(fake_ddk::kFakeParent, i2c, bus_id) {}
    void Transact(uint16_t address, const i2c_op_t* op_list, size_t op_count,
                  i2c_transact_callback callback, void* cookie) override {
      if (op_count != 4) {
        callback(cookie, ZX_ERR_INTERNAL, nullptr, 0);
        return;
      }
      auto p0 = static_cast<uint8_t*>(const_cast<void*>(op_list[0].data_buffer));
      if (p0[0] != kTestWrite0 || op_list[0].data_size != 1 || op_list[0].is_read != false ||
          op_list[0].stop != false || op_list[1].data_size != 1 || op_list[1].is_read != true ||
          op_list[1].stop != false || op_list[2].data_size != 1 || op_list[2].is_read != true ||
          op_list[2].stop != false || op_list[3].data_size != 1 || op_list[3].is_read != true ||
          op_list[3].stop != true) {
        callback(cookie, ZX_ERR_INTERNAL, nullptr, 0);
        return;
      }
      uint8_t reply0 = kTestRead0;
      uint8_t reply1 = kTestRead1;
      uint8_t reply2 = kTestRead2;
      i2c_op_t replies[3] = {
          {&reply0, 1, true, false},
          {&reply1, 1, true, false},
          {&reply2, 1, true, false},
      };
      callback(cookie, ZX_OK, replies, countof(replies));
    }
  };
  ddk::I2cImplProtocolClient i2c = {};
  I2cChildTest server(fake_ddk::kFakeParent, fbl::AdoptRef<I2cBus>(new I2cBusTest(i2c, 0)), 0);
  llcpp::fuchsia::hardware::i2c::Device2::SyncClient client_wrap(std::move(tester.FidlClient()));

  bool is_write[] = {true, false, false, false};  // 1 write, 3 reads.
  fidl::VectorView<bool> segments_is_write(fidl::unowned_ptr(is_write), countof(is_write));

  // 1 byte in 1 write segment.
  size_t n_write_bytes = 1;
  auto write_buffer = std::make_unique<uint8_t[]>(n_write_bytes);
  write_buffer[0] = kTestWrite0;
  fidl::VectorView<uint8_t> write_segment(fidl::unowned_ptr(write_buffer.get()), n_write_bytes);

  // 3 read segments expecting 1 byte each.
  constexpr size_t n_reads = 3;
  auto read_lengths = std::make_unique<uint8_t[]>(n_reads);
  read_lengths[0] = 1;
  read_lengths[1] = 1;
  read_lengths[2] = 1;

  auto read = client_wrap.Transfer(
      std::move(segments_is_write),
      fidl::VectorView<fidl::VectorView<uint8_t>>(fidl::unowned_ptr(&write_segment),
                                                  1),  // 1 write segment.
      fidl::VectorView<uint8_t>(fidl::unowned_ptr(read_lengths.get()),
                                n_reads));  // 3 read segmenets.
  ASSERT_OK(read.status());
  ASSERT_FALSE(read->result.is_err());

  auto& read_data = read->result.response().read_segments_data;
  ASSERT_EQ(read_data[0].data()[0], kTestRead0);
  ASSERT_EQ(read_data[1].data()[0], kTestRead1);
  ASSERT_EQ(read_data[2].data()[0], kTestRead2);
}

TEST(I2cChildTest, BadTransfers) {
  fake_ddk::Bind tester;
  ddk::I2cImplProtocolClient i2c = {};
  I2cChildTest server(fake_ddk::kFakeParent,
                      fbl::AdoptRef<I2cBus>(new I2cBus(fake_ddk::kFakeParent, i2c, 0)), 0);
  llcpp::fuchsia::hardware::i2c::Device2::SyncClient client_wrap(std::move(tester.FidlClient()));

  {
    // 2 write segments, inconsistent with 1 segment write below.
    bool is_write[] = {true, true};
    fidl::VectorView<bool> segments_is_write(fidl::unowned_ptr(is_write), countof(is_write));
    size_t n_write_bytes = 1;
    auto write_buffer = std::make_unique<uint8_t[]>(n_write_bytes);
    write_buffer[0] = kTestWrite0;
    fidl::VectorView<uint8_t> write_segment(fidl::unowned_ptr(write_buffer.get()), n_write_bytes);

    auto read = client_wrap.Transfer(
        std::move(segments_is_write),
        // 1 segment write (incosistent with the 2 segments_is_write above).
        fidl::VectorView<fidl::VectorView<uint8_t>>(fidl::unowned_ptr(&write_segment), 1),
        fidl::VectorView<uint8_t>(nullptr, 0));  // No reads.
    ASSERT_OK(read.status());
    ASSERT_TRUE(read->result.is_err());
  }

  {
    bool is_write[] = {true};  // 1 write segment, inconsistent with segments below.
    fidl::VectorView<bool> segments_is_write(fidl::unowned_ptr(is_write), countof(is_write));

    // 1 byte in 2 write segments.
    size_t n_write_bytes = 1;
    auto write_buffer = std::make_unique<uint8_t[]>(n_write_bytes);
    write_buffer[0] = kTestWrite0;
    fidl::VectorView<uint8_t> write_segments[2];
    write_segments[0].set_data(fidl::unowned_ptr(write_buffer.get()));
    write_segments[0].set_count(n_write_bytes);
    write_segments[1].set_data(fidl::unowned_ptr(write_buffer.get()));
    write_segments[1].set_count(n_write_bytes);

    auto read =
        client_wrap.Transfer(std::move(segments_is_write),
                             fidl::VectorView<fidl::VectorView<uint8_t>>(
                                 fidl::unowned_ptr(write_segments), 2),  // 2 write segments.
                             fidl::VectorView<uint8_t>(nullptr, 0));     // No reads.
    ASSERT_OK(read.status());
    ASSERT_TRUE(read->result.is_err());
  }

  {
    bool is_write[] = {false};  // 1 read segment, inconsistent with segments below.
    fidl::VectorView<bool> segments_is_write(fidl::unowned_ptr(is_write), countof(is_write));

    // 2 read segments expecting 2 bytes each.
    constexpr size_t n_reads = 2;
    auto read_lengths = std::make_unique<uint8_t[]>(n_reads);
    read_lengths[0] = 2;
    read_lengths[1] = 2;

    auto read =
        client_wrap.Transfer(std::move(segments_is_write),
                             fidl::VectorView<fidl::VectorView<uint8_t>>(nullptr, 0),  // No writes.
                             fidl::VectorView<uint8_t>(fidl::unowned_ptr(read_lengths.get()),
                                                       n_reads));  // 2 read segments.
    ASSERT_OK(read.status());
    ASSERT_TRUE(read->result.is_err());
  }

  {
    bool is_write[] = {false, false};  // 2 read segments, inconsistent with segments below.
    fidl::VectorView<bool> segments_is_write(fidl::unowned_ptr(is_write), countof(is_write));

    // 1 read segment expecting 2 bytes.
    constexpr size_t n_reads = 1;
    auto read_lengths = std::make_unique<uint8_t[]>(n_reads);
    read_lengths[0] = 2;

    auto read =
        client_wrap.Transfer(std::move(segments_is_write),
                             fidl::VectorView<fidl::VectorView<uint8_t>>(nullptr, 0),  // No writes.
                             fidl::VectorView<uint8_t>(fidl::unowned_ptr(read_lengths.get()),
                                                       n_reads));  // 1 read segment.
    ASSERT_OK(read.status());
    ASSERT_TRUE(read->result.is_err());
  }
}

}  // namespace i2c
