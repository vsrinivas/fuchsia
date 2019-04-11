// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <fuchsia/overnet/protocol/cpp/overnet_internal.h>
#include <lib/fidl/cpp/coding_traits.h>
#include "src/connectivity/overnet/lib/embedded/decoder.h"
#include "src/connectivity/overnet/lib/embedded/encoder.h"

namespace overnet {

class ZxChannel final : public fuchsia::overnet::protocol::ZirconChannel {
 public:
  ~ZxChannel();

  void Close(Callback<void> quiesced);

  void Message(fuchsia::overnet::protocol::ZirconChannelMessage message);

  void SetReader(fuchsia::overnet::protocol::ZirconChannel* reader);

  void Encode(internal::Encoder* encoder, size_t offset);
  static ClosedPtr<ZxChannel> Decode(internal::Decoder* decoder, size_t offset);

  static std::pair<ClosedPtr<ZxChannel>, ClosedPtr<ZxChannel>> MakePair();

  RouterEndpoint::Stream* overnet_stream();
  void Bind(RouterEndpoint::NewStream new_stream);

 private:
  // Unbound constructor
  ZxChannel();
  // Bound constructor
  ZxChannel(RouterEndpoint::NewStream new_stream);

  void Ref() { ++refs_; }
  void Unref() {
    if (0 == --refs_ && !quiesced_.empty()) {
      quiesced_();
    }
  }

  struct Unbound {
    ZxChannel* peer = nullptr;
    std::vector<fuchsia::overnet::protocol::ZirconChannelMessage> queued;
  };

  class Proxy final : public fuchsia::overnet::protocol::ZirconChannel_Proxy {
   public:
    Proxy(RouterEndpoint::Stream* stream) : stream_(stream) {}

   private:
    void Send_(fidl::Message message) override;

    RouterEndpoint::Stream* const stream_;
  };

  class Stub final : public fuchsia::overnet::protocol::ZirconChannel_Stub {
   public:
    Stub(RouterEndpoint::Stream* stream, ZxChannel* channel)
        : stream_(stream), channel_(channel) {}
    ~Stub() { OVERNET_TRACE(DEBUG) << this << " :::: " << __PRETTY_FUNCTION__; }

    void Start();

    void Message(
        fuchsia::overnet::protocol::ZirconChannelMessage message) override;

   private:
    void Send_(fidl::Message message) override;

    RouterEndpoint::Stream* const stream_;
    ZxChannel* const channel_;
    Optional<RouterEndpoint::Stream::ReceiveOp> recv_op_;
  };

  struct Bound {
    Bound(RouterEndpoint::NewStream ns, ZxChannel* channel)
        : stream(MakeClosedPtr<RouterEndpoint::Stream>(std::move(ns))),
          proxy(stream.get()),
          stub(stream.get(), channel) {}
    ClosedPtr<RouterEndpoint::Stream> stream;
    Proxy proxy;
    Stub stub;
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
  int refs_ = 0;
  bool closed_ = false;
  Callback<void> quiesced_;
  fuchsia::overnet::protocol::ZirconChannel* reader_ = nullptr;
};

}  // namespace overnet

namespace fidl {

template <>
struct CodingTraits<overnet::ClosedPtr<overnet::ZxChannel>> {
 public:
  static void Encode(overnet::internal::Encoder* encoder,
                     overnet::ClosedPtr<overnet::ZxChannel>* channel,
                     size_t offset) {
    (*channel)->Encode(encoder, offset);
    channel->reset();
  }
  static void Decode(overnet::internal::Decoder* decoder,
                     overnet::ClosedPtr<overnet::ZxChannel>* channel,
                     size_t offset) {
    *channel = overnet::ZxChannel::Decode(decoder, offset);
  }
};

}  // namespace fidl
