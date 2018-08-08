// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <unordered_map>
#include "callback.h"
#include "routable_message.h"
#include "routing_table.h"
#include "sink.h"
#include "slice.h"
#include "timer.h"

namespace overnet {
namespace router_impl {
struct LocalStreamId {
  NodeId peer;
  StreamId stream_id;
  uint64_t Hash() const { return peer.Hash() ^ stream_id.Hash(); }
};
inline bool operator==(const LocalStreamId& lhs, const LocalStreamId& rhs) {
  return lhs.peer == rhs.peer && lhs.stream_id == rhs.stream_id;
}
}  // namespace router_impl
}  // namespace overnet

namespace std {
template <>
struct hash<overnet::router_impl::LocalStreamId> {
  size_t operator()(const overnet::router_impl::LocalStreamId& id) const {
    return id.Hash();
  }
};
}  // namespace std

namespace overnet {

struct Message final {
  RoutableMessage wire;
  TimeStamp received;
  StatusCallback done;
};

class Link {
 public:
  virtual ~Link() {}
  virtual void Forward(Message message) = 0;
  virtual LinkMetrics GetLinkMetrics() = 0;
};

class Router final {
 public:
  class StreamHandler {
   public:
    virtual ~StreamHandler() {}
    virtual void HandleMessage(Optional<SeqNum> seq, TimeStamp received,
                               Slice data, StatusCallback done) = 0;
  };

  Router(Timer* timer, NodeId node_id, bool allow_threading)
      : timer_(timer),
        node_id_(node_id),
        routing_table_(node_id, timer, allow_threading) {
    UpdateRoutingTable({NodeMetrics(node_id, 0)}, {}, false);
  }

  // Forward a message to either ourselves or a link
  void Forward(Message message);
  // Register a (locally handled) stream into this Router
  Status RegisterStream(NodeId peer, StreamId stream_id,
                        StreamHandler* stream_handler);
  // Register a link to another router (usually on a different machine)
  void RegisterLink(std::unique_ptr<Link> link);

  NodeId node_id() const { return node_id_; }
  Timer* timer() const { return timer_; }

  void UpdateRoutingTable(std::vector<NodeMetrics> node_metrics,
                          std::vector<LinkMetrics> link_metrics) {
    UpdateRoutingTable(std::move(node_metrics), std::move(link_metrics), false);
  }

  void BlockUntilNoBackgroundUpdatesProcessing() {
    routing_table_.BlockUntilNoBackgroundUpdatesProcessing();
  }

  // Return true if this router believes a route exists to a particular node.
  bool HasRouteTo(NodeId node_id) {
    return node_id == node_id_ || links_[node_id].link() != nullptr;
  }

 private:
  Timer* const timer_;
  const NodeId node_id_;

  void UpdateRoutingTable(std::vector<NodeMetrics> node_metrics,
                          std::vector<LinkMetrics> link_metrics,
                          bool flush_old_nodes);

  void MaybeStartPollingLinkChanges();
  void MaybeStartFlushingOldEntries();

  class StreamHolder {
   public:
    void HandleMessage(Optional<SeqNum> seq, TimeStamp received, Slice data,
                       StatusCallback done);
    Status SetHandler(StreamHandler* handler);

   private:
    struct Pending {
      Optional<SeqNum> seq;
      TimeStamp received;
      Slice data;
      StatusCallback done;
    };

    StreamHandler* handler_ = nullptr;
    std::vector<Pending> pending_;
  };

  class LinkHolder {
   public:
    void Forward(Message message);
    void SetLink(Link* link);
    Link* link() { return link_; }

   private:
    Link* link_ = nullptr;
    std::vector<Message> pending_;
  };

  typedef router_impl::LocalStreamId LocalStreamId;

  std::unordered_map<uint64_t, std::unique_ptr<Link>> owned_links_;

  std::unordered_map<LocalStreamId, StreamHolder> streams_;
  std::unordered_map<NodeId, LinkHolder> links_;

  RoutingTable routing_table_;
  Optional<Timeout> poll_link_changes_timeout_;
  Optional<Timeout> flush_old_nodes_timeout_;
};

}  // namespace overnet
