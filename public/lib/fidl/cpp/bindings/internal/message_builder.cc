// Copyright 2013 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/fidl/cpp/bindings/internal/message_builder.h"

#include "lib/fidl/cpp/bindings/internal/bindings_serialization.h"
#include "lib/fidl/cpp/bindings/message.h"

namespace fidl {
namespace {
using internal::MessageHeader;
using internal::MessageHeaderWithRequestID;

template <typename Header>
void Allocate(internal::Buffer* buf, Header** header) {
  *header = static_cast<Header*>(buf->Allocate(sizeof(Header)));
  (*header)->num_bytes = sizeof(Header);
}

}  // namespace

namespace internal {

MessageWithRequestIDBuilder::MessageWithRequestIDBuilder(uint32_t name,
                                                         size_t payload_size,
                                                         uint32_t flags,
                                                         uint64_t request_id) {
  Initialize(sizeof(MessageHeaderWithRequestID) + payload_size);
  MessageHeaderWithRequestID* header;
  Allocate(&buf_, &header);
  header->version = 1;
  header->name = name;
  header->flags = flags;
  header->request_id = request_id;
}

}  // namespace internal

MessageBuilder::MessageBuilder(uint32_t name, size_t payload_size) {
  Initialize(sizeof(MessageHeader) + payload_size);

  MessageHeader* header;
  Allocate(&buf_, &header);
  header->version = 0;
  header->name = name;
  header->flags = 0;
}

MessageBuilder::~MessageBuilder() {}

MessageBuilder::MessageBuilder() {}

void MessageBuilder::Initialize(size_t size) {
  message_.AllocData(static_cast<uint32_t>(internal::Align(size)));
  buf_.Initialize(message_.mutable_data(), message_.data_num_bytes());
}

}  // namespace fidl
