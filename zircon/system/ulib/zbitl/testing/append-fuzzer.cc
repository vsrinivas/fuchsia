// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/zbitl/image.h>
#include <lib/zbitl/memory.h>
#include <zircon/assert.h>
#include <zircon/boot/image.h>

#include <fuzzer/FuzzedDataProvider.h>

constexpr size_t kMaxAppends = 5;
constexpr size_t kMaxPayloadSize = 512;

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  zbitl::Image<fbl::Array<uint8_t>> image;
  ZX_ASSERT(image.clear().is_ok());

  FuzzedDataProvider provider(data, size);
  size_t appends = 0;
  while (provider.remaining_bytes() && appends++ < kMaxAppends) {
    bool deferred_write = provider.ConsumeBool();
    bool random_header = provider.ConsumeBool();
    auto payload_size = provider.ConsumeIntegralInRange<uint32_t>(0, kMaxPayloadSize);

    zbi_header_t header;
    if (random_header) {
      provider.ConsumeData(&header, sizeof(header));
    } else {
      header = zbitl::SanitizeHeader(zbi_header_t{
          .type = ZBI_TYPE_IMAGE_ARGS,
          .length = payload_size,
      });
    }

    if (deferred_write) {
      auto result = image.Append(header);
      if (result.is_error()) {
        continue;
      }
      auto it = result.value();
      fbl::Span<uint8_t> payload = (*it).payload;
      provider.ConsumeData(payload.data(), payload.size());
    } else {
      std::string payload = provider.ConsumeBytesAsString(payload_size);
      static_cast<void>(image.Append(header, zbitl::AsBytes(payload.data(), payload.size())));
    }
  }

  for (auto [header, payload] : image) {
    static_cast<void>(header);
    static_cast<void>(payload);
  }
  image.ignore_error();
  return 0;
}
