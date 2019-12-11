// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fidl/coding.h>
#include <lib/fidl/llcpp/string_view.h>
#include <lib/fidl/llcpp/vector_view.h>
#include <limits.h>
#include <unittest/unittest.h>
#include <zircon/syscalls.h>

#include <cstddef>
#include <memory>
#include <vector>

#include "extra_messages.h"
#include "fidl_structs.h"
#include "fidl_coded_types.h"

namespace fidl {
namespace {

bool linearize_present_nonnullable_string() {
  BEGIN_TEST;

  unbounded_nonnullable_string_inline_data message = {};
  constexpr const char* kStr = "hello!";
  constexpr size_t kLength = 6;

  char some_other_string[kLength] = {0};
  message.string = fidl_string_t{kLength, some_other_string};
  memcpy(some_other_string, kStr, kLength);

  const char* error = nullptr;
  zx_status_t status;
  uint32_t actual_num_bytes = 0;

  // Manually compute linearized size
  constexpr uint32_t buf_size = 32u + FIDL_ALIGN(kLength);
  // Allocate a buffer of the specified size
  std::unique_ptr<uint8_t[]> buf(new uint8_t[buf_size]);
  status = fidl_linearize(&unbounded_nonnullable_string_message_type, &message, buf.get(), buf_size,
                          &actual_num_bytes, &error);
  EXPECT_EQ(status, ZX_OK);
  EXPECT_NULL(error, error);
  EXPECT_EQ(actual_num_bytes, buf_size);
  // Verify pointers and object packing
  auto inline_data = reinterpret_cast<unbounded_nonnullable_string_inline_data*>(buf.get());
  EXPECT_EQ(static_cast<void*>(inline_data->string.data),
            static_cast<void*>(&buf[FIDL_ALIGN(sizeof(message))]));
  EXPECT_BYTES_EQ(reinterpret_cast<const uint8_t*>("hello!"),
                  reinterpret_cast<const uint8_t*>(inline_data->string.data), kLength,
                  "Secondary object string must be hello!");

  END_TEST;
}

bool linearize_present_nonnullable_string_unaligned_error() {
  BEGIN_TEST;

  unbounded_nonnullable_string_inline_data message = {};
  constexpr const char* kStr = "hello!";
  constexpr size_t kLength = 6;

  char some_other_string[kLength] = {0};
  message.string = fidl_string_t{kLength, some_other_string};
  memcpy(some_other_string, kStr, kLength);

  const char* error = nullptr;
  zx_status_t status;
  uint32_t actual_num_bytes = 0;

  // Pass in an unaligned storage
  constexpr uint32_t buf_size = 32u + FIDL_ALIGN(kLength);
  std::unique_ptr<uint8_t[]> buf(new uint8_t[buf_size * 2]);
  uint8_t* unaligned_ptr = buf.get() + 1;
  status = fidl_linearize(&unbounded_nonnullable_string_message_type, &message, unaligned_ptr,
                          buf_size, &actual_num_bytes, &error);
  EXPECT_EQ(status, ZX_ERR_INVALID_ARGS);
  EXPECT_NONNULL(error);
  ASSERT_STR_STR(error, "must be aligned to FIDL_ALIGNMENT");

  END_TEST;
}

bool linearize_present_nonnullable_longer_string() {
  BEGIN_TEST;

  unbounded_nonnullable_string_inline_data message = {};
  constexpr const char* kStr = "hello world!";
  constexpr size_t kLength = 12;

  char some_other_string[kLength] = {0};
  message.string = fidl_string_t{kLength, some_other_string};
  memcpy(some_other_string, kStr, kLength);

  const char* error = nullptr;
  zx_status_t status;
  uint32_t actual_num_bytes = 0;

  // Manually compute linearized size
  constexpr uint32_t buf_size = 32u + FIDL_ALIGN(kLength);

  // For non-handle-containing structures, linearizing should not change anything
  const unbounded_nonnullable_string_inline_data message_shallow_copy = message;

  // Allocate a buffer of the specified size
  std::unique_ptr<uint8_t[]> buf(new uint8_t[buf_size]);
  status = fidl_linearize(&unbounded_nonnullable_string_message_type, &message, buf.get(), buf_size,
                          &actual_num_bytes, &error);
  EXPECT_EQ(status, ZX_OK);
  EXPECT_NULL(error, error);
  EXPECT_EQ(actual_num_bytes, buf_size);
  // Verify pointers and object packing
  auto inline_data = reinterpret_cast<unbounded_nonnullable_string_inline_data*>(buf.get());
  EXPECT_EQ(static_cast<void*>(inline_data->string.data),
            static_cast<void*>(&buf[FIDL_ALIGN(sizeof(message))]));
  EXPECT_BYTES_EQ(reinterpret_cast<const uint8_t*>("hello world!"),
                  reinterpret_cast<const uint8_t*>(inline_data->string.data), kLength,
                  "Secondary object string must be hello world!");
  // Verify that message is not destroyed
  EXPECT_BYTES_EQ(reinterpret_cast<const uint8_t*>(&message_shallow_copy),
                  reinterpret_cast<uint8_t*>(&message), sizeof(message),
                  "Input object should not change");

  // Linearizing with a buffer size smaller than allowed should error out
  status = fidl_linearize(&unbounded_nonnullable_string_message_type, &message, buf.get(),
                          buf_size - 1, nullptr, &error);
  EXPECT_EQ(status, ZX_ERR_BUFFER_TOO_SMALL);
  EXPECT_NONNULL(error, "Should report error when buffer too small");

  END_TEST;
}

bool linearize_vector_of_uint32() {
  BEGIN_TEST;

  // Linearizing this array...
  constexpr uint32_t array_len = 40;
  std::unique_ptr<uint32_t[]> numbers(new uint32_t[array_len]);
  for (uint32_t i = 0; i < array_len; i++) {
    numbers[i] = i;
  }
  // into this buffer, which is big enough for the entire message
  constexpr uint32_t buf_size = 512;
  std::unique_ptr<uint8_t[]> buffer(new uint8_t[buf_size]);

  unbounded_nonnullable_vector_of_uint32_inline_data message = {};
  message.header.ordinal = 456;
  message.header.txid = 789;
  message.vector = (fidl_vector_t){
      .count = array_len,
      .data = numbers.get(),
  };

  const char* error = nullptr;
  zx_status_t status;
  uint32_t actual_num_bytes = 0;
  status = fidl_linearize(&unbounded_nonnullable_vector_of_uint32_message_type, &message,
                          buffer.get(), buf_size, &actual_num_bytes, &error);
  ASSERT_EQ(status, ZX_OK);
  ASSERT_NULL(error, error);
  ASSERT_GT(actual_num_bytes, sizeof(message));

  // Verify that vector contents have been copied copied correctly
  auto linearized_message =
      reinterpret_cast<unbounded_nonnullable_vector_of_uint32_inline_data*>(buffer.get());
  EXPECT_NONNULL(linearized_message->vector.data);
  EXPECT_NE(linearized_message->vector.data, message.vector.data);
  auto copied_numbers = reinterpret_cast<uint32_t*>(linearized_message->vector.data);
  for (uint32_t i = 0; i < array_len; i++) {
    EXPECT_EQ(copied_numbers[i], i);
  }
  EXPECT_EQ(memcmp(&message.header, &linearized_message->header, sizeof(message.header)), 0);

  // Verify that linearizing with less number of bytes does fail
  status = fidl_linearize(&unbounded_nonnullable_vector_of_uint32_message_type, &message,
                          buffer.get(), actual_num_bytes - 1, nullptr, &error);
  EXPECT_EQ(status, ZX_ERR_BUFFER_TOO_SMALL);
  EXPECT_NONNULL(error);

  END_TEST;
}

bool linearize_vector_of_nonnullable_uint32_coerce_null_to_empty() {
  BEGIN_TEST;

  std::vector<uint8_t> buffer(512);
  unbounded_nonnullable_vector_of_uint32_inline_data message = {};
  message.header.ordinal = 456;
  message.header.txid = 789;
  // Null data pointer and zero count should be treated as an empty vector
  // by the linearizer.
  message.vector = (fidl_vector_t){
      .count = 0,
      .data = nullptr,
  };

  const char* error = nullptr;
  zx_status_t status;
  uint32_t actual_num_bytes = 0;
  status =
      fidl_linearize(&unbounded_nonnullable_vector_of_uint32_message_type, &message, &buffer[0],
                     static_cast<uint32_t>(buffer.size()), &actual_num_bytes, &error);
  ASSERT_EQ(status, ZX_OK);
  ASSERT_NULL(error, error);
  ASSERT_EQ(actual_num_bytes, sizeof(message));

  auto linearized_message =
      reinterpret_cast<unbounded_nonnullable_vector_of_uint32_inline_data*>(&buffer[0]);
  EXPECT_EQ(memcmp(&message.header, &linearized_message->header, sizeof(message.header)), 0);
  EXPECT_NONNULL(linearized_message->vector.data);
  // Verify that the data pointer in the linearized message points to the next
  // out-of-line location.
  EXPECT_EQ(linearized_message->vector.data,
            reinterpret_cast<uint8_t*>(linearized_message) + sizeof(message));

  // Verify that linearizing with less number of bytes does fail
  status = fidl_linearize(&unbounded_nonnullable_vector_of_uint32_message_type, &message,
                          &buffer[0], actual_num_bytes - 1, nullptr, &error);
  EXPECT_EQ(status, ZX_ERR_BUFFER_TOO_SMALL);
  EXPECT_NONNULL(error);

  END_TEST;
}

bool linearize_vector_of_string() {
  BEGIN_TEST;

  // Define the memory-layout of the inline object
  struct VectorOfStringRequest {
    alignas(FIDL_ALIGNMENT) fidl_message_header_t header;
    fidl::VectorView<fidl::StringView> vector;
  };

  // Serialize these strings...
  const char str1[] = "Open connection,";
  const char str2[] = "Send the wrong FIDL message,";
  const char str3[] = "Get an epitaph.";
  // into this buffer, which is big enough for the entire message
  constexpr uint32_t buf_size = 512;
  std::unique_ptr<uint8_t[]> buffer(new uint8_t[buf_size]);

  fidl::StringView strings[3] = {};
  strings[0].set_data(str1);
  strings[0].set_size(sizeof(str1));
  strings[1].set_data(str2);
  strings[1].set_size(sizeof(str2));
  strings[2].set_data(str3);
  strings[2].set_size(sizeof(str3));

  VectorOfStringRequest message;
  message.vector.set_data(strings);
  message.vector.set_count(3);

  const char* error = nullptr;
  zx_status_t status;
  uint32_t actual_num_bytes = 0;
  status = fidl_linearize(&fidl_test_coding_LinearizerTestVectorOfStringRequestTable, &message,
                          buffer.get(), buf_size, &actual_num_bytes, &error);
  ASSERT_EQ(status, ZX_OK);
  ASSERT_NULL(error, error);
  ASSERT_GT(actual_num_bytes, sizeof(message));

  // Verify that vector contents have been copied copied correctly
  auto linearized_message = reinterpret_cast<VectorOfStringRequest*>(buffer.get());
  EXPECT_NONNULL(linearized_message->vector.data());
  EXPECT_EQ(linearized_message->vector.count(), 3);

  EXPECT_NE(linearized_message->vector[0].data(), str1);
  EXPECT_BYTES_EQ(reinterpret_cast<const uint8_t*>(linearized_message->vector[0].data()),
                  reinterpret_cast<const uint8_t*>(str1), sizeof(str1), str1);
  EXPECT_NE(linearized_message->vector[1].data(), str2);
  EXPECT_BYTES_EQ(reinterpret_cast<const uint8_t*>(linearized_message->vector[1].data()),
                  reinterpret_cast<const uint8_t*>(str2), sizeof(str2), str2);
  EXPECT_NE(linearized_message->vector[2].data(), str3);
  EXPECT_BYTES_EQ(reinterpret_cast<const uint8_t*>(linearized_message->vector[2].data()),
                  reinterpret_cast<const uint8_t*>(str3), sizeof(str3), str3);

  END_TEST;
}

bool linearize_struct_with_handle() {
  BEGIN_TEST;

  constexpr zx_handle_t dummy_handle = static_cast<zx_handle_t>(42);

  // Define the memory-layout of the inline object
  struct StructWithHandle {
    alignas(FIDL_ALIGNMENT) zx_handle_t h;
    int32_t foo;
  };

  // Since there are no out-of-line objects, the size is known
  constexpr uint32_t buf_size = sizeof(StructWithHandle);
  StructWithHandle message = {
      .h = dummy_handle,
      .foo = 0,
  };
  uint8_t buffer[buf_size];

  const char* error = nullptr;
  zx_status_t status;
  uint32_t actual_num_bytes = 0;
  status = fidl_linearize(&fidl_test_coding_StructWithHandleTable, &message, buffer, buf_size,
                          &actual_num_bytes, &error);
  ASSERT_EQ(status, ZX_OK);
  ASSERT_EQ(actual_num_bytes, sizeof(StructWithHandle));
  ASSERT_NULL(error, error);

  // Handles in the original object are moved
  auto linearized_message = reinterpret_cast<StructWithHandle*>(buffer);
  EXPECT_EQ(message.h, ZX_HANDLE_INVALID);
  EXPECT_EQ(linearized_message->h, dummy_handle);

  END_TEST;
}

bool linearize_struct_with_many_handles() {
  BEGIN_TEST;

  zx_handle_t dummy_handles[4] = {};
  auto handle_value_at = [](int i) -> zx_handle_t { return static_cast<zx_handle_t>(100 + i); };
  for (int i = 0; i < 4; i++) {
    dummy_handles[i] = handle_value_at(i);
  }

  // Define the memory-layout of the inline object
  struct StructWithManyHandles {
    alignas(FIDL_ALIGNMENT) zx_handle_t h1;
    zx_handle_t h2;
    fidl::VectorView<zx_handle_t> hs;
  };

  fidl::VectorView<zx_handle_t> hs;
  hs.set_count(2);
  hs.set_data(&dummy_handles[2]);

  constexpr uint32_t buf_size = 512;
  StructWithManyHandles message = {
      .h1 = dummy_handles[0],
      .h2 = dummy_handles[1],
      .hs = hs,
  };
  uint8_t buffer[buf_size];

  const char* error = nullptr;
  zx_status_t status;
  uint32_t actual_num_bytes = 0;
  status = fidl_linearize(&fidl_test_coding_StructWithManyHandlesTable, &message, buffer, buf_size,
                          &actual_num_bytes, &error);
  ASSERT_EQ(status, ZX_OK);
  ASSERT_GT(actual_num_bytes, sizeof(StructWithManyHandles));
  ASSERT_NULL(error, error);

  // Handles in the original object are moved
  auto linearized_message = reinterpret_cast<StructWithManyHandles*>(buffer);
  EXPECT_EQ(message.h1, ZX_HANDLE_INVALID);
  EXPECT_EQ(message.h2, ZX_HANDLE_INVALID);
  EXPECT_EQ(message.hs[0], ZX_HANDLE_INVALID);
  EXPECT_EQ(message.hs[1], ZX_HANDLE_INVALID);
  EXPECT_EQ(linearized_message->h1, handle_value_at(0));
  EXPECT_EQ(linearized_message->h2, handle_value_at(1));
  EXPECT_EQ(linearized_message->hs[0], handle_value_at(2));
  EXPECT_EQ(linearized_message->hs[1], handle_value_at(3));

  END_TEST;
}

bool linearize_simple_table() {
  BEGIN_TEST;

  SimpleTableEnvelopes envelopes = {};
  SimpleTable simple_table;
  simple_table.set_count(5);
  simple_table.set_data(&envelopes.x);

  IntStruct x = {10};
  IntStruct y = {20};
  envelopes.x.data = &x;
  envelopes.y.data = &y;

  // Attempt to linearize with different table schemas to verify evolution-compatibility
  for (auto coding_table :
       {&fidl_test_coding_SimpleTableTable, &fidl_test_coding_NewerSimpleTableTable}) {
    constexpr uint32_t buf_size = 512;
    uint8_t buffer[buf_size];
    const char* error = nullptr;
    zx_status_t status;
    uint32_t actual_num_bytes = 0;
    status =
        fidl_linearize(coding_table, &simple_table, buffer, buf_size, &actual_num_bytes, &error);
    EXPECT_EQ(status, ZX_OK);
    EXPECT_GT(actual_num_bytes, sizeof(SimpleTable));
    EXPECT_NULL(error, error);

    // Verify object placement
    const auto& linearized = *reinterpret_cast<SimpleTable*>(buffer);
    EXPECT_EQ(reinterpret_cast<IntStruct*>(linearized[0].data)->v, 10);
    EXPECT_EQ(reinterpret_cast<IntStruct*>(linearized[4].data)->v, 20);

    // Verify auto-filling envelope header
    for (int i = 0; i < 5; i++) {
      if (i == 0 || i == 4) {
        EXPECT_EQ(linearized[i].num_bytes, 8);
        EXPECT_EQ(linearized[i].num_handles, 0);
      } else {
        EXPECT_EQ(linearized[i].num_bytes, 0);
        EXPECT_EQ(linearized[i].num_handles, 0);
      }
    }
  }

  // Alternative version with only x set, such that we can use OlderSimpleTable
  envelopes.y.data = nullptr;
  // Attempt to linearize with different table schemas to verify evolution-compatibility
  for (auto coding_table :
       {&fidl_test_coding_OlderSimpleTableTable, &fidl_test_coding_SimpleTableTable,
        &fidl_test_coding_NewerSimpleTableTable}) {
    constexpr uint32_t buf_size = 512;
    uint8_t buffer[buf_size];
    const char* error = nullptr;
    zx_status_t status;
    uint32_t actual_num_bytes = 0;
    status =
        fidl_linearize(coding_table, &simple_table, buffer, buf_size, &actual_num_bytes, &error);
    EXPECT_EQ(status, ZX_OK);
    EXPECT_GT(actual_num_bytes, sizeof(SimpleTable));
    EXPECT_NULL(error, error);

    // Verify object placement
    const auto& linearized = *reinterpret_cast<SimpleTable*>(buffer);
    EXPECT_EQ(reinterpret_cast<IntStruct*>(linearized[0].data)->v, 10);

    // Verify auto-filling envelope header
    EXPECT_EQ(linearized[0].num_bytes, 8);
    EXPECT_EQ(linearized[0].num_handles, 0);
    for (int i = 1; i < 5; i++) {
      EXPECT_EQ(linearized[i].num_bytes, 0);
      EXPECT_EQ(linearized[i].num_handles, 0);
    }
  }

  // If y is set,
  envelopes.y.data = &y;
  // but OlderSimpleTable is used, it should error as the walker does not know how to process y.
  {
    constexpr uint32_t buf_size = 512;
    uint8_t buffer[buf_size];
    const char* error = nullptr;
    zx_status_t status;
    uint32_t actual_num_bytes = 0;
    status = fidl_linearize(&fidl_test_coding_OlderSimpleTableTable, &simple_table, buffer,
                            buf_size, &actual_num_bytes, &error);
    EXPECT_EQ(status, ZX_ERR_INVALID_ARGS);
    EXPECT_EQ(actual_num_bytes, 0);
    EXPECT_NONNULL(error);
  }

  END_TEST;
}

namespace {

bool linearize_table_field_1() {
  BEGIN_TEST;

  TableOfStructEnvelopes envelopes = {};
  TableOfStruct table;
  table.set_count(1);
  table.set_data(&envelopes.a);

  constexpr zx_handle_t dummy_handle = static_cast<zx_handle_t>(42);
  OrdinalOneStructWithHandle ordinal1 = {.h = dummy_handle, .foo = 0};
  envelopes.a.data = &ordinal1;

  constexpr uint32_t buf_size = 512;
  uint8_t buffer[buf_size];
  const char* error = nullptr;
  zx_status_t status;
  uint32_t actual_num_bytes = 0;
  status = fidl_linearize(&fidl_test_coding_TableOfStructWithHandleTable, &table, buffer, buf_size,
                          &actual_num_bytes, &error);
  EXPECT_EQ(status, ZX_OK);
  EXPECT_GT(actual_num_bytes, sizeof(TableOfStruct));
  EXPECT_NULL(error, error);

  // Verify that handles have been moved
  const auto& linearized = *reinterpret_cast<TableOfStruct*>(buffer);
  EXPECT_EQ(reinterpret_cast<OrdinalOneStructWithHandle*>(linearized[0].data)->h, dummy_handle);
  EXPECT_EQ(ordinal1.h, ZX_HANDLE_INVALID);

  // Verify auto-filling envelope header
  EXPECT_EQ(linearized[0].num_bytes, sizeof(ordinal1));
  EXPECT_EQ(linearized[0].num_handles, 1);

  END_TEST;
}

bool linearize_table_field_2() {
  BEGIN_TEST;

  TableOfStructEnvelopes envelopes = {};
  TableOfStruct table;
  table.set_count(2);
  table.set_data(&envelopes.a);

  zx_handle_t dummy_handles[4] = {};
  auto handle_value_at = [](int i) -> zx_handle_t { return static_cast<zx_handle_t>(100 + i); };
  for (int i = 0; i < 4; i++) {
    dummy_handles[i] = handle_value_at(i);
  }
  fidl::VectorView<zx_handle_t> hs;
  hs.set_count(2);
  hs.set_data(&dummy_handles[2]);

  OrdinalTwoStructWithManyHandles ordinal2 = {
      .h1 = dummy_handles[0],
      .h2 = dummy_handles[1],
      .hs = hs,
  };
  envelopes.b.data = &ordinal2;

  constexpr uint32_t buf_size = 512;
  uint8_t buffer[buf_size];
  const char* error = nullptr;
  zx_status_t status;
  uint32_t actual_num_bytes = 0;
  status = fidl_linearize(&fidl_test_coding_TableOfStructWithHandleTable, &table, buffer, buf_size,
                          &actual_num_bytes, &error);
  EXPECT_EQ(status, ZX_OK);
  EXPECT_GT(actual_num_bytes, sizeof(TableOfStruct));
  EXPECT_NULL(error, error);

  // Verify that handles have been moved
  const auto& linearized = *reinterpret_cast<TableOfStruct*>(buffer);
  auto linearized_field = reinterpret_cast<OrdinalTwoStructWithManyHandles*>(linearized[1].data);
  ASSERT_NONNULL(linearized_field);
  EXPECT_EQ(linearized_field->h1, handle_value_at(0));
  EXPECT_EQ(ordinal2.h1, ZX_HANDLE_INVALID);
  EXPECT_EQ(linearized_field->h2, handle_value_at(1));
  EXPECT_EQ(ordinal2.h2, ZX_HANDLE_INVALID);
  EXPECT_EQ(linearized_field->hs[0], handle_value_at(2));
  EXPECT_EQ(ordinal2.hs[0], ZX_HANDLE_INVALID);
  EXPECT_EQ(linearized_field->hs[1], handle_value_at(3));
  EXPECT_EQ(ordinal2.hs[1], ZX_HANDLE_INVALID);

  // Verify auto-filling envelope header
  EXPECT_EQ(linearized[0].num_bytes, 0);
  EXPECT_EQ(linearized[0].num_handles, 0);
  EXPECT_EQ(linearized[1].num_bytes,
            FIDL_ALIGN(sizeof(ordinal2)) + sizeof(zx_handle_t) * hs.count());
  EXPECT_EQ(linearized[1].num_handles, 4);

  END_TEST;
}

}  // namespace

bool linearize_xunion_empty_invariant_empty() {
  BEGIN_TEST;

  // Non-zero ordinal with empty envelope is an error
  SampleNullableXUnionStruct xunion = {};
  xunion.opt_xu.header =
      (fidl_xunion_t){.tag = kSampleXUnionIntStructOrdinal, .envelope = {}};
  constexpr uint32_t buf_size = 512;
  uint8_t buffer[buf_size];
  const char* error = nullptr;
  zx_status_t status;
  uint32_t actual_num_bytes = 0;
  status = fidl_linearize(&fidl_test_coding_SampleNullableXUnionStructTable, &xunion, buffer,
                          buf_size, &actual_num_bytes, &error);
  EXPECT_EQ(status, ZX_ERR_INVALID_ARGS);
  EXPECT_NONNULL(error);
  EXPECT_STR_EQ(error, "empty xunion must have zero as ordinal");

  END_TEST;
}

bool linearize_xunion_empty_invariant_zero_ordinal() {
  BEGIN_TEST;

  // Zero ordinal with non-empty envelope is an error
  IntStruct int_struct = {.v = 100};
  SampleNullableXUnionStruct xunion = {};
  xunion.opt_xu.header = (fidl_xunion_t){
      .tag = 0,
      .envelope = (fidl_envelope_t){.num_bytes = 8, .num_handles = 0, .data = &int_struct}};
  constexpr uint32_t buf_size = 512;
  uint8_t buffer[buf_size];
  const char* error = nullptr;
  zx_status_t status;
  uint32_t actual_num_bytes = 0;
  status = fidl_linearize(&fidl_test_coding_SampleNullableXUnionStructTable, &xunion, buffer,
                          buf_size, &actual_num_bytes, &error);
  EXPECT_EQ(status, ZX_ERR_INVALID_ARGS);
  EXPECT_NONNULL(error);
  EXPECT_STR_EQ(error, "xunion with zero as ordinal must be empty");

  END_TEST;
}

bool linearize_xunion_primitive_field() {
  BEGIN_TEST;

  int32_t raw_int = 42;
  SampleXUnionStruct xunion = {};
  xunion.xu.header = (fidl_xunion_t){
      .tag = kSampleXUnionRawIntOrdinal,
      .envelope = (fidl_envelope_t){.num_bytes = 0, .num_handles = 0, .data = &raw_int}};
  constexpr uint32_t buf_size = 512;
  uint8_t buffer[buf_size];
  const char* error = nullptr;
  zx_status_t status;
  uint32_t actual_num_bytes = 0;
  status = fidl_linearize(&fidl_test_coding_SampleXUnionStructTable, &xunion, buffer, buf_size,
                          &actual_num_bytes, &error);
  EXPECT_EQ(status, ZX_OK);
  EXPECT_NULL(error, error);

  uint8_t golden_linearized_prefix[] = {
      0xe3, 0x60, 0x0e, 0x13,  // The ordinal value is 0x130e60e3
      0x00, 0x00, 0x00, 0x00,  // xunion padding
      0x08, 0x00, 0x00, 0x00,  // num_bytes of envelope
      0x00, 0x00, 0x00, 0x00,  // num_handles of envelope
                               // The out-of-line address of the payload would follow.
  };
  constexpr uint32_t kEnvelopeDataPointerSize = sizeof(uintptr_t);
  constexpr uint32_t kEnvelopePayloadSize = FidlAlign(sizeof(int32_t));
  ASSERT_EQ(actual_num_bytes,
            sizeof(golden_linearized_prefix) + kEnvelopeDataPointerSize + kEnvelopePayloadSize);
  ASSERT_BYTES_EQ(buffer, golden_linearized_prefix, sizeof(golden_linearized_prefix),
                  "linearized result is different from goldens");
  SampleXUnionStruct* linearized = reinterpret_cast<SampleXUnionStruct*>(&buffer[0]);
  int32_t* payload_addr = reinterpret_cast<int32_t*>(linearized->xu.header.envelope.data);
  std::ptrdiff_t distance = reinterpret_cast<uint8_t*>(payload_addr) - &buffer[0];
  ASSERT_EQ(distance, sizeof(golden_linearized_prefix) + kEnvelopeDataPointerSize);
  ASSERT_EQ(*payload_addr, raw_int);

  END_TEST;
}

BEGIN_TEST_CASE(strings)
RUN_TEST(linearize_present_nonnullable_string)
RUN_TEST(linearize_present_nonnullable_longer_string)
END_TEST_CASE(strings)

BEGIN_TEST_CASE(unaligned)
RUN_TEST(linearize_present_nonnullable_string_unaligned_error)
END_TEST_CASE(unaligned)

BEGIN_TEST_CASE(vectors)
RUN_TEST(linearize_vector_of_uint32)
RUN_TEST(linearize_vector_of_nonnullable_uint32_coerce_null_to_empty)
RUN_TEST(linearize_vector_of_string)
END_TEST_CASE(vectors)

BEGIN_TEST_CASE(handles)
RUN_TEST(linearize_struct_with_handle)
RUN_TEST(linearize_struct_with_many_handles)
END_TEST_CASE(handles)

BEGIN_TEST_CASE(tables)
RUN_TEST(linearize_simple_table)
RUN_TEST(linearize_table_field_1)
RUN_TEST(linearize_table_field_2)
END_TEST_CASE(tables)

BEGIN_TEST_CASE(xunions)
RUN_TEST(linearize_xunion_empty_invariant_empty)
RUN_TEST(linearize_xunion_empty_invariant_zero_ordinal)
RUN_TEST(linearize_xunion_primitive_field)
END_TEST_CASE(xunions)

}  // namespace
}  // namespace fidl
