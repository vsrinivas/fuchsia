// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/overnet/routing/routing_table.h"
#include <iostream>
#include <unordered_set>
#include "garnet/lib/overnet/protocol/varint.h"
#include "garnet/public/lib/fostr/fidl/fuchsia/overnet/protocol/formatting.h"

using overnet::routing_table_impl::FullLinkLabel;

namespace overnet {

namespace {

template <class T>
void MoveInto(std::vector<T>* from, std::vector<T>* to) {
  if (to->empty()) {
    *to = std::move(*from);
  } else {
    for (auto& val : *from) {
      to->emplace_back(std::move(val));
    }
  }
}

TimeDelta UnpackTime(const uint64_t* delta) {
  return delta == nullptr ? TimeDelta::PositiveInf()
                          : TimeDelta::FromMicroseconds(*delta);
};

}  // namespace

RoutingTable::RoutingTable(NodeId root_node, Timer* timer, bool allow_threading)
    : root_node_(root_node), timer_(timer), allow_threading_(allow_threading) {}

RoutingTable::~RoutingTable() {
  std::unique_lock<std::mutex> lock(mu_);
  if (processing_changes_) {
    std::thread pending_processing = std::move(*processing_changes_);
    processing_changes_.Reset();
    cv_.notify_all();
    lock.unlock();
    pending_processing.join();
  }

  for (auto& n : node_metrics_) {
    RemoveOutgoingLinks(n.second);
  }
}

void RoutingTable::ProcessUpdate(
    std::vector<fuchsia::overnet::protocol::NodeMetrics> node_metrics,
    std::vector<fuchsia::overnet::protocol::LinkMetrics> link_metrics,
    bool flush_old_nodes) {
  if (node_metrics.empty() && link_metrics.empty() && !flush_old_nodes)
    return;
  std::unique_lock<std::mutex> lock(mu_);
  last_update_ = timer_->Now();
  const bool was_empty = change_log_.Empty() && !flush_requested_;
  if (flush_old_nodes)
    flush_requested_ = true;
  MoveInto(&node_metrics, &change_log_.node_metrics);
  MoveInto(&link_metrics, &change_log_.link_metrics);
  if (!was_empty)
    return;
  if (processing_changes_)
    return;
  auto process_changes = [this, changes = std::move(change_log_),
                          flush = flush_requested_, now = last_update_,
                          renderer = ScopedRenderer::current(),
                          severity = ScopedSeverity::current()]() mutable {
    ScopedRenderer scoped_renderer(renderer);
    ScopedSeverity scoped_severity(severity);
    while (true) {
      ApplyChanges(now, changes, flush);
      SelectedLinks new_selected_links = BuildForwardingTable();

      // Publish changes. If change-log has grown, restart update.
      std::lock_guard<std::mutex> lock(mu_);
      if (selected_links_ != new_selected_links) {
        selected_links_.swap(new_selected_links);
        selected_links_version_++;
      }
      if (!processing_changes_) {
        // Indicates that the owning RoutingTable instance is in its destruction
        // sequence.
        cv_.notify_all();
        return;
      } else if (change_log_.Empty() && !flush_requested_) {
        processing_changes_->detach();
        processing_changes_.Reset();
        cv_.notify_all();
        return;
      } else {
        changes = std::move(change_log_);
        change_log_.Clear();
        flush = flush_requested_;
        flush_requested_ = false;
        now = last_update_;
      }
    }
  };
  if (allow_threading_) {
    processing_changes_.Reset(std::move(process_changes));
    cv_.notify_all();
  }
  flush_requested_ = false;
  change_log_.Clear();
  if (!allow_threading_) {
    lock.unlock();
    process_changes();
  }
}

void RoutingTable::ApplyChanges(TimeStamp now, const Metrics& changes,
                                bool flush) {
  bool new_gossip_version = false;

  // Update all metrics from changelogs.
  for (const auto& m : changes.node_metrics) {
    auto it = node_metrics_.find(NodeId(m.label()->id));
    const char* log_verb = "uninteresting";
    if (it == node_metrics_.end()) {
      if (m.label()->version !=
          fuchsia::overnet::protocol::METRIC_VERSION_TOMBSTONE) {
        new_gossip_version = true;
        node_metrics_.emplace(std::piecewise_construct,
                              std::forward_as_tuple(NodeId(m.label()->id)),
                              std::forward_as_tuple(now, fidl::Clone(m)));
        log_verb = "new";
      }
    } else if (m.label()->version > it->second.metrics.label()->version) {
      new_gossip_version = true;
      it->second.metrics = fidl::Clone(m);
      it->second.last_updated = now;
      log_verb = "updated";
    }
    OVERNET_TRACE(DEBUG) << "NODE UPDATE: " << log_verb << " " << m;
  }
  for (const auto& m : changes.link_metrics) {
    auto report_drop = [&m](const char* why) {
      OVERNET_TRACE(INFO) << "Drop link info: from=" << m.label()->from
                          << " to=" << m.label()->to
                          << " label=" << m.label()->local_id
                          << " version=" << m.label()->version << ": " << why;
    };
    // Cannot add a link if the relevant nodes are unknown.
    auto from_node = node_metrics_.find(NodeId(m.label()->from));
    if (from_node == node_metrics_.end()) {
      report_drop("from node does not exist in routing table");
      continue;
    }
    auto to_node = node_metrics_.find(NodeId(m.label()->to));
    if (to_node == node_metrics_.end()) {
      report_drop("to node does not exist in routing table");
      continue;
    }

    // Add the link.
    const FullLinkLabel key = {NodeId(m.label()->from), NodeId(m.label()->to),
                               m.label()->local_id};
    auto it = link_metrics_.find(key);
    if (it == link_metrics_.end() &&
        m.label()->version !=
            fuchsia::overnet::protocol::METRIC_VERSION_TOMBSTONE) {
      new_gossip_version = true;
      it = link_metrics_
               .emplace(
                   std::piecewise_construct, std::forward_as_tuple(key),
                   std::forward_as_tuple(now, fidl::Clone(m), &to_node->second))
               .first;
      from_node->second.outgoing_links.PushBack(&it->second);
      OVERNET_TRACE(DEBUG) << "NEWLINK: " << m;
    } else if (m.label()->version > it->second.metrics.label()->version) {
      new_gossip_version = true;
      it->second.metrics = fidl::Clone(m);
      it->second.last_updated = now;
      OVERNET_TRACE(DEBUG) << "UPDATELINK: " << m;
    } else {
      report_drop("old version");
      continue;  // Skip keep-alive.
    }

    // Keep-alive the nodes.
    from_node->second.last_updated = now;
    to_node->second.last_updated = now;
  }

  // Remove anything old if we've been asked to.
  if (flush) {
    for (auto it = node_metrics_.begin(); it != node_metrics_.end();) {
      if (it->first != root_node_ &&
          it->second.last_updated >= now + EntryExpiry()) {
        RemoveOutgoingLinks(it->second);
        it = node_metrics_.erase(it);
      } else {
        ++it;
      }
    }
  }

  // Publish out to the application for propagation.
  if (new_gossip_version) {
    std::vector<fuchsia::overnet::protocol::NodeMetrics> publish_node_metrics;
    std::vector<fuchsia::overnet::protocol::LinkMetrics> publish_link_metrics;
    for (const auto& np : node_metrics_) {
      publish_node_metrics.push_back(fidl::Clone(np.second.metrics));
    }
    for (const auto& lp : link_metrics_) {
      publish_link_metrics.push_back(fidl::Clone(lp.second.metrics));
    }
    std::lock_guard<std::mutex> mutex(shared_table_mu_);
    gossip_version_++;
    shared_node_metrics_.swap(publish_node_metrics);
    shared_link_metrics_.swap(publish_link_metrics);
  }
}

void RoutingTable::RemoveOutgoingLinks(Node& node) {
  while (Link* link = node.outgoing_links.PopFront()) {
    link_metrics_.erase(FullLinkLabel{NodeId(link->metrics.label()->from),
                                      NodeId(link->metrics.label()->to),
                                      link->metrics.label()->local_id});
  }
}

RoutingTable::SelectedLinks RoutingTable::BuildForwardingTable() {
  OVERNET_TRACE(DEBUG) << "Rebuilding forwarding table";
  auto node_it = node_metrics_.find(root_node_);
  if (node_it == node_metrics_.end()) {
    OVERNET_TRACE(DEBUG) << "No root known";
    return SelectedLinks();  // Root node as yet unknown.
  }

  ++path_finding_run_;
  node_it->second.last_path_finding_run = path_finding_run_;
  node_it->second.best_rtt = TimeDelta::Zero();
  node_it->second.mss = std::numeric_limits<uint32_t>::max();
  InternalList<Node, &Node::path_finding_node> todo;

  auto enqueue = [&todo](Node* node) {
    if (node->queued)
      return;
    node->queued = true;
    todo.PushBack(node);
  };

  enqueue(&node_it->second);

  while (!todo.Empty()) {
    Node* src = todo.PopFront();
    src->queued = false;
    for (auto link : src->outgoing_links) {
      if (link->metrics.label()->version ==
          fuchsia::overnet::protocol::METRIC_VERSION_TOMBSTONE)
        continue;
      TimeDelta rtt = src->best_rtt +
                      UnpackTime(src->metrics.forwarding_time()) +
                      UnpackTime(link->metrics.rtt());
      Node* dst = link->to_node;
      // For now we order by RTT.
      if (dst->last_path_finding_run != path_finding_run_ ||
          dst->best_rtt > rtt) {
        dst->last_path_finding_run = path_finding_run_;
        dst->best_rtt = rtt;
        dst->best_from = src;
        dst->best_link = link;
        dst->mss =
            std::min(src->mss, link->metrics.mss()
                                   ? *link->metrics.mss()
                                   : std::numeric_limits<uint32_t>::max());
        enqueue(dst);
      }
    }
  }

  SelectedLinks selected_links;

  for (node_it = node_metrics_.begin(); node_it != node_metrics_.end();
       ++node_it) {
    if (node_it->second.last_path_finding_run != path_finding_run_) {
      continue;  // Unreachable
    }
    if (node_it->first == root_node_) {
      continue;
    }
    Node* n = &node_it->second;
    while (n->best_from->metrics.label()->id != root_node_) {
      n = n->best_from;
    }
    Link* link = n->best_link;
    assert(link->metrics.label()->from == root_node_);
    selected_links[node_it->first] =
        SelectedLink{link->metrics.label()->local_id, n->mss};
  }

  return selected_links;
}

RoutingTable::Update RoutingTable::GenerateUpdate(
    Optional<NodeId> exclude_node) const {
  std::unordered_set<NodeId> version_zero_nodes;
  fuchsia::overnet::protocol::RoutingTableUpdate data;

  std::lock_guard<std::mutex> mutex(shared_table_mu_);

  for (const auto& m : shared_node_metrics_) {
    if (m.label()->version == 0) {
      version_zero_nodes.insert(NodeId(m.label()->id));
      continue;
    }
    if (NodeId(m.label()->id) == exclude_node) {
      continue;
    }
    data.mutable_nodes()->push_back(fidl::Clone(m));
  }

  for (const auto& m : shared_link_metrics_) {
    if (NodeId(m.label()->from) == exclude_node ||
        version_zero_nodes.count(NodeId(m.label()->from)) > 0 ||
        version_zero_nodes.count(NodeId(m.label()->to)) > 0) {
      continue;
    }
    data.mutable_links()->push_back(fidl::Clone(m));
  }

  return Update{gossip_version_, std::move(data)};
}

}  // namespace overnet
