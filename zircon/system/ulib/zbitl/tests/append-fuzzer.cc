// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/zbitl/image.h>
#include <zircon/assert.h>
#include <zircon/boot/image.h>

#include <fuzzer/FuzzedDataProvider.h>

#include "traits.h"

namespace {

template <typename Storage>
int Fuzz(FuzzedDataProvider& provider) {
  using Traits = FuzzTraits<Storage>;

  Storage zbi;
  {
    auto result = Traits::Create(zbi, 0, 0);
    ZX_ASSERT(result.is_ok());
    zbi = std::move(std::move(result).value());
  }
  zbitl::Image image(std::move(zbi));
  ZX_ASSERT(image.clear().is_ok());  // Now an empty container.

  bool deferred_write = provider.ConsumeBool();
  bool fuzzed_header = provider.ConsumeBool();
  auto payload_size = provider.ConsumeIntegralInRange<uint32_t>(0, Traits::kRoughSizeMax);
  std::string payload_str = provider.ConsumeBytesAsString(payload_size);
  zbitl::ByteView payload_bytes = zbitl::AsBytes(payload_str.data(), payload_str.size());

  zbi_header_t header;
  if (fuzzed_header) {
    provider.ConsumeData(&header, sizeof(header));
    header.length = payload_size;
  } else {
    header = zbitl::SanitizeHeader(zbi_header_t{
        .type = ZBI_TYPE_IMAGE_ARGS,
        .length = payload_size,
    });
  }

  // Fuzz one of two paths: if `deferred_write`, append the header and header
  // separately; otherwise, write them as a one-shot call.
  if (deferred_write) {
    auto result = image.Append(header);
    if (result.is_error()) {
      return 0;
    }
    auto it = result.value();
    // Write to the resulting payload 'pointer', so that the fuzzing
    // instrumentation can catch weird memory access/bounds issues.
    if (!payload_bytes.empty()) {
      static_cast<void>(Traits::Write(image.storage(), it.payload_offset(), payload_bytes));
    }
  } else {
    static_cast<void>(image.Append(header, payload_bytes));
  }

  return 0;
}

}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  FuzzedDataProvider provider(data, size);

  switch (provider.ConsumeEnum<StorageType>()) {
    case StorageType::kFblByteArray:
      return Fuzz<fbl::Array<std::byte>>(provider);
#ifdef __Fuchsia__
    case StorageType::kVmo:
      return Fuzz<zx::vmo>(provider);
#endif
    case StorageType::kMaxValue:  // Placeholder.
      return 0;
  };
}
