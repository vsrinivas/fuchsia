// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include "../decompress.h"

#include <inttypes.h>
#include <lib/fitx/result.h>
#include <lib/zbitl/error_stdio.h>
#include <lib/zbitl/view.h>
#include <stdio.h>
#include <string.h>
#include <zircon/assert.h>

#include <ktl/byte.h>
#include <ktl/span.h>

#include "../memory.h"
#include "test-main.h"

const char Symbolize::kProgramName_[] = "decompress-test";

namespace {

constexpr char kTestPayload[] =
    "abcdefghijklmnopqrstuvwxyz"
    "abcdefghijklmnopqrstuvwxyz";
constexpr uint32_t kTestPayloadSize = sizeof(kTestPayload) - 1;

}  // namespace

int TestMain(void* zbi_ptr, arch::EarlyTicks) {
  // Initialize memory for allocation/free.
  InitMemory(static_cast<const zbi_header_t*>(zbi_ptr));

  // Fetch ZBI.
  zbitl::View<zbitl::ByteView> zbi(
      zbitl::StorageFromRawHeader(static_cast<const zbi_header_t*>(zbi_ptr)));

  printf("Scanning ZBI of %zu bytes...\n", zbi.size_bytes());

  // Search for a payload of type ZBI_TYPE_STORAGE_RAMDISK.
  auto it = zbi.begin();
  while (it != zbi.end() && (*it).header->type != ZBI_TYPE_STORAGE_RAMDISK) {
    ++it;
  }

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

  // Attempt to decompress the payload.
  auto result = CopyAndDecompressItem(zbi, it);
  if (result.is_error()) {
    printf("FAILED: Could not decompress payload.\n");
    return 1;
  }

  // Ensure the payload matched our expected value.
  printf("Copied payload: `%.*s`\n", static_cast<int>(result->size),
         reinterpret_cast<uint8_t*>(result->ptr.get()));
  if (result->size != kTestPayloadSize) {
    printf("FAILED: Payload size incorrect: wanted %" PRIu32 ", got %" PRIu64 "\n",
           kTestPayloadSize, result->size);
    return 1;
  }
  if (memcmp(result->ptr.get(), kTestPayload, kTestPayloadSize) != 0) {
    printf("FAILED! Incorrect payload value. Expected payload: `%s`\n", kTestPayload);
    return 1;
  }

  printf("Success.\n");
  return 0;
}
