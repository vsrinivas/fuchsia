// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "routing_table.h"
#include <iostream>
#include <unordered_set>
#include "garnet/lib/overnet/protocol/varint.h"

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

void RoutingTable::Update(std::vector<NodeMetrics> node_metrics,
                          std::vector<LinkMetrics> link_metrics,
                          bool flush_old_nodes) {
  if (node_metrics.empty() && link_metrics.empty() && !flush_old_nodes)
    return;
  std::unique_lock<std::mutex> lock(mu_);
  last_update_ = timer_->Now();
  const bool was_empty = change_log_.Empty() && !flush_requested_;
  if (flush_old_nodes)
    flush_requested_ = true;
#ifndef NDEBUG
  for (const auto& m : node_metrics) {
    assert(m.version() != 0 || m.created_locally());
  }
  for (const auto& m : link_metrics) {
    assert(m.version() != 0);
  }
#endif
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
    OVERNET_TRACE(DEBUG) << "APPLY: " << m;
    if (m.node_id() == root_node_ && !m.created_locally()) {
      OVERNET_TRACE(WARNING) << "Dropping node update received to self: " << m;
      continue;
    }
    auto it = node_metrics_.find(m.node_id());
    if (it == node_metrics_.end()) {
      if (m.version() != METRIC_VERSION_TOMBSTONE) {
        new_gossip_version = true;
        node_metrics_.emplace(std::piecewise_construct,
                              std::forward_as_tuple(m.node_id()),
                              std::forward_as_tuple(now, m));
      }
    } else if (m.version() > it->second.metrics.version()) {
      new_gossip_version = true;
      it->second.metrics = m;
      it->second.last_updated = now;
    }
  }
  for (const auto& m : changes.link_metrics) {
    OVERNET_TRACE(DEBUG) << "APPLY: " << m;
    auto report_drop = [&m](const char* why) {
      OVERNET_TRACE(INFO) << "Drop link info: from=" << m.from()
                          << " to=" << m.to() << " label=" << m.link_label()
                          << " version=" << m.version() << ": " << why;
    };
    if (m.from() == root_node_ && !m.created_locally()) {
      OVERNET_TRACE(WARNING)
          << "Dropping link update for link owned by self: " << m;
      continue;
    }
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

    // Add the link.
    const FullLinkLabel key = {m.from(), m.to(), m.link_label()};
    auto it = link_metrics_.find(key);
    if (it == link_metrics_.end() && m.version() != METRIC_VERSION_TOMBSTONE) {
      new_gossip_version = true;
      it = link_metrics_
               .emplace(std::piecewise_construct, std::forward_as_tuple(key),
                        std::forward_as_tuple(now, m, &to_node->second))
               .first;
      from_node->second.outgoing_links.PushBack(&it->second);
    } else if (m.version() > it->second.metrics.version()) {
      new_gossip_version = true;
      it->second.metrics = m;
      it->second.last_updated = now;
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
    std::vector<NodeMetrics> publish_node_metrics;
    std::vector<LinkMetrics> publish_link_metrics;
    for (const auto& np : node_metrics_) {
      publish_node_metrics.push_back(np.second.metrics);
    }
    for (const auto& lp : link_metrics_) {
      publish_link_metrics.push_back(lp.second.metrics);
    }
    std::lock_guard<std::mutex> mutex(shared_table_mu_);
    gossip_version_++;
    shared_node_metrics_.swap(publish_node_metrics);
    shared_link_metrics_.swap(publish_link_metrics);
  }
}

void RoutingTable::RemoveOutgoingLinks(Node& node) {
  while (Link* link = node.outgoing_links.PopFront()) {
    link_metrics_.erase(FullLinkLabel{link->metrics.from(), link->metrics.to(),
                                      link->metrics.link_label()});
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
    OVERNET_TRACE(DEBUG) << "VISIT: " << src->metrics;
    src->queued = false;
    for (auto link : src->outgoing_links) {
      OVERNET_TRACE(DEBUG) << "VISIT_LINK: " << link->metrics;
      if (link->metrics.version() == METRIC_VERSION_TOMBSTONE)
        continue;
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
        dst->mss = std::min(src->mss, link->metrics.mss());
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
    selected_links[node_it->first] =
        SelectedLink{link->metrics.link_label(), n->mss};
  }

  return selected_links;
}

namespace {
enum class Key : uint64_t {
  NODE = 1,
  LINK = 2,
  VERSION = 3,

  FORWARDING_TIME_US = 10,
  DESCRIPTION = 11,

  LABEL = 49,
  BW_LINK = 50,
  BW_USED = 51,
  RTT = 52,
  MSS = 53,
};
}

std::pair<Slice, uint64_t> RoutingTable::Write(
    Border desired_border, Optional<NodeId> exclude_node) const {
  struct Segment {
    Segment(Key key, NodeId id)
        : key(key),
          length(id.wire_length()),
          write([id](uint8_t* p) { id.Write(p); }) {}
    Segment(Key key, std::pair<NodeId, NodeId> p)
        : key(key),
          length(p.first.wire_length() + p.second.wire_length()),
          write([p](uint8_t* out) { p.second.Write(p.first.Write(out)); }) {}
    Segment(Key key, uint64_t x)
        : key(key),
          length(varint::WireSizeFor(x)),
          write([x, length = length](uint8_t* p) {
            varint::Write(x, length, p);
          }) {}
    Segment(Key key, Slice value)
        : key(key),
          length(value.length()),
          write([value = std::move(value)](uint8_t* p) {
            memcpy(p, value.begin(), value.length());
          }) {}
    Key key;
    uint8_t key_length = varint::WireSizeFor(static_cast<uint64_t>(key));
    uint32_t length;
    uint8_t length_length = varint::WireSizeFor(length);
    std::function<void(uint8_t*)> write;
  };

  std::vector<Segment> segments;

  uint64_t version;

  {
    std::unordered_set<NodeId> version_zero_nodes;

    std::lock_guard<std::mutex> mutex(shared_table_mu_);
    version = gossip_version_;

    for (const auto& m : shared_node_metrics_) {
      if (m.version() == 0) {
        version_zero_nodes.insert(m.node_id());
        continue;
      }
      if (m.node_id() == exclude_node)
        continue;
      OVERNET_TRACE(DEBUG) << "WRITE: " << m;
      segments.emplace_back(Key::NODE, m.node_id());
      segments.emplace_back(Key::VERSION, m.version());
      if (m.forwarding_time() != TimeDelta::PositiveInf()) {
        segments.emplace_back(Key::FORWARDING_TIME_US,
                              m.forwarding_time().as_us());
      }
      if (m.description().length() != 0) {
        segments.emplace_back(Key::DESCRIPTION, m.description());
      }
    }

    for (const auto& m : shared_link_metrics_) {
      if (m.from() == exclude_node || version_zero_nodes.count(m.from()) > 0 ||
          version_zero_nodes.count(m.to()) > 0)
        continue;
      OVERNET_TRACE(DEBUG) << "WRITE: " << m;
      segments.emplace_back(Key::LINK, std::make_pair(m.from(), m.to()));
      segments.emplace_back(Key::VERSION, m.version());
      segments.emplace_back(Key::LABEL, m.link_label());
      if (m.bw_link() != Bandwidth::Zero()) {
        segments.emplace_back(Key::BW_LINK, m.bw_link().bits_per_second());
      }
      if (m.bw_used() != Bandwidth::Zero()) {
        segments.emplace_back(Key::BW_USED, m.bw_used().bits_per_second());
      }
      if (m.rtt() != TimeDelta::PositiveInf()) {
        segments.emplace_back(Key::RTT, m.rtt().as_us());
      }
      if (m.mss() != std::numeric_limits<uint32_t>::max()) {
        segments.emplace_back(Key::MSS, m.mss());
      }
    }
  }

  size_t slice_length = 0;
  for (const auto& seg : segments) {
    slice_length += seg.key_length;
    slice_length += seg.length_length;
    slice_length += seg.length;
  }

  return std::make_pair(
      Slice::WithInitializerAndBorders(
          slice_length, desired_border,
          [&](uint8_t* out) {
            for (const auto& seg : segments) {
              out = varint::Write(static_cast<uint64_t>(seg.key),
                                  seg.key_length, out);
              out = varint::Write(seg.length, seg.length_length, out);
              seg.write(out);
              out += seg.length;
            }
          }),
      version);
}

Status RoutingTable::Parse(Slice update, std::vector<NodeMetrics>* node_metrics,
                           std::vector<LinkMetrics>* link_metrics) const {
  const uint8_t* p = update.begin();
  const uint8_t* const end = update.end();
  NodeMetrics* adding_node = nullptr;
  LinkMetrics* adding_link = nullptr;
  uint64_t key;
  uint64_t length;
  auto read_node = [&](const char* what) -> StatusOr<NodeId> {
    uint64_t id;
    if (!ParseLE64(&p, end, &id)) {
      return StatusOr<NodeId>(StatusCode::FAILED_PRECONDITION, what);
    }
    return NodeId(id);
  };
  auto read_uint64 = [&](const char* what) -> StatusOr<uint64_t> {
    uint64_t x;
    if (!varint::Read(&p, end, &x)) {
      return StatusOr<uint64_t>(StatusCode::FAILED_PRECONDITION, what);
    }
    return x;
  };
  auto read_slice = [&](const char* what) -> Slice {
    auto out = update.FromPointer(p).ToOffset(length);
    p += length;
    return out;
  };
  auto read_bandwidth = [&](const char* what) -> StatusOr<Bandwidth> {
    return read_uint64(what).Then([](uint64_t x) {
      return StatusOr<Bandwidth>(Bandwidth::FromBitsPerSecond(x));
    });
  };
  auto read_time_delta = [&](const char* what) -> StatusOr<TimeDelta> {
    return read_uint64(what).Then([](uint64_t x) {
      return StatusOr<TimeDelta>(TimeDelta::FromMicroseconds(x));
    });
  };
  auto read_uint32 = [&](const char* what) -> StatusOr<uint32_t> {
    return read_uint64(what).Then([](uint64_t x) {
      if (x > std::numeric_limits<uint32_t>::max()) {
        return StatusOr<uint32_t>(StatusCode::FAILED_PRECONDITION,
                                  "Out of range");
      }
      return StatusOr<uint32_t>(static_cast<uint32_t>(x));
    });
  };
  auto read_node_pair =
      [&](const char* what) -> StatusOr<std::pair<NodeId, NodeId>> {
    return read_node(what).Then([&](NodeId n) {
      return read_node(what).Then(
          [n](NodeId m) -> StatusOr<std::pair<NodeId, NodeId>> {
            return std::make_pair(n, m);
          });
    });
  };
  auto with_link = [&adding_link](auto setter) {
    return [&adding_link, setter](auto x) {
      if (!adding_link)
        return Status(StatusCode::FAILED_PRECONDITION,
                      "Property only on links");
      (adding_link->*setter)(x);
      return Status::Ok();
    };
  };
  auto with_node = [&adding_node](auto setter) {
    return [&adding_node, setter](auto x) {
      if (!adding_node)
        return Status(StatusCode::FAILED_PRECONDITION,
                      "Property only on nodes");
      (adding_node->*setter)(x);
      return Status::Ok();
    };
  };
  while (p != end) {
    if (!varint::Read(&p, end, &key) || !varint::Read(&p, end, &length)) {
      return Status(StatusCode::FAILED_PRECONDITION,
                    "Failed to read segment header");
    }
    if (uint64_t(end - p) < length) {
      return Status(StatusCode::FAILED_PRECONDITION, "Short segment");
    }
    Status status = Status::Ok();
    const uint8_t* const next = p + length;
    switch (static_cast<Key>(key)) {
      case Key::NODE:
        status = read_node("Node::Id").Then([&](NodeId n) {
          adding_link = nullptr;
          node_metrics->emplace_back();
          adding_node = &node_metrics->back();
          adding_node->set_node_id(n);
          return Status::Ok();
        });
        break;
      case Key::LINK:
        status = read_node_pair("Link::Id").Then([&](auto p) {
          adding_node = nullptr;
          link_metrics->emplace_back();
          adding_link = &link_metrics->back();
          adding_link->set_from_to(p.first, p.second);
          return Status::Ok();
        });
        break;
      case Key::VERSION:
        status = read_uint64("Version").Then([&](uint64_t v) {
          if (v == 0) {
            return Status(StatusCode::INVALID_ARGUMENT,
                          "Zero version number is not allowed");
          }
          if (adding_link)
            adding_link->set_version(v);
          if (adding_node)
            adding_node->set_version(v);
          return Status::Ok();
        });
        break;
      case Key::FORWARDING_TIME_US:
        status = read_time_delta("ForwardingTime")
                     .Then(with_node(&NodeMetrics::set_forwarding_time));
        break;
      case Key::DESCRIPTION:
        status =
            with_node(&NodeMetrics::set_description)(read_slice("Description"));
        break;
      case Key::LABEL:
        status =
            read_uint64("Label").Then(with_link(&LinkMetrics::set_link_label));
        break;
      case Key::BW_LINK:
        status =
            read_bandwidth("BWLink").Then(with_link(&LinkMetrics::set_bw_link));
        break;
      case Key::BW_USED:
        status =
            read_bandwidth("BWUsed").Then(with_link(&LinkMetrics::set_bw_link));
        break;
      case Key::RTT:
        status = read_time_delta("RTT").Then(with_link(&LinkMetrics::set_rtt));
        break;
      case Key::MSS:
        status = read_uint32("MSS").Then(with_link(&LinkMetrics::set_mss));
        break;
      default:
        // Unknown field: skip it.
        OVERNET_TRACE(DEBUG) << "Skipping unknown routing table key " << key;
        p = next;
        break;
    }
    if (status.is_error()) {
      return status;
    }
    if (p != next) {
      return Status(StatusCode::FAILED_PRECONDITION,
                    "Length mismatch reading segment");
    }
    p = next;
  }
  // Verify everythings ok
  for (const auto& m : *node_metrics) {
    if (m.version() == 0) {
      return Status(StatusCode::INVALID_ARGUMENT,
                    "Bad node metric: no version number");
    }
  }
  for (const auto& m : *link_metrics) {
    if (m.version() == 0) {
      return Status(StatusCode::INVALID_ARGUMENT,
                    "Bad link metric: no version number");
    }
  }
  return Status::Ok();
}

Status RoutingTable::Read(Slice update) {
  OVERNET_TRACE(DEBUG) << "READ: " << update;
  std::vector<NodeMetrics> node_metrics;
  std::vector<LinkMetrics> link_metrics;
  return Parse(std::move(update), &node_metrics, &link_metrics).Then([&] {
    Update(std::move(node_metrics), std::move(link_metrics), true);
    return Status::Ok();
  });
}

std::ostream& operator<<(std::ostream& out, const LinkMetrics& m) {
  return out << "LINK{" << m.from() << "->" << m.to() << "/" << m.link_label()
             << " @ " << m.version() << "; bw_link=" << m.bw_link()
             << ", bw_used=" << m.bw_used() << ", rtt=" << m.rtt()
             << ", mss=" << m.mss() << "}";
}

std::ostream& operator<<(std::ostream& out, const NodeMetrics& m) {
  return out << "NODE{" << m.node_id() << " @ " << m.version()
             << "; forwarding_time=" << m.forwarding_time()
             << ", description=" << m.description() << "}";
}

}  // namespace overnet
