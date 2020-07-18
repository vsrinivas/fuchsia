// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/zbitl/vmo.h>

#include "tests.h"

namespace {

struct VmoIo {
  using storage_type = zx::vmo;

  void Create(std::string_view contents, zx::vmo* zbi) {
    zx::vmo vmo;
    ASSERT_OK(zx::vmo::create(contents.size(), 0u, &vmo));
    ASSERT_OK(vmo.write(contents.data(), 0u, contents.size()));
    *zbi = std::move(vmo);
  }

  void ReadPayload(const zx::vmo& zbi, const zbi_header_t& header, uint64_t payload,
              std::string* string) {
    string->resize(header.length);
    ASSERT_EQ(ZX_OK, zbi.read(string->data(), payload, header.length));
  }
};

TEST(ZbitlViewVmoTests, DefaultConstructed) {
  ASSERT_NO_FATAL_FAILURES(TestDefaultConstructedView<VmoIo>(true));
}

TEST(ZbitlViewVmoTests, EmptyZbi) { ASSERT_NO_FATAL_FAILURES(TestEmptyZbi<VmoIo>()); }

TEST(ZbitlViewVmoTests, SimpleZbi) { ASSERT_NO_FATAL_FAILURES(TestSimpleZbi<VmoIo>()); }

TEST(ZbitlViewVmoTests, BadCrcZbi) { ASSERT_NO_FATAL_FAILURES(TestBadCrcZbi<VmoIo>()); }

}  // namespace
