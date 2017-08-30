// Copyright 2014 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIDL_CPP_BINDINGS_INTERNAL_MESSAGE_INTERNAL_H_
#define LIB_FIDL_CPP_BINDINGS_INTERNAL_MESSAGE_INTERNAL_H_

#include "lib/fidl/cpp/bindings/internal/bindings_internal.h"

namespace fidl {
namespace internal {

#pragma pack(push, 1)

enum { kMessageExpectsResponse = 1 << 0, kMessageIsResponse = 1 << 1 };

struct MessageHeader : internal::StructHeader {
  uint32_t name;
  uint32_t flags;
};
static_assert(sizeof(MessageHeader) == 16, "Bad sizeof(MessageHeader)");

struct MessageHeaderWithRequestID : MessageHeader {
  uint64_t request_id;
};
static_assert(sizeof(MessageHeaderWithRequestID) == 24,
              "Bad sizeof(MessageHeaderWithRequestID)");

struct MessageData {
  MessageHeader header;
};

static_assert(sizeof(MessageData) == sizeof(MessageHeader),
              "Bad sizeof(MessageData)");

#pragma pack(pop)

}  // namespace internal
}  // namespace fidl

#endif  // LIB_FIDL_CPP_BINDINGS_INTERNAL_MESSAGE_INTERNAL_H_
