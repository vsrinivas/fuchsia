// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/fitx/result.h>
#include <lib/zbitl/error_stdio.h>
#include <lib/zbitl/view.h>
#include <stdio.h>
#include <string.h>
#include <zircon/assert.h>

#include <ktl/byte.h>
#include <ktl/span.h>

#include "test-main.h"

const char Symbolize::kProgramName_[] = "decompress-test";

namespace {

constexpr char kTestPayload[] =
    "abcdefghijklmnopqrstuvwxyz"
    "abcdefghijklmnopqrstuvwxyz";
constexpr uint32_t kTestPayloadSize = sizeof(kTestPayload) - 1;

// This test uses a fixed buffer so it doesn't need any real memory handling.
constexpr size_t kScratchSize = 256 << 10;

struct ScratchAllocator {
  fitx::result<ktl::string_view, ScratchAllocator> operator()(size_t size) const {
    ZX_ASSERT(size <= sizeof(buffer_));
    return fitx::ok(ScratchAllocator{});
  }

  void* get() const { return buffer_; }

  inline static ktl::byte buffer_ alignas(std::max_align_t)[kScratchSize];
};

}  // namespace

int TestMain(void* zbi_ptr, arch::EarlyTicks) {
  zbitl::PermissiveView<ktl::span<ktl::byte>> zbi({static_cast<ktl::byte*>(zbi_ptr), SIZE_MAX});

  printf("Scanning ZBI of %zu bytes...\n", zbi.size_bytes());

  for (auto it = zbi.begin(); it != zbi.end(); ++it) {
    if ((*it).header->type == ZBI_TYPE_STORAGE_RAMDISK) {
      uint32_t size = zbitl::UncompressedLength(*(*it).header);
      printf("Found RAMDISK item of %u bytes (%u)...\n", (*it).header->length, size);

      // Ignore the iteration state now that we've found the item.
      zbi.ignore_error();

      char payload[kTestPayloadSize];
      ZX_ASSERT_MSG(size == kTestPayloadSize, "decompressed size %u != test size %u\n", size,
                    kTestPayloadSize);

      if (auto result =
              zbi.CopyStorageItem(ktl::span(payload, sizeof(payload)), it, ScratchAllocator{});
          result.is_error()) {
        zbitl::PrintViewCopyError(result.error_value());
        return 1;
      }

      printf("Copied payload: `%.*s`\n", static_cast<int>(sizeof(payload)), payload);
      if (!memcmp(payload, kTestPayload, sizeof(payload))) {
        return 0;
      }

      printf("FAILED!  Expected payload: `%s`\n", kTestPayload);
      return 1;
    }
  }

  if (auto result = zbi.take_error(); result.is_error()) {
    zbitl::PrintViewError(result.error_value());
    return 1;
  }

  printf("No RAMDISK item found!\n");
  return 1;
}
