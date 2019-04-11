// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/connectivity/overnet/lib/embedded/encoder.h"
#include "src/connectivity/overnet/lib/embedded/fidl_channel.h"

namespace overnet {
namespace internal {
namespace {

size_t Align(size_t size) {
  constexpr size_t alignment_mask = FIDL_ALIGNMENT - 1;
  return (size + alignment_mask) & ~alignment_mask;
}

}  // namespace

Encoder::Encoder(uint32_t ordinal, FidlChannelIO* fidl_channel_io)
    : stream_(fidl_channel_io->channel()->overnet_stream()) {
  EncodeMessageHeader(ordinal);
}

size_t Encoder::Alloc(size_t size) {
  size_t offset = message_.bytes.size();
  size_t new_size = message_.bytes.size() + Align(size);
  ZX_ASSERT(new_size >= offset);
  message_.bytes.resize(new_size);
  return offset;
}

void Encoder::EncodeMessageHeader(uint32_t ordinal) {
  size_t offset = Alloc(sizeof(fidl_message_header_t));
  fidl_message_header_t* header = GetPtr<fidl_message_header_t>(offset);
  header->ordinal = ordinal;
}

}  // namespace internal
}  // namespace overnet
