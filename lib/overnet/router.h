// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <unordered_map>
#include "callback.h"
#include "closed_ptr.h"
#include "lazy_slice.h"
#include "once_fn.h"
#include "routable_message.h"
#include "routing_table.h"
#include "sink.h"
#include "slice.h"
#include "timer.h"
#include "trace.h"

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
  virtual LinkMetrics GetLinkMetrics() = 0;
};

template <class T = Link>
using LinkPtr = ClosedPtr<T, Link>;
template <class T, class... Args>
LinkPtr<T> MakeLink(Args&&... args) {
  return MakeClosedPtr<T, Link>(std::forward<Args>(args)...);
}

class Router final {
 public:
  class StreamHandler {
   public:
    virtual ~StreamHandler() {}
    virtual void Close(Callback<void> quiesced) = 0;
    virtual void HandleMessage(SeqNum seq, TimeStamp received, Slice data) = 0;
  };

  Router(Timer* timer, TraceSink trace_sink, NodeId node_id,
         bool allow_threading)
      : timer_(timer),
        trace_sink_(trace_sink.Decorate([this](const std::string& msg) {
          std::ostringstream out;
          out << "Router[" << this << "] " << msg;
          return out.str();
        })),
        node_id_(node_id),
        routing_table_(node_id, timer, trace_sink_, allow_threading) {
    UpdateRoutingTable({NodeMetrics(node_id, 0)}, {}, false);
  }

  ~Router();

  void Close(Callback<void> quiesced);

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

  void UpdateRoutingTable(std::vector<NodeMetrics> node_metrics,
                          std::vector<LinkMetrics> link_metrics) {
    UpdateRoutingTable(std::move(node_metrics), std::move(link_metrics), false);
  }

  void BlockUntilNoBackgroundUpdatesProcessing() {
    routing_table_.BlockUntilNoBackgroundUpdatesProcessing();
  }

  // Return true if this router believes a route exists to a particular node.
  bool HasRouteTo(NodeId node_id) {
    return node_id == node_id_ || link_holder(node_id)->link() != nullptr;
  }

  TraceSink trace_sink() const { return trace_sink_; }

 private:
  Timer* const timer_;
  const TraceSink trace_sink_;
  const NodeId node_id_;

  void UpdateRoutingTable(std::vector<NodeMetrics> node_metrics,
                          std::vector<LinkMetrics> link_metrics,
                          bool flush_old_nodes);

  void MaybeStartPollingLinkChanges();
  void MaybeStartFlushingOldEntries();

  void CloseLinks(Callback<void> quiesced);
  void CloseStreams(Callback<void> quiesced);

  class StreamHolder {
   public:
    void HandleMessage(SeqNum seq, TimeStamp received, Slice payload);
    Status SetHandler(StreamHandler* handler);
    Status ClearHandler(StreamHandler* handler);
    void Close(Callback<void> quiesced) {
      if (handler_ != nullptr)
        handler_->Close(std::move(quiesced));
    }
    bool has_handler() { return handler_ != nullptr; }

   private:
    struct Pending {
      SeqNum seq;
      TimeStamp received;
      Slice payload;
    };

    StreamHandler* handler_ = nullptr;
    std::vector<Pending> pending_;
  };

  class LinkHolder {
   public:
    LinkHolder(NodeId target, TraceSink trace_sink)
        : trace_sink_(
              trace_sink.Decorate([this, target](const std::string& msg) {
                std::ostringstream out;
                out << "Link[" << this << ";to=" << target << "] " << msg;
                return out.str();
              })) {}
    void Forward(Message message);
    void SetLink(Link* link, uint32_t path_mss);
    Link* link() { return link_; }
    uint32_t path_mss() { return path_mss_; }

   private:
    const TraceSink trace_sink_;
    Link* link_ = nullptr;
    uint32_t path_mss_ = std::numeric_limits<uint32_t>::max();
    std::vector<Message> pending_;
  };

  LinkHolder* link_holder(NodeId node_id) {
    auto it = links_.find(node_id);
    if (it != links_.end())
      return &it->second;
    return &links_
                .emplace(std::piecewise_construct,
                         std::forward_as_tuple(node_id),
                         std::forward_as_tuple(node_id, trace_sink_))
                .first->second;
  }

  typedef router_impl::LocalStreamId LocalStreamId;

  bool shutting_down_ = false;
  std::unordered_map<uint64_t, LinkPtr<>> owned_links_;

  std::unordered_map<LocalStreamId, StreamHolder> streams_;
  std::unordered_map<NodeId, LinkHolder> links_;

  RoutingTable routing_table_;
  Optional<Timeout> poll_link_changes_timeout_;
  Optional<Timeout> flush_old_nodes_timeout_;
};

}  // namespace overnet
