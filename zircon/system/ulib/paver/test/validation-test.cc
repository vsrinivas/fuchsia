// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "validation.h"

#include <zircon/boot/image.h>
#include <zircon/errors.h>

#include <memory>
#include <vector>

#include <fbl/array.h>
#include <fbl/span.h>
#include <zxtest/zxtest.h>

#include "device-partitioner.h"
#include "test-utils.h"

namespace paver {
namespace {

// Allocate header and data following it, and give it some basic defaults.
//
// If "span" is non-null, it will be initialized with a span covering
// the allocated data.
//
// If "result_header" is non-null, it will point to the beginning of the
// uint8_t. It must not outlive the returned fbl::Array object.
fbl::Array<uint8_t> CreateZbiHeader(Arch arch, size_t payload_size, zircon_kernel_t** result_header,
                                    fbl::Span<uint8_t>* span = nullptr) {
  // Allocate raw memory.
  const size_t data_size = sizeof(zircon_kernel_t) + payload_size;
  auto data = fbl::Array<uint8_t>(new uint8_t[data_size], data_size);
  memset(data.get(), 0xee, data_size);

  // Set up header for outer ZBI header.
  auto header = reinterpret_cast<zircon_kernel_t*>(data.get());
  header->hdr_file.type = ZBI_TYPE_CONTAINER;
  header->hdr_file.extra = ZBI_CONTAINER_MAGIC;
  header->hdr_file.magic = ZBI_ITEM_MAGIC;
  header->hdr_file.flags = ZBI_FLAG_VERSION;
  header->hdr_file.crc32 = ZBI_ITEM_NO_CRC32;
  header->hdr_file.length =
      static_cast<uint32_t>(sizeof(zbi_header_t) + sizeof(zbi_kernel_t) + payload_size);

  // Set up header for inner ZBI header.
  header->hdr_kernel.type = (arch == Arch::kX64) ? ZBI_TYPE_KERNEL_X64 : ZBI_TYPE_KERNEL_ARM64;
  header->hdr_kernel.magic = ZBI_ITEM_MAGIC;
  header->hdr_kernel.flags = ZBI_FLAG_VERSION;
  header->hdr_kernel.crc32 = ZBI_ITEM_NO_CRC32;
  header->hdr_kernel.length = static_cast<uint32_t>(sizeof(zbi_kernel_t) + payload_size);

  if (span != nullptr) {
    *span = fbl::Span<uint8_t>(data.get(), data_size);
  }
  if (result_header != nullptr) {
    *result_header = reinterpret_cast<zircon_kernel_t*>(data.get());
  }

  return data;
}

TEST(IsValidKernelZbi, EmptyData) {
  ASSERT_FALSE(IsValidKernelZbi(Arch::kX64, fbl::Span<uint8_t>()));
}

TEST(IsValidKernelZbi, MinimalValid) {
  fbl::Span<uint8_t> data;
  zircon_kernel_t* header;
  auto array = CreateZbiHeader(Arch::kX64, 0, &header, &data);
  ASSERT_TRUE(IsValidKernelZbi(Arch::kX64, data));
}

TEST(IsValidKernelZbi, DataTooSmall) {
  fbl::Span<uint8_t> data;
  zircon_kernel_t* header;
  auto array = CreateZbiHeader(Arch::kX64, 1024, &header, &data);
  header->hdr_file.length += 1;
  ASSERT_FALSE(IsValidKernelZbi(Arch::kX64, data));
}

TEST(IsValidKernelZbi, DataTooBig) {
  fbl::Span<uint8_t> data;
  zircon_kernel_t* header;
  auto array = CreateZbiHeader(Arch::kX64, 1024, &header, &data);
  header->hdr_file.length = 0xffff'ffffu;
  ASSERT_FALSE(IsValidKernelZbi(Arch::kX64, data));
}

TEST(IsValidKernelZbi, KernelDataTooSmall) {
  fbl::Span<uint8_t> data;
  zircon_kernel_t* header;
  auto array = CreateZbiHeader(Arch::kX64, 1024, &header, &data);
  header->hdr_kernel.length += 1;
  ASSERT_FALSE(IsValidKernelZbi(Arch::kX64, data));
}

TEST(IsValidKernelZbi, ValidWithPayload) {
  fbl::Span<uint8_t> data;
  zircon_kernel_t* header;
  auto array = CreateZbiHeader(Arch::kX64, 1024, &header, &data);
  ASSERT_TRUE(IsValidKernelZbi(Arch::kX64, data));
}

TEST(IsValidKernelZbi, InvalidArch) {
  fbl::Span<uint8_t> data;
  zircon_kernel_t* header;
  auto array = CreateZbiHeader(Arch::kX64, 0, &header, &data);
  ASSERT_FALSE(IsValidKernelZbi(Arch::kArm64, data));
}

TEST(IsValidKernelZbi, InvalidMagic) {
  fbl::Span<uint8_t> data;
  zircon_kernel_t* header;
  auto array = CreateZbiHeader(Arch::kX64, 0, &header, &data);
  header->hdr_file.magic = 0;
  ASSERT_FALSE(IsValidKernelZbi(Arch::kX64, data));
}

TEST(IsValidKernelZbi, ValidCrc) {
  fbl::Span<uint8_t> data;
  zircon_kernel_t* header;
  auto array = CreateZbiHeader(Arch::kX64, 0, &header, &data);
  header->hdr_kernel.flags |= ZBI_FLAG_CRC32;
  header->data_kernel.entry = 0x1122334455667788;
  header->data_kernel.reserve_memory_size = 0xaabbccdd12345678;
  header->hdr_kernel.crc32 = 0x8b8e6cfc;  // == crc32({header->data_kernel})
  ASSERT_TRUE(IsValidKernelZbi(Arch::kX64, data));
}

TEST(IsValidKernelZbi, InvalidCrc) {
  fbl::Span<uint8_t> data;
  zircon_kernel_t* header;
  auto array = CreateZbiHeader(Arch::kX64, 0, &header, &data);
  header->hdr_kernel.flags |= ZBI_FLAG_CRC32;
  header->data_kernel.entry = 0x1122334455667788;
  header->data_kernel.reserve_memory_size = 0xaabbccdd12345678;
  header->hdr_kernel.crc32 = 0xffffffff;
  ASSERT_FALSE(IsValidKernelZbi(Arch::kX64, data));
}

}  // namespace
}  // namespace paver
