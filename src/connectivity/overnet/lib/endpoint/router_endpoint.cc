// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/connectivity/overnet/lib/endpoint/router_endpoint.h"

#include <iostream>
#include <memory>

#include "garnet/public/lib/fostr/fidl/fuchsia/overnet/protocol/formatting.h"
#include "src/connectivity/overnet/lib/protocol/coding.h"
#include "src/connectivity/overnet/lib/protocol/fidl.h"

namespace overnet {

void RouterEndpoint::NewStream::Fail(const Status& status) {
  auto* s = new Stream(std::move(*this));
  s->Close(status, [s] { delete s; });
}

RouterEndpoint::RouterEndpoint(Timer* timer, NodeId node_id,
                               bool allow_non_determinism)
    : Router(timer, node_id, allow_non_determinism) {
  StartGossipTimer();
}

RouterEndpoint::~RouterEndpoint() { assert(connection_streams_.empty()); }

void RouterEndpoint::StartGossipTimer() {
  Timer* timer = this->timer();
  gossip_timer_.Reset(
      timer, timer->Now() + gossip_interval_, [this](const Status& status) {
        if (status.is_error())
          return;
        auto node = SelectGossipPeer();
        if (!node) {
          gossip_interval_ =
              std::min(3 * gossip_interval_ / 2, TimeDelta::FromMinutes(30));
        } else {
          gossip_interval_ = InitialGossipInterval();
          SendGossipTo(*node);
        }
        StartGossipTimer();
      });
}

void RouterEndpoint::SendGossipTo(NodeId target) {
  // Are we still gossiping?
  if (!gossip_timer_.get()) {
    OVERNET_TRACE(DEBUG) << node_id() << " send gossip to " << target
                         << " [skipped: STOPPED GOSSIPING]";
    return;
  }
  auto con = connection_streams_.find(target);
  if (con == connection_streams_.end() || con->second.IsClosedForSending()) {
    OVERNET_TRACE(DEBUG) << node_id() << " send gossip to " << target
                         << " [skipped: NO CONNECTION]";
    return;
  }
  OVERNET_TRACE(DEBUG) << node_id() << " send gossip to " << target;
  SendGossipUpdate(con->second.proxy(), target);
}

void RouterEndpoint::Close(Callback<void> done) {
  closing_ = true;
  gossip_timer_.Reset();
  description_timer_.Reset();
  if (connection_streams_.empty()) {
    Router::Close(std::move(done));
    return;
  }
  auto it = connection_streams_.begin();
  OVERNET_TRACE(DEBUG) << "Closing peer " << it->first;
  Callback<void> after_close(
      ALLOCATED_CALLBACK, [this, it, done = std::move(done)]() mutable {
        OVERNET_TRACE(DEBUG) << "Closed peer " << it->first;
        connection_streams_.erase(it);
        NewNodeDescriptionTableVersion();
        Close(std::move(done));
      });
  it->second.Close(Status::Cancelled(), std::move(after_close));
}

void RouterEndpoint::RegisterPeer(NodeId peer) {
  GetOrCreateConnectionStream(peer);
}

void RouterEndpoint::Bind(Service* service) {
  if (services_.emplace(service->fully_qualified_name, service).second) {
    UpdatedDescription();
  }
}

void RouterEndpoint::Unbind(Service* service) {
  auto it = services_.find(service->fully_qualified_name);
  if (it != services_.end() && it->second == service) {
    services_.erase(it);
    UpdatedDescription();
  }
}

void RouterEndpoint::UpdatedDescription() {
  // Send out a new description to all peers (after a brief delay)
  if (description_timer_.has_value() || closing_) {
    return;
  }
  OVERNET_TRACE(DEBUG) << "Schedule send update";
  description_timer_.Reset(
      timer(), timer()->Now() + TimeDelta::FromMilliseconds(200),
      [this](const Status& status) {
        if (status.is_error()) {
          return;
        }
        description_timer_.Reset();
        auto description = BuildDescription();
        for (auto& id_conn_pair : connection_streams_) {
          if (id_conn_pair.second.IsClosedForSending()) {
            OVERNET_TRACE(DEBUG) << node_id() << " skip sending description to "
                                 << id_conn_pair.first << ": Closing";
            continue;
          }
          OVERNET_TRACE(DEBUG)
              << node_id() << " send description to " << id_conn_pair.first
              << ": " << BuildDescription();
          id_conn_pair.second.proxy()->UpdateNodeDescription(
              fidl::Clone(description));
        }
      });
}

fuchsia::overnet::protocol::PeerDescription RouterEndpoint::BuildDescription()
    const {
  fuchsia::overnet::protocol::PeerDescription desc;
  for (const auto& str_svc_pair : services_) {
    desc.mutable_services()->push_back(str_svc_pair.first);
  }
  return desc;
}

RouterEndpoint::ConnectionStream* RouterEndpoint::GetOrCreateConnectionStream(
    NodeId peer) {
  assert(peer != node_id());
  auto it = connection_streams_.find(peer);
  if (it != connection_streams_.end()) {
    return &it->second;
  }
  if (closing_) {
    OVERNET_TRACE(DEBUG) << node_id()
                         << " skip creating connection stream for peer " << peer
                         << " as we're closing";
    return nullptr;
  }
  OVERNET_TRACE(DEBUG) << node_id() << " creating connection stream for peer "
                       << peer;
  auto* stream =
      &connection_streams_
           .emplace(std::piecewise_construct, std::forward_as_tuple(peer),
                    std::forward_as_tuple(this, peer))
           .first->second;
  stream->Register();
  if (!description_timer_.has_value()) {
    // Send a description (if there's a description timer then it'll anyway be
    // sent soon, so skip)
    OVERNET_TRACE(DEBUG) << node_id() << " send description to " << peer << ": "
                         << BuildDescription();
    stream->proxy()->UpdateNodeDescription(BuildDescription());
  }
  NewNodeDescriptionTableVersion();
  return stream;
}

RouterEndpoint::Stream::Stream(NewStream introduction)
    : DatagramStream(introduction.creator_, introduction.peer_,
                     introduction.reliability_and_ordering_,
                     introduction.stream_id_) {
  auto it = introduction.creator_->connection_streams_.find(introduction.peer_);
  if (it == introduction.creator_->connection_streams_.end()) {
    OVERNET_TRACE(DEBUG) << "Failed to find connection " << introduction.peer_;
    Close(Status(StatusCode::FAILED_PRECONDITION,
                 "Connection closed before stream creation"),
          Callback<void>::Ignored());
  } else {
    connection_stream_ = &it->second;
    connection_stream_->forked_streams_.PushBack(this);
  }
  introduction.creator_ = nullptr;
  Register();
}

void RouterEndpoint::Stream::Close(const Status& status,
                                   Callback<void> quiesced) {
  if (connection_stream_ != nullptr) {
    connection_stream_->forked_streams_.Remove(this);
    connection_stream_ = nullptr;
  }
  DatagramStream::Close(status, std::move(quiesced));
}

RouterEndpoint::ConnectionStream::ConnectionStream(RouterEndpoint* endpoint,
                                                   NodeId peer)
    : DatagramStream(
          endpoint, peer,
          fuchsia::overnet::protocol::ReliabilityAndOrdering::ReliableUnordered,
          StreamId(0)),
      endpoint_(endpoint),
      next_stream_id_(peer < endpoint->node_id() ? 2 : 1),
      proxy_(this),
      stub_(this) {
  BeginReading();
}

RouterEndpoint::ConnectionStream::~ConnectionStream() {}

void RouterEndpoint::ConnectionStream::SendFidl(fidl::Message message) {
  auto slice = Encode(Slice::FromContainer(message.bytes()));
  if (slice.is_error()) {
    OVERNET_TRACE(ERROR) << "Failed to encode connection stream message: "
                         << slice.AsStatus();
  }
  SendOp(this, slice->length())
      .Push(std::move(*slice), Callback<void>::Ignored());
}

void RouterEndpoint::ConnectionStream::BeginReading() {
  reader_.Reset(this);
  reader_->PullAll(StatusOrCallback<Optional<std::vector<Slice>>>(
      [this](StatusOr<Optional<std::vector<Slice>>>&& read_status) {
        if (read_status.is_error()) {
          Close(read_status.AsStatus(), Callback<void>::Ignored());
          return;
        } else if (!read_status->has_value()) {
          Close(Status::Ok(), Callback<void>::Ignored());
          return;
        }

        auto bytes =
            Decode(Slice::Join((*read_status)->begin(), (*read_status)->end()));
        if (bytes.is_error()) {
          Close(bytes.AsStatus(), Callback<void>::Ignored());
          return;
        }
        auto process_with = [&bytes](auto& with) {
          std::vector<uint8_t> copy(bytes->begin(), bytes->end());
          return with.Process_(fidl::Message(
              fidl::BytePart(copy.data(), copy.size(), copy.size()),
              fidl::HandlePart(nullptr, 0)));
        };

        auto status = process_with(proxy_);
        if (status == ZX_ERR_NOT_SUPPORTED) {
          status = process_with(stub_);
        }
        if (status != ZX_OK) {
          OVERNET_TRACE(ERROR) << "Failed to process message: " << bytes
                               << " ; coded: " << *read_status;
        }

        BeginReading();
      }));
}

StatusOr<RouterEndpoint::NewStream> RouterEndpoint::InitiateStream(
    NodeId peer,
    fuchsia::overnet::protocol::ReliabilityAndOrdering reliability_and_ordering,
    const std::string& service_name) {
  return GetOrCreateConnectionStream(peer)->Fork(reliability_and_ordering,
                                                 service_name);
}

StatusOr<RouterEndpoint::NewStream> RouterEndpoint::Stream::InitiateFork(
    fuchsia::overnet::protocol::ReliabilityAndOrdering
        reliability_and_ordering) {
  if (connection_stream_ == nullptr) {
    return StatusOr<NewStream>(StatusCode::FAILED_PRECONDITION,
                               "Closed stream");
  }
  return connection_stream_->MakeFork(reliability_and_ordering);
}

void RouterEndpoint::ConnectionStream::Close(const Status& status,
                                             Callback<void> quiesced) {
  if (status.is_error()) {
    OVERNET_TRACE(ERROR) << "Connection to " << peer()
                         << " closed with error: " << status;
  }
  if (!closing_status_) {
    closing_status_.Reset(status);
  }
  reader_.Reset();
  if (forked_streams_.Empty()) {
    DatagramStream::Close(status, std::move(quiesced));
  } else {
    forked_streams_.Front()->Close(
        status,
        Callback<void>(ALLOCATED_CALLBACK,
                       [this, status, quiesced{std::move(quiesced)}]() mutable {
                         this->Close(status, std::move(quiesced));
                       }));
  }
  assert(quiesced.empty());
}

StatusOr<RouterEndpoint::NewStream> RouterEndpoint::ConnectionStream::MakeFork(
    fuchsia::overnet::protocol::ReliabilityAndOrdering
        reliability_and_ordering) {
  if (closing_status_) {
    return *closing_status_;
  }

  StreamId id(next_stream_id_);
  next_stream_id_ += 2;

  return MakeFork(id, reliability_and_ordering);
}

StatusOr<RouterEndpoint::NewStream> RouterEndpoint::ConnectionStream::MakeFork(
    StreamId id, fuchsia::overnet::protocol::ReliabilityAndOrdering
                     reliability_and_ordering) {
  return NewStream{endpoint_, peer(), reliability_and_ordering, id};
}

StatusOr<RouterEndpoint::NewStream> RouterEndpoint::ConnectionStream::Fork(
    fuchsia::overnet::protocol::ReliabilityAndOrdering reliability_and_ordering,
    std::string service_name) {
  auto outgoing_fork = MakeFork(reliability_and_ordering);
  if (outgoing_fork.is_error()) {
    return outgoing_fork.AsStatus();
  }

  proxy_.ConnectToService(std::move(service_name),
                          outgoing_fork->stream_id().as_fidl());
  return outgoing_fork;
}

StatusOr<RouterEndpoint::NewStream> RouterEndpoint::Stream::ReceiveFork(
    fuchsia::overnet::protocol::StreamId stream_id,
    fuchsia::overnet::protocol::ReliabilityAndOrdering
        reliability_and_ordering) {
  if (connection_stream_ == nullptr) {
    return StatusOr<NewStream>(StatusCode::FAILED_PRECONDITION,
                               "Closed stream");
  }
  return connection_stream_->MakeFork(stream_id, reliability_and_ordering);
}

void RouterEndpoint::OnUnknownStream(NodeId node_id, StreamId stream_id) {
  if (stream_id == StreamId(0)) {
    GetOrCreateConnectionStream(node_id);
  }
}

void RouterEndpoint::ConnectionStream::Stub::ConnectToService(
    std::string service_name, fuchsia::overnet::protocol::StreamId stream_id) {
  auto new_stream = connection_stream_->MakeFork(
      stream_id,
      fuchsia::overnet::protocol::ReliabilityAndOrdering::ReliableOrdered);
  if (new_stream.is_error()) {
    OVERNET_TRACE(ERROR) << "Failed to process ConnectToService NewStream: "
                         << new_stream.AsStatus();
    return;
  }
  if (auto it = connection_stream_->endpoint_->services_.find(service_name);
      it != connection_stream_->endpoint_->services_.end()) {
    it->second->AcceptStream(std::move(*new_stream));
  } else {
    new_stream->Fail(
        Status(StatusCode::INVALID_ARGUMENT, "Service not supported"));
  }
}

void RouterEndpoint::ConnectionStream::Stub::Ping(PingCallback callback) {
#ifdef __Fuchsia__
  zx_time_t now = 0;
  zx_clock_get_new(ZX_CLOCK_UTC, &now);
  callback(now);
#else
  timespec ts;
  clock_gettime(CLOCK_REALTIME, &ts);
  callback(uint64_t(ts.tv_sec) * 1000000000 + ts.tv_nsec);
#endif
}

void RouterEndpoint::ConnectionStream::Stub::UpdateNodeStatus(
    fuchsia::overnet::protocol::NodeStatus status) {
  auto* const endpoint = connection_stream_->endpoint_;
  OVERNET_TRACE(DEBUG) << "Got: UpdateNodeStatus " << status;
  if (status.id == endpoint->node_id()) {
    connection_stream_->Close(Status(StatusCode::INVALID_ARGUMENT,
                                     "Attempt to set this nodes status"),
                              Callback<void>::Ignored());
    return;
  }
  connection_stream_->endpoint_->RegisterPeer(NodeId(status.id));
  endpoint->ApplyGossipUpdate(std::move(status));
}

void RouterEndpoint::ConnectionStream::Stub::UpdateLinkStatus(
    fuchsia::overnet::protocol::LinkStatus status) {
  auto* const endpoint = connection_stream_->endpoint_;
  OVERNET_TRACE(DEBUG) << "Got: UpdateLinkStatus " << status;
  if (status.from == endpoint->node_id()) {
    connection_stream_->Close(Status(StatusCode::INVALID_ARGUMENT,
                                     "Attempt to set this nodes link status"),
                              Callback<void>::Ignored());
    return;
  }
  endpoint->ApplyGossipUpdate(std::move(status));
}

void RouterEndpoint::ConnectionStream::Stub::UpdateNodeDescription(
    fuchsia::overnet::protocol::PeerDescription description) {
  OVERNET_TRACE(DEBUG) << connection_stream_->endpoint_->node_id()
                       << " update node description for "
                       << connection_stream_->peer() << ": " << description;
  connection_stream_->description_ = std::move(description);
  connection_stream_->endpoint_->NewNodeDescriptionTableVersion();
}

void RouterEndpoint::OnNodeDescriptionTableChange(uint64_t last_seen_version,
                                                  StatusCallback on_change) {
  if (last_seen_version == node_description_table_version_) {
    on_node_description_table_change_.emplace_back(std::move(on_change));
  }
  // else don't store on_change, forcing its destructor to be called, forcing it
  // to be called.
}

void RouterEndpoint::NewNodeDescriptionTableVersion() {
  ++node_description_table_version_;
  for (auto& cb : std::move(on_node_description_table_change_)) {
    cb(Status::Ok());
  }
}

}  // namespace overnet
