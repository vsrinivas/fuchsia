// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "router_endpoint.h"
#include <iostream>
#include <memory>
#include "fork_frame.h"
#include "linearizer.h"

namespace overnet {

namespace {
template <class T>
void ReadAllAndParse(Source<Slice>* src, StatusOrCallback<T> ready) {
  src->PullAll(StatusOrCallback<std::vector<Slice>>(
      ALLOCATED_CALLBACK,
      [ready{std::move(ready)}](StatusOr<std::vector<Slice>>&& status) mutable {
        if (status.is_error()) {
          ready(status.AsStatus());
          return;
        }
        ready(T::Parse(Slice::Join(status->begin(), status->end())));
      }));
}
}  // namespace

RouterEndpoint::RouterEndpoint(Timer* timer, NodeId node_id,
                               bool allow_threading)
    : router_(timer, node_id, allow_threading) {}

void RouterEndpoint::RegisterPeer(NodeId peer) {
  assert(peer != router_.node_id());
  if (connection_streams_.count(peer) != 0) return;
  connection_streams_.emplace(std::piecewise_construct,
                              std::forward_as_tuple(peer),
                              std::forward_as_tuple(this, peer));
}

RouterEndpoint::Stream::Stream(NewStream introduction)
    : DatagramStream(&introduction.creator_->router_, introduction.peer_,
                     introduction.reliability_and_ordering_,
                     introduction.stream_id_) {}

RouterEndpoint::ConnectionStream::ConnectionStream(RouterEndpoint* endpoint,
                                                   NodeId peer)
    : DatagramStream(&endpoint->router_, peer,
                     ReliabilityAndOrdering::ReliableUnordered, StreamId(0)),
      endpoint_(endpoint),
      next_stream_id_(peer < endpoint->node_id() ? 2 : 1) {
  BeginRead();
}

RouterEndpoint::ConnectionStream::~ConnectionStream() {
  if (fork_read_state_ == ForkReadState::Reading) {
    fork_read_->Close(Status::Cancelled());
  }
  assert(fork_read_state_ == ForkReadState::Stopped);
}

void RouterEndpoint::ConnectionStream::BeginRead() {
  fork_read_state_ = ForkReadState::Reading;
  fork_read_.Init(this);
  fork_read_->PullAll(StatusOrCallback<std::vector<Slice>>(
      [this](StatusOr<std::vector<Slice>>&& read_status) {
        assert(fork_read_state_ == ForkReadState::Reading);
        if (read_status.is_error()) {
          fork_read_state_ = ForkReadState::Stopped;
          Close(read_status.AsStatus());
          return;
        }
        auto fork_frame_status = ForkFrame::Parse(
            Slice::Join(read_status->begin(), read_status->end()));
        if (fork_frame_status.is_error()) {
          fork_read_state_ = ForkReadState::Stopped;
          Close(fork_frame_status.AsStatus());
          return;
        }
        fork_frame_.Init(std::move(*fork_frame_status));
        endpoint_->incoming_forks_.PushBack(this);
        fork_read_.Destroy();
        fork_read_state_ = ForkReadState::Waiting;
        if (this == endpoint_->incoming_forks_.Front()) {
          endpoint_->MaybeContinueIncomingForks();
        }
      }));
}

StatusOr<RouterEndpoint::NewStream> RouterEndpoint::SendIntro(
    NodeId peer, ReliabilityAndOrdering reliability_and_ordering,
    Slice introduction) {
  auto it = connection_streams_.find(peer);
  if (it == connection_streams_.end()) {
    return StatusOr<NewStream>(StatusCode::FAILED_PRECONDITION,
                               "Remote peer not registered with this endpoint");
  }
  return it->second.Fork(reliability_and_ordering, std::move(introduction));
}

StatusOr<RouterEndpoint::NewStream> RouterEndpoint::ConnectionStream::Fork(
    ReliabilityAndOrdering reliability_and_ordering, Slice introduction) {
  StreamId id(next_stream_id_);
  next_stream_id_ += 2;
  Slice payload =
      ForkFrame(id, reliability_and_ordering, std::move(introduction)).Write();

  // TODO(ctiller): Don't allocate.
  auto* send_op = new SendOp(this, payload.length());
  send_op->Push(payload, StatusCallback([send_op](const Status& status) {
                  send_op->Close(status, [send_op]() { delete send_op; });
                }));
  return NewStream{endpoint_, peer(), reliability_and_ordering, id};
}

void RouterEndpoint::RecvIntro(StatusOrCallback<ReceivedIntroduction> ready) {
  recv_intro_ready_ = std::move(ready);
  MaybeContinueIncomingForks();
}

void RouterEndpoint::MaybeContinueIncomingForks() {
  if (recv_intro_ready_.empty() || incoming_forks_.Empty()) return;
  auto* incoming_fork = incoming_forks_.Front();
  incoming_forks_.Remove(incoming_fork);
  assert(incoming_fork->fork_read_state_ ==
         ConnectionStream::ForkReadState::Waiting);
  recv_intro_ready_(ReceivedIntroduction{
      NewStream{this, incoming_fork->peer(),
                incoming_fork->fork_frame_->reliability_and_ordering(),
                incoming_fork->fork_frame_->stream_id()},
      incoming_fork->fork_frame_->introduction()});
  incoming_fork->fork_frame_.Destroy();
  incoming_fork->BeginRead();
}

}  // namespace overnet
