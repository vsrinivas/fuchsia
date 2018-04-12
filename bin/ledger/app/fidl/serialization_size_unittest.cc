// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/ledger/app/fidl/serialization_size.h"

#include <memory>

#include "garnet/lib/gtest/test_with_message_loop.h"
#include "gtest/gtest.h"
#include "lib/fidl/cpp/binding.h"
#include "lib/fidl/cpp/clone.h"
#include "lib/fidl/cpp/optional.h"
#include "lib/fsl/vmo/strings.h"
#include "lib/fxl/strings/string_printf.h"
#include "peridot/lib/convert/convert.h"

namespace ledger {
namespace fidl_serialization {
namespace {
fidl::VectorPtr<uint8_t> GetKey(size_t index, size_t min_key_size = 0u) {
  std::string result = fxl::StringPrintf("key %04" PRIuMAX, index);
  result.resize(std::max(result.size(), min_key_size));
  return convert::ToArray(result);
}

std::string GetValue(size_t index, size_t min_value_size = 0u) {
  std::string result = fxl::StringPrintf("val %zu", index);
  result.resize(std::max(result.size(), min_value_size));
  return result;
}

::testing::AssertionResult CheckMessageSize(zx::channel channel,
                                            size_t expected_bytes,
                                            size_t expected_handles) {
  uint32_t actual_bytes, actual_handles;
  zx_status_t status =
      channel.read(0, nullptr, 0, &actual_bytes, nullptr, 0, &actual_handles);
  if (status != ZX_ERR_BUFFER_TOO_SMALL) {
    return ::testing::AssertionFailure()
           << "Channel read status = " << status
           << ", expected ZX_ERR_BUFFER_TOO_SMALL (" << ZX_ERR_BUFFER_TOO_SMALL
           << ").";
  }
  EXPECT_EQ(expected_bytes, actual_bytes);
  EXPECT_EQ(expected_handles, actual_handles);
  if ((expected_bytes != actual_bytes) ||
      (expected_handles != actual_handles)) {
    return ::testing::AssertionFailure() << "Unexpected message size.";
  }
  return ::testing::AssertionSuccess();
}

using SerializationSizeTest = gtest::TestWithMessageLoop;

class FakeSnapshotImpl : public PageSnapshot {
 public:
  FakeSnapshotImpl() {}
  ~FakeSnapshotImpl() override {}

  GetEntriesInlineCallback get_entries_inline_callback;
  GetEntriesCallback get_entries_callback;
  GetCallback get_callback;
  GetInlineCallback get_inline_callback;

  // PageSnapshot:
  void GetEntriesInline(fidl::VectorPtr<uint8_t> /*key_start*/,
                        fidl::VectorPtr<uint8_t> /*token*/,
                        GetEntriesInlineCallback callback) override {
    get_entries_inline_callback = std::move(callback);
  }

  void GetEntries(fidl::VectorPtr<uint8_t> /*key_start*/,
                  fidl::VectorPtr<uint8_t> /*token*/,
                  GetEntriesCallback callback) override {
    get_entries_callback = std::move(callback);
  }

  void GetKeys(fidl::VectorPtr<uint8_t> /*key_start*/,
               fidl::VectorPtr<uint8_t> /*token*/,
               GetKeysCallback /*callback*/) override {}

  void Get(fidl::VectorPtr<uint8_t> /*key*/, GetCallback callback) override {
    get_callback = std::move(callback);
  }

  void GetInline(fidl::VectorPtr<uint8_t> /*key*/,
                 GetInlineCallback callback) override {
    get_inline_callback = std::move(callback);
  }

  void Fetch(fidl::VectorPtr<uint8_t> /*key*/,
             FetchCallback /*callback*/) override {
    FXL_NOTIMPLEMENTED();
  }

  void FetchPartial(fidl::VectorPtr<uint8_t> /*key*/,
                    int64_t /*offset*/,
                    int64_t /*max_size*/,
                    FetchPartialCallback /*callback*/) override {
    FXL_NOTIMPLEMENTED();
  }
};

TEST_F(SerializationSizeTest, GetInline) {
  PageSnapshotPtr snapshot_proxy;
  FakeSnapshotImpl snapshot_impl;

  zx::channel writer, reader;
  ASSERT_EQ(ZX_OK, zx::channel::create(0, &writer, &reader));

  snapshot_proxy.Bind(std::move(reader));
  fidl::Binding<PageSnapshot> binding(&snapshot_impl);
  binding.Bind(std::move(writer));

  const size_t key_size = 125;
  const size_t value_size = 125;
  fidl::VectorPtr<uint8_t> key = GetKey(0, key_size);
  fidl::VectorPtr<uint8_t> value = convert::ToArray(GetValue(0, value_size));

  auto client_callback = [](Status /*status*/,
                            fidl::VectorPtr<uint8_t> /*value*/) {};
  // FakeSnapshot saves the callback instead of running it.
  snapshot_proxy->GetInline(std::move(key), std::move(client_callback));
  RunLoopUntilIdle();

  fidl::InterfaceHandle<ledger::PageSnapshot> handle = snapshot_proxy.Unbind();
  reader = handle.TakeChannel();

  // Run the callback directly.
  snapshot_impl.get_inline_callback(Status::OK, std::move(value));

  const size_t expected_bytes = Align(
      kMessageHeaderSize + GetByteVectorSize(value_size) + kStatusEnumSize);
  const size_t expected_handles = 0;
  EXPECT_TRUE(
      CheckMessageSize(std::move(reader), expected_bytes, expected_handles));
}

TEST_F(SerializationSizeTest, Get) {
  PageSnapshotPtr snapshot_proxy;
  FakeSnapshotImpl snapshot_impl;

  zx::channel writer, reader;
  ASSERT_EQ(ZX_OK, zx::channel::create(0, &writer, &reader));

  snapshot_proxy.Bind(std::move(reader));
  fidl::Binding<PageSnapshot> binding(&snapshot_impl);
  binding.Bind(std::move(writer));

  const size_t key_size = 8;
  const size_t value_size = 125;
  fidl::VectorPtr<uint8_t> key = GetKey(0, key_size);
  std::string object_data = GetValue(0, value_size);
  fsl::SizedVmo vmo;
  ASSERT_TRUE(fsl::VmoFromString(object_data, &vmo));
  mem::BufferPtr value = fidl::MakeOptional(std::move(vmo).ToTransport());

  auto client_callback = [](Status /*status*/, mem::BufferPtr /*value*/) {};
  // FakeSnapshot saves the callback instead of running it.
  snapshot_proxy->Get(std::move(key), std::move(client_callback));
  RunLoopUntilIdle();

  fidl::InterfaceHandle<ledger::PageSnapshot> handle = snapshot_proxy.Unbind();
  reader = handle.TakeChannel();

  // Run the callback directly.
  snapshot_impl.get_callback(Status::OK, std::move(value));

  const size_t expected_bytes =
      Align(kMessageHeaderSize +  // Header.
            kPointerSize +        // BufferPtr.
            kPointerSize +        // Size.
            Align(kHandleSize) +  // FIDL_HANDLE_PRESENT.
            kStatusEnumSize       // Status.
      );
  const size_t expected_handles = 1;
  EXPECT_TRUE(
      CheckMessageSize(std::move(reader), expected_bytes, expected_handles));
}

TEST_F(SerializationSizeTest, GetEntriesInline) {
  PageSnapshotPtr snapshot_proxy;
  FakeSnapshotImpl snapshot_impl;
  zx::channel writer, reader;
  ASSERT_EQ(ZX_OK, zx::channel::create(0, &writer, &reader));

  snapshot_proxy.Bind(std::move(reader));
  fidl::Binding<PageSnapshot> binding(&snapshot_impl);
  binding.Bind(std::move(writer));

  auto client_callback = [](Status /*status*/,
                            fidl::VectorPtr<InlinedEntry> /*entries*/,
                            fidl::VectorPtr<uint8_t> /*next_token*/) {};
  // FakeSnapshot saves the callback instead of running it.
  snapshot_proxy->GetEntriesInline(nullptr, nullptr,
                                   std::move(client_callback));
  RunLoopUntilIdle();

  fidl::InterfaceHandle<ledger::PageSnapshot> handle = snapshot_proxy.Unbind();
  reader = handle.TakeChannel();

  fidl::VectorPtr<InlinedEntry> entries_to_send;

  const size_t key_size = 125;
  const size_t value_size = 125;
  const size_t n_entries = 10;
  fidl::VectorPtr<uint8_t> token = GetKey(0, key_size);
  InlinedEntry entry;
  entry.key = GetKey(0, key_size);
  entry.value = convert::ToArray(GetValue(0, value_size));
  for (size_t i = 0; i < n_entries; i++) {
    entries_to_send->push_back(fidl::Clone(entry));
  }

  // Run the callback directly.
  snapshot_impl.get_entries_inline_callback(
      Status::OK, std::move(entries_to_send), std::move(token));

  const size_t expected_bytes = Align(
      kMessageHeaderSize +                                 // Header.
      kVectorHeaderSize +                                  // VectorPtr.
      n_entries * GetInlinedEntrySize(std::move(entry)) +  // Vector of entries.
      GetByteVectorSize(key_size) +                        // next_token.
      kStatusEnumSize                                      // Status.
  );
  const size_t expected_handles = 0;
  EXPECT_TRUE(
      CheckMessageSize(std::move(reader), expected_bytes, expected_handles));
}

TEST_F(SerializationSizeTest, GetEntries) {
  PageSnapshotPtr snapshot_proxy;
  FakeSnapshotImpl snapshot_impl;
  zx::channel writer, reader;
  ASSERT_EQ(ZX_OK, zx::channel::create(0, &writer, &reader));

  snapshot_proxy.Bind(std::move(reader));
  fidl::Binding<PageSnapshot> binding(&snapshot_impl);
  binding.Bind(std::move(writer));

  auto client_callback = [](Status /*status*/,
                            fidl::VectorPtr<Entry> /*entries*/,
                            fidl::VectorPtr<uint8_t> /*next_token*/) {};
  // FakeSnapshot saves the callback instead of running it.
  snapshot_proxy->GetEntries(nullptr, nullptr, std::move(client_callback));
  RunLoopUntilIdle();

  fidl::InterfaceHandle<ledger::PageSnapshot> handle = snapshot_proxy.Unbind();
  reader = handle.TakeChannel();

  fidl::VectorPtr<Entry> entries_to_send;

  const size_t key_size = 125;
  const size_t value_size = 125;
  const size_t n_entries = 10;
  fidl::VectorPtr<uint8_t> token = GetKey(0, key_size);
  for (size_t i = 0; i < n_entries; i++) {
    Entry entry;
    std::string object_data = GetValue(0, value_size);
    fsl::SizedVmo vmo;
    ASSERT_TRUE(fsl::VmoFromString(object_data, &vmo));
    entry.value = fidl::MakeOptional(std::move(vmo).ToTransport());
    entry.key = GetKey(0, key_size);
    entry.priority = Priority::EAGER;
    entries_to_send->push_back(std::move(entry));
  }
  fidl::VectorPtr<uint8_t> next_token_to_send = convert::ToArray(token);

  // Run the callback directly.
  snapshot_impl.get_entries_callback(Status::OK, std::move(entries_to_send),
                                     std::move(next_token_to_send));

  const size_t expected_bytes =
      Align(kMessageHeaderSize +                  // Header.
            kVectorHeaderSize +                   // VectorPtr.
            n_entries * GetEntrySize(key_size) +  // Vector of entries.
            GetByteVectorSize(key_size) +         // next_token.
            kStatusEnumSize                       // Status.
      );
  const size_t expected_handles = n_entries;
  EXPECT_TRUE(
      CheckMessageSize(std::move(reader), expected_bytes, expected_handles));
}

}  // namespace
}  // namespace fidl_serialization
}  // namespace ledger
