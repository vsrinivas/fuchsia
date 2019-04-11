// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <lib/fidl/cpp/coding_traits.h>
#include "src/connectivity/overnet/lib/embedded/decoder.h"
#include "src/connectivity/overnet/lib/embedded/encoder.h"

namespace overnet {

class ZxSocket final : public fuchsia::overnet::protocol::ZirconSocket {
 public:
  ~ZxSocket();

  void Close(Callback<void> quiesced) {}

  void Message(std::vector<uint8_t> bytes);
  void Control(std::vector<uint8_t> bytes);
  void Share(fuchsia::overnet::protocol::SocketHandle handle);

  void Encode(internal::Encoder* encoder, size_t offset);
  static ClosedPtr<ZxSocket> Decode(internal::Decoder* decoder, size_t offset);

  static std::pair<ClosedPtr<ZxSocket>, ClosedPtr<ZxSocket>> MakePair(
      uint32_t options);

  RouterEndpoint::Stream* overnet_stream();

 private:
  // Unbound constructor
  ZxSocket(uint32_t options);
  // Bound constructor
  ZxSocket(uint32_t options, RouterEndpoint::NewStream new_stream);

  enum class QueueItem { kMessage, kControl, kShare };
  struct QueueSlot {
    QueueItem item;
    std::vector<uint8_t> bytes;
    fuchsia::overnet::protocol::SocketHandle handle;
  };

  struct Unbound {
    ZxSocket* peer = nullptr;
    std::vector<QueueSlot> queued;
  };

  class Proxy final : public fuchsia::overnet::protocol::ZirconSocket_Proxy {
   public:
    Proxy(RouterEndpoint::Stream* stream) : stream_(stream) {}

   private:
    void Send_(fidl::Message message) override;

    RouterEndpoint::Stream* const stream_;
  };

  struct Bound {
    Bound(RouterEndpoint::NewStream ns)
        : stream(MakeClosedPtr<RouterEndpoint::Stream>(std::move(ns))) {}
    ClosedPtr<RouterEndpoint::Stream> stream;
    Proxy proxy{stream.get()};
  };

  enum class StateTag {
    kUnbound,
    kBound,
    kDisconnected,
  };

  union State {
    State() {}
    ~State() {}
    Unbound unbound;
    Bound bound;
  };

  StateTag state_tag_;
  State state_;
  const uint32_t options_;
};

}  // namespace overnet

namespace fidl {

template <>
struct CodingTraits<overnet::ClosedPtr<overnet::ZxSocket>> {
 public:
  static void Encode(overnet::internal::Encoder* encoder,
                     overnet::ClosedPtr<overnet::ZxSocket>* socket,
                     size_t offset) {
    (*socket)->Encode(encoder, offset);
    socket->reset();
  }
  static void Decode(overnet::internal::Decoder* decoder,
                     overnet::ClosedPtr<overnet::ZxSocket>* socket,
                     size_t offset) {
    *socket = overnet::ZxSocket::Decode(decoder, offset);
  }
};

}  // namespace fidl
