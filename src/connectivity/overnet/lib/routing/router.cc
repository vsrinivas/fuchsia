// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/connectivity/overnet/lib/routing/router.h"

#include <iostream>

#include "garnet/public/lib/fostr/fidl/fuchsia/overnet/protocol/formatting.h"
#include "src/connectivity/overnet/lib/protocol/fidl.h"

namespace overnet {

static constexpr TimeDelta kPollLinkChangeTimeout =
    TimeDelta::FromMilliseconds(100);

Router::Router(Timer* timer, NodeId node_id, bool allow_non_determinism)
    : timer_(timer),
      node_id_(node_id),
      rng_(allow_non_determinism ? std::random_device()() : 0),
      routing_table_(node_id, timer, allow_non_determinism),
      own_node_status_{node_id.as_fidl(), 1} {
  std::vector<fuchsia::overnet::protocol::NodeStatus> node_status;
  node_status.emplace_back(fidl::Clone(own_node_status_));
  UpdateRoutingTable({fidl::Clone(own_node_status_)}, {}, false);
}

Router::~Router() { shutting_down_ = true; }

void Router::Close(Callback<void> quiesced) {
  ScopedModule<Router> scoped_module(this);
  OVERNET_TRACE(DEBUG) << node_id_ << " Close";
  shutting_down_ = true;
  poll_link_changes_timeout_.Reset();
  flush_old_nodes_timeout_.Reset();
  links_.clear();
  CloseLinks(std::move(quiesced));
}

void Router::CloseLinks(Callback<void> quiesced) {
  OVERNET_TRACE(DEBUG) << node_id_
                       << " CloseLinks remaining=" << owned_links_.size();
  if (owned_links_.empty()) {
    CloseStreams(std::move(quiesced));
    return;
  }
  auto it = owned_links_.begin();
  auto p = it->second.release();
  owned_links_.erase(it);
  p->Close(Callback<void>(ALLOCATED_CALLBACK,
                          [this, p, quiesced = std::move(quiesced)]() mutable {
                            ScopedModule<Router> scoped_module(this);
                            delete p;
                            CloseLinks(std::move(quiesced));
                          }));
}

void Router::CloseStreams(Callback<void> quiesced) {
  if (streams_.empty()) {
    OVERNET_TRACE(DEBUG) << "Closed";
    return;
  }
  auto it = streams_.begin();
  auto id = it->first;
  OVERNET_TRACE(DEBUG) << node_id_
                       << " CloseStreams remaining=" << streams_.size()
                       << " next=" << id.peer << "/" << id.stream_id;
  it->second.Close(Callback<void>(
      ALLOCATED_CALLBACK, [this, id, quiesced = std::move(quiesced)]() mutable {
        ScopedModule<Router> scoped_module(this);
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
  ScopedModule<Router> scoped_module(this);
  OVERNET_TRACE(DEBUG) << "Forward " << message.header
                       << " shutting_down=" << shutting_down_;
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
        if (!stream_holder(message.header.src(), dst.stream_id())
                 ->HandleMessage(dst.seq(), message.received,
                                 message.make_payload(LazySliceArgs{
                                     Border::None(),
                                     std::numeric_limits<uint32_t>::max()}))) {
          OnUnknownStream(message.header.src(), dst.stream_id());
        }
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
                dst, stream_holder(message.header.src(), dst.stream_id()));
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
      const auto max_header_length = message.header.MaxHeaderLength();
      if (overall_mss < max_header_length) {
        return;
      }
      overall_mss -= max_header_length;
      Slice payload =
          message.make_payload(LazySliceArgs{Border::None(), overall_mss});
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
        if (!handle_locally->second->HandleMessage(handle_locally->first.seq(),
                                                   message.received,
                                                   std::move(payload))) {
          OnUnknownStream(message.header.src(),
                          handle_locally->first.stream_id());
        }
      }
    } break;
  }
}

void Router::UpdateRoutingTable(
    std::initializer_list<fuchsia::overnet::protocol::NodeStatus> node_updates,
    std::initializer_list<fuchsia::overnet::protocol::LinkStatus> link_updates,
    bool flush_old_nodes) {
  ScopedModule<Router> scoped_module(this);
  routing_table_.ProcessUpdate(std::move(node_updates), std::move(link_updates),
                               flush_old_nodes);
  MaybeStartPollingLinkChanges();
}

void Router::MaybeStartPollingLinkChanges() {
  ScopedModule<Router> scoped_module(this);
  if (shutting_down_ || poll_link_changes_timeout_) {
    return;
  }
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
                    lnk.second.SetLink(nullptr, 0, false);
                  }
                }
                // Set routing information for other links.
                for (const auto& sl : selected_links) {
                  OVERNET_TRACE(DEBUG)
                      << "Select: " << sl.first << " " << sl.second.link_id
                      << " (route_mss=" << sl.second.route_mss << ")";
                  auto it = owned_links_.find(sl.second.link_id);
                  auto* link =
                      it == owned_links_.end() ? nullptr : it->second.get();
                  link_holder(sl.first)->SetLink(
                      link, sl.second.route_mss,
                      link ? link->GetLinkStatus().to == sl.first : false);
                }
                MaybeStartFlushingOldEntries();
              });
          if (keep_polling) {
            MaybeStartPollingLinkChanges();
          }
        }
      });
}

void Router::MaybeStartFlushingOldEntries() {
  ScopedModule<Router> scoped_module(this);
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
  ScopedModule<Router> scoped_module(this);
  OVERNET_TRACE(DEBUG) << "RegisterStream: " << peer << "/" << stream_id
                       << " at " << stream_handler
                       << " shutting_down=" << shutting_down_;
  if (shutting_down_) {
    return Status(StatusCode::FAILED_PRECONDITION, "Router shutting down");
  }
  return stream_holder(peer, stream_id)->SetHandler(stream_handler);
}

Status Router::UnregisterStream(NodeId peer, StreamId stream_id,
                                StreamHandler* stream_handler) {
  ScopedModule<Router> scoped_module(this);
  OVERNET_TRACE(DEBUG) << "UnregisterStream: " << peer << "/" << stream_id
                       << " at " << stream_handler
                       << " shutting_down=" << shutting_down_;
  auto it = streams_.find(LocalStreamId{peer, stream_id});
  if (it == streams_.end()) {
    return Status(StatusCode::FAILED_PRECONDITION, "Stream not registered");
  }
  Status status = it->second.ClearHandler(stream_handler);
  streams_.erase(it);
  return status;
}

Optional<NodeId> Router::SelectGossipPeer() {
  ScopedModule<Router> scoped_module(this);
  const uint64_t gossip_version = routing_table_.gossip_version();
  std::vector<NodeId> eligible_nodes;
  for (const auto& peer : links_) {
    if (peer.second.has_direct_link() &&
        peer.second.last_gossip_version() < gossip_version) {
      eligible_nodes.push_back(peer.first);
    }
  }
  if (eligible_nodes.empty()) {
    return Nothing;
  }
  std::uniform_int_distribution<> dis(0, eligible_nodes.size() - 1);
  return eligible_nodes[dis(rng_)];
}

void Router::SendGossipUpdate(fuchsia::overnet::protocol::Peer_Proxy* peer,
                              NodeId target) {
  ScopedModule<Router> scoped_module(this);
  link_holder(target)->set_last_gossip_version(
      routing_table_.SendUpdate(peer, target));
}

namespace {
template <class T>
std::vector<T> TakeVector(std::vector<T>* vec) {
  if (vec == nullptr) {
    return {};
  }
  return std::move(*vec);
}
}  // namespace

void Router::RegisterLink(LinkPtr<> link) {
  ScopedModule<Router> scoped_module(this);
  auto status = link->GetLinkStatus();
  owned_links_.emplace(status.local_id, std::move(link));
  auto target = status.to;
  UpdateRoutingTable({{target, 0}}, {std::move(status)}, false);
}

bool Router::StreamHolder::HandleMessage(SeqNum seq, TimeStamp received,
                                         Slice payload) {
  if (handler_ == nullptr) {
    if (!buffered_ || buffered_->seq.Reconstruct(1) < seq.Reconstruct(1)) {
      OVERNET_TRACE(DEBUG) << "Buffer message: peer=" << peer_
                           << " stream=" << stream_ << " seq=" << seq
                           << " message=" << payload;
      buffered_.reset(new BufferedPacket{seq, received, std::move(payload)});
    } else {
      OVERNET_TRACE(DEBUG) << "Drop message: peer=" << peer_
                           << " stream=" << stream_ << " seq=" << seq
                           << " message=" << payload;
    }
    return false;
  } else {
    handler_->HandleMessage(seq, received, std::move(payload));
    return true;
  }
}

Status Router::StreamHolder::SetHandler(StreamHandler* handler) {
  if (handler_ != nullptr) {
    return Status(StatusCode::FAILED_PRECONDITION, "Handler already set");
  }
  handler_ = handler;
  if (buffered_) {
    auto pkt = std::move(buffered_);
    handler_->HandleMessage(pkt->seq, pkt->received, std::move(pkt->payload));
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
    OVERNET_TRACE(DEBUG) << "Queue: " << message.header;
    pending_.emplace_back(std::move(message));
  } else {
    message.mss = std::min(message.mss, path_mss_);
    link_->Forward(std::move(message));
  }
}

void Router::LinkHolder::SetLink(Link* link, uint32_t path_mss,
                                 bool is_direct) {
  link_ = link;
  is_direct_ = is_direct;
  path_mss_ = path_mss;
  std::vector<Message> pending;
  pending.swap(pending_);
  for (auto& p : pending) {
    Forward(std::move(p));
  }
}

}  // namespace overnet
