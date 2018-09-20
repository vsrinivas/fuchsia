// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <condition_variable>
#include <mutex>
#include <thread>
#include <tuple>
#include <unordered_map>
#include <vector>
#include "garnet/lib/overnet/environment/timer.h"
#include "garnet/lib/overnet/environment/trace.h"
#include "garnet/lib/overnet/labels/node_id.h"
#include "garnet/lib/overnet/vocabulary/bandwidth.h"
#include "garnet/lib/overnet/vocabulary/internal_list.h"
#include "garnet/lib/overnet/vocabulary/optional.h"
#include "garnet/lib/overnet/vocabulary/slice.h"

#include <iostream>

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

static constexpr uint64_t METRIC_VERSION_TOMBSTONE = ~uint64_t(0);

class LinkMetrics {
 public:
  LinkMetrics() = default;
  LinkMetrics(NodeId from, NodeId to, uint64_t version, uint64_t link_label,
              bool created_locally)
      : created_locally_(created_locally),
        from_(from),
        to_(to),
        version_(version),
        link_label_(link_label) {}

  NodeId from() const { return from_; }
  NodeId to() const { return to_; }
  uint64_t version() const { return version_; }
  uint64_t link_label() const { return link_label_; }
  Bandwidth bw_link() const { return bw_link_; }
  Bandwidth bw_used() const { return bw_used_; }
  TimeDelta rtt() const { return rtt_; }
  uint32_t mss() const { return mss_; }
  bool created_locally() const { return created_locally_; }

  void set_from_to(NodeId from, NodeId to) {
    from_ = from;
    to_ = to;
  }
  void set_link_label(uint64_t label) { link_label_ = label; }
  void set_version(uint64_t version) { version_ = version; }
  void set_bw_link(Bandwidth x) { bw_link_ = x; }
  void set_bw_used(Bandwidth x) { bw_used_ = x; }
  void set_rtt(TimeDelta x) { rtt_ = x; }
  void set_mss(uint32_t x) { mss_ = x; }

 private:
  bool created_locally_ = false;
  NodeId from_{0};
  NodeId to_{0};
  uint64_t version_{0};
  uint64_t link_label_{0};
  Bandwidth bw_link_{Bandwidth::Zero()};
  Bandwidth bw_used_{Bandwidth::Zero()};
  TimeDelta rtt_{TimeDelta::PositiveInf()};
  uint32_t mss_{std::numeric_limits<uint32_t>::max()};
};

std::ostream& operator<<(std::ostream& out, const LinkMetrics& m);

class NodeMetrics {
 public:
  NodeMetrics() = default;
  NodeMetrics(NodeId node_id, uint64_t version, bool created_locally)
      : created_locally_(created_locally),
        node_id_(node_id),
        version_(version) {}

  NodeId node_id() const { return node_id_; }
  uint64_t version() const { return version_; }
  TimeDelta forwarding_time() const { return forwarding_time_; }
  const Slice& description() const { return description_; }

  void set_node_id(NodeId node_id) { node_id_ = node_id; }
  void set_version(uint64_t version) { version_ = version; }
  void IncrementVersion() { version_++; }
  void set_forwarding_time(TimeDelta forwarding_time) {
    forwarding_time_ = forwarding_time;
  }
  void set_description(Slice description) {
    description_ = std::move(description);
  }
  bool created_locally() const { return created_locally_; }

  auto value_parts() const { return std::tie(forwarding_time_, description_); }

 private:
  bool created_locally_ = false;
  NodeId node_id_{0};
  uint64_t version_{0};
  TimeDelta forwarding_time_{TimeDelta::PositiveInf()};
  Slice description_;
};

std::ostream& operator<<(std::ostream& out, const NodeMetrics& m);

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

  void Update(std::vector<NodeMetrics> node_metrics,
              std::vector<LinkMetrics> link_metrics, bool flush_old_nodes);

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
  std::pair<Slice, uint64_t> Write(Border desired_border,
                                   Optional<NodeId> exclude_node) const;
  Status Parse(Slice update, std::vector<NodeMetrics>* node_metrics,
               std::vector<LinkMetrics>* link_metrics) const;
  Status Read(Slice update);

  template <class F>
  void ForEachNodeMetric(F visitor) const {
    std::vector<NodeMetrics> nodes_copy =
        (std::lock_guard{shared_table_mu_}, shared_node_metrics_);
    for (const auto& m : nodes_copy) {
      visitor(m);
    }
  }

 private:
  const NodeId root_node_;
  Timer* const timer_;

  struct Metrics {
    std::vector<NodeMetrics> node_metrics;
    std::vector<LinkMetrics> link_metrics;
    bool Empty() const { return node_metrics.empty() && link_metrics.empty(); }
    void Clear() {
      node_metrics.clear();
      link_metrics.clear();
    }
  };
  Metrics change_log_;
  const bool allow_threading_;
  bool flush_requested_ = false;

  void ApplyChanges(TimeStamp now, const Metrics& changes, bool flush);
  SelectedLinks BuildForwardingTable();

  TimeStamp last_update_{TimeStamp::Epoch()};
  uint64_t path_finding_run_ = 0;

  std::mutex mu_;
  std::condition_variable cv_;
  Optional<std::thread> processing_changes_;

  struct Node;

  struct Link {
    Link(TimeStamp now, LinkMetrics initial_metrics, Node* to)
        : metrics(initial_metrics), last_updated(now), to_node(to) {}
    LinkMetrics metrics;
    TimeStamp last_updated;
    InternalListNode<Link> outgoing_link;
    Node* const to_node;
  };

  struct Node {
    Node(TimeStamp now, NodeMetrics initial_metrics)
        : metrics(initial_metrics), last_updated(now) {}
    NodeMetrics metrics;
    TimeStamp last_updated;
    InternalList<Link, &Link::outgoing_link> outgoing_links;

    // Path finding temporary state.
    uint64_t last_path_finding_run = 0;
    TimeDelta best_rtt{TimeDelta::Zero()};
    Node* best_from;
    Link* best_link;
    uint32_t mss;
    bool queued = false;
    InternalListNode<Node> path_finding_node;
  };

  void RemoveOutgoingLinks(Node& node);

  std::unordered_map<NodeId, Node> node_metrics_;
  std::unordered_map<routing_table_impl::FullLinkLabel, Link> link_metrics_;

  mutable std::mutex shared_table_mu_;
  uint64_t gossip_version_ = 0;
  std::vector<NodeMetrics> shared_node_metrics_;
  std::vector<LinkMetrics> shared_link_metrics_;

  uint64_t selected_links_version_ = 0;
  SelectedLinks selected_links_;
  uint64_t published_links_version_ = 0;
};

}  // namespace overnet
