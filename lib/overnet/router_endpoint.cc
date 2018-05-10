// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "router_endpoint.h"
#include <iostream>
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
        ready(
            T::Parse(Slice::Join(status.get()->begin(), status.get()->end())));
      }));
}
}  // namespace

RouterEndpoint::RouterEndpoint(NodeId node_id) : router_(node_id) {}

void RouterEndpoint::RegisterPeer(NodeId peer) {
  assert(peer != router_.node_id());
  connection_streams_.emplace(std::piecewise_construct,
                              std::forward_as_tuple(peer),
                              std::forward_as_tuple(this, peer));
}

RouterEndpoint::Stream::Stream(NewStream introduction)
    : router_(&introduction.creator_->router_),
      reliability_and_ordering_(introduction.reliability_and_ordering_),
      peer_(introduction.peer_),
      stream_id_(introduction.stream_id_),
      recv_mode_(introduction.reliability_and_ordering_) {
  if (router_->RegisterStream(peer_, stream_id_, this).is_error()) {
    abort();
  }
}

void RouterEndpoint::Stream::Send(
    size_t payload_length, StatusOrCallback<Sink<Slice>*> ready_for_data) {
  router_->Forward(Message{
      // TODO(ctiller): Hard coded 10 here until we track sequence numbers.
      std::move(
          RoutingHeader(router_->node_id(), payload_length,
                        reliability_and_ordering_)
              .AddDestination(peer_, stream_id_, SeqNum(next_seq_++, 10))),
      // TODO(ctiller): Don't allocate.
      StatusOrCallback<Sink<Chunk>*>(
          ALLOCATED_CALLBACK, [cb = std::move(ready_for_data)](
                                  StatusOr<Sink<Chunk>*>&& status) mutable {
            if (status.is_error()) {
              cb(status.AsStatus());
              return;
            }

            // TODO(ctiller): Move somewhere better, don't dynamically allocate.
            class ChunkToSliceSink final : public Sink<Slice> {
             public:
              ChunkToSliceSink(Sink<Chunk>* chunk_sink)
                  : chunk_sink_(chunk_sink) {}

              void Push(Slice item, StatusCallback done) override {
                size_t chunk_offset = offset_;
                offset_ += item.length();
                chunk_sink_->Push(Chunk{chunk_offset, std::move(item)},
                                  std::move(done));
              }

              void PushMany(Slice* items, size_t count,
                            StatusCallback done) override {
                std::vector<Chunk> chunks;
                chunks.reserve(count);
                for (size_t i = 0; i < count; i++) {
                  size_t chunk_offset = offset_;
                  offset_ += items[i].length();
                  chunks.emplace_back(Chunk{chunk_offset, std::move(items[i])});
                }
                chunk_sink_->PushMany(chunks.data(), chunks.size(),
                                      std::move(done));
              }

              void Close(const Status& status) override {
                chunk_sink_->Close(status);
                delete this;
              }

             private:
              uint64_t offset_ = 0;
              Sink<Chunk>* const chunk_sink_;
            };

            cb(new ChunkToSliceSink(*status.get()));
          })});
}

void RouterEndpoint::Stream::HandleMessage(
    SeqNum seq, uint64_t payload_length, bool is_control,
    ReliabilityAndOrdering reliability_and_ordering,
    StatusOrCallback<Sink<Chunk>*> ready_for_data) {
  if (is_control) {
    // TODO(ctiller): Don't dynamically allocate.
    // TODO(ctiller): 1024 hardcoded until we track window sizes.
    auto* linearizer = ReffedLinearizer::Make(1024);
    ReadAllAndParse(linearizer, StatusOrCallback<AckFrame>::Unimplemented());
    ready_for_data(linearizer);
    return;
  }

  if (reliability_and_ordering != reliability_and_ordering_) {
    ready_for_data(StatusOr<Sink<Chunk>*>(
        StatusCode::INVALID_ARGUMENT,
        "Reliability and ordering of stream changed after creation"));
    return;
  }

  // TODO(ctiller): Don't dynamically allocate.
  const uint64_t seq_idx = seq.Reconstruct(recv_mode_.WindowBase());
  /*const bool trigger_ack =*/recv_mode_.Begin(
      seq_idx, StatusCallback(ALLOCATED_CALLBACK,
                              [this, ready = std::move(ready_for_data)](
                                  Status&& status) mutable {
                                if (status.is_error()) {
                                  ready(std::forward<Status>(status));
                                  return;
                                }
                                // TODO(ctiller): Don't dynamically allocate.
                                // TODO(ctiller): 1024 hardcoded until we track
                                // window sizes.
                                auto* linearizer = ReffedLinearizer::Make(1024);
                                if (!pending_recvs_.empty()) {
                                  auto cb = std::move(pending_recvs_.front());
                                  pending_recvs_.pop();
                                  cb(linearizer);
                                } else {
                                  incoming_messages_.push(linearizer);
                                }
                                ready(linearizer);
                              }));
}

void RouterEndpoint::Stream::Recv(
    StatusOrCallback<Source<Slice>*> ready_to_read) {
  if (!incoming_messages_.empty()) {
    auto src = incoming_messages_.front();
    incoming_messages_.pop();
    ready_to_read(src);
  } else {
    pending_recvs_.emplace(std::move(ready_to_read));
  }
}

RouterEndpoint::ConnectionStream::ConnectionStream(RouterEndpoint* endpoint,
                                                   NodeId peer)
    : router_(&endpoint->router_),
      endpoint_(endpoint),
      peer_(peer),
      next_stream_id_(endpoint->node_id() < peer ? 1 : 2) {
  if (endpoint->router_.RegisterStream(peer, StreamId(0), this).is_error()) {
    abort();
  }
}

void RouterEndpoint::SendIntro(NodeId peer,
                               ReliabilityAndOrdering reliability_and_ordering,
                               Slice introduction,
                               StatusOrCallback<NewStream> ready) {
  auto it = connection_streams_.find(peer);
  if (it == connection_streams_.end()) {
    ready(StatusOr<NewStream>(StatusCode::FAILED_PRECONDITION,
                              "Remote peer not registered with this endpoint"));
    return;
  }
  it->second.Fork(reliability_and_ordering, std::move(introduction),
                  std::move(ready));
}

void RouterEndpoint::ConnectionStream::Fork(
    ReliabilityAndOrdering reliability_and_ordering, Slice introduction,
    StatusOrCallback<NewStream> ready) {
  StreamId id(next_stream_id_);
  next_stream_id_ += 2;
  Slice payload =
      ForkFrame(id, reliability_and_ordering, std::move(introduction)).Write();
  NewStream new_stream(endpoint_, peer_, reliability_and_ordering, id);
  router_->Forward(Message{
      // TODO(ctiller): Hard coded 10 here until we track sequence numbers.
      std::move(
          RoutingHeader(router_->node_id(), payload.length(),
                        ReliabilityAndOrdering::ReliableOrdered)
              .AddDestination(peer_, StreamId(0), SeqNum(next_seq_++, 10))),
      // TODO(ctiller): Don't allocate.
      StatusOrCallback<Sink<Chunk>*>(
          ALLOCATED_CALLBACK,
          [payload{std::move(payload)}, new_stream{std::move(new_stream)},
           ready{std::move(ready)}](StatusOr<Sink<Chunk>*>&& status) mutable {
            if (status.is_error()) {
              ready(status.AsStatus());
              return;
            }
            Sink<Chunk>* sink = *status.get();
            // TODO(ctiller): Don't allocate.
            sink->Push(Chunk{0, std::move(payload)},
                       StatusCallback(ALLOCATED_CALLBACK,
                                      [new_stream{std::move(new_stream)},
                                       ready{std::move(ready)},
                                       sink](Status&& status) mutable {
                                        if (status.is_error()) {
                                          ready(std::forward<Status>(status));
                                          return;
                                        }
                                        ready(std::move(new_stream));
                                        sink->Close(Status::Ok());
                                      }));
          }  // namespace overnet
          )});
}

void RouterEndpoint::ConnectionStream::HandleMessage(
    SeqNum seq, uint64_t payload_length, bool is_control,
    ReliabilityAndOrdering reliability_and_ordering,
    StatusOrCallback<Sink<Chunk>*> ready_for_data) {
  if (is_control) {
    // TODO(ctiller): Don't dynamically allocate.
    // TODO(ctiller): 1024 hardcoded until we track window sizes.
    auto* linearizer = ReffedLinearizer::Make(1024);
    ReadAllAndParse(linearizer, StatusOrCallback<AckFrame>::Unimplemented());
    ready_for_data(linearizer);
    return;
  }

  if (reliability_and_ordering != ReliabilityAndOrdering::ReliableOrdered) {
    ready_for_data(StatusOr<Sink<Chunk>*>(
        StatusCode::INVALID_ARGUMENT,
        "Reliability and ordering of stream changed after creation"));
    return;
  }

  // TODO(ctiller): Don't dynamically allocate.
  const uint64_t seq_idx = seq.Reconstruct(recv_mode_.WindowBase());
  /*const bool trigger_ack =*/recv_mode_.Begin(
      seq_idx,
      StatusCallback(
          ALLOCATED_CALLBACK,
          [this, payload_length,
           ready_for_data{std::move(ready_for_data)}](Status&& status) mutable {
            if (status.is_error()) {
              ready_for_data(std::forward<Status>(status));
              return;
            }
            endpoint_->incoming_forks_.emplace(
                IncomingFork{std::move(ready_for_data), payload_length, peer_});
            endpoint_->MaybeContinueIncomingForks();
          }));
}

void RouterEndpoint::RecvIntro(StatusOrCallback<ReceivedIntroduction> ready) {
  recv_intro_ready_ = std::move(ready);
  MaybeContinueIncomingForks();
}

void RouterEndpoint::MaybeContinueIncomingForks() {
  if (recv_intro_ready_.empty() || incoming_forks_.empty()) return;
  auto incoming_fork = std::move(incoming_forks_.front());
  incoming_forks_.pop();
  auto peer = incoming_fork.peer;
  // TODO(ctiller): 1024 hardcoded until we track window sizes.
  auto* linearizer = ReffedLinearizer::Make(1024);
  ReadAllAndParse(
      linearizer,
      StatusOrCallback<ForkFrame>(
          ALLOCATED_CALLBACK,
          [this, peer, recv_intro = std::move(recv_intro_ready_)](
              StatusOr<ForkFrame>&& status) mutable {
            if (status.is_error()) {
              recv_intro(status.AsStatus());
              return;
            }
            const ForkFrame& fork_frame = *status.get();
            recv_intro(ReceivedIntroduction{
                NewStream(this, peer, fork_frame.reliability_and_ordering(),
                          fork_frame.stream_id()),
                fork_frame.introduction()});
          }));
  incoming_fork.ready_for_data(linearizer);
}

}  // namespace overnet
