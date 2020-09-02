// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/zbitl/vmo.h>

#include "tests.h"

namespace {

struct VmoIo {
  using storage_type = zx::vmo;

  void Create(fbl::unique_fd fd, size_t size, zx::vmo* zbi) {
    ASSERT_TRUE(fd);
    char buff[kMaxZbiSize];
    ASSERT_EQ(size, read(fd.get(), buff, size), "%s", strerror(errno));

    zx::vmo vmo;
    ASSERT_OK(zx::vmo::create(size, 0u, &vmo));
    ASSERT_OK(vmo.write(buff, 0u, size));
    *zbi = std::move(vmo);
  }

  void ReadPayload(const zx::vmo& zbi, const zbi_header_t& header, uint64_t payload,
                   std::string* string) {
    string->resize(header.length);
    ASSERT_EQ(ZX_OK, zbi.read(string->data(), payload, header.length));
  }
};

struct UnownedVmoIo : private VmoIo {
  using storage_type = zx::unowned_vmo;

  void Create(fbl::unique_fd fd, size_t size, zx::unowned_vmo* zbi) {
    ASSERT_FALSE(vmo_, "StorageIo reused for multiple tests");
    VmoIo::Create(std::move(fd), size, &vmo_);
    *zbi = zx::unowned_vmo{vmo_};
  }

  void ReadPayload(const zx::unowned_vmo& zbi, const zbi_header_t& header, uint64_t payload,
                   std::string* string) {
    VmoIo::ReadPayload(*zbi, header, payload, string);
  }

  zx::vmo vmo_;
};

struct MapUnownedVmoIo : protected VmoIo {
  using storage_type = zbitl::MapUnownedVmo;

  void Create(fbl::unique_fd fd, size_t size, zbitl::MapUnownedVmo* zbi) {
    ASSERT_FALSE(vmo_, "StorageIo reused for multiple tests");
    VmoIo::Create(std::move(fd), size, &vmo_);
    *zbi = zbitl::MapUnownedVmo(zx::unowned_vmo{vmo_});
  }

  void ReadPayload(zbitl::MapUnownedVmo& zbi, const zbi_header_t& header, uint64_t payload,
                   std::string* string) {
    VmoIo::ReadPayload(zbi.vmo(), header, payload, string);
  }

  zx::vmo vmo_;
};

struct MapOwnedVmoIo : public MapUnownedVmoIo {
  using storage_type = zbitl::MapOwnedVmo;

  void Create(fbl::unique_fd fd, size_t size, zbitl::MapOwnedVmo* zbi) {
    zx::vmo vmo;
    VmoIo::Create(std::move(fd), size, &vmo);
    *zbi = zbitl::MapOwnedVmo(std::move(vmo));
  }
};

TEST(ZbitlViewVmoTests, DefaultConstructed) {
  ASSERT_NO_FATAL_FAILURES(TestDefaultConstructedView<VmoIo>(true));
}

TEST(ZbitlViewVmoTests, CrcCheckFailure) { ASSERT_NO_FATAL_FAILURES(TestCrcCheckFailure<VmoIo>()); }

TEST_ITERATIONS(ZbitlViewVmoTests, VmoIo)

TEST_MUTATIONS(ZbitlViewVmoTests, VmoIo)

TEST(ZbitlViewUnownedVmoTests, DefaultConstructed) {
  ASSERT_NO_FATAL_FAILURES(TestDefaultConstructedView<UnownedVmoIo>(true));
}

TEST(ZbitlViewUnownedVmoTests, CrcCheckFailure) {
  ASSERT_NO_FATAL_FAILURES(TestCrcCheckFailure<UnownedVmoIo>());
}

TEST_ITERATIONS(ZbitlViewUnownedVmoTests, UnownedVmoIo)

TEST_MUTATIONS(ZbitlViewUnownedVmoTests, UnownedVmoIo)

TEST(ZbitlViewMapUnownedVmoTests, DefaultConstructed) {
  ASSERT_NO_FATAL_FAILURES(TestDefaultConstructedView<MapUnownedVmoIo>(true));
}

TEST(ZbitlViewMapUnownedVmoTests, CrcCheckFailure) {
  ASSERT_NO_FATAL_FAILURES(TestCrcCheckFailure<MapUnownedVmoIo>());
}

// Note that the iterations over many-small-items.zbi and
// second-item-on-page-boundary.zbi with CRC32 checking will cover the cases of
// mapping window re-use and replacement, respectively.
TEST_ITERATIONS(ZbitlViewMapUnownedVmoTests, MapUnownedVmoIo)

TEST_MUTATIONS(ZbitlViewMapUnownedVmoTests, MapUnownedVmoIo)

TEST(ZbitlViewMapOwnedVmoTests, DefaultConstructed) {
  ASSERT_NO_FATAL_FAILURES(TestDefaultConstructedView<MapOwnedVmoIo>(true));
}

TEST(ZbitlViewMapOwnedVmoTests, CrcCheckFailure) {
  ASSERT_NO_FATAL_FAILURES(TestCrcCheckFailure<MapOwnedVmoIo>());
}

TEST_ITERATIONS(ZbitlViewMapOwnedVmoTests, MapOwnedVmoIo)

TEST_MUTATIONS(ZbitlViewMapOwnedVmoTests, MapOwnedVmoIo)

}  // namespace
