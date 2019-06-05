// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <random>
#include <unordered_map>

#include "src/connectivity/overnet/lib/environment/timer.h"
#include "src/connectivity/overnet/lib/environment/trace.h"
#include "src/connectivity/overnet/lib/protocol/routable_message.h"
#include "src/connectivity/overnet/lib/routing/routing_table.h"
#include "src/connectivity/overnet/lib/stats/link.h"
#include "src/connectivity/overnet/lib/vocabulary/callback.h"
#include "src/connectivity/overnet/lib/vocabulary/closed_ptr.h"
#include "src/connectivity/overnet/lib/vocabulary/lazy_slice.h"
#include "src/connectivity/overnet/lib/vocabulary/slice.h"

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

inline auto ForwardingPayloadFactory(Slice payload) {
  return [payload = std::move(payload)](auto args) mutable {
    return std::move(payload);
  };
}

struct Message final {
  RoutableMessage header;
  LazySlice make_payload;
  TimeStamp received;
  uint32_t mss = std::numeric_limits<uint32_t>::max();

  static Message SimpleForwarder(RoutableMessage msg, Slice payload,
                                 TimeStamp received) {
    return Message{std::move(msg), ForwardingPayloadFactory(payload), received};
  }
};

class Link {
 public:
  virtual ~Link() {}
  virtual void Close(Callback<void> quiesced) = 0;
  virtual void Forward(Message message) = 0;
  virtual fuchsia::overnet::protocol::LinkStatus GetLinkStatus() = 0;
  virtual const LinkStats* GetStats() const = 0;
};

template <class T = Link>
using LinkPtr = ClosedPtr<T, Link>;
template <class T, class... Args>
LinkPtr<T> MakeLink(Args&&... args) {
  return MakeClosedPtr<T, Link>(std::forward<Args>(args)...);
}

class Router {
 public:
  static constexpr inline auto kModule = Module::ROUTER;

  class StreamHandler {
   public:
    virtual ~StreamHandler() {}
    virtual void RouterClose(Callback<void> quiesced) = 0;
    virtual void HandleMessage(SeqNum seq, TimeStamp received, Slice data) = 0;
  };

  Router(Timer* timer, NodeId node_id, bool allow_non_determinism);
  virtual ~Router();

  virtual void Close(Callback<void> quiesced);

  // Forward a message to either ourselves or a link
  void Forward(Message message);
  // Register a (locally handled) stream into this Router
  Status RegisterStream(NodeId peer, StreamId stream_id,
                        StreamHandler* stream_handler);
  Status UnregisterStream(NodeId peer, StreamId stream_id,
                          StreamHandler* stream_handler);
  // Register a link to another router (usually on a different machine)
  void RegisterLink(LinkPtr<> link);

  NodeId node_id() const { return node_id_; }
  Timer* timer() const { return timer_; }
  auto* rng() { return &rng_; }

  void UpdateRoutingTable(
      std::initializer_list<fuchsia::overnet::protocol::NodeStatus> node_status,
      std::initializer_list<fuchsia::overnet::protocol::LinkStatus>
          link_status) {
    UpdateRoutingTable(std::move(node_status), std::move(link_status), false);
  }

  void BlockUntilNoBackgroundUpdatesProcessing() {
    routing_table_.BlockUntilNoBackgroundUpdatesProcessing();
  }

  // Return true if this router believes a route exists to a particular node.
  bool HasRouteTo(NodeId node_id) {
    return node_id == node_id_ || link_holder(node_id)->link() != nullptr;
  }

  Optional<NodeId> SelectGossipPeer();
  void SendGossipUpdate(fuchsia::overnet::protocol::Peer_Proxy* peer,
                        NodeId target);

  void ApplyGossipUpdate(fuchsia::overnet::protocol::NodeStatus node_status) {
    UpdateRoutingTable({std::move(node_status)}, {}, false);
  }

  void ApplyGossipUpdate(fuchsia::overnet::protocol::LinkStatus link_status) {
    UpdateRoutingTable({}, {std::move(link_status)}, false);
  }

  template <class F>
  void ForEachNodeMetric(F mutator) {
    routing_table_.ForEachNodeMetric(mutator);
  }

  // Request notification that the node tables has been updated.
  // This may be called back on an arbitrary thread (unlike most of overnet).
  void OnNodeTableUpdate(uint64_t last_seen_version, Callback<void> callback) {
    routing_table_.OnNodeTableUpdate(last_seen_version, std::move(callback));
  }

  template <class F>
  void ForEachLink(F f) const {
    for (const auto& [label, link] : owned_links_) {
      f(label.target_node, link.get());
    }
  }

  uint64_t GenerateLinkLabel() { return primary_rng_(); }

 private:
  Timer* const timer_;
  const NodeId node_id_;

  void UpdateRoutingTable(
      std::initializer_list<fuchsia::overnet::protocol::NodeStatus> node_status,
      std::initializer_list<fuchsia::overnet::protocol::LinkStatus> link_status,
      bool flush_old_nodes);
  virtual void OnUnknownStream(NodeId peer, StreamId stream_id) {}

  void MaybeStartPollingLinkChanges();
  void MaybeStartFlushingOldEntries();

  void CloseLinks(Callback<void> quiesced);
  void CloseStreams(Callback<void> quiesced);

  class StreamHolder {
   public:
    StreamHolder(NodeId peer, StreamId id) : peer_(peer), stream_(id) {}
    Status SetHandler(StreamHandler* handler);
    [[nodiscard]] bool HandleMessage(SeqNum seq, TimeStamp received,
                                     Slice payload);
    Status ClearHandler(StreamHandler* handler);
    void Close(Callback<void> quiesced) {
      if (handler_ != nullptr)
        handler_->RouterClose(std::move(quiesced));
    }
    bool has_handler() { return handler_ != nullptr; }

   private:
    const NodeId peer_;
    const StreamId stream_;
    StreamHandler* handler_ = nullptr;
    // TODO(ctiller): globally cap the number of buffered packets within Router
    struct BufferedPacket {
      SeqNum seq;
      TimeStamp received;
      Slice payload;
    };
    std::unique_ptr<BufferedPacket> buffered_;
  };

  class LinkHolder {
   public:
    LinkHolder(NodeId target) {}
    void Forward(Message message);
    void SetLink(Link* link, uint32_t path_mss, bool is_direct);
    Link* link() { return link_; }
    const Link* link() const { return link_; }
    bool has_direct_link() const { return is_direct_; }
    uint32_t path_mss() { return path_mss_; }

    uint64_t last_gossip_version() const { return last_gossip_version_; }
    void set_last_gossip_version(uint64_t n) { last_gossip_version_ = n; }

   private:
    Link* link_ = nullptr;
    bool is_direct_ = false;
    uint32_t path_mss_ = std::numeric_limits<uint32_t>::max();
    std::vector<Message> pending_;
    uint64_t last_gossip_version_ = 0;
  };

  LinkHolder* link_holder(NodeId node_id) {
    auto it = links_.find(node_id);
    if (it != links_.end())
      return &it->second;
    return &links_
                .emplace(std::piecewise_construct,
                         std::forward_as_tuple(node_id),
                         std::forward_as_tuple(node_id))
                .first->second;
  }

  StreamHolder* stream_holder(NodeId node_id, StreamId stream_id) {
    auto it = streams_.find(LocalStreamId{node_id, stream_id});
    if (it != streams_.end())
      return &it->second;
    return &streams_
                .emplace(
                    std::piecewise_construct,
                    std::forward_as_tuple(LocalStreamId{node_id, stream_id}),
                    std::forward_as_tuple(node_id, stream_id))
                .first->second;
  }

  typedef router_impl::LocalStreamId LocalStreamId;

  struct OwnedLabel {
    NodeId target_node;
    uint64_t target_nodes_label;

    bool operator==(const OwnedLabel& other) const {
      return target_node == other.target_node &&
             target_nodes_label == other.target_nodes_label;
    }
  };

  struct HashOwnedLabel {
    size_t operator()(const OwnedLabel& label) const {
      return label.target_node.get() ^ label.target_nodes_label;
    }
  };

  bool shutting_down_ = false;
  std::unordered_map<OwnedLabel, LinkPtr<>, HashOwnedLabel> owned_links_;

  std::unordered_map<LocalStreamId, StreamHolder> streams_;
  std::unordered_map<NodeId, LinkHolder> links_;
  fit::function<uint64_t()> primary_rng_;
  std::mt19937 rng_;

  RoutingTable routing_table_;
  Optional<Timeout> poll_link_changes_timeout_;
  Optional<Timeout> flush_old_nodes_timeout_;
  fuchsia::overnet::protocol::NodeStatus own_node_status_;
};

}  // namespace overnet
