// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <queue>
#include "receive_mode.h"
#include "reliability_and_ordering.h"
#include "router.h"
#include "sink.h"
#include "slice.h"

namespace overnet {

class RouterEndpoint final {
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

   private:
    friend class RouterEndpoint;
    NewStream(RouterEndpoint* creator, NodeId peer,
              ReliabilityAndOrdering reliability_and_ordering,
              StreamId stream_id)
        : creator_(creator),
          peer_(peer),
          reliability_and_ordering_(reliability_and_ordering),
          stream_id_(stream_id) {}

    RouterEndpoint* creator_;
    NodeId peer_;
    ReliabilityAndOrdering reliability_and_ordering_;
    StreamId stream_id_;
  };

  struct ReceivedIntroduction final {
    NewStream new_stream;
    Slice introduction;
  };

  class Stream final : private Router::StreamHandler {
   public:
    Stream(NewStream introduction);

    Stream(const Stream&) = delete;
    Stream& operator=(const Stream&) = delete;
    Stream(Stream&&) = delete;
    Stream& operator=(Stream&&) = delete;

    void Send(size_t payload_length,
              StatusOrCallback<Sink<Slice>*> ready_for_data);
    void Recv(StatusOrCallback<Source<Slice>*> ready_to_read);

   private:
    void HandleMessage(SeqNum seq, uint64_t payload_length, bool is_control,
                       ReliabilityAndOrdering reliability_and_ordering,
                       StatusOrCallback<Sink<Chunk>*> ready_for_data) override;

    Router* const router_;
    const ReliabilityAndOrdering reliability_and_ordering_;
    const NodeId peer_;
    const StreamId stream_id_;
    uint64_t next_seq_ = 1;
    receive_mode::ParameterizedReceiveMode recv_mode_;
    // TODO(ctiller): do we need a back-pressure strategy here?
    std::queue<StatusOrCallback<Source<Slice>*>> pending_recvs_;
    std::queue<Source<Slice>*> incoming_messages_;
  };

  explicit RouterEndpoint(NodeId node_id);

  void RegisterPeer(NodeId peer);

  Router* router() { return &router_; }
  NodeId node_id() const { return router_.node_id(); }

  void RecvIntro(StatusOrCallback<ReceivedIntroduction> ready);
  void SendIntro(NodeId peer, ReliabilityAndOrdering reliability_and_ordering,
                 Slice introduction, StatusOrCallback<NewStream> ready);

 private:
  void MaybeContinueIncomingForks();

  class ConnectionStream final : private Router::StreamHandler {
   public:
    ConnectionStream(RouterEndpoint* endpoint, NodeId peer);

    ConnectionStream(const ConnectionStream&) = delete;
    ConnectionStream& operator=(const ConnectionStream&) = delete;
    ConnectionStream(ConnectionStream&&) = delete;
    ConnectionStream& operator=(ConnectionStream&&) = delete;

    void Fork(ReliabilityAndOrdering reliability_and_ordering,
              Slice introduction, StatusOrCallback<NewStream> ready);

   private:
    void HandleMessage(SeqNum seq, uint64_t payload_length, bool is_control,
                       ReliabilityAndOrdering reliability_and_ordering,
                       StatusOrCallback<Sink<Chunk>*> ready_for_data) override;

    Router* const router_;
    RouterEndpoint* const endpoint_;
    const NodeId peer_;
    uint64_t next_stream_id_;
    uint64_t next_seq_ = 1;
    receive_mode::ReliableOrdered recv_mode_;
  };

  Router router_;
  std::unordered_map<NodeId, ConnectionStream> connection_streams_;

  struct IncomingFork {
    StatusOrCallback<Sink<Chunk>*> ready_for_data;
    uint64_t payload_length;
    NodeId peer;
  };
  std::queue<IncomingFork> incoming_forks_;
  StatusOrCallback<ReceivedIntroduction> recv_intro_ready_;
};

}  // namespace overnet
