// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <queue>
#include "datagram_stream.h"
#include "fork_frame.h"
#include "manual_constructor.h"
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

    friend std::ostream& operator<<(std::ostream& out, const NewStream& s) {
      return out << "NewStream{node=" << s.peer_ << ",reliability_and_ordering="
                 << ReliabilityAndOrderingString(s.reliability_and_ordering_)
                 << ",stream_id=" << s.stream_id_ << "}";
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

  class Stream final : public DatagramStream {
   public:
    Stream(NewStream introduction);
  };

  using SendOp = Stream::SendOp;
  using ReceiveOp = Stream::ReceiveOp;

  explicit RouterEndpoint(Timer* timer, NodeId node_id);

  void RegisterPeer(NodeId peer);

  Router* router() { return &router_; }
  NodeId node_id() const { return router_.node_id(); }

  void RecvIntro(StatusOrCallback<ReceivedIntroduction> ready);
  StatusOr<NewStream> SendIntro(NodeId peer,
                                ReliabilityAndOrdering reliability_and_ordering,
                                Slice introduction);

 private:
  void MaybeContinueIncomingForks();

  class ConnectionStream final : public DatagramStream {
    friend class RouterEndpoint;

   public:
    ConnectionStream(RouterEndpoint* endpoint, NodeId peer);
    ~ConnectionStream();

    StatusOr<NewStream> Fork(ReliabilityAndOrdering reliability_and_ordering,
                             Slice introduction);

   private:
    void BeginRead();

    enum class ForkReadState {
      Reading,
      Waiting,
      Stopped,
    };

    RouterEndpoint* const endpoint_;
    uint64_t next_stream_id_;

    ForkReadState fork_read_state_;
    ManualConstructor<ReceiveOp> fork_read_;
    InternalListNode<ConnectionStream> forking_ready_;
    ManualConstructor<ForkFrame> fork_frame_;
  };

  Timer* const timer_;
  Router router_;
  std::unordered_map<NodeId, ConnectionStream> connection_streams_;
  InternalList<ConnectionStream, &ConnectionStream::forking_ready_>
      incoming_forks_;
  StatusOrCallback<ReceivedIntroduction> recv_intro_ready_;
};

}  // namespace overnet
