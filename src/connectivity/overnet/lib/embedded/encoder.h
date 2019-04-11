// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <fuchsia/overnet/protocol/cpp/fidl.h>
#include "src/connectivity/overnet/lib/endpoint/router_endpoint.h"

namespace overnet {
namespace internal {

class FidlChannelIO;

class Encoder {
 public:
  explicit Encoder(uint32_t ordinal, FidlChannelIO* channel);

  size_t Alloc(size_t size);

  template <typename T>
  T* GetPtr(size_t offset) {
    return reinterpret_cast<T*>(message_.bytes.data() + offset);
  }

  fuchsia::overnet::protocol::ZirconChannelMessage GetMessage() {
    return std::move(message_);
  }

  size_t CurrentLength() const { return message_.bytes.size(); }

  size_t CurrentHandleCount() const { return message_.handles.size(); }

  template <class Constructor>
  RouterEndpoint::NewStream AppendHandle(
      size_t offset,
      fuchsia::overnet::protocol::ReliabilityAndOrdering
          reliability_and_ordering,
      Constructor constructor) {
    auto new_stream =
        std::move(*stream_->InitiateFork(reliability_and_ordering));
    constructor(&message_.handles.emplace_back(), new_stream.stream_id());
    *GetPtr<zx_handle_t>(offset) = FIDL_HANDLE_PRESENT;
    return new_stream;
  }

 private:
  void EncodeMessageHeader(uint32_t ordinal);

  fuchsia::overnet::protocol::ZirconChannelMessage message_;
  RouterEndpoint::Stream* const stream_;
};

}  // namespace internal
}  // namespace overnet
