// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/connectivity/overnet/lib/routing/routing_table.h"

#include <iostream>
#include <unordered_set>

#include "garnet/public/lib/fostr/fidl/fuchsia/overnet/protocol/formatting.h"
#include "src/connectivity/overnet/lib/protocol/varint.h"

using overnet::routing_table_impl::FullLinkLabel;

namespace overnet {

namespace {

template <class T>
void MoveInto(std::initializer_list<T> from, std::vector<T>* to) {
  for (auto& val : from) {
    to->emplace_back(fidl::Clone(val));
  }
}

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

  for (auto& n : nodes_) {
    RemoveOutgoingLinks(&n.second);
  }
}

void RoutingTable::ProcessUpdate(
    std::initializer_list<fuchsia::overnet::protocol::NodeStatus> nodes,
    std::initializer_list<fuchsia::overnet::protocol::LinkStatus> links,
    bool flush_old_nodes) {
  if (nodes.size() == 0 && links.size() == 0 && !flush_old_nodes)
    return;
  std::unique_lock<std::mutex> lock(mu_);
  last_update_ = timer_->Now();
  const bool was_empty = change_log_.Empty() && !flush_requested_;
  if (flush_old_nodes)
    flush_requested_ = true;
  MoveInto(nodes, &change_log_.nodes);
  MoveInto(links, &change_log_.links);
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

void RoutingTable::ApplyChanges(TimeStamp now, const StatusVecs& changes,
                                bool flush) {
  bool new_gossip_version = false;

  // Update all metrics from changelogs.
  for (const auto& m : changes.nodes) {
    auto it = nodes_.find(NodeId(m.id));
    const char* log_verb = "uninteresting";
    if (it == nodes_.end()) {
      if (m.version != fuchsia::overnet::protocol::METRIC_VERSION_TOMBSTONE) {
        new_gossip_version = true;
        nodes_.emplace(std::piecewise_construct,
                       std::forward_as_tuple(NodeId(m.id)),
                       std::forward_as_tuple(now, fidl::Clone(m)));
        log_verb = "new";
      }
    } else if (m.version > it->second.status.version) {
      new_gossip_version = true;
      it->second.status = fidl::Clone(m);
      it->second.last_updated = now;
      log_verb = "updated";
    }
    OVERNET_TRACE(DEBUG) << "NODE UPDATE: " << log_verb << " " << m;
  }
  for (const auto& m : changes.links) {
    auto report_drop = [&m](const char* why) {
      OVERNET_TRACE(INFO) << "Drop link info: from=" << m.from << " to=" << m.to
                          << " label=" << m.local_id << " version=" << m.version
                          << ": " << why;
    };
    // Cannot add a link if the relevant nodes are unknown.
    auto from_node = nodes_.find(NodeId(m.from));
    if (from_node == nodes_.end()) {
      report_drop("from node does not exist in routing table");
      continue;
    }
    auto to_node = nodes_.find(NodeId(m.to));
    if (to_node == nodes_.end()) {
      report_drop("to node does not exist in routing table");
      continue;
    }

    // Add the link.
    const FullLinkLabel key = {NodeId(m.from), NodeId(m.to), m.local_id};
    auto it = links_.find(key);
    if (it == links_.end() &&
        m.version != fuchsia::overnet::protocol::METRIC_VERSION_TOMBSTONE) {
      new_gossip_version = true;
      it = links_
               .emplace(
                   std::piecewise_construct, std::forward_as_tuple(key),
                   std::forward_as_tuple(now, fidl::Clone(m),
                                         &from_node->second, &to_node->second))
               .first;
      from_node->second.outgoing_links.PushBack(&it->second);
      to_node->second.incoming_links.PushBack(&it->second);
      OVERNET_TRACE(DEBUG) << "NEWLINK: " << m;
    } else if (m.version > it->second.status.version) {
      new_gossip_version = true;
      it->second.status = fidl::Clone(m);
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
    for (auto it = nodes_.begin(); it != nodes_.end();) {
      if (it->first != root_node_ &&
          it->second.last_updated >= now + EntryExpiry()) {
        RemoveOutgoingLinks(&it->second);
        RemoveIncomingLinks(&it->second);
        it = nodes_.erase(it);
      } else {
        ++it;
      }
    }
  }

  // Publish out to the application for propagation.
  if (new_gossip_version) {
    std::vector<fuchsia::overnet::protocol::NodeStatus> publish_node_status;
    std::vector<fuchsia::overnet::protocol::LinkStatus> publish_link_status;
    for (const auto& np : nodes_) {
      publish_node_status.push_back(fidl::Clone(np.second.status));
    }
    for (const auto& lp : links_) {
      publish_link_status.push_back(fidl::Clone(lp.second.status));
    }
    std::vector<Callback<void>> notify_callbacks;
    std::lock_guard<std::mutex> mutex(shared_table_mu_);
    gossip_version_++;
    shared_node_status_.swap(publish_node_status);
    shared_link_status_.swap(publish_link_status);
    // Places complete list of notification callbacks into notify_callbacks,
    // which will be destroyed after mutex is released, forcing all callbacks to
    // be called.
    on_node_table_update_.swap(notify_callbacks);
  }
}

void RoutingTable::RemoveOutgoingLinks(Node* node) {
  while (Link* link = node->outgoing_links.PopFront()) {
    link->to_node->incoming_links.Remove(link);
    links_.erase(FullLinkLabel{NodeId(link->status.from),
                               NodeId(link->status.to), link->status.local_id});
  }
}

void RoutingTable::RemoveIncomingLinks(Node* node) {
  while (Link* link = node->incoming_links.PopFront()) {
    link->from_node->outgoing_links.Remove(link);
    links_.erase(FullLinkLabel{NodeId(link->status.from),
                               NodeId(link->status.to), link->status.local_id});
  }
}

RoutingTable::SelectedLinks RoutingTable::BuildForwardingTable() {
  OVERNET_TRACE(DEBUG) << "Rebuilding forwarding table";
  auto node_it = nodes_.find(root_node_);
  if (node_it == nodes_.end()) {
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
      if (link->status.version ==
          fuchsia::overnet::protocol::METRIC_VERSION_TOMBSTONE)
        continue;
      TimeDelta rtt =
          src->best_rtt +
          (src->status.metrics.has_forwarding_time()
               ? TimeDelta::FromMicroseconds(
                     src->status.metrics.forwarding_time())
               : TimeDelta::PositiveInf()) +
          (link->status.metrics.has_rtt()
               ? TimeDelta::FromMicroseconds(link->status.metrics.rtt())
               : TimeDelta::PositiveInf());
      Node* dst = link->to_node;
      // For now we order by RTT.
      if (dst->last_path_finding_run != path_finding_run_ ||
          dst->best_rtt > rtt) {
        dst->last_path_finding_run = path_finding_run_;
        dst->best_rtt = rtt;
        dst->best_from = src;
        dst->best_link = link;
        dst->mss =
            std::min(src->mss, link->status.metrics.has_mss()
                                   ? link->status.metrics.mss()
                                   : std::numeric_limits<uint32_t>::max());
        enqueue(dst);
      }
    }
  }

  SelectedLinks selected_links;

  for (node_it = nodes_.begin(); node_it != nodes_.end(); ++node_it) {
    if (node_it->second.last_path_finding_run != path_finding_run_) {
      continue;  // Unreachable
    }
    if (node_it->first == root_node_) {
      continue;
    }
    Node* n = &node_it->second;
    while (n->best_from->status.id != root_node_) {
      n = n->best_from;
    }
    Link* link = n->best_link;
    assert(link->status.from == root_node_);
    selected_links[node_it->first] =
        SelectedLink{link->status.local_id, n->mss};
  }

  return selected_links;
}

uint64_t RoutingTable::SendUpdate(fuchsia::overnet::protocol::Peer_Proxy* peer,
                                  Optional<NodeId> exclude_node) const {
  OVERNET_TRACE(DEBUG) << "SendUpdate";

  std::lock_guard<std::mutex> mutex(shared_table_mu_);
  std::unordered_set<NodeId> version_zero_nodes;

  for (const auto& m : shared_node_status_) {
    if (m.version == 0) {
      version_zero_nodes.insert(NodeId(m.id));
      continue;
    }
    if (NodeId(m.id) == exclude_node) {
      continue;
    }
    OVERNET_TRACE(DEBUG) << "Send: " << m;
    peer->UpdateNodeStatus(fidl::Clone(m));
  }

  for (const auto& m : shared_link_status_) {
    if (NodeId(m.from) == exclude_node ||
        version_zero_nodes.count(NodeId(m.from)) > 0 ||
        version_zero_nodes.count(NodeId(m.to)) > 0) {
      continue;
    }
    OVERNET_TRACE(DEBUG) << "Send: " << m;
    peer->UpdateLinkStatus(fidl::Clone(m));
  }

  return gossip_version_;
}

}  // namespace overnet
