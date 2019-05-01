// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <fuchsia/overnet/protocol/cpp/fidl.h>
#include <fuchsia/overnet/protocol/cpp/overnet_internal.h>

#include <condition_variable>
#include <iostream>
#include <mutex>
#include <thread>
#include <tuple>
#include <unordered_map>
#include <vector>

#include "lib/fidl/cpp/clone.h"
#include "src/connectivity/overnet/lib/environment/timer.h"
#include "src/connectivity/overnet/lib/environment/trace.h"
#include "src/connectivity/overnet/lib/labels/node_id.h"
#include "src/connectivity/overnet/lib/vocabulary/bandwidth.h"
#include "src/connectivity/overnet/lib/vocabulary/internal_list.h"
#include "src/connectivity/overnet/lib/vocabulary/optional.h"
#include "src/connectivity/overnet/lib/vocabulary/slice.h"

namespace overnet {
namespace routing_table_impl {

struct FullLinkLabel {
  NodeId from;
  NodeId to;
  uint64_t link_label;
};

inline bool operator==(const FullLinkLabel& a, const FullLinkLabel& b) {
  return a.from == b.from && a.to == b.to && a.link_label == b.link_label;
}

}  // namespace routing_table_impl
}  // namespace overnet

namespace std {
template <>
struct hash<overnet::routing_table_impl::FullLinkLabel> {
  size_t operator()(
      const overnet::routing_table_impl::FullLinkLabel& id) const {
    return id.from.Hash() ^ id.to.Hash() ^ id.link_label;
  }
};
}  // namespace std

namespace overnet {

class RoutingTable {
 public:
  RoutingTable(NodeId root_node, Timer* timer, bool allow_threading);
  ~RoutingTable();
  RoutingTable(const RoutingTable&) = delete;
  RoutingTable& operator=(const RoutingTable&) = delete;

  static constexpr TimeDelta EntryExpiry() { return TimeDelta::FromMinutes(5); }

  struct SelectedLink {
    uint64_t link_id;
    uint32_t route_mss;

    bool operator==(SelectedLink other) const {
      return link_id == other.link_id && route_mss == other.route_mss;
    }
  };
  using SelectedLinks = std::unordered_map<NodeId, SelectedLink>;

  void ProcessUpdate(
      std::initializer_list<fuchsia::overnet::protocol::NodeStatus>
          node_updates,
      std::initializer_list<fuchsia::overnet::protocol::LinkStatus>
          link_updates,
      bool flush_old_nodes);

  // Returns true if this update concludes any changes begun by all prior
  // Update() calls.
  template <class F>
  bool PollLinkUpdates(F f) {
    if (!mu_.try_lock()) {
      return false;
    }
    if (selected_links_version_ != published_links_version_) {
      published_links_version_ = selected_links_version_;
      f(selected_links_);
    }
    const bool done = !processing_changes_.has_value();
    mu_.unlock();
    return done;
  }

  void BlockUntilNoBackgroundUpdatesProcessing() {
    std::unique_lock<std::mutex> lock(mu_);
    cv_.wait(lock, [this]() -> bool { return !processing_changes_; });
  }

  uint64_t gossip_version() const {
    std::lock_guard<std::mutex> lock(shared_table_mu_);
    return gossip_version_;
  }

  uint64_t SendUpdate(fuchsia::overnet::protocol::Peer_Proxy* peer,
                      Optional<NodeId> exclude_node) const;

  template <class F>
  void ForEachNodeMetric(F visitor) const {
    std::vector<fuchsia::overnet::protocol::NodeStatus> nodes_copy;
    {
      std::lock_guard lock{shared_table_mu_};
      for (const auto& m : shared_node_status_) {
        nodes_copy.emplace_back(fidl::Clone(m));
      }
    }
    for (const auto& m : nodes_copy) {
      visitor(m);
    }
  }

  // Request notification that the node tables has been updated.
  // This may be called back on an arbitrary thread (unlike most of overnet).
  void OnNodeTableUpdate(uint64_t last_seen_version, Callback<void> callback) {
    std::lock_guard lock{shared_table_mu_};
    if (gossip_version_ != last_seen_version) {
      // Forces callback to be called after lock is released.
      return;
    }
    on_node_table_update_.emplace_back(std::move(callback));
  }

 private:
  const NodeId root_node_;
  Timer* const timer_;

  struct StatusVecs {
    std::vector<fuchsia::overnet::protocol::NodeStatus> nodes;
    std::vector<fuchsia::overnet::protocol::LinkStatus> links;
    bool Empty() const { return nodes.empty() && links.empty(); }
    void Clear() {
      nodes.clear();
      links.clear();
    }
  };
  StatusVecs change_log_;
  const bool allow_threading_;
  bool flush_requested_ = false;

  void ApplyChanges(TimeStamp now, const StatusVecs& changes, bool flush);
  SelectedLinks BuildForwardingTable();

  TimeStamp last_update_{TimeStamp::Epoch()};
  uint64_t path_finding_run_ = 0;

  std::mutex mu_;
  std::condition_variable cv_;
  Optional<std::thread> processing_changes_;

  struct Node;

  struct Link {
    Link(TimeStamp now, fuchsia::overnet::protocol::LinkStatus initial_status,
         Node* from, Node* to)
        : status(std::move(initial_status)),
          last_updated(now),
          from_node(from),
          to_node(to) {}
    fuchsia::overnet::protocol::LinkStatus status;
    TimeStamp last_updated;
    InternalListNode<Link> outgoing_link;
    InternalListNode<Link> incoming_link;
    Node* const from_node;
    Node* const to_node;
  };

  struct Node {
    Node(TimeStamp now, fuchsia::overnet::protocol::NodeStatus initial_status)
        : status(std::move(initial_status)), last_updated(now) {}
    fuchsia::overnet::protocol::NodeStatus status;
    TimeStamp last_updated;
    InternalList<Link, &Link::outgoing_link> outgoing_links;
    InternalList<Link, &Link::incoming_link> incoming_links;

    // Path finding temporary state.
    uint64_t last_path_finding_run = 0;
    TimeDelta best_rtt{TimeDelta::Zero()};
    Node* best_from;
    Link* best_link;
    uint32_t mss;
    bool queued = false;
    InternalListNode<Node> path_finding_node;
  };

  void RemoveOutgoingLinks(Node* node);
  void RemoveIncomingLinks(Node* node);

  std::unordered_map<NodeId, Node> nodes_;
  std::unordered_map<routing_table_impl::FullLinkLabel, Link> links_;

  mutable std::mutex shared_table_mu_;
  uint64_t gossip_version_ = 0;
  std::vector<fuchsia::overnet::protocol::NodeStatus> shared_node_status_;
  std::vector<fuchsia::overnet::protocol::LinkStatus> shared_link_status_;
  std::vector<Callback<void>> on_node_table_update_;

  uint64_t selected_links_version_ = 0;
  SelectedLinks selected_links_;
  uint64_t published_links_version_ = 0;
};

}  // namespace overnet
