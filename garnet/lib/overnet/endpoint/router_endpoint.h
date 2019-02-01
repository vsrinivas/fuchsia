// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <fuchsia/overnet/protocol/cpp/fidl.h>
#include <queue>
#include "garnet/lib/overnet/datagram_stream/datagram_stream.h"
#include "garnet/lib/overnet/routing/router.h"
#include "garnet/lib/overnet/vocabulary/manual_constructor.h"
#include "garnet/lib/overnet/vocabulary/optional.h"
#include "garnet/lib/overnet/vocabulary/slice.h"
#include "garnet/public/lib/fostr/fidl/fuchsia/overnet/protocol/formatting.h"

namespace overnet {

class RouterEndpoint final : public Router {
 private:
  class ConnectionStream;

 public:
  class NewStream final {
   public:
    NewStream(const NewStream&) = delete;
    NewStream& operator=(const NewStream&) = delete;

    NewStream(NewStream&& other)
        : creator_(other.creator_),
          peer_(other.peer_),
          reliability_and_ordering_(other.reliability_and_ordering_),
          stream_id_(other.stream_id_) {
      other.creator_ = nullptr;
    }
    NewStream& operator=(NewStream&& other) {
      creator_ = other.creator_;
      peer_ = other.peer_;
      reliability_and_ordering_ = other.reliability_and_ordering_;
      stream_id_ = other.stream_id_;
      other.creator_ = nullptr;
      return *this;
    }

    void Fail(const Status& status);
    ~NewStream() { assert(creator_ == nullptr); }

    friend std::ostream& operator<<(std::ostream& out, const NewStream& s) {
      return out << "NewStream{node=" << s.peer_
                 << ",reliability_and_ordering=" << s.reliability_and_ordering_
                 << ",stream_id=" << s.stream_id_ << "}";
    }

   private:
    friend class RouterEndpoint;
    NewStream(RouterEndpoint* creator, NodeId peer,
              fuchsia::overnet::protocol::ReliabilityAndOrdering
                  reliability_and_ordering,
              StreamId stream_id)
        : creator_(creator),
          peer_(peer),
          reliability_and_ordering_(reliability_and_ordering),
          stream_id_(stream_id) {}

    RouterEndpoint* creator_;
    NodeId peer_;
    fuchsia::overnet::protocol::ReliabilityAndOrdering
        reliability_and_ordering_;
    StreamId stream_id_;
  };

  struct ReceivedIntroduction final {
    NewStream new_stream;
    fuchsia::overnet::protocol::Introduction introduction;
  };

  struct OutgoingFork {
    NewStream new_stream;
    fuchsia::overnet::protocol::ForkFrame fork_frame;
  };

  class Stream final : public DatagramStream {
    friend class ConnectionStream;

   public:
    Stream(NewStream introduction);

    StatusOr<OutgoingFork> Fork(
        fuchsia::overnet::protocol::ReliabilityAndOrdering
            reliability_and_ordering,
        fuchsia::overnet::protocol::Introduction introduction);
    using DatagramStream::Close;
    void Close(const Status& status, Callback<void> quiesced) override;

   private:
    ConnectionStream* connection_stream_ = nullptr;
    InternalListNode<Stream> connection_stream_link_;
  };

  StatusOr<ReceivedIntroduction> UnwrapForkFrame(
      NodeId peer, fuchsia::overnet::protocol::ForkFrame fork_frame);

  using SendOp = Stream::SendOp;
  using ReceiveOp = Stream::ReceiveOp;

  explicit RouterEndpoint(Timer* timer, NodeId node_id,
                          bool allow_non_determinism);
  ~RouterEndpoint();
  void Close(Callback<void> done) override;

  void RegisterPeer(NodeId peer);

  template <class F>
  void ForEachConnectedPeer(F f) {
    for (const auto& peer : connection_streams_) {
      f(peer.first);
    }
  }

  void RecvIntro(StatusOrCallback<ReceivedIntroduction> ready);
  void SendIntro(NodeId peer,
                 fuchsia::overnet::protocol::ReliabilityAndOrdering
                     reliability_and_ordering,
                 fuchsia::overnet::protocol::Introduction introduction,
                 StatusOrCallback<NewStream> new_stream);

 private:
  void MaybeContinueIncomingForks();
  void OnUnknownStream(NodeId peer, StreamId stream) override;

  class ConnectionStream final : public DatagramStream {
    friend class RouterEndpoint;
    friend class Stream;

   public:
    ConnectionStream(RouterEndpoint* endpoint, NodeId peer);
    ~ConnectionStream();

    using DatagramStream::Close;
    void Close(const Status& status, Callback<void> quiesced) override;

    void Register() { DatagramStream::Register(); }

    StatusOr<OutgoingFork> MakeFork(
        fuchsia::overnet::protocol::ReliabilityAndOrdering
            reliability_and_ordering,
        fuchsia::overnet::protocol::Introduction introduction);

    void Fork(fuchsia::overnet::protocol::ReliabilityAndOrdering
                  reliability_and_ordering,
              fuchsia::overnet::protocol::Introduction introduction,
              StatusOrCallback<NewStream> new_stream);

    Stream* GossipStream();

   private:
    void BeginForkRead();
    void BeginGossipRead();
    bool IsGossipStreamInitiator() { return endpoint_->node_id() < peer(); }
    void InstantiateGossipStream(NewStream ns);

    enum class ReadState {
      Reading,
      Waiting,
      Stopped,
    };

    RouterEndpoint* const endpoint_;
    uint64_t next_stream_id_;

    ReadState fork_read_state_ = ReadState::Waiting;
    ReadState gossip_read_state_ = ReadState::Waiting;
    ManualConstructor<ReceiveOp> fork_read_;
    ManualConstructor<ReceiveOp> gossip_read_;
    InternalListNode<ConnectionStream> forking_ready_;
    InternalList<Stream, &Stream::connection_stream_link_> forked_streams_;
    ManualConstructor<fuchsia::overnet::protocol::ForkFrame> fork_frame_;
    Optional<Status> closing_status_;
    bool forking_gossip_stream_ = false;
    ClosedPtr<Stream> gossip_stream_;
  };

  StatusOr<OutgoingFork> ForkImpl(
      fuchsia::overnet::protocol::ReliabilityAndOrdering
          reliability_and_ordering,
      fuchsia::overnet::protocol::Introduction introduction);
  ConnectionStream* GetOrCreateConnectionStream(NodeId peer);

  static constexpr TimeDelta InitialGossipInterval() {
    return TimeDelta::FromMilliseconds(42);
  }
  void StartGossipTimer();
  void SendGossipTo(NodeId target, Callback<void> done);

  std::unordered_map<NodeId, ConnectionStream> connection_streams_;
  InternalList<ConnectionStream, &ConnectionStream::forking_ready_>
      incoming_forks_;
  StatusOrCallback<ReceivedIntroduction> recv_intro_ready_;
  Optional<Timeout> gossip_timer_;
  TimeDelta gossip_interval_ = InitialGossipInterval();
  bool closing_ = false;
};

}  // namespace overnet
