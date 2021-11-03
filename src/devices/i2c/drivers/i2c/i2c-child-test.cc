// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "i2c-child.h"

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/loop.h>

#include <zxtest/zxtest.h>

#include "i2c-bus.h"
#include "src/devices/testing/mock-ddk/mock-device.h"

namespace i2c {

constexpr uint8_t kTestWrite0 = 0x99;
constexpr uint8_t kTestWrite1 = 0x88;
constexpr uint8_t kTestWrite2 = 0x77;
constexpr uint8_t kTestRead0 = 0x12;
constexpr uint8_t kTestRead1 = 0x34;
constexpr uint8_t kTestRead2 = 0x56;

class I2cTestChild : public I2cChild {
 public:
  I2cTestChild(zx_device_t* parent, fbl::RefPtr<I2cBus> bus, uint16_t address)
      : I2cChild(parent, std::move(bus), address) {
    ddk_proto_id_ = ZX_PROTOCOL_I2C;
    ZX_ASSERT(DdkAdd("Test-device") == ZX_OK);
  }
};

class I2cBusTest : public I2cBus {
 public:
  using TransactCallback =
      std::function<void(uint16_t, const i2c_op_t*, size_t, i2c_transact_callback, void*)>;
  explicit I2cBusTest(zx_device_t* fake_root, ddk::I2cImplProtocolClient i2c, uint32_t bus_id,
                      TransactCallback transact)
      : I2cBus(fake_root, i2c, bus_id), transact_(std::move(transact)) {}

  void Transact(uint16_t address, const i2c_op_t* op_list, size_t op_count,
                i2c_transact_callback callback, void* cookie) override {
    transact_(address, op_list, op_count, callback, cookie);
  }

 private:
  TransactCallback transact_;
};

class I2cChildTest : public zxtest::Test {
 public:
  I2cChildTest() : loop_(&kAsyncLoopConfigNeverAttachToThread) {}
  void SetUp() override {
    fake_root_ = MockDevice::FakeRootParent();
    ASSERT_OK(loop_.StartThread("i2c-child-test-fidl"));
  }

  void StartFidl(I2cTestChild* child, fidl::WireSyncClient<fuchsia_hardware_i2c::Device2>* device) {
    auto endpoints = fidl::CreateEndpoints<fuchsia_hardware_i2c::Device2>();
    ASSERT_OK(endpoints.status_value());

    fidl::BindServer(loop_.dispatcher(), std::move(endpoints->server), child);

    *device = fidl::WireSyncClient<fuchsia_hardware_i2c::Device2>(std::move(endpoints->client));
  }

  void TearDown() override {
    for (auto& device : fake_root_->children()) {
      device_async_remove(device.get());
    }
    mock_ddk::ReleaseFlaggedDevices(fake_root_.get());
  }

 protected:
  std::shared_ptr<zx_device> fake_root_;
  async::Loop loop_;
};

TEST_F(I2cChildTest, Write3BytesOnce) {
  ddk::I2cImplProtocolClient i2c = {};
  auto bus = fbl::AdoptRef(new I2cBusTest(
      fake_root_.get(), i2c, 0,
      [](uint16_t address, const i2c_op_t* op_list, size_t op_count, i2c_transact_callback callback,
         void* cookie) {
        if (op_count != 1) {
          callback(cookie, ZX_ERR_INTERNAL, nullptr, 0);
          return;
        }
        auto p0 = op_list[0].data_buffer;
        if (p0[0] != kTestWrite0 || p0[1] != kTestWrite1 || p0[2] != kTestWrite2 ||
            op_list[0].data_size != 3 || op_list[0].is_read != false || op_list[0].stop != true) {
          callback(cookie, ZX_ERR_INTERNAL, nullptr, 0);
          return;
        }
        callback(cookie, ZX_OK, nullptr, 0);
      }));
  // Allocate using new as the mock DDK takes ownership of the child.
  auto server = new I2cTestChild(fake_root_.get(), std::move(bus), 0);
  fidl::WireSyncClient<fuchsia_hardware_i2c::Device2> client_wrap;
  ASSERT_NO_FATAL_FAILURES(StartFidl(server, &client_wrap));

  bool is_write[] = {true};  // 1 write segment.
  auto segments_is_write = fidl::VectorView<bool>::FromExternal(is_write);

  // 3 bytes in 1 write segment.
  size_t n_write_bytes = 3;
  auto write_buffer = std::make_unique<uint8_t[]>(n_write_bytes);
  write_buffer[0] = kTestWrite0;
  write_buffer[1] = kTestWrite1;
  write_buffer[2] = kTestWrite2;
  auto write_segment = fidl::VectorView<uint8_t>::FromExternal(write_buffer.get(), n_write_bytes);

  auto read = client_wrap->Transfer(std::move(segments_is_write),
                                    fidl::VectorView<fidl::VectorView<uint8_t>>::FromExternal(
                                        &write_segment, 1),        // 1 write segment.
                                    fidl::VectorView<uint8_t>());  // No reads.
  ASSERT_OK(read.status());
  ASSERT_FALSE(read->result.is_err());
}

TEST_F(I2cChildTest, Read3BytesOnce) {
  ddk::I2cImplProtocolClient i2c = {};
  auto bus = fbl::AdoptRef(new I2cBusTest(
      fake_root_.get(), i2c, 0,
      [](uint16_t address, const i2c_op_t* op_list, size_t op_count, i2c_transact_callback callback,
         void* cookie) {
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
      }));
  // Allocate using new as the mock DDK takes ownership of the child.
  auto server = new I2cTestChild(fake_root_.get(), std::move(bus), 0);
  fidl::WireSyncClient<fuchsia_hardware_i2c::Device2> client_wrap;
  ASSERT_NO_FATAL_FAILURES(StartFidl(server, &client_wrap));

  bool is_write[] = {false};  // 1 read segment.
  auto segments_is_write = fidl::VectorView<bool>::FromExternal(is_write);

  // 1 read segment expecting 3 bytes.
  constexpr size_t n_reads = 1;
  auto read_lengths = std::make_unique<uint8_t[]>(n_reads);
  read_lengths[0] = 3;  // 3 bytes to read in 1 segment.

  auto read =
      client_wrap->Transfer(std::move(segments_is_write),
                            fidl::VectorView<fidl::VectorView<uint8_t>>(),  // No writes.
                            fidl::VectorView<uint8_t>::FromExternal(read_lengths.get(),
                                                                    n_reads));  // 1 read segment.
  ASSERT_OK(read.status());
  ASSERT_FALSE(read->result.is_err());

  auto& read_data = read->result.response().read_segments_data;
  ASSERT_EQ(read_data[0].data()[0], kTestRead0);
  ASSERT_EQ(read_data[1].data()[0], kTestRead1);
  ASSERT_EQ(read_data[2].data()[0], kTestRead2);
}

TEST_F(I2cChildTest, Write1ByteOnceRead1Byte3Times) {
  ddk::I2cImplProtocolClient i2c = {};
  auto bus = fbl::AdoptRef(new I2cBusTest(
      fake_root_.get(), i2c, 0,
      [](uint16_t address, const i2c_op_t* op_list, size_t op_count, i2c_transact_callback callback,
         void* cookie) {
        if (op_count != 4) {
          callback(cookie, ZX_ERR_INTERNAL, nullptr, 0);
          return;
        }
        auto p0 = op_list[0].data_buffer;
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
      }));

  // Allocate using new as the mock DDK takes ownership of the child.
  auto server = new I2cTestChild(fake_root_.get(), std::move(bus), 0);
  fidl::WireSyncClient<fuchsia_hardware_i2c::Device2> client_wrap;
  ASSERT_NO_FATAL_FAILURES(StartFidl(server, &client_wrap));

  bool is_write[] = {true, false, false, false};  // 1 write, 3 reads.
  auto segments_is_write = fidl::VectorView<bool>::FromExternal(is_write);

  // 1 byte in 1 write segment.
  size_t n_write_bytes = 1;
  auto write_buffer = std::make_unique<uint8_t[]>(n_write_bytes);
  write_buffer[0] = kTestWrite0;
  auto write_segment = fidl::VectorView<uint8_t>::FromExternal(write_buffer.get(), n_write_bytes);

  // 3 read segments expecting 1 byte each.
  constexpr size_t n_reads = 3;
  auto read_lengths = std::make_unique<uint8_t[]>(n_reads);
  read_lengths[0] = 1;
  read_lengths[1] = 1;
  read_lengths[2] = 1;

  auto read = client_wrap->Transfer(
      std::move(segments_is_write),
      fidl::VectorView<fidl::VectorView<uint8_t>>::FromExternal(&write_segment,
                                                                1),  // 1 write segment.
      fidl::VectorView<uint8_t>::FromExternal(read_lengths.get(),
                                              n_reads));  // 3 read segmenets.
  ASSERT_OK(read.status());
  ASSERT_FALSE(read->result.is_err());

  auto& read_data = read->result.response().read_segments_data;
  ASSERT_EQ(read_data[0].data()[0], kTestRead0);
  ASSERT_EQ(read_data[1].data()[0], kTestRead1);
  ASSERT_EQ(read_data[2].data()[0], kTestRead2);
}

TEST_F(I2cChildTest, BadTransfers) {
  ddk::I2cImplProtocolClient i2c = {};
  // Allocate using new as the mock DDK takes ownership of the child.
  auto server = new I2cTestChild(fake_root_.get(),
                                 fbl::AdoptRef<I2cBus>(new I2cBus(fake_root_.get(), i2c, 0)), 0);
  fidl::WireSyncClient<fuchsia_hardware_i2c::Device2> client_wrap;
  ASSERT_NO_FATAL_FAILURES(StartFidl(server, &client_wrap));

  {
    // 2 write segments, inconsistent with 1 segment write below.
    bool is_write[] = {true, true};
    auto segments_is_write = fidl::VectorView<bool>::FromExternal(is_write);
    size_t n_write_bytes = 1;
    auto write_buffer = std::make_unique<uint8_t[]>(n_write_bytes);
    write_buffer[0] = kTestWrite0;
    auto write_segment = fidl::VectorView<uint8_t>::FromExternal(write_buffer.get(), n_write_bytes);

    auto read = client_wrap->Transfer(
        std::move(segments_is_write),
        // 1 segment write (incosistent with the 2 segments_is_write above).
        fidl::VectorView<fidl::VectorView<uint8_t>>::FromExternal(&write_segment, 1),
        fidl::VectorView<uint8_t>());  // No reads.
    ASSERT_OK(read.status());
    ASSERT_TRUE(read->result.is_err());
  }

  {
    bool is_write[] = {true};  // 1 write segment, inconsistent with segments below.
    auto segments_is_write = fidl::VectorView<bool>::FromExternal(is_write, countof(is_write));

    // 1 byte in 2 write segments.
    size_t n_write_bytes = 1;
    auto write_buffer = std::make_unique<uint8_t[]>(n_write_bytes);
    write_buffer[0] = kTestWrite0;
    fidl::VectorView<uint8_t> write_segments[2];
    write_segments[0] = fidl::VectorView<uint8_t>::FromExternal(write_buffer.get(), n_write_bytes);
    write_segments[1] = fidl::VectorView<uint8_t>::FromExternal(write_buffer.get(), n_write_bytes);

    auto read = client_wrap->Transfer(std::move(segments_is_write),
                                      fidl::VectorView<fidl::VectorView<uint8_t>>::FromExternal(
                                          write_segments, 2),        // 2 write segments.
                                      fidl::VectorView<uint8_t>());  // No reads.
    ASSERT_OK(read.status());
    ASSERT_TRUE(read->result.is_err());
  }

  {
    bool is_write[] = {false};  // 1 read segment, inconsistent with segments below.
    auto segments_is_write = fidl::VectorView<bool>::FromExternal(is_write, countof(is_write));

    // 2 read segments expecting 2 bytes each.
    constexpr size_t n_reads = 2;
    auto read_lengths = std::make_unique<uint8_t[]>(n_reads);
    read_lengths[0] = 2;
    read_lengths[1] = 2;

    auto read = client_wrap->Transfer(
        std::move(segments_is_write),
        fidl::VectorView<fidl::VectorView<uint8_t>>(),  // No writes.
        fidl::VectorView<uint8_t>::FromExternal(read_lengths.get(),
                                                n_reads));  // 2 read segments.
    ASSERT_OK(read.status());
    ASSERT_TRUE(read->result.is_err());
  }

  {
    bool is_write[] = {false, false};  // 2 read segments, inconsistent with segments below.
    auto segments_is_write = fidl::VectorView<bool>::FromExternal(is_write);

    // 1 read segment expecting 2 bytes.
    constexpr size_t n_reads = 1;
    auto read_lengths = std::make_unique<uint8_t[]>(n_reads);
    read_lengths[0] = 2;

    auto read = client_wrap->Transfer(
        std::move(segments_is_write),
        fidl::VectorView<fidl::VectorView<uint8_t>>(nullptr, 0),  // No writes.
        fidl::VectorView<uint8_t>::FromExternal(read_lengths.get(),
                                                n_reads));  // 1 read segment.
    ASSERT_OK(read.status());
    ASSERT_TRUE(read->result.is_err());
  }
}

}  // namespace i2c
