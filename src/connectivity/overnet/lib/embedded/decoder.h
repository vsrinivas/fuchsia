// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <fuchsia/overnet/protocol/cpp/fidl.h>
#include "src/connectivity/overnet/lib/endpoint/router_endpoint.h"
#include "src/connectivity/overnet/lib/vocabulary/optional.h"

namespace overnet {
namespace internal {

class FidlChannelIO;

class Decoder {
 public:
  explicit Decoder(fuchsia::overnet::protocol::ZirconChannelMessage message,
                   FidlChannelIO* channel);

  zx_status_t FidlDecode(const fidl_type_t* type, const char** error_msg);

  template <typename T>
  T* GetPtr(size_t offset) {
    return reinterpret_cast<T*>(message_.bytes.data() + offset);
  }

  size_t GetOffset(void* ptr);
  size_t GetOffset(uintptr_t ptr);

  template <class StreamIdExtractor>
  Optional<RouterEndpoint::NewStream> ClaimHandle(
      size_t offset,
      fuchsia::overnet::protocol::ReliabilityAndOrdering
          reliability_and_ordering,
      StreamIdExtractor stream_id_extractor) {
    auto index = *GetPtr<zx_handle_t>(offset);
    if (index == 0) {
      return Nothing;
    }
    return stream_id_extractor(std::move(message_.handles[index - 1]))
        .Then([this, reliability_and_ordering](
                  fuchsia::overnet::protocol::StreamId stream_id)
                  -> Optional<RouterEndpoint::NewStream> {
          auto status =
              stream_->ReceiveFork(stream_id, reliability_and_ordering);
          if (status.is_error()) {
            OVERNET_TRACE(ERROR) << status.AsStatus();
            return Nothing;
          }
          return std::move(*status);
        });
  }

  const fidl_message_header_t& header() const {
    return *reinterpret_cast<const fidl_message_header_t*>(
        message_.bytes.data());
  }
  uint32_t ordinal() const { return header().ordinal; }

 private:
  fuchsia::overnet::protocol::ZirconChannelMessage message_;
  RouterEndpoint::Stream* const stream_;
};

}  // namespace internal
}  // namespace overnet
