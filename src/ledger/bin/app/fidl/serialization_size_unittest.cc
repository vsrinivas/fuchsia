// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/bin/app/fidl/serialization_size.h"

#include <lib/fidl/cpp/binding.h>
#include <lib/fidl/cpp/clone.h>
#include <lib/fidl/cpp/optional.h>
#include <lib/fidl/runtime_flag.h>
#include <lib/gtest/test_loop_fixture.h>

#include <memory>

#include "gtest/gtest.h"
#include "src/ledger/lib/convert/convert.h"
#include "src/lib/fsl/vmo/strings.h"
#include "src/lib/fxl/logging.h"
#include "src/lib/fxl/strings/string_printf.h"

namespace ledger {
namespace fidl_serialization {
namespace {
std::vector<uint8_t> GetKey(size_t index, size_t min_key_size = 0u) {
  std::string result = fxl::StringPrintf("key %04" PRIuMAX, index);
  result.resize(std::max(result.size(), min_key_size));
  return convert::ToArray(result);
}

std::string GetValue(size_t index, size_t min_value_size = 0u) {
  std::string result = fxl::StringPrintf("val %zu", index);
  result.resize(std::max(result.size(), min_value_size));
  return result;
}

::testing::AssertionResult CheckMessageSize(zx::channel channel, size_t expected_bytes,
                                            size_t expected_handles) {
  uint32_t actual_bytes, actual_handles;
  zx_status_t status = channel.read(0, nullptr, nullptr, 0, 0, &actual_bytes, &actual_handles);
  if (status != ZX_ERR_BUFFER_TOO_SMALL) {
    return ::testing::AssertionFailure()
           << "Channel read status = " << status << ", expected ZX_ERR_BUFFER_TOO_SMALL ("
           << ZX_ERR_BUFFER_TOO_SMALL << ").";
  }
  EXPECT_EQ(actual_bytes, expected_bytes);
  EXPECT_EQ(actual_handles, expected_handles);
  if ((expected_bytes != actual_bytes) || (expected_handles != actual_handles)) {
    return ::testing::AssertionFailure() << "Unexpected message size.";
  }
  return ::testing::AssertionSuccess();
}

using SerializationSizeTest = gtest::TestLoopFixture;

class FakeSnapshotImpl : public PageSnapshot {
 public:
  FakeSnapshotImpl() = default;
  ~FakeSnapshotImpl() override = default;

  GetEntriesInlineCallback get_entries_inline_callback;
  GetEntriesCallback get_entries_callback;
  GetCallback get_callback;
  GetInlineCallback get_inline_callback;

  // PageSnapshot:
  void Sync(SyncCallback /*callback*/) override { FXL_NOTIMPLEMENTED(); }

  void GetEntriesInline(std::vector<uint8_t> /*key_start*/, std::unique_ptr<Token> /*token*/,
                        GetEntriesInlineCallback callback) override {
    get_entries_inline_callback = std::move(callback);
  }

  void GetEntries(std::vector<uint8_t> /*key_start*/, std::unique_ptr<Token> /*token*/,
                  GetEntriesCallback callback) override {
    get_entries_callback = std::move(callback);
  }

  void GetKeys(std::vector<uint8_t> /*key_start*/, std::unique_ptr<Token> /*token*/,
               GetKeysCallback /*callback*/) override {
    FXL_NOTIMPLEMENTED();
  }

  void Get(std::vector<uint8_t> /*key*/, GetCallback callback) override {
    get_callback = std::move(callback);
  }

  void GetInline(std::vector<uint8_t> /*key*/, GetInlineCallback callback) override {
    get_inline_callback = std::move(callback);
  }

  void Fetch(std::vector<uint8_t> /*key*/, FetchCallback /*callback*/) override {
    FXL_NOTIMPLEMENTED();
  }

  void FetchPartial(std::vector<uint8_t> /*key*/, int64_t /*offset*/, int64_t /*max_size*/,
                    FetchPartialCallback /*callback*/) override {
    FXL_NOTIMPLEMENTED();
  }
};

TEST_F(SerializationSizeTest, GetInline) {
  PageSnapshotPtr snapshot_proxy;
  FakeSnapshotImpl snapshot_impl;

  zx::channel writer, reader;
  ASSERT_EQ(zx::channel::create(0, &writer, &reader), ZX_OK);

  snapshot_proxy.Bind(std::move(reader));
  fidl::Binding<PageSnapshot> binding(&snapshot_impl);
  binding.Bind(std::move(writer));

  const size_t key_size = 125;
  const size_t value_size = 125;
  std::vector<uint8_t> key = GetKey(0, key_size);
  std::vector<uint8_t> value = convert::ToArray(GetValue(0, value_size));

  auto client_callback = [](fuchsia::ledger::PageSnapshot_GetInline_Result /*value*/) {};

  // FakeSnapshot saves the callback instead of running it.
  snapshot_proxy->GetInline(std::move(key), std::move(client_callback));
  RunLoopUntilIdle();

  fidl::InterfaceHandle<PageSnapshot> handle = snapshot_proxy.Unbind();
  reader = handle.TakeChannel();

  // Run the callback directly.
  fuchsia::ledger::PageSnapshot_GetInline_Result inlined_value;
  inlined_value.response().value.value = std::move(value);
  snapshot_impl.get_inline_callback(std::move(inlined_value));

  const size_t expected_bytes = Align(
      kMessageHeaderSize +
      (fidl_global_get_should_write_union_as_xunion() ? kFlexibleUnionHdrSize : kPointerSize) +
      GetByteVectorSize(value_size));
  const size_t expected_handles = 0;
  EXPECT_TRUE(CheckMessageSize(std::move(reader), expected_bytes, expected_handles));
}

TEST_F(SerializationSizeTest, Get) {
  PageSnapshotPtr snapshot_proxy;
  FakeSnapshotImpl snapshot_impl;

  zx::channel writer, reader;
  ASSERT_EQ(zx::channel::create(0, &writer, &reader), ZX_OK);

  snapshot_proxy.Bind(std::move(reader));
  fidl::Binding<PageSnapshot> binding(&snapshot_impl);
  binding.Bind(std::move(writer));

  const size_t key_size = 8;
  const size_t value_size = 125;
  std::vector<uint8_t> key = GetKey(0, key_size);
  std::string object_data = GetValue(0, value_size);
  fsl::SizedVmo vmo;
  ASSERT_TRUE(fsl::VmoFromString(object_data, &vmo));
  fuchsia::mem::Buffer value = std::move(vmo).ToTransport();

  auto client_callback = [](fuchsia::ledger::PageSnapshot_Get_Result /*result*/) {};
  // FakeSnapshot saves the callback instead of running it.
  snapshot_proxy->Get(std::move(key), std::move(client_callback));
  RunLoopUntilIdle();

  fidl::InterfaceHandle<PageSnapshot> handle = snapshot_proxy.Unbind();
  reader = handle.TakeChannel();

  // Run the callback directly.
  fuchsia::ledger::PageSnapshot_Get_Result result;
  result.response().buffer = std::move(value);
  snapshot_impl.get_callback(std::move(result));

  const size_t expected_bytes =
      Align(kMessageHeaderSize +  // Header.
            (fidl_global_get_should_write_union_as_xunion() ? kFlexibleUnionHdrSize
                                                            : kPointerSize) +  // Union tag.
            Align(kHandleSize) +  // FIDL_HANDLE_PRESENT.
            kPointerSize          // Size.
      );
  const size_t expected_handles = 1;
  EXPECT_TRUE(CheckMessageSize(std::move(reader), expected_bytes, expected_handles));
}

TEST_F(SerializationSizeTest, GetEntriesInline) {
  PageSnapshotPtr snapshot_proxy;
  FakeSnapshotImpl snapshot_impl;
  zx::channel writer, reader;
  ASSERT_EQ(zx::channel::create(0, &writer, &reader), ZX_OK);

  snapshot_proxy.Bind(std::move(reader));
  fidl::Binding<PageSnapshot> binding(&snapshot_impl);
  binding.Bind(std::move(writer));

  auto client_callback = [](std::vector<InlinedEntry> /*entries*/,
                            std::unique_ptr<Token> /*next_token*/) {};
  // FakeSnapshot saves the callback instead of running it.
  snapshot_proxy->GetEntriesInline({}, nullptr, std::move(client_callback));
  RunLoopUntilIdle();

  fidl::InterfaceHandle<PageSnapshot> handle = snapshot_proxy.Unbind();
  reader = handle.TakeChannel();

  std::vector<InlinedEntry> entries_to_send;

  const size_t key_size = 125;
  const size_t value_size = 125;
  const size_t n_entries = 7;
  const size_t n_empty_entries = 7;
  auto token = std::make_unique<Token>();
  token->opaque_id = GetKey(0, key_size);
  InlinedEntry entry;
  entry.key = GetKey(0, key_size);
  entry.inlined_value = std::make_unique<InlinedValue>();
  entry.inlined_value->value = convert::ToArray(GetValue(0, value_size));
  size_t kExpectedEntrySize = GetInlinedEntrySize(entry);
  for (size_t i = 0; i < n_entries; i++) {
    entries_to_send.push_back(fidl::Clone(entry));
  }
  InlinedEntry empty_entry;
  empty_entry.key = GetKey(0, key_size);
  for (size_t i = 0; i < n_empty_entries; i++) {
    entries_to_send.push_back(fidl::Clone(empty_entry));
  }
  size_t kExpectedEmptyEntrySize = GetInlinedEntrySize(empty_entry);

  // Run the callback directly.
  snapshot_impl.get_entries_inline_callback(std::move(entries_to_send), std::move(token));

  const size_t expected_bytes =
      Align(kMessageHeaderSize +                         // Header.
            kVectorHeaderSize +                          // VectorPtr.
            n_entries * kExpectedEntrySize +             // Vector of entries.
            n_empty_entries * kExpectedEmptyEntrySize +  // Vector of entries.
            kPointerSize +                               // Pointer to next_token.
            GetByteVectorSize(key_size)                  // next_token.
      );
  const size_t expected_handles = 0;
  EXPECT_TRUE(CheckMessageSize(std::move(reader), expected_bytes, expected_handles));
}

TEST_F(SerializationSizeTest, GetEntries) {
  PageSnapshotPtr snapshot_proxy;
  FakeSnapshotImpl snapshot_impl;
  zx::channel writer, reader;
  ASSERT_EQ(zx::channel::create(0, &writer, &reader), ZX_OK);

  snapshot_proxy.Bind(std::move(reader));
  fidl::Binding<PageSnapshot> binding(&snapshot_impl);
  binding.Bind(std::move(writer));

  auto client_callback = [](std::vector<Entry> /*entries*/, std::unique_ptr<Token> /*next_token*/) {
  };
  // FakeSnapshot saves the callback instead of running it.
  snapshot_proxy->GetEntries({}, nullptr, std::move(client_callback));
  RunLoopUntilIdle();

  fidl::InterfaceHandle<PageSnapshot> handle = snapshot_proxy.Unbind();
  reader = handle.TakeChannel();

  std::vector<Entry> entries_to_send;

  const size_t key_size = 125;
  const size_t value_size = 125;
  const size_t n_entries = 10;
  auto token = std::make_unique<Token>();
  token->opaque_id = GetKey(0, key_size);
  for (size_t i = 0; i < n_entries; i++) {
    Entry entry;
    std::string object_data = GetValue(0, value_size);
    fsl::SizedVmo vmo;
    ASSERT_TRUE(fsl::VmoFromString(object_data, &vmo));
    entry.value = fidl::MakeOptional(std::move(vmo).ToTransport());
    entry.key = GetKey(0, key_size);
    entry.priority = Priority::EAGER;
    entries_to_send.push_back(std::move(entry));
  }

  // Run the callback directly.
  snapshot_impl.get_entries_callback(std::move(entries_to_send), std::move(token));

  const size_t expected_bytes = Align(kMessageHeaderSize +                  // Header.
                                      kVectorHeaderSize +                   // VectorPtr.
                                      n_entries * GetEntrySize(key_size) +  // Vector of entries.
                                      kPointerSize +               // Pointer to next_token.
                                      GetByteVectorSize(key_size)  // next_token.
  );
  const size_t expected_handles = n_entries;
  EXPECT_TRUE(CheckMessageSize(std::move(reader), expected_bytes, expected_handles));
}

}  // namespace
}  // namespace fidl_serialization
}  // namespace ledger
