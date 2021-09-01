// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <fidl/fuchsia.hardware.acpi/cpp/wire.h>
#include <lib/fdio/cpp/caller.h>
#include <lib/fdio/directory.h>
#include <lib/zx/channel.h>
#include <lib/zx/vmo.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <fbl/array.h>
#include <fbl/span.h>
#include <fbl/unique_fd.h>
#include <zxtest/zxtest.h>

// These are integration tests of the x86 board drive which check that exported services
// are correctly functioning.

namespace {

constexpr uint32_t kGiB = 1024 * 1024 * 1024;

using fuchsia_hardware_acpi::Acpi;
using fuchsia_hardware_acpi::wire::TableInfo;

const char kAcpiDevicePath[] = "/dev/sys/platform/acpi";

// Open up channel to ACPI device.
fdio_cpp::FdioCaller OpenChannel() {
  fbl::unique_fd fd{open(kAcpiDevicePath, O_RDWR)};
  ZX_ASSERT(fd.is_valid());
  return fdio_cpp::FdioCaller{std::move(fd)};
}

// Convert a fidl::Array<uint8_t, n> type to a std::string.
template <uint64_t N>
std::string SignatureToString(const fidl::Array<uint8_t, N>& array) {
  return std::string(reinterpret_cast<const char*>(array.data()), array.size());
}

// Convert a string type to a fidl::Array<uint8_t, 4>.
fidl::Array<uint8_t, 4> StringToSignature(std::string_view str) {
  fidl::Array<uint8_t, 4> result;
  ZX_ASSERT(str.size() >= 4);
  memcpy(result.data(), str.data(), 4);
  return result;
}

// Create a pair of VMOs for transferring data to and from the x86 board driver.
//
// |size| specifies how much memory to use for the VMO. By default, we allocate
// 1 GiB to ensure that we have more than enough space: the kernel won't actually
// allocate this memory until needed, though, so in practice we will only use
// a tiny fraction of this (A typical size for the DSDT table is ~100kiB.)
std::tuple<zx::vmo, zx::vmo> CreateVmoPair(size_t size = 1 * kGiB) {
  zx::vmo a, b;
  ZX_ASSERT(zx::vmo::create(size, /*options=*/0, &a) == ZX_OK);
  ZX_ASSERT(a.duplicate(ZX_RIGHT_SAME_RIGHTS, &b) == ZX_OK);
  return std::make_tuple(std::move(a), std::move(b));
}

TEST(X86Board, Connect) {
  fdio_cpp::FdioCaller dev = OpenChannel();
  EXPECT_TRUE(dev.channel()->is_valid());
}

TEST(X86Board, ListTableEntries) {
  fdio_cpp::FdioCaller dev = OpenChannel();

  fidl::WireResult<Acpi::ListTableEntries> result =
      fidl::WireCall<Acpi>(dev.channel()).ListTableEntries();
  ASSERT_TRUE(result.ok());
  ASSERT_TRUE(result.value().result.is_response());
  const auto& response = result.value().result.response();

  // We expect to find at least a DSDT entry.
  EXPECT_GE(response.entries.count(), 1);
  bool found_dsdt = false;
  for (const TableInfo& info : response.entries) {
    if (SignatureToString(info.name) != "DSDT") {
      continue;
    }
    EXPECT_GE(info.size, 1);
    found_dsdt = true;
  }
  EXPECT_TRUE(found_dsdt);
}

TEST(X86Board, ReadNamedTable) {
  fdio_cpp::FdioCaller dev = OpenChannel();

  // Read the system's DSDT entry. Every system should have one of these.
  auto [vmo, vmo_copy] = CreateVmoPair();
  fidl::WireResult<Acpi::ReadNamedTable> result =
      fidl::WireCall<Acpi>(dev.channel())
          .ReadNamedTable(StringToSignature("DSDT"), 0, std::move(vmo_copy));
  ASSERT_TRUE(result.ok());
  ASSERT_TRUE(result.value().result.is_response());
  const auto& response = result.value().result.response();

  // Ensure the size looks sensible.
  ASSERT_GE(response.size, 4);

  // Ensure the first four bytes match "DSDT".
  char buff[4];
  ASSERT_OK(vmo.read(buff, /*offset=*/0, /*length=*/4));
  EXPECT_EQ("DSDT", std::string_view(buff, 4));
}

TEST(X86Board, InvalidTableName) {
  fdio_cpp::FdioCaller dev = OpenChannel();

  // Read an invalid entry.
  auto [vmo, vmo_copy] = CreateVmoPair();
  fidl::WireResult<Acpi::ReadNamedTable> result =
      fidl::WireCall<Acpi>(dev.channel())
          .ReadNamedTable(StringToSignature("???\n"), 0, std::move(vmo_copy));
  ASSERT_TRUE(result.ok());
  EXPECT_TRUE(result.value().result.is_err());
  EXPECT_EQ(result.value().result.err(), ZX_ERR_NOT_FOUND);
}

TEST(X86Board, InvalidIndexNumber) {
  fdio_cpp::FdioCaller dev = OpenChannel();

  // Read a large index of the DSDT table. We should have a DSDT table, but really only
  // have 1 of them.
  auto [vmo, vmo_copy] = CreateVmoPair();
  fidl::WireResult<Acpi::ReadNamedTable> result =
      fidl::WireCall<Acpi>(dev.channel())
          .ReadNamedTable(StringToSignature("DSDT"), 1234, std::move(vmo_copy));
  ASSERT_TRUE(result.ok());
  EXPECT_TRUE(result.value().result.is_err());
  EXPECT_EQ(result.value().result.err(), ZX_ERR_NOT_FOUND);
}

TEST(X86Board, VmoTooSmall) {
  fdio_cpp::FdioCaller dev = OpenChannel();

  // Only allocate a VMO with 3 bytes backing it.
  auto [vmo, vmo_copy] = CreateVmoPair(/*size=*/3);
  fidl::WireResult<Acpi::ReadNamedTable> result =
      fidl::WireCall<Acpi>(dev.channel())
          .ReadNamedTable(StringToSignature("DSDT"), 0, std::move(vmo_copy));
  ASSERT_TRUE(result.ok());
  EXPECT_TRUE(result.value().result.is_err());
  EXPECT_EQ(result.value().result.err(), ZX_ERR_OUT_OF_RANGE);
}

TEST(X86Board, ReadOnlyVmoSent) {
  fdio_cpp::FdioCaller dev = OpenChannel();

  // Send a read-only VMO.
  auto [vmo, vmo_copy] = CreateVmoPair();
  zx::vmo read_only_vmo;
  ZX_ASSERT(vmo_copy.replace(ZX_RIGHT_NONE, &read_only_vmo) == ZX_OK);
  fidl::WireResult<Acpi::ReadNamedTable> result =
      fidl::WireCall<Acpi>(dev.channel())
          .ReadNamedTable(StringToSignature("DSDT"), 0, std::move(read_only_vmo));
  EXPECT_EQ(result.status(), ZX_ERR_ACCESS_DENIED);
}

TEST(X86Board, InvalidObject) {
  fdio_cpp::FdioCaller dev = OpenChannel();

  // Send something that is not a VMO.
  zx::channel a, b;
  zx::channel::create(/*flags=*/0, &a, &b);
  fidl::WireResult<Acpi::ReadNamedTable> result =
      fidl::WireCall<Acpi>(dev.channel())
          .ReadNamedTable(StringToSignature("DSDT"), 0, zx::vmo(a.release()));
  // FIDL detects that a channel is being sent as a VMO handle.
  ASSERT_FALSE(result.ok());
}

}  // namespace
