// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/zbitl/memory.h>

#include "tests.h"

namespace {

template <typename T>
struct FblArrayIo {
  using storage_type = fbl::Array<T>;

  void Create(fbl::unique_fd fd, size_t size, storage_type* storage) {
    ASSERT_TRUE(fd);
    const size_t n = (size + sizeof(T) - 1) / sizeof(T);
    *storage = storage_type{new T[n], n};
    ASSERT_EQ(size, read(fd.get(), storage->data(), size));
  }

  void ReadPayload(const storage_type& zbi, const zbi_header_t& header, fbl::Span<T> payload,
                   std::string* string) {
    string->resize(header.length);
    auto bytes = fbl::as_bytes(payload);
    ASSERT_EQ(header.length, bytes.size());
    memcpy(string->data(), bytes.data(), bytes.size());
  }
};

using FblByteArrayIo = FblArrayIo<std::byte>;
using FblUint64ArrayIo = FblArrayIo<uint64_t>;

TEST(ZbitlViewFblByteArrayTests, DefaultConstructed) {
  ASSERT_NO_FATAL_FAILURES(TestDefaultConstructedView<FblByteArrayIo>(false));
}

TEST(ZbitlViewFblByteArrayTests, CrcCheckFailure) {
  ASSERT_NO_FATAL_FAILURES(TestCrcCheckFailure<FblByteArrayIo>());
}

TEST_ITERATIONS(ZbitlViewFblByteArrayTests, FblByteArrayIo)

TEST_MUTATIONS(ZbitlViewFblByteArrayTests, FblByteArrayIo)

TEST(ZbitlViewFblUint64ArrayTests, DefaultConstructed) {
  ASSERT_NO_FATAL_FAILURES(TestDefaultConstructedView<FblUint64ArrayIo>(false));
}

// TODO(joshuaseaton): Use ZBIs with payload size divisible by eight so we can
// further test FblUint64Array.

}  // namespace
