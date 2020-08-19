// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/zbitl/memory.h>

#include "tests.h"

namespace {

template <typename T>
struct FblArrayIo {
  using storage_type = fbl::Array<T>;

  void Create(std::string_view contents, storage_type* storage) {
    const size_t n = (contents.size() + sizeof(T) - 1) / sizeof(T);
    *storage = storage_type{new T[n], n};
    ASSERT_GE(storage->size() * sizeof(T), contents.size());
    memcpy(storage->data(), contents.data(), contents.size());
  }

  void ReadPayload(const storage_type& zbi, const zbi_header_t& header, fbl::Span<T> payload,
                   std::string* string) {
    string->resize(header.length);
    auto bytes = fbl::as_bytes(payload);
    ASSERT_EQ(header.length, bytes.size());
    memcpy(string->data(), bytes.data(), bytes.size());
  }
};

TEST(ZbitlViewFblArrayTests, DefaultConstructed) {
  ASSERT_NO_FATAL_FAILURES(TestDefaultConstructedView<FblArrayIo<std::byte>>(false));
  ASSERT_NO_FATAL_FAILURES(TestDefaultConstructedView<FblArrayIo<uint64_t>>(false));
}

TEST(ZbitlViewFblArrayTests, EmptyZbi) {
  ASSERT_NO_FATAL_FAILURES(TestEmptyZbi<FblArrayIo<std::byte>>());
  ASSERT_NO_FATAL_FAILURES(TestEmptyZbi<FblArrayIo<uint64_t>>());
}

// TODO: These use data with odd-sized payloads so they can't test uint64_t.

TEST(ZbitlViewFblArrayTests, SimpleZbi) {
  ASSERT_NO_FATAL_FAILURES(TestSimpleZbi<FblArrayIo<std::byte>>());
}

TEST(ZbitlViewFblArrayTests, BadCrcZbi) {
  ASSERT_NO_FATAL_FAILURES(TestBadCrcZbi<FblArrayIo<std::byte>>());
}

TEST(ZbitlViewFblArrayTests, Mutation) {
  ASSERT_NO_FATAL_FAILURES(TestMutation<FblArrayIo<std::byte>>());
}

}  // namespace
