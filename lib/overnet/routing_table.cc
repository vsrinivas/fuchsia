// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "routing_table.h"
#include <iostream>

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

}  // namespace

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

void RoutingTable::Update(std::vector<NodeMetrics> node_metrics,
                          std::vector<LinkMetrics> link_metrics,
                          bool flush_old_nodes) {
  if (node_metrics.empty() && link_metrics.empty() && !flush_old_nodes) return;
  std::unique_lock<std::mutex> lock(mu_);
  last_update_ = timer_->Now();
  const bool was_empty = change_log_.Empty() && !flush_requested_;
  if (flush_old_nodes) flush_requested_ = true;
  MoveInto(&node_metrics, &change_log_.node_metrics);
  MoveInto(&link_metrics, &change_log_.link_metrics);
  if (!was_empty) return;
  if (processing_changes_) return;
  auto process_changes = [this, changes = std::move(change_log_),
                          flush = flush_requested_,
                          now = last_update_]() mutable {
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
  // Update all metrics from changelogs.
  for (const auto& m : changes.node_metrics) {
    auto it = node_metrics_.find(m.node_id());
    if (it == node_metrics_.end()) {
      node_metrics_.emplace(std::piecewise_construct,
                            std::forward_as_tuple(m.node_id()),
                            std::forward_as_tuple(now, m));
    } else if (m.version() > it->second.metrics.version()) {
      it->second.metrics = m;
      it->second.last_updated = now;
    }
  }
  for (const auto& m : changes.link_metrics) {
    auto report_drop = [&m](const char* why) {
      std::cerr << "Drop link info: from=" << m.from() << " to=" << m.to()
                << " label=" << m.link_label() << " version=" << m.version()
                << ": " << why << std::endl;
    };
    // Cannot add a link if the relevant nodes are unknown.
    auto from_node = node_metrics_.find(m.from());
    if (from_node == node_metrics_.end()) {
      report_drop("from node does not exist in routing table");
      continue;
    }
    auto to_node = node_metrics_.find(m.to());
    if (to_node == node_metrics_.end()) {
      report_drop("to node does not exist in routing table");
      continue;
    }

    // Keep-alive the nodes.
    from_node->second.last_updated = now;
    to_node->second.last_updated = now;

    // Add the link.
    const FullLinkLabel key = {m.from(), m.to(), m.link_label()};
    auto it = link_metrics_.find(key);
    if (it == link_metrics_.end()) {
      it = link_metrics_
               .emplace(std::piecewise_construct, std::forward_as_tuple(key),
                        std::forward_as_tuple(now, m, &to_node->second))
               .first;
      from_node->second.outgoing_links.PushBack(&it->second);
    } else if (m.version() > it->second.metrics.version()) {
      it->second.metrics = m;
      it->second.last_updated = now;
    } else {
      report_drop("old version");
    }
  }

  // Remove anything old if we've been asked to.
  if (flush) {
    for (auto it = node_metrics_.begin(); it != node_metrics_.end();) {
      if (it->first == root_node_) continue;
      if (it->second.last_updated >= now + EntryExpiry()) {
        RemoveOutgoingLinks(it->second);
        it = node_metrics_.erase(it);
      } else {
        ++it;
      }
    }
  }
}

void RoutingTable::RemoveOutgoingLinks(Node& node) {
  while (Link* link = node.outgoing_links.PopFront()) {
    link_metrics_.erase(FullLinkLabel{link->metrics.from(), link->metrics.to(),
                                      link->metrics.link_label()});
  }
}

RoutingTable::SelectedLinks RoutingTable::BuildForwardingTable() {
  auto node_it = node_metrics_.find(root_node_);
  if (node_it == node_metrics_.end()) {
    return SelectedLinks();  // Root node as yet unknown.
  }

  ++path_finding_run_;
  node_it->second.last_path_finding_run = path_finding_run_;
  node_it->second.best_rtt = TimeDelta::Zero();
  InternalList<Node, &Node::path_finding_node> todo;

  auto enqueue = [&todo](Node* node) {
    if (node->queued) return;
    node->queued = true;
    todo.PushBack(node);
  };

  enqueue(&node_it->second);

  while (!todo.Empty()) {
    Node* src = todo.PopFront();
    src->queued = false;
    for (auto link : src->outgoing_links) {
      if (link->metrics.version() == METRIC_VERSION_TOMBSTONE) continue;
      TimeDelta rtt =
          src->best_rtt + src->metrics.forwarding_time() + link->metrics.rtt();
      Node* dst = link->to_node;
      // For now we order by RTT.
      if (dst->last_path_finding_run != path_finding_run_ ||
          dst->best_rtt > rtt) {
        dst->last_path_finding_run = path_finding_run_;
        dst->best_rtt = rtt;
        dst->best_from = src;
        dst->best_link = link;
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
    while (n->best_from->metrics.node_id() != root_node_) {
      n = n->best_from;
    }
    Link* link = n->best_link;
    assert(link->metrics.from() == root_node_);
    selected_links[node_it->first] = link->metrics.link_label();
  }

  return selected_links;
}

}  // namespace overnet
