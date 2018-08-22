// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "router.h"
#include <iostream>

namespace overnet {

static constexpr TimeDelta kPollLinkChangeTimeout =
    TimeDelta::FromMilliseconds(100);

Router::~Router() { shutting_down_ = true; }

void Router::Close(Callback<void> quiesced) {
  OVERNET_TRACE(DEBUG, trace_sink_) << "Close";
  shutting_down_ = true;
  poll_link_changes_timeout_.Reset();
  flush_old_nodes_timeout_.Reset();
  links_.clear();
  CloseLinks(std::move(quiesced));
}

void Router::CloseLinks(Callback<void> quiesced) {
  OVERNET_TRACE(DEBUG, trace_sink_)
      << "CloseLinks remaining=" << owned_links_.size();
  if (owned_links_.empty()) {
    CloseStreams(std::move(quiesced));
    return;
  }
  auto it = owned_links_.begin();
  auto p = it->second.release();
  owned_links_.erase(it);
  p->Close(Callback<void>(ALLOCATED_CALLBACK,
                          [this, p, quiesced = std::move(quiesced)]() mutable {
                            delete p;
                            CloseLinks(std::move(quiesced));
                          }));
}

void Router::CloseStreams(Callback<void> quiesced) {
  OVERNET_TRACE(DEBUG, trace_sink_)
      << "CloseStreams remaining=" << streams_.size();
  if (streams_.empty()) {
    OVERNET_TRACE(DEBUG, trace_sink_) << "Closed";
    return;
  }
  auto it = streams_.begin();
  auto id = it->first;
  it->second.Close(Callback<void>(
      ALLOCATED_CALLBACK, [this, id, quiesced = std::move(quiesced)]() mutable {
        auto it = streams_.find(id);
        if (it != streams_.end()) {
          assert(!it->second.has_handler());
          streams_.erase(it);
        }
        assert(streams_.count(id) == 0);
        CloseStreams(std::move(quiesced));
      }));
}

void Router::Forward(Message message) {
  OVERNET_TRACE(DEBUG, trace_sink_) << "Forward " << message.header;
  if (shutting_down_) {
    return;
  }
  assert(!message.make_payload.empty());
  // There are three primary cases we care about here, that can be discriminated
  // based on the destination count of the message:
  // 1. If there are zero destinations, this is a malformed message (fail).
  // 2. If there is one destination, forward the message on.
  // 3. If there are multiple destinations, broadcast this message to all
  //    destinations.
  // We separate 2 & 3 as the single forwarding case can be made
  // (much) more efficient.
  switch (message.header.destinations().size()) {
    case 0:
      // Malformed message, bail
      // TODO(ctiller): Log error: Routing header must have at least one
      // destination
      break;
    case 1: {
      // Single destination... it could be either a local stream or need to be
      // forwarded to a remote node over some link.
      const RoutableMessage::Destination& dst =
          message.header.destinations()[0];
      if (dst.dst() == node_id_) {
        streams_[LocalStreamId{message.header.src(), dst.stream_id()}]
            .HandleMessage(dst.seq(), message.received,
                           message.make_payload(LazySliceArgs{
                               0, std::numeric_limits<uint32_t>::max()}));
      } else {
        link_holder(dst.dst())->Forward(std::move(message));
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
      std::unordered_map<Link*, std::vector<RoutableMessage::Destination>>
          group_forward;
      std::vector<std::pair<RoutableMessage::Destination, LinkHolder*>>
          disconnected_holders;
      Optional<std::pair<RoutableMessage::Destination, StreamHolder*>>
          handle_locally;
      uint32_t overall_mss = std::numeric_limits<uint32_t>::max();
      for (const auto& dst : message.header.destinations()) {
        if (dst.dst() == node_id_) {
          // Locally handled stream
          if (!handle_locally.has_value()) {
            handle_locally = std::make_pair(
                dst, &streams_[LocalStreamId{message.header.src(),
                                             dst.stream_id()}]);
          }
        } else {
          // Remote destination
          LinkHolder* h = link_holder(dst.dst());
          if (h->link() == nullptr) {
            // We don't know the next link, ask the LinkHolder to forward (which
            // will continue forwarding the message when we know the next hop).
            disconnected_holders.push_back(std::make_pair(dst, h));
          } else {
            // We know the next link: gather destinations together by link so
            // that we can (hopefully) keep multicast groups together
            group_forward[h->link()].emplace_back(dst);
            overall_mss = std::min(overall_mss, h->path_mss());
          }
        }
      }
      Slice payload = message.make_payload(LazySliceArgs{0, overall_mss});
      // Forward any grouped messages now that we've examined all destinations
      for (auto& grp : group_forward) {
        grp.first->Forward(Message::SimpleForwarder(
            message.header.WithDestinations(std::move(grp.second)), payload,
            message.received));
      }
      for (auto& lh : disconnected_holders) {
        lh.second->Forward(Message::SimpleForwarder(
            message.header.WithDestinations({lh.first}), payload,
            message.received));
      }
      if (handle_locally.has_value()) {
        handle_locally->second->HandleMessage(
            handle_locally->first.seq(), message.received, std::move(payload));
      }
    } break;
  }
}

void Router::UpdateRoutingTable(std::vector<NodeMetrics> node_metrics,
                                std::vector<LinkMetrics> link_metrics,
                                bool flush_old_nodes) {
  routing_table_.Update(std::move(node_metrics), std::move(link_metrics),
                        flush_old_nodes);
  MaybeStartPollingLinkChanges();
}

void Router::MaybeStartPollingLinkChanges() {
  if (shutting_down_ || poll_link_changes_timeout_)
    return;
  poll_link_changes_timeout_.Reset(
      timer_, timer_->Now() + kPollLinkChangeTimeout,
      [this](const Status& status) {
        if (status.is_ok()) {
          poll_link_changes_timeout_.Reset();
          const bool keep_polling = !routing_table_.PollLinkUpdates(
              [this](const RoutingTable::SelectedLinks& selected_links) {
                // Clear routing information for now unreachable links.
                for (auto& lnk : links_) {
                  if (selected_links.count(lnk.first) == 0) {
                    lnk.second.SetLink(nullptr, 0);
                  }
                }
                // Set routing information for other links.
                for (const auto& sl : selected_links) {
                  OVERNET_TRACE(INFO, trace_sink_)
                      << "Select: " << sl.first << " " << sl.second.link_id
                      << " (route_mss=" << sl.second.route_mss << ")";
                  auto it = owned_links_.find(sl.second.link_id);
                  link_holder(sl.first)->SetLink(
                      it == owned_links_.end() ? nullptr : it->second.get(),
                      sl.second.route_mss);
                }
                MaybeStartFlushingOldEntries();
              });
          if (keep_polling)
            MaybeStartPollingLinkChanges();
        }
      });
}

void Router::MaybeStartFlushingOldEntries() {
  if (flush_old_nodes_timeout_)
    return;
  flush_old_nodes_timeout_.Reset(timer_,
                                 timer_->Now() + routing_table_.EntryExpiry(),
                                 [this](const Status& status) {
                                   if (status.is_ok()) {
                                     flush_old_nodes_timeout_.Reset();
                                     UpdateRoutingTable({}, {}, true);
                                   }
                                 });
}

Status Router::RegisterStream(NodeId peer, StreamId stream_id,
                              StreamHandler* stream_handler) {
  OVERNET_TRACE(DEBUG, trace_sink_) << "RegisterStream: " << peer << "/"
                                    << stream_id << " at " << stream_handler;
  return streams_[LocalStreamId{peer, stream_id}].SetHandler(stream_handler);
}

Status Router::UnregisterStream(NodeId peer, StreamId stream_id,
                                StreamHandler* stream_handler) {
  OVERNET_TRACE(DEBUG, trace_sink_) << "UnregisterStream: " << peer << "/"
                                    << stream_id << " at " << stream_handler;
  auto it = streams_.find(LocalStreamId{peer, stream_id});
  if (it == streams_.end()) {
    return Status(StatusCode::FAILED_PRECONDITION, "Stream not registered");
  }
  Status status = it->second.ClearHandler(stream_handler);
  streams_.erase(it);
  return status;
}

void Router::RegisterLink(LinkPtr<> link) {
  const auto& metrics = link->GetLinkMetrics();
  owned_links_.emplace(metrics.link_label(), std::move(link));
  UpdateRoutingTable({NodeMetrics(metrics.to(), 0)}, {metrics}, false);
}

void Router::StreamHolder::HandleMessage(SeqNum seq, TimeStamp received,
                                         Slice payload) {
  if (handler_ == nullptr) {
    pending_.emplace_back(Pending{seq, received, std::move(payload)});
  } else {
    handler_->HandleMessage(seq, received, std::move(payload));
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
    handler_->HandleMessage(p.seq, p.received, std::move(p.payload));
  }
  return Status::Ok();
}

Status Router::StreamHolder::ClearHandler(StreamHandler* handler) {
  if (handler_ != handler) {
    return Status(StatusCode::FAILED_PRECONDITION, "Invalid clear handler");
  }
  handler_ = nullptr;
  return Status::Ok();
}

void Router::LinkHolder::Forward(Message message) {
  if (link_ == nullptr) {
    OVERNET_TRACE(DEBUG, trace_sink_) << "Queue: " << message.header;
    pending_.emplace_back(std::move(message));
  } else {
    link_->Forward(std::move(message));
  }
}

void Router::LinkHolder::SetLink(Link* link, uint32_t path_mss) {
  link_ = link;
  std::vector<Message> pending;
  pending.swap(pending_);
  for (auto& p : pending) {
    link_->Forward(std::move(p));
  }
}

}  // namespace overnet
