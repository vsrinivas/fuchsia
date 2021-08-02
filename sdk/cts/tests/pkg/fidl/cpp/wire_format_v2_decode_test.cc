// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/zx/channel.h>

#include <zxtest/zxtest.h>

#include "lib/fidl/cpp/binding.h"
#include "testing/fidl/async_loop_for_test.h"
#include "testing/fidl/frobinator_impl.h"

namespace fidl {
namespace {

typedef struct {
  uint64_t ordinal;
  uint32_t value;
  uint16_t num_handles;
  uint16_t flags;
} union_t;

// Send a V2 wire format payload and ensure the protocol method is called correctly.
TEST(WireFormatV2DecodeTest, Success) {
  test::AsyncLoopForTest loop;

  zx::channel ch1, ch2;
  ASSERT_OK(zx::channel::create(0, &ch1, &ch2));

  test::FrobinatorImpl impl;
  Binding<test::frobinator::Frobinator> binding(&impl);
  binding.set_error_handler([](zx_status_t status) { ZX_PANIC("shouldn't be called"); });
  ASSERT_OK(binding.Bind(std::move(ch1)));

  fidl_message_header_t header = {
      .txid = 0,
      .flags = {FIDL_MESSAGE_HEADER_FLAGS_0_USE_VERSION_V2, 0, 0},
      .magic_number = kFidlWireFormatMagicNumberInitial,
      .ordinal = 6185413960342748878,  // Frobinator.SendBasicUnion
  };
  union_t payload = {
      .ordinal = 1,
      .value = 123,
      .num_handles = 0,
      .flags = 1,  // 1 == inlined
  };
  uint8_t bytes[sizeof(header) + sizeof(payload)] = {};
  memcpy(bytes, &header, sizeof(header));
  memcpy(bytes + sizeof(header), &payload, sizeof(payload));
  ASSERT_OK(ch2.write(0, bytes, std::size(bytes), nullptr, 0));

  loop.RunUntilIdle();

  ASSERT_EQ(123, impl.send_basic_union_received_value_);
}

}  // namespace
}  // namespace fidl
