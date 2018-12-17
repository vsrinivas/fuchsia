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

// A thin wrapper over Router to provide a stream abstraction, and provide
// for connecting streams to node-wide services.
class RouterEndpoint : public Router {
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

  // A service is published by an endpoint for clients to connect to.
  // The service automatically binds to the endpoint at construction, and
  // unbinds at destruction.
  class Service {
   public:
    Service(RouterEndpoint* endpoint, std::string fully_qualified_name,
            fuchsia::overnet::protocol::ReliabilityAndOrdering
                reliability_and_ordering)
        : fully_qualified_name(std::move(fully_qualified_name)),
          reliability_and_ordering(reliability_and_ordering),
          endpoint_(endpoint) {
      endpoint_->Bind(this);
    }
    virtual ~Service() { endpoint_->Unbind(this); }

    Service(const Service&) = delete;
    Service& operator=(const Service&) = delete;

    // The name clients can request to reach this service.
    const std::string fully_qualified_name;
    // The reliability and ordering constraints expected by this service.
    const fuchsia::overnet::protocol::ReliabilityAndOrdering
        reliability_and_ordering;

    // Acceptor function to create a new stream.
    virtual void AcceptStream(NewStream stream) = 0;

   private:
    RouterEndpoint* const endpoint_;
  };

  StatusOr<ReceivedIntroduction> UnwrapForkFrame(
      NodeId peer, fuchsia::overnet::protocol::ForkFrame fork_frame);

  using SendOp = Stream::SendOp;
  using ReceiveOp = Stream::ReceiveOp;

  explicit RouterEndpoint(Timer* timer, NodeId node_id,
                          bool allow_non_determinism);
  ~RouterEndpoint();
  void Close(Callback<void> done) override final;

  void RegisterPeer(NodeId peer);

  template <class F>
  void ForEachConnectedPeer(F f) {
    for (const auto& peer : connection_streams_) {
      f(peer.first);
    }
  }

  template <class F>
  void ForEachNodeDescription(F f) {
    for (const auto& peer : connection_streams_) {
      OVERNET_TRACE(DEBUG) << node_id() << " query desc on " << peer.first
                           << " = " << peer.second.description_;
      f(peer.first, peer.second.description_);
    }
  }

  StatusOr<NewStream> InitiateStream(
      NodeId peer,
      fuchsia::overnet::protocol::ReliabilityAndOrdering
          reliability_and_ordering,
      const std::string& service_name);

 private:
  void OnUnknownStream(NodeId peer, StreamId stream) override final;
  void Bind(Service* service);
  void Unbind(Service* service);
  void UpdatedDescription();
  fuchsia::overnet::protocol::PeerDescription BuildDescription() const;

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

    StatusOr<NewStream> Fork(fuchsia::overnet::protocol::ReliabilityAndOrdering
                                 reliability_and_ordering,
                             const std::string& introduction);

    fuchsia::overnet::protocol::Peer_Proxy* proxy() { return &proxy_; }

   private:
    void BeginReading();
    void SendFidl(fidl::Message message);

    RouterEndpoint* const endpoint_;
    uint64_t next_stream_id_;

    Optional<ReceiveOp> reader_;
    InternalList<Stream, &Stream::connection_stream_link_> forked_streams_;
    Optional<Status> closing_status_;
    fuchsia::overnet::protocol::PeerDescription description_;

    class Proxy final : public fuchsia::overnet::protocol::Peer_Proxy {
     public:
      Proxy(ConnectionStream* connection_stream)
          : connection_stream_(connection_stream) {}

     private:
      void Send_(fidl::Message message) override {
        connection_stream_->SendFidl(std::move(message));
      }
      ConnectionStream* const connection_stream_;
    };
    class Stub final : public fuchsia::overnet::protocol::Peer_Stub {
     public:
      Stub(ConnectionStream* connection_stream)
          : connection_stream_(connection_stream) {}

      void Fork(fuchsia::overnet::protocol::ForkFrame fork) override;
      void Ping(PingCallback callback) override;
      void UpdateNodeStatus(
          fuchsia::overnet::protocol::NodeStatus node) override;
      void UpdateNodeDescription(
          fuchsia::overnet::protocol::PeerDescription desc) override;
      void UpdateLinkStatus(
          fuchsia::overnet::protocol::LinkStatus link) override;

     private:
      void Send_(fidl::Message message) override {
        connection_stream_->SendFidl(std::move(message));
      }
      ConnectionStream* const connection_stream_;
    };

    Proxy proxy_;
    Stub stub_;
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
  void SendGossipTo(NodeId target);

  std::unordered_map<NodeId, ConnectionStream> connection_streams_;
  Optional<Timeout> gossip_timer_;
  Optional<Timeout> description_timer_;
  TimeDelta gossip_interval_ = InitialGossipInterval();
  bool closing_ = false;
  std::map<std::string, Service*> services_;
};

}  // namespace overnet
