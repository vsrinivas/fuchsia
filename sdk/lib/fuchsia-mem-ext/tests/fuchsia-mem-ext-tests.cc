// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fuchsia-mem-ext/fuchsia-mem-ext.h>

#include <cstddef>

#include <zxtest/zxtest.h>

TEST(CreateWithData, EmptySpan) {
  cpp20::span<uint8_t> empty_span;
  zx::result empty_data = fuchsia_mem_ext::CreateWithData(empty_span);

  ASSERT_OK(empty_data.status_value());

  ASSERT_TRUE(empty_data->is_bytes());
  EXPECT_EQ(empty_data->bytes().size(), 0u);
}

TEST(CreateWithData, EmptyVector) {
  std::vector<uint8_t> empty_vector;
  zx::result empty_data = fuchsia_mem_ext::CreateWithData(std::move(empty_vector));

  ASSERT_OK(empty_data.status_value());

  ASSERT_TRUE(empty_data->is_bytes());
  EXPECT_EQ(empty_data->bytes().size(), 0u);
}

TEST(CreateWithData, SmallData) {
  uint8_t single_byte_array[] = {42};
  zx::result small_data = fuchsia_mem_ext::CreateWithData(cpp20::span(single_byte_array));

  ASSERT_OK(small_data.status_value());

  ASSERT_TRUE(small_data->is_bytes());
  ASSERT_EQ(small_data->bytes().size(), 1u);
  EXPECT_EQ(small_data->bytes()[0], 42);
}

TEST(CreateWithData, JustUnderThreshold) {
  constexpr size_t data_size = 16 * 1024 - 1;
  std::vector<uint8_t> byte_vector(data_size, 42);

  zx::result data =
      fuchsia_mem_ext::CreateWithData(cpp20::span(byte_vector.data(), byte_vector.size()));

  ASSERT_OK(data.status_value());

  ASSERT_TRUE(data->is_bytes());
  ASSERT_EQ(data->bytes().size(), data_size);
  EXPECT_EQ(data->bytes()[0], 42);
  EXPECT_EQ(data->bytes()[data_size - 1], 42);
}

TEST(CreateWithData, AtThreshold) {
  constexpr size_t data_size = 16 * 1024;
  std::vector<uint8_t> byte_vector(data_size, 42);

  zx::result data =
      fuchsia_mem_ext::CreateWithData(cpp20::span(byte_vector.data(), byte_vector.size()));

  ASSERT_OK(data.status_value());

  ASSERT_TRUE(data->is_bytes());
  ASSERT_EQ(data->bytes().size(), data_size);
  EXPECT_EQ(data->bytes()[0], 42);
  EXPECT_EQ(data->bytes()[data_size - 1], 42);
}

TEST(CreateWithData, JustOverThreshold) {
  constexpr size_t data_size = 16 * 1024 + 1;
  std::vector<uint8_t> byte_vector(data_size, 42);

  zx::result data =
      fuchsia_mem_ext::CreateWithData(cpp20::span(byte_vector.data(), byte_vector.size()));

  ASSERT_OK(data.status_value());

  ASSERT_TRUE(data->is_buffer());
  ASSERT_EQ(data->buffer().size, data_size);
  uint64_t content_size = 0;
  // TODO(https://fxbug.dev/85472): Use vmo.get_prop_content_size() when available.
  ASSERT_OK(data->buffer().vmo.get_property(ZX_PROP_VMO_CONTENT_SIZE, &content_size,
                                            sizeof(content_size)));
  EXPECT_EQ(content_size, data_size);

  uint8_t contents{0};
  ASSERT_OK(data->buffer().vmo.read(&contents, 0, 1));
  EXPECT_EQ(contents, 42);

  ASSERT_OK(data->buffer().vmo.read(&contents, data_size - 1, 1));
  EXPECT_EQ(contents, 42);
}

TEST(CreateWithData, LargeDataSpan) {
  constexpr size_t large_data_size = 128 * 1024;
  std::vector<uint8_t> large_byte_vector(large_data_size, 42);

  zx::result data = fuchsia_mem_ext::CreateWithData(
      cpp20::span(large_byte_vector.data(), large_byte_vector.size()));

  ASSERT_OK(data.status_value());

  ASSERT_TRUE(data->is_buffer());
  EXPECT_GE(data->buffer().size, large_data_size);
  uint64_t content_size = 0;
  // TODO(https://fxbug.dev/85472): Use vmo.get_prop_content_size() when available.
  ASSERT_OK(data->buffer().vmo.get_property(ZX_PROP_VMO_CONTENT_SIZE, &content_size,
                                            sizeof(content_size)));
  EXPECT_EQ(content_size, large_data_size);

  uint8_t contents{0};
  ASSERT_OK(data->buffer().vmo.read(&contents, 0, 1));
  EXPECT_EQ(contents, 42);
}

TEST(CreateWithData, LargeDataVector) {
  constexpr size_t large_data_size = 128 * 1024;
  std::vector<uint8_t> large_byte_vector(large_data_size, 42);

  zx::result data = fuchsia_mem_ext::CreateWithData(std::move(large_byte_vector));

  ASSERT_OK(data.status_value());

  ASSERT_TRUE(data->is_buffer());
  EXPECT_GE(data->buffer().size, large_data_size);
  uint64_t content_size = 0;
  // TODO(https://fxbug.dev/85472): Use vmo.get_prop_content_size() when available.
  ASSERT_OK(data->buffer().vmo.get_property(ZX_PROP_VMO_CONTENT_SIZE, &content_size,
                                            sizeof(content_size)));
  EXPECT_EQ(content_size, large_data_size);

  uint8_t contents{0};
  ASSERT_OK(data->buffer().vmo.read(&contents, 0, 1));
  EXPECT_EQ(contents, 42);
}

TEST(CreateWithDataAndThreshold, ZeroThreshold) {
  uint8_t single_byte_array[] = {42};
  constexpr size_t zero_threshold = 0;
  zx::result data = fuchsia_mem_ext::CreateWithData(cpp20::span(single_byte_array), zero_threshold);

  ASSERT_OK(data.status_value());

  ASSERT_TRUE(data->is_buffer());
  EXPECT_GE(data->buffer().size, 1);
  uint64_t content_size = 0;
  // TODO(https://fxbug.dev/85472): Use vmo.get_prop_content_size() when available.
  ASSERT_OK(data->buffer().vmo.get_property(ZX_PROP_VMO_CONTENT_SIZE, &content_size,
                                            sizeof(content_size)));
  EXPECT_EQ(content_size, 1);

  uint8_t contents{0};
  ASSERT_OK(data->buffer().vmo.read(&contents, 0, 1));
  EXPECT_EQ(contents, 42);
}

TEST(CreateWithDataAndThreshold, OverlyLargeThreshold) {
  uint8_t single_byte_array[] = {42};
  constexpr size_t huge_threshold = 128 * 1024;
  zx::result data = fuchsia_mem_ext::CreateWithData(cpp20::span(single_byte_array), huge_threshold);

  EXPECT_TRUE(data.is_error());
  EXPECT_EQ(data.status_value(), ZX_ERR_OUT_OF_RANGE);
}

TEST(CreateWithDataAndThreshold, VmoName) {
  const std::string vmo_name = "test_vmo_name";

  uint8_t single_byte_array[] = {42};
  constexpr size_t zero_threshold = 0;
  zx::result data =
      fuchsia_mem_ext::CreateWithData(cpp20::span(single_byte_array), zero_threshold, vmo_name);

  ASSERT_OK(data.status_value());

  ASSERT_TRUE(data->is_buffer());

  std::array<char, ZX_MAX_NAME_LEN> name_buffer;
  ASSERT_OK(data->buffer().vmo.get_property(ZX_PROP_NAME, name_buffer.data(), name_buffer.size()));

  // get_property(ZX_PROP_NAME) provides a null-terminated buffer.
  std::string retrieved_name = std::string(name_buffer.data());

  EXPECT_EQ(vmo_name, name_buffer.data());
}

TEST(CreateWithData, VmoNameTooLong) {
  const std::string long_vmo_name(2 * ZX_MAX_NAME_LEN, 'a');

  uint8_t single_byte_array[] = {42};
  zx::result data = fuchsia_mem_ext::CreateWithData(cpp20::span(single_byte_array), long_vmo_name);

  ASSERT_FALSE(data.is_ok());

  EXPECT_EQ(data.status_value(), ZX_ERR_OUT_OF_RANGE);
}

TEST(CreateWithDataAndThreshold, VmoNameTooLong) {
  const std::string long_vmo_name(2 * ZX_MAX_NAME_LEN, 'a');

  uint8_t single_byte_array[] = {42};
  constexpr size_t zero_threshold = 0;
  zx::result data = fuchsia_mem_ext::CreateWithData(cpp20::span(single_byte_array), zero_threshold,
                                                    long_vmo_name);

  ASSERT_FALSE(data.is_ok());

  EXPECT_EQ(data.status_value(), ZX_ERR_OUT_OF_RANGE);
}

TEST(ExtractData, EmptyBytes) {
  std::vector<uint8_t> empty;
  auto data = fuchsia::mem::Data::WithBytes(std::move(empty));

  zx::result extracted = fuchsia_mem_ext::ExtractData(std::move(data));

  ASSERT_OK(extracted.status_value());

  EXPECT_EQ(extracted->size(), 0);
}

TEST(ExtractData, EmptyBuffer) {
  zx::vmo empty_vmo;
  ASSERT_OK(zx::vmo::create(0u, 0u, &empty_vmo));
  auto data = fuchsia::mem::Data::WithBuffer({std::move(empty_vmo), 0u});

  zx::result extracted = fuchsia_mem_ext::ExtractData(std::move(data));

  ASSERT_OK(extracted.status_value());

  EXPECT_EQ(extracted->size(), 0);
}

TEST(ExtractData, Bytes) {
  std::vector<uint8_t> data{1, 2, 3, 4, 5};
  fuchsia::mem::Data bytes_backed_data = fuchsia::mem::Data::WithBytes(std::move(data));

  zx::result extracted = fuchsia_mem_ext::ExtractData(std::move(bytes_backed_data));

  ASSERT_OK(extracted.status_value());

  EXPECT_EQ(extracted->size(), 5);

  for (uint8_t i = 0; i < 5; ++i) {
    EXPECT_EQ(extracted->at(i), i + 1);
  }
}

TEST(ExtractData, Buffer) {
  constexpr size_t data_size = 8 * 1024;
  std::vector<uint8_t> data(data_size, 42);
  zx::vmo vmo;
  uint32_t options = 0;
  ASSERT_OK(zx::vmo::create(data.size(), options, &vmo));

  ASSERT_OK(vmo.write(data.data(), 0u, data.size()));

  auto buffer_backed_data = fuchsia::mem::Data::WithBuffer({std::move(vmo), data.size()});

  zx::result extracted = fuchsia_mem_ext::ExtractData(std::move(buffer_backed_data));

  ASSERT_OK(extracted.status_value());

  EXPECT_EQ(extracted->size(), data_size);

  for (size_t i = 0; i < data_size; ++i) {
    EXPECT_EQ(data[i], extracted->at(i));
  }
}
