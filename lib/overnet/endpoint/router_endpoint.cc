// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "router_endpoint.h"
#include <iostream>
#include <memory>
#include "garnet/lib/overnet/protocol/fidl.h"

namespace overnet {

static const auto kOvernetSystemNamespace =
    std::string("fuchsia.overnet.system.");
static const auto kOvernetGossipService =
    std::string("fuchsia.overnet.system.gossip");

void RouterEndpoint::NewStream::Fail(const Status& status) {
  auto* s = new Stream(std::move(*this));
  s->Close(status, [s] { delete s; });
}

RouterEndpoint::RouterEndpoint(Timer* timer, NodeId node_id,
                               bool allow_non_determinism)
    : Router(timer, node_id, allow_non_determinism) {
  StartGossipTimer();
}

RouterEndpoint::~RouterEndpoint() { assert(connection_streams_.empty()); }

void RouterEndpoint::StartGossipTimer() {
  Timer* timer = this->timer();
  gossip_timer_.Reset(
      timer, timer->Now() + gossip_interval_, [this](const Status& status) {
        if (status.is_error())
          return;
        auto node = SelectGossipPeer();
        if (!node) {
          gossip_interval_ =
              std::min(3 * gossip_interval_ / 2, TimeDelta::FromMinutes(30));
          StartGossipTimer();
        } else {
          gossip_interval_ = InitialGossipInterval();
          SendGossipTo(*node, [this] {
            if (!closing_) {
              StartGossipTimer();
            }
          });
        }
      });
}

void RouterEndpoint::SendGossipTo(NodeId target, Callback<void> done) {
  // Are we still gossiping?
  if (!gossip_timer_.get()) {
    return;
  }
  auto con = connection_streams_.find(target);
  if (con == connection_streams_.end()) {
    return;
  }
  auto* stream = con->second.GossipStream();
  if (stream == nullptr) {
    return;
  }
  auto slice = WriteGossipUpdate(Border::None(), target);
  OVERNET_TRACE(DEBUG) << "SEND_GOSSIP_TO:" << target << " " << slice;
  Stream::SendOp(stream, slice.length()).Push(slice, std::move(done));
}

void RouterEndpoint::Close(Callback<void> done) {
  closing_ = true;
  gossip_timer_.Reset();
  if (connection_streams_.empty()) {
    Router::Close(std::move(done));
    return;
  }
  auto it = connection_streams_.begin();
  OVERNET_TRACE(INFO) << "Closing peer " << it->first;
  Callback<void> after_close(
      ALLOCATED_CALLBACK, [this, it, done = std::move(done)]() mutable {
        OVERNET_TRACE(INFO) << "Closed peer " << it->first;
        connection_streams_.erase(it);
        Close(std::move(done));
      });
  it->second.Close(Status::Cancelled(), std::move(after_close));
}

void RouterEndpoint::RegisterPeer(NodeId peer) {
  GetOrCreateConnectionStream(peer);
}

RouterEndpoint::ConnectionStream* RouterEndpoint::GetOrCreateConnectionStream(
    NodeId peer) {
  assert(peer != node_id());
  auto it = connection_streams_.find(peer);
  if (it != connection_streams_.end()) {
    return &it->second;
  }
  OVERNET_TRACE(DEBUG) << "Creating connection stream for peer " << peer;
  auto* stream =
      &connection_streams_
           .emplace(std::piecewise_construct, std::forward_as_tuple(peer),
                    std::forward_as_tuple(this, peer))
           .first->second;
  stream->Register();
  return stream;
}

RouterEndpoint::Stream::Stream(NewStream introduction)
    : DatagramStream(introduction.creator_, introduction.peer_,
                     introduction.reliability_and_ordering_,
                     introduction.stream_id_) {
  auto it = introduction.creator_->connection_streams_.find(introduction.peer_);
  if (it == introduction.creator_->connection_streams_.end()) {
    OVERNET_TRACE(DEBUG) << "Failed to find connection " << introduction.peer_;
    Close(Status(StatusCode::FAILED_PRECONDITION,
                 "Connection closed before stream creation"),
          Callback<void>::Ignored());
  } else {
    connection_stream_ = &it->second;
    connection_stream_->forked_streams_.PushBack(this);
  }
  introduction.creator_ = nullptr;
  Register();
}

void RouterEndpoint::Stream::Close(const Status& status,
                                   Callback<void> quiesced) {
  if (connection_stream_ != nullptr) {
    connection_stream_->forked_streams_.Remove(this);
    connection_stream_ = nullptr;
  }
  DatagramStream::Close(status, std::move(quiesced));
}

RouterEndpoint::ConnectionStream::ConnectionStream(RouterEndpoint* endpoint,
                                                   NodeId peer)
    : DatagramStream(
          endpoint, peer,
          fuchsia::overnet::protocol::ReliabilityAndOrdering::ReliableUnordered,
          StreamId(0)),
      endpoint_(endpoint),
      next_stream_id_(peer < endpoint->node_id() ? 2 : 1) {
  BeginForkRead();
}

RouterEndpoint::ConnectionStream::~ConnectionStream() {
  if (fork_read_state_ == ReadState::Reading) {
    fork_read_->Close(Status::Cancelled());
  }
  assert(fork_read_state_ == ReadState::Stopped);
  fork_read_.Destroy();
  if (gossip_read_state_ == ReadState::Reading) {
    gossip_read_->Close(Status::Cancelled());
  }
  if (gossip_read_state_ != ReadState::Waiting) {
    assert(gossip_read_state_ == ReadState::Stopped);
    gossip_read_.Destroy();
  }
}

void RouterEndpoint::ConnectionStream::BeginGossipRead() {
  ScopedModule<DatagramStream> dgstream(gossip_stream_.get());
  OVERNET_TRACE(DEBUG) << "BEGIN_GOSSIP_READ";
  gossip_read_state_ = ReadState::Reading;
  gossip_read_.Init(gossip_stream_.get());
  gossip_read_->PullAll(StatusOrCallback<Optional<std::vector<Slice>>>(
      [this](StatusOr<Optional<std::vector<Slice>>>&& read_status) {
        ScopedModule<DatagramStream> dgstream(gossip_stream_.get());
        OVERNET_TRACE(DEBUG) << "GOSSIP_READ:" << read_status;
        assert(gossip_read_state_ == ReadState::Reading);
        if (read_status.is_error()) {
          gossip_read_state_ = ReadState::Stopped;
          Close(read_status.AsStatus(), Callback<void>::Ignored());
          return;
        } else if (!read_status->has_value()) {
          gossip_read_state_ = ReadState::Stopped;
          Close(Status::Ok(), Callback<void>::Ignored());
          return;
        }
        auto apply_status = endpoint_->ApplyGossipUpdate(
            Slice::Join((*read_status)->begin(), (*read_status)->end()),
            peer());
        if (apply_status.is_error()) {
          gossip_read_state_ = ReadState::Stopped;
          Close(apply_status, Callback<void>::Ignored());
          return;
        }
        gossip_read_.Destroy();
        gossip_read_state_ = ReadState::Waiting;
        BeginGossipRead();
      }));
}

void RouterEndpoint::ConnectionStream::BeginForkRead() {
  fork_read_state_ = ReadState::Reading;
  fork_read_.Init(this);
  fork_read_->PullAll(StatusOrCallback<Optional<std::vector<Slice>>>(
      [this](StatusOr<Optional<std::vector<Slice>>>&& read_status) {
        assert(fork_read_state_ == ReadState::Reading);
        if (read_status.is_error()) {
          fork_read_state_ = ReadState::Stopped;
          Close(read_status.AsStatus(), Callback<void>::Ignored());
          return;
        } else if (!read_status->has_value()) {
          fork_read_state_ = ReadState::Stopped;
          Close(Status::Ok(), Callback<void>::Ignored());
          return;
        }

        auto merged =
            Slice::Join((*read_status)->begin(), (*read_status)->end());
        auto fork_frame_status = Decode<fuchsia::overnet::protocol::ForkFrame>(
            const_cast<uint8_t*>(merged.begin()), merged.length());
        if (fork_frame_status.is_error()) {
          fork_read_state_ = ReadState::Stopped;
          Close(fork_frame_status.AsStatus(), Callback<void>::Ignored());
          return;
        }
        if (fork_frame_status->introduction.has_service_name() &&
            fork_frame_status->introduction.service_name()->find(
                kOvernetSystemNamespace) == 0) {
          const auto& svc =
              fork_frame_status->introduction.service_name();
          enum class SystemService {
            NO_IDEA,
            GOSSIP,
          };
          auto svc_type = SystemService::NO_IDEA;
          if (*svc == kOvernetGossipService) {
            svc_type = SystemService::GOSSIP;
          }
          // fork_frame_status, and therefore svc is no longer valid after this
          // line
          auto received_intro =
              endpoint_->UnwrapForkFrame(peer(), std::move(*fork_frame_status));
          if (received_intro.is_error()) {
            fork_read_state_ = ReadState::Stopped;
            Close(received_intro.AsStatus(), Callback<void>::Ignored());
            return;
          }
          switch (svc_type) {
            case SystemService::NO_IDEA:
              received_intro->new_stream.Fail(
                  Status(StatusCode::FAILED_PRECONDITION, "Unknown service"));
              break;
            case SystemService::GOSSIP:
              if (IsGossipStreamInitiator()) {
                received_intro->new_stream.Fail(
                    Status(StatusCode::FAILED_PRECONDITION,
                           "Not gossip stream initiator"));
              } else if (gossip_stream_) {
                received_intro->new_stream.Fail(
                    Status(StatusCode::FAILED_PRECONDITION,
                           "Gossip channel already exists"));
              } else {
                InstantiateGossipStream(std::move(received_intro->new_stream));
              }
              break;
          }
          fork_read_.Destroy();
          fork_read_state_ = ReadState::Waiting;
          BeginForkRead();
        } else {
          fork_frame_.Init(std::move(*fork_frame_status));
          endpoint_->incoming_forks_.PushBack(this);
          fork_read_.Destroy();
          fork_read_state_ = ReadState::Waiting;
          if (this == endpoint_->incoming_forks_.Front()) {
            endpoint_->MaybeContinueIncomingForks();
          }
        }
      }));
}

void RouterEndpoint::SendIntro(
    NodeId peer,
    fuchsia::overnet::protocol::ReliabilityAndOrdering reliability_and_ordering,
    fuchsia::overnet::protocol::Introduction introduction,
    StatusOrCallback<NewStream> new_stream_ready) {
  GetOrCreateConnectionStream(peer)->Fork(reliability_and_ordering,
                                          std::move(introduction),
                                          std::move(new_stream_ready));
}

StatusOr<RouterEndpoint::OutgoingFork> RouterEndpoint::Stream::Fork(
    fuchsia::overnet::protocol::ReliabilityAndOrdering reliability_and_ordering,
    fuchsia::overnet::protocol::Introduction introduction) {
  if (connection_stream_ == nullptr) {
    return StatusOr<OutgoingFork>(StatusCode::FAILED_PRECONDITION,
                                  "Closed stream");
  }
  return connection_stream_->MakeFork(reliability_and_ordering,
                                      std::move(introduction));
}

void RouterEndpoint::ConnectionStream::Close(const Status& status,
                                             Callback<void> quiesced) {
  if (status.is_error()) {
    OVERNET_TRACE(ERROR) << "Connection to " << peer()
                         << " closed with error: " << status;
  }
  if (gossip_read_state_ == ReadState::Reading) {
    gossip_read_->Close(status);
  }
  if (gossip_stream_) {
    gossip_stream_->Close(status, Callback<void>::Ignored());
  }
  gossip_stream_.reset();
  if (!closing_status_) {
    closing_status_.Reset(status);
  }
  if (forked_streams_.Empty()) {
    DatagramStream::Close(status, std::move(quiesced));
  } else {
    forked_streams_.Front()->Close(
        status,
        Callback<void>(ALLOCATED_CALLBACK,
                       [this, status, quiesced{std::move(quiesced)}]() mutable {
                         this->Close(status, std::move(quiesced));
                       }));
  }
  assert(quiesced.empty());
}

RouterEndpoint::Stream* RouterEndpoint::ConnectionStream::GossipStream() {
  if (gossip_stream_ == nullptr && !closing_status_.has_value() &&
      IsGossipStreamInitiator() && !forking_gossip_stream_) {
    OVERNET_TRACE(DEBUG) << "Initiate gossip stream: ep="
                         << endpoint_->node_id() << " peer=" << peer();
    forking_gossip_stream_ = true;
    fuchsia::overnet::protocol::Introduction introduction;
    introduction.set_service_name(kOvernetGossipService);
    Fork(fuchsia::overnet::protocol::ReliabilityAndOrdering::
             ReliableOrdered /* TODO(ctiller): should be UnreliableUnordered */,
         std::move(introduction), [this](StatusOr<NewStream> new_stream) {
           OVERNET_TRACE(DEBUG)
               << "Forked gossip stream: ep=" << endpoint_->node_id()
               << " peer=" << peer();
           assert(forking_gossip_stream_);
           forking_gossip_stream_ = false;
           if (new_stream.is_error()) {
             Close(new_stream.AsStatus().WithContext("Opening gossip stream"),
                   Callback<void>::Ignored());
           } else {
             InstantiateGossipStream(std::move(*new_stream));
           }
         });
  }
  return gossip_stream_.get();
}

void RouterEndpoint::ConnectionStream::InstantiateGossipStream(NewStream ns) {
  OVERNET_TRACE(DEBUG) << "Instantiate gossip stream: ep="
                       << endpoint_->node_id() << " peer=" << peer();
  assert(gossip_stream_ == nullptr);
  gossip_stream_.reset(new Stream(std::move(ns)));
  BeginGossipRead();
}

StatusOr<RouterEndpoint::OutgoingFork>
RouterEndpoint::ConnectionStream::MakeFork(
    fuchsia::overnet::protocol::ReliabilityAndOrdering reliability_and_ordering,
    fuchsia::overnet::protocol::Introduction introduction) {
  if (closing_status_) {
    return *closing_status_;
  }

  StreamId id(next_stream_id_);
  next_stream_id_ += 2;

  return OutgoingFork{
      NewStream{endpoint_, peer(), reliability_and_ordering, id},
      fuchsia::overnet::protocol::ForkFrame{id.get(), reliability_and_ordering,
                                            std::move(introduction)}};
}

void RouterEndpoint::ConnectionStream::Fork(
    fuchsia::overnet::protocol::ReliabilityAndOrdering reliability_and_ordering,
    fuchsia::overnet::protocol::Introduction introduction,
    StatusOrCallback<NewStream> new_stream_ready) {
  auto outgoing_fork =
      MakeFork(reliability_and_ordering, std::move(introduction));
  if (outgoing_fork.is_error()) {
    new_stream_ready(outgoing_fork.AsStatus());
    return;
  }
  std::vector<uint8_t> bytes;
  auto encoded = Encode(&outgoing_fork->fork_frame);
  if (encoded.is_error()) {
    new_stream_ready(encoded.AsStatus());
    return;
  }
  Slice payload = std::move(*encoded);

  SendOp send_op(this, payload.length());
  send_op.Push(payload, Callback<void>::Ignored());
  send_op.Push(Slice(),
               Callback<void>(
                   ALLOCATED_CALLBACK,
                   [new_stream = std::move(outgoing_fork->new_stream),
                    new_stream_ready = std::move(new_stream_ready)]() mutable {
                     new_stream_ready(std::move(new_stream));
                   }));
}

void RouterEndpoint::RecvIntro(StatusOrCallback<ReceivedIntroduction> ready) {
  recv_intro_ready_ = std::move(ready);
  MaybeContinueIncomingForks();
}

void RouterEndpoint::MaybeContinueIncomingForks() {
  if (recv_intro_ready_.empty() || incoming_forks_.Empty())
    return;
  auto* incoming_fork = incoming_forks_.Front();
  incoming_forks_.Remove(incoming_fork);
  assert(incoming_fork->fork_read_state_ ==
         ConnectionStream::ReadState::Waiting);
  recv_intro_ready_(UnwrapForkFrame(
      incoming_fork->peer(), std::move(*incoming_fork->fork_frame_.get())));
  incoming_fork->fork_frame_.Destroy();
  incoming_fork->BeginForkRead();
}

StatusOr<RouterEndpoint::ReceivedIntroduction> RouterEndpoint::UnwrapForkFrame(
    NodeId peer, fuchsia::overnet::protocol::ForkFrame fork_frame) {
  return ReceivedIntroduction{
      NewStream{this, peer, fork_frame.reliability_and_ordering,
                StreamId(fork_frame.stream_id)},
      std::move(fork_frame.introduction)};
}

void RouterEndpoint::OnUnknownStream(NodeId node_id, StreamId stream_id) {
  if (stream_id == StreamId(0)) {
    GetOrCreateConnectionStream(node_id);
  }
}

}  // namespace overnet
