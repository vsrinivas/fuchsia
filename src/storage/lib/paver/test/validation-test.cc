// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/lib/paver/validation.h"

#include <lib/stdcompat/span.h>
#include <zircon/boot/image.h>
#include <zircon/errors.h>

#include <memory>
#include <vector>

#include <fbl/array.h>
#include <zxtest/zxtest.h>

#include "src/storage/lib/paver/device-partitioner.h"
#include "src/storage/lib/paver/test/test-utils.h"

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
                                    cpp20::span<uint8_t>* span = nullptr) {
  // Allocate raw memory.
  const size_t data_size = sizeof(zircon_kernel_t) + payload_size;
  auto data = fbl::Array<uint8_t>(new uint8_t[data_size], data_size);
  memset(data.get(), 0xee, data_size);

  // Set up header for outer ZBI header.
  auto header = reinterpret_cast<zircon_kernel_t*>(data.get());
  header->hdr_file.type = ZBI_TYPE_CONTAINER;
  header->hdr_file.extra = ZBI_CONTAINER_MAGIC;
  header->hdr_file.magic = ZBI_ITEM_MAGIC;
  header->hdr_file.flags = ZBI_FLAGS_VERSION;
  header->hdr_file.crc32 = ZBI_ITEM_NO_CRC32;
  header->hdr_file.length =
      static_cast<uint32_t>(sizeof(zbi_header_t) + sizeof(zbi_kernel_t) + payload_size);

  // Set up header for inner ZBI header.
  header->hdr_kernel.type = (arch == Arch::kX64) ? ZBI_TYPE_KERNEL_X64 : ZBI_TYPE_KERNEL_ARM64;
  header->hdr_kernel.magic = ZBI_ITEM_MAGIC;
  header->hdr_kernel.flags = ZBI_FLAGS_VERSION;
  header->hdr_kernel.crc32 = ZBI_ITEM_NO_CRC32;
  header->hdr_kernel.length = static_cast<uint32_t>(sizeof(zbi_kernel_t) + payload_size);

  if (span != nullptr) {
    *span = cpp20::span<uint8_t>(data.get(), data_size);
  }
  if (result_header != nullptr) {
    *result_header = reinterpret_cast<zircon_kernel_t*>(data.get());
  }

  return data;
}

TEST(IsValidKernelZbi, EmptyData) {
  ASSERT_FALSE(IsValidKernelZbi(Arch::kX64, cpp20::span<uint8_t>()));
}

TEST(IsValidKernelZbi, MinimalValid) {
  cpp20::span<uint8_t> data;
  zircon_kernel_t* header;
  auto array = CreateZbiHeader(Arch::kX64, 0, &header, &data);
  ASSERT_TRUE(IsValidKernelZbi(Arch::kX64, data));
}

TEST(IsValidKernelZbi, DataTooSmall) {
  cpp20::span<uint8_t> data;
  zircon_kernel_t* header;
  auto array = CreateZbiHeader(Arch::kX64, 1024, &header, &data);
  header->hdr_file.length += 1;
  ASSERT_FALSE(IsValidKernelZbi(Arch::kX64, data));
}

TEST(IsValidKernelZbi, DataTooBig) {
  cpp20::span<uint8_t> data;
  zircon_kernel_t* header;
  auto array = CreateZbiHeader(Arch::kX64, 1024, &header, &data);
  header->hdr_file.length = 0xffff'ffffu;
  ASSERT_FALSE(IsValidKernelZbi(Arch::kX64, data));
}

TEST(IsValidKernelZbi, KernelDataTooSmall) {
  cpp20::span<uint8_t> data;
  zircon_kernel_t* header;
  auto array = CreateZbiHeader(Arch::kX64, 1024, &header, &data);
  header->hdr_kernel.length += 1;
  ASSERT_FALSE(IsValidKernelZbi(Arch::kX64, data));
}

TEST(IsValidKernelZbi, ValidWithPayload) {
  cpp20::span<uint8_t> data;
  zircon_kernel_t* header;
  auto array = CreateZbiHeader(Arch::kX64, 1024, &header, &data);
  ASSERT_TRUE(IsValidKernelZbi(Arch::kX64, data));
}

TEST(IsValidKernelZbi, InvalidArch) {
  cpp20::span<uint8_t> data;
  zircon_kernel_t* header;
  auto array = CreateZbiHeader(Arch::kX64, 0, &header, &data);
  ASSERT_FALSE(IsValidKernelZbi(Arch::kArm64, data));
}

TEST(IsValidKernelZbi, InvalidMagic) {
  cpp20::span<uint8_t> data;
  zircon_kernel_t* header;
  auto array = CreateZbiHeader(Arch::kX64, 0, &header, &data);
  header->hdr_file.magic = 0;
  ASSERT_FALSE(IsValidKernelZbi(Arch::kX64, data));
}

TEST(IsValidKernelZbi, ValidCrc) {
  cpp20::span<uint8_t> data;
  zircon_kernel_t* header;
  auto array = CreateZbiHeader(Arch::kX64, 0, &header, &data);
  header->hdr_kernel.flags |= ZBI_FLAGS_CRC32;
  header->data_kernel.entry = 0x1122334455667788;
  header->data_kernel.reserve_memory_size = 0xaabbccdd12345678;
  header->hdr_kernel.crc32 = 0x8b8e6cfc;  // == crc32({header->data_kernel})
  ASSERT_TRUE(IsValidKernelZbi(Arch::kX64, data));
}

TEST(IsValidKernelZbi, InvalidCrc) {
  cpp20::span<uint8_t> data;
  zircon_kernel_t* header;
  auto array = CreateZbiHeader(Arch::kX64, 0, &header, &data);
  header->hdr_kernel.flags |= ZBI_FLAGS_CRC32;
  header->data_kernel.entry = 0x1122334455667788;
  header->data_kernel.reserve_memory_size = 0xaabbccdd12345678;
  header->hdr_kernel.crc32 = 0xffffffff;
  ASSERT_FALSE(IsValidKernelZbi(Arch::kX64, data));
}

static cpp20::span<const uint8_t> StringToSpan(const std::string& data) {
  return cpp20::span<const uint8_t>(reinterpret_cast<const uint8_t*>(data.data()), data.size());
}

TEST(IsValidChromeOSKernel, TooSmall) {
  ASSERT_FALSE(IsValidChromeOSKernel(StringToSpan("")));
  ASSERT_FALSE(IsValidChromeOSKernel(StringToSpan("C")));
  ASSERT_FALSE(IsValidChromeOSKernel(StringToSpan("CHROMEO")));
}

TEST(IsValidChromeOSKernel, IncorrectMagic) {
  ASSERT_FALSE(IsValidChromeOSKernel(StringToSpan("CHROMEOX")));
}

TEST(IsValidChromeOSKernel, MinimalValid) {
  ASSERT_TRUE(IsValidChromeOSKernel(StringToSpan("CHROMEOS")));
}

TEST(IsValidChromeOSKernel, ExcessData) {
  ASSERT_TRUE(IsValidChromeOSKernel(StringToSpan("CHROMEOS-1234")));
}

}  // namespace
}  // namespace paver
