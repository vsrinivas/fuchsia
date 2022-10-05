// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <inttypes.h>
#include <lib/fit/result.h>
#include <lib/memalloc/range.h>
#include <lib/zbitl/error-stdio.h>
#include <lib/zbitl/view.h>
#include <stdio.h>
#include <string.h>
#include <zircon/assert.h>

#include <ktl/byte.h>
#include <ktl/span.h>
#include <phys/allocation.h>
#include <phys/zbitl-allocation.h>

#include "test-main.h"

#include <ktl/enforce.h>

namespace {

constexpr char kTestPayload[] =
    "abcdefghijklmnopqrstuvwxyz"
    "abcdefghijklmnopqrstuvwxyz";
constexpr uint32_t kTestPayloadSize = sizeof(kTestPayload) - 1;

}  // namespace

int TestMain(void* zbi_ptr, arch::EarlyTicks) {
  MainSymbolize symbolize("decompress-test");

  // Initialize memory for allocation/free.
  InitMemory(zbi_ptr);

  // Fetch ZBI.
  zbitl::View<zbitl::ByteView> zbi(
      zbitl::StorageFromRawHeader(static_cast<const zbi_header_t*>(zbi_ptr)));

  printf("Scanning ZBI of %zu bytes...\n", zbi.size_bytes());

  // Search for a payload of type ZBI_TYPE_STORAGE_RAMDISK.
  auto it = zbi.find(ZBI_TYPE_STORAGE_RAMDISK);

  // Ensure there was no error during iteration.
  if (auto result = zbi.take_error(); result.is_error()) {
    printf("FAILED: Error while enumerating ZBI payload: ");
    zbitl::PrintViewError(result.error_value());
    return 1;
  }

  // Fail if we didn't find anything.
  if (it == zbi.end()) {
    printf("FAILED: No payload found.\n");
    return 1;
  }

  const auto length = zbitl::UncompressedLength(*it->header);
  fbl::AllocChecker ac;
  auto payload = Allocation::New(ac, memalloc::Type::kZbiTestPayload, length);
  if (!ac.check()) {
    printf("FAILED: Could not allocate %u bytes for payload.\n", length);
    return 1;
  }

  // Attempt to decompress the payload.
  auto result = zbi.CopyStorageItem(payload.data(), it, ZbitlScratchAllocator);
  if (result.is_error()) {
    printf("FAILED: Could not decompress payload: ");
    zbitl::PrintViewCopyError(result.error_value());
    return 1;
  }

  // Ensure the payload matched our expected value.
  printf("Copied payload: `%.*s`\n", static_cast<int>(payload.data().size()),
         reinterpret_cast<const char*>(payload.get()));
  if (payload.data().size() != kTestPayloadSize) {
    printf("FAILED: Payload size incorrect: wanted %" PRIu32 ", got %" PRIu64 "\n",
           kTestPayloadSize, payload.data().size());
    return 1;
  }
  if (memcmp(payload.get(), kTestPayload, kTestPayloadSize) != 0) {
    printf("FAILED! Incorrect payload value. Expected payload: `%s`\n", kTestPayload);
    return 1;
  }

  printf("Success.\n");
  return 0;
}
