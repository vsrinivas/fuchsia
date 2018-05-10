// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "router.h"
#include <iostream>

namespace overnet {

void Router::Forward(Message message) {
  // There are three primary cases we care about here, that can be discriminated
  // based on the destination count of the message:
  // 1. If there are zero destinations, this is a malformed message (fail).
  // 2. If there is one destination, forward the message on.
  // 3. If there are multiple destinations, broadcast this message to all
  //    destinations.
  // We separate 2 & 3 as the single forwarding case can be made
  // (much) more efficient.
  switch (message.routing_header.destinations().size()) {
    case 0:
      // Malformed message, bail
      message.ready_for_data(StatusOr<Sink<Chunk>*>(
          StatusCode::INVALID_ARGUMENT,
          "Routing header must have at least one destination"));
      break;
    case 1: {
      // Single destination... it could be either a local stream or need to be
      // forwarded to a remote node over some link.
      const RoutingHeader::Destination& dst =
          message.routing_header.destinations()[0];
      if (dst.dst() == node_id_) {
        streams_[LocalStreamId{message.routing_header.src(), dst.stream_id()}]
            .HandleMessage(dst.seq(), message.routing_header.payload_length(),
                           message.routing_header.is_control(),
                           message.routing_header.reliability_and_ordering(),
                           std::move(message.ready_for_data));
      } else {
        links_[dst.dst()].Forward(std::move(message));
      }
    } break;
    default: {
      // Multiple destination:
      // - Handle local streams directly.
      // - For remote forwarding:
      //   1. If we know the next hop, and that next hop is used for multiple of
      //      our destinations, keep the multicast group together for that set.
      //   2. Separate the multicast if next hops are different.
      //   3. Separate the multicast if we do not know about next hops yet.
      auto* sink = new BroadcastSink<Chunk>(std::move(message.ready_for_data));
      std::unordered_map<Link*, std::vector<RoutingHeader::Destination>>
          group_forward;
      for (const auto& dst : message.routing_header.destinations()) {
        if (dst.dst() == node_id_) {
          // Locally handled stream
          streams_[LocalStreamId{message.routing_header.src(), dst.stream_id()}]
              .HandleMessage(dst.seq(), message.routing_header.payload_length(),
                             message.routing_header.is_control(),
                             message.routing_header.reliability_and_ordering(),
                             sink->AddTarget());
        } else {
          // Remote destination
          LinkHolder& h = links_[dst.dst()];
          if (h.link() == nullptr) {
            // We don't know the next link, ask the LinkHolder to forward (which
            // will continue forwarding the message when we know the next hop).
            h.Forward(Message{message.routing_header.WithDestinations({dst}),
                              sink->AddTarget()});
          } else {
            // We know the next link: gather destinations together by link so
            // that we can (hopefully) keep multicast groups together
            group_forward[h.link()].emplace_back(dst);
          }
        }
      }
      // Forward any grouped messages now that we've examined all destinations
      for (auto& grp : group_forward) {
        grp.first->Forward(Message{
            message.routing_header.WithDestinations(std::move(grp.second)),
            sink->AddTarget()});
      }
    } break;
  }
}

Status Router::RegisterStream(NodeId peer, StreamId stream_id,
                              StreamHandler* stream_handler) {
  return streams_[LocalStreamId{peer, stream_id}].SetHandler(stream_handler);
}

Status Router::RegisterLink(NodeId peer, Link* link) {
  links_[peer].SetLink(link);
  return Status::Ok();
}

void Router::StreamHolder::HandleMessage(
    SeqNum seq, uint64_t payload_length, bool is_control,
    ReliabilityAndOrdering reliability_and_ordering,
    StatusOrCallback<Sink<Chunk>*> ready_for_data) {
  if (handler_ == nullptr) {
    pending_.emplace_back(Pending{seq, payload_length, is_control,
                                  reliability_and_ordering,
                                  std::move(ready_for_data)});
  } else {
    handler_->HandleMessage(seq, payload_length, is_control,
                            reliability_and_ordering,
                            std::move(ready_for_data));
  }
}

Status Router::StreamHolder::SetHandler(StreamHandler* handler) {
  if (handler_ != nullptr) {
    return Status(StatusCode::FAILED_PRECONDITION, "Handler already set");
  }
  handler_ = handler;
  std::vector<Pending> pending;
  pending.swap(pending_);
  for (auto& p : pending) {
    handler_->HandleMessage(p.seq, p.length, p.is_control,
                            p.reliability_and_ordering,
                            std::move(p.ready_for_data));
  }
  return Status::Ok();
}

void Router::LinkHolder::Forward(Message message) {
  if (link_ == nullptr) {
    pending_.emplace_back(std::move(message));
  } else {
    link_->Forward(std::move(message));
  }
}

void Router::LinkHolder::SetLink(Link* link) {
  link_ = link;
  std::vector<Message> pending;
  pending.swap(pending_);
  for (auto& p : pending) {
    link_->Forward(std::move(p));
  }
}

}  // namespace overnet
