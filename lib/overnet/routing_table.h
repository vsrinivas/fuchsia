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
#include "bandwidth.h"
#include "internal_list.h"
#include "node_id.h"
#include "optional.h"
#include "timer.h"

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

enum class LinkMedia : uint8_t {
  Unknown,
  Wired,
  Wireless,
  Internet,
};

static constexpr uint64_t METRIC_VERSION_TOMBSTONE = ~uint64_t(0);

class LinkMetrics {
 public:
  LinkMetrics(NodeId from, NodeId to, uint64_t version, uint64_t link_label)
      : from_(from), to_(to), version_(version), link_label_(link_label) {}

  NodeId from() const { return from_; }
  NodeId to() const { return to_; }
  uint64_t version() const { return version_; }
  uint64_t link_label() const { return link_label_; }
  Bandwidth bw_link() const { return bw_link_; }
  Bandwidth bw_used() const { return bw_used_; }
  TimeDelta rtt() const { return rtt_; }
  LinkMedia media() const { return media_; }

  void set_bw_link(Bandwidth x) { bw_link_ = x; }
  void set_bw_used(Bandwidth x) { bw_used_ = x; }
  void set_rtt(TimeDelta x) { rtt_ = x; }
  void set_media(LinkMedia x) { media_ = x; }

 private:
  NodeId from_;
  NodeId to_;
  uint64_t version_;
  uint64_t link_label_;
  Bandwidth bw_link_{Bandwidth::Zero()};
  Bandwidth bw_used_{Bandwidth::Zero()};
  TimeDelta rtt_{TimeDelta::PositiveInf()};
  LinkMedia media_{LinkMedia::Unknown};
};

std::ostream& operator<<(std::ostream& out, const LinkMetrics& m);

class NodeMetrics {
 public:
  NodeMetrics(NodeId node_id, uint64_t version)
      : node_id_(node_id), version_(version) {}

  NodeId node_id() const { return node_id_; }
  uint64_t version() const { return version_; }
  uint8_t battery_pct() const { return battery_pct_; }
  TimeDelta forwarding_time() const { return forwarding_time_; }

  void set_battery_pct(uint8_t battery_pct) { battery_pct_ = battery_pct; }
  void set_forwarding_time(TimeDelta forwarding_time) {
    forwarding_time_ = forwarding_time;
  }

 private:
  NodeId node_id_;
  uint64_t version_;
  uint8_t battery_pct_ = 100;
  TimeDelta forwarding_time_{TimeDelta::PositiveInf()};
};

std::ostream& operator<<(std::ostream& out, const LinkMetrics& m);

class RoutingTable {
 public:
  RoutingTable(NodeId root_node, Timer* timer, bool allow_threading)
      : root_node_(root_node),
        timer_(timer),
        allow_threading_(allow_threading) {}
  ~RoutingTable();

  static constexpr TimeDelta EntryExpiry() { return TimeDelta::FromMinutes(5); }

  using SelectedLinks = std::unordered_map<NodeId, uint64_t>;

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
    bool queued = false;
    InternalListNode<Node> path_finding_node;
  };

  void RemoveOutgoingLinks(Node& node);

  std::unordered_map<NodeId, Node> node_metrics_;
  std::unordered_map<routing_table_impl::FullLinkLabel, Link> link_metrics_;

  uint64_t selected_links_version_ = 0;
  SelectedLinks selected_links_;
  uint64_t published_links_version_ = 0;
};

}  // namespace overnet
