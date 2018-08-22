// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <random>
#include "node_id.h"
#include "packet_link.h"
#include "slice.h"
#include "timer.h"

namespace overnet {

template <class Address, uint32_t kMSS, class HashAddress = std::hash<Address>,
          class EqAddress = std::equal_to<Address>>
class PacketNub {
 public:
  static constexpr size_t kCallMeMaybeSize = 256;
  static constexpr size_t kHelloSize = 256;
  static constexpr uint64_t kAnnounceResendMillis = 1000;

  PacketNub(Timer* timer, TraceSink trace_sink, NodeId node)
      : timer_(timer),
        trace_sink_(trace_sink.Decorate([this](const std::string& msg) {
          std::ostringstream out;
          out << "Nub[" << this << "] " << msg;
          return out.str();
        })),
        local_node_(node) {}

  virtual void SendTo(Address dest, Slice slice) = 0;
  virtual Router* GetRouter() = 0;
  virtual void Publish(LinkPtr<> link) = 0;

  void Process(TimeStamp received, Address src, Slice slice) {
    // Extract node id and op from slice... this code must be identical with
    // PacketLink.
    const uint8_t* const begin = slice.begin();
    const uint8_t* p = begin;
    const uint8_t* const end = slice.end();

    if (p == end) {
      OVERNET_TRACE(INFO, trace_sink_) << "Short packet received (no op code)";
      return;
    }
    const PacketOp op = static_cast<PacketOp>(*p++);

    while (true) {
      Link* link = link_for(src);
      uint64_t node_id;
      OVERNET_TRACE(DEBUG, link->trace_sink) << " op=" << static_cast<int>(op);
      switch (OpState(op, link->state)) {
        case OpState(PacketOp::Connected, LinkState::AckingHello):
          if (!link->SetState(LinkState::Connected)) {
            links_.erase(src);
            return;
          }
          BecomePublished(src);
          continue;
        case OpState(PacketOp::Connected, LinkState::SemiConnected):
          if (!link->SetState(LinkState::Connected)) {
            links_.erase(src);
            return;
          }
          continue;
        case OpState(PacketOp::Connected, LinkState::Connected):
          if (p == end && link->node_id && *link->node_id < local_node_) {
            // Empty connected packets get reflected to fully advance state
            // machine in the case of connecting when applications are idle.
            SendTo(src, std::move(slice));
          } else {
            link->link->Process(received, std::move(slice));
          }
          return;
        case OpState(PacketOp::CallMeMaybe, LinkState::Initial):
          if (end - begin != kCallMeMaybeSize) {
            OVERNET_TRACE(INFO, link->trace_sink)
                << "Received a mis-sized CallMeMaybe packet";
          } else if (!ParseLE64(&p, end, &node_id)) {
            OVERNET_TRACE(INFO, link->trace_sink)
                << "Failed to parse node id from CallMeMaybe packet";
          } else if (!AllZeros(p, end)) {
            OVERNET_TRACE(INFO, link->trace_sink)
                << "CallMeMaybe padding should be all zeros";
          } else if (NodeId(node_id) == local_node_) {
            OVERNET_TRACE(INFO, link->trace_sink)
                << "CallMeMaybe received for local node";
          } else if (NodeId(node_id) < local_node_) {
            OVERNET_TRACE(INFO, link->trace_sink)
                << "CallMeMaybe received from a smaller numbered node id";
          } else {
            StartHello(src, NodeId(node_id));
          }
          return;
        case OpState(PacketOp::Hello, LinkState::Initial):
        case OpState(PacketOp::Hello, LinkState::Announcing):
          if (end - begin != kHelloSize) {
            OVERNET_TRACE(INFO, link->trace_sink)
                << "Received a mis-sized Hello packet";
          } else if (!ParseLE64(&p, end, &node_id)) {
            OVERNET_TRACE(INFO, link->trace_sink)
                << "Failed to parse node id from Hello packet";
          } else if (!AllZeros(p, end)) {
            OVERNET_TRACE(INFO, link->trace_sink)
                << "Hello padding should be all zeros";
          } else if (local_node_ < NodeId(node_id)) {
            OVERNET_TRACE(INFO, link->trace_sink)
                << "Hello received from a larger numbered node id";
          } else if (NodeId(node_id) == local_node_) {
            OVERNET_TRACE(INFO, link->trace_sink)
                << "Hello received for local node";
          } else {
            StartHelloAck(src, NodeId(node_id));
          }
          return;
        case OpState(PacketOp::HelloAck, LinkState::SayingHello):
          if (end != p) {
            OVERNET_TRACE(INFO, link->trace_sink)
                << "Received a mis-sized HelloAck packet";
          } else {
            // Must BecomePublished *AFTER* the state change, else the link
            // could be dropped immediately during publishing forcing an
            // undetected state change that gets wiped out.
            StartSemiConnected(src);
            BecomePublished(src);
          }
          return;
        default:
          OVERNET_TRACE(INFO, link->trace_sink)
              << "Received packet op " << static_cast<int>(op)
              << " on link state " << static_cast<int>(link->state)
              << ": ignoring.";
          return;
      }
    }
  }

  void Initiate(Address peer, NodeId node) {
    Link* link = link_for(peer);
    OVERNET_TRACE(DEBUG, link->trace_sink) << "Initiate";
    assert(node != local_node_);
    if (link->state == LinkState::Initial) {
      if (node < local_node_) {
        // To avoid duplicating links, we insist that lower indexed nodes
        // initiate the connection.
        StartAnnouncing(peer, node);
      } else {
        StartHello(peer, node);
      }
    }
  }

 private:
  class NubLink final : public PacketLink {
   public:
    NubLink(PacketNub* nub, Address address, NodeId peer)
        : PacketLink(
              nub->GetRouter(),
              nub->trace_sink_.Decorate([address](const std::string& message) {
                std::ostringstream out;
                out << "addr:" << address << " " << message;
                return out.str();
              }),
              peer, kMSS),
          nub_(nub),
          address_(address) {}

    ~NubLink() {
      auto it = nub_->links_.find(address_);
      assert(it != nub_->links_.end());
      assert(it->second.link == this);
      it->second.link = nullptr;
      nub_->links_.erase(it);
    }

    void Emit(Slice packet) { nub_->SendTo(address_, std::move(packet)); }

   private:
    PacketNub* const nub_;
    const Address address_;
  };

  static constexpr bool AllZeros(const uint8_t* begin, const uint8_t* end) {
    for (const uint8_t* p = begin; p != end; ++p) {
      if (*p != 0)
        return false;
    }
    return true;
  }

  enum class PacketOp : uint8_t {
    Connected = 0,
    CallMeMaybe = 1,
    Hello = 2,
    HelloAck = 3,
  };

  enum class LinkState : uint8_t {
    Initial,
    Announcing,
    SayingHello,
    AckingHello,
    SemiConnected,
    Connected,
  };

  struct Link {
    explicit Link(Address addr, TraceSink trace_sink)
        : trace_sink(
              trace_sink.Decorate([this, addr](const std::string& message) {
                std::ostringstream out;
                out << "Link[" << this << ";addr=" << addr
                    << ";node=" << node_id << ";st=" << static_cast<int>(state)
                    << "] " << message;
                return out.str();
              })) {}
    ~Link() { assert(link == nullptr); }

    LinkState state = LinkState::Initial;
    Optional<NodeId> node_id;
    NubLink* link = nullptr;
    int ticks = -1;
    Optional<Timeout> next_timeout;
    const TraceSink trace_sink;

    Optional<int> SetState(LinkState st) {
      OVERNET_TRACE(DEBUG, trace_sink)
          << "SetState to " << static_cast<int>(st);
      next_timeout.Reset();
      if (state != st) {
        state = st;
        ticks = 0;
      } else {
        ticks++;
        if (ticks >= 5)
          return Nothing;
      }
      return ticks;
    }

    Optional<int> SetStateAndMaybeNode(LinkState st, Optional<NodeId> node) {
      if (node) {
        if (node_id) {
          if (*node_id != *node) {
            OVERNET_TRACE(DEBUG, trace_sink) << "Node id changed to " << *node;
            return Nothing;
          }
        } else {
          node_id = *node;
        }
      } else {
        assert(node_id);
      }
      return SetState(st);
    }
  };

  static constexpr uint16_t OpState(PacketOp op, LinkState state) {
    return (static_cast<uint16_t>(op) << 8) | static_cast<uint16_t>(state);
  }

  TimeStamp BackoffForTicks(uint64_t initial_millis, int ticks) {
    assert(initial_millis);
    uint64_t millis = initial_millis;
    for (int i = 0; i <= ticks; i++) {
      millis = 11 * millis / 10;
    }
    assert(millis != initial_millis);
    return timer_->Now() +
           TimeDelta::FromMilliseconds(std::uniform_int_distribution<uint64_t>(
               initial_millis, millis)(rng_));
  }

  Link* link_for(Address address) {
    auto it = links_.find(address);
    if (it != links_.end())
      return &it->second;
    return &links_
                .emplace(std::piecewise_construct,
                         std::forward_as_tuple(address),
                         std::forward_as_tuple(address, trace_sink_))
                .first->second;
  }

  template <class F>
  void StartSimpleState(Address address, Optional<NodeId> node, LinkState state,
                        size_t packet_size, F packet_writer) {
    Link* link = link_for(address);
    const Optional<int> ticks_or_nothing =
        link->SetStateAndMaybeNode(state, node);
    if (!ticks_or_nothing) {
      links_.erase(address);
      return;
    }
    const int ticks = *ticks_or_nothing;
    SendTo(address, Slice::WithInitializer(packet_size, packet_writer));
    link->next_timeout.Reset(
        timer_, BackoffForTicks(kAnnounceResendMillis, ticks),
        StatusCallback(ALLOCATED_CALLBACK, [=](const Status& status) {
          if (status.is_error())
            return;
          StartSimpleState(address, node, state, packet_size, packet_writer);
        }));
  }

  void StartAnnouncing(Address address, NodeId node) {
    StartSimpleState(address, node, LinkState::Announcing, kCallMeMaybeSize,
                     [local_node = local_node_](uint8_t* p) {
                       memset(p, 0, kCallMeMaybeSize);
                       *p++ = static_cast<uint8_t>(PacketOp::CallMeMaybe);
                       p = local_node.Write(p);
                     });
  }

  void StartHello(Address address, NodeId node) {
    StartSimpleState(address, node, LinkState::SayingHello, kHelloSize,
                     [local_node = local_node_](uint8_t* p) {
                       memset(p, 0, kHelloSize);
                       *p++ = static_cast<uint8_t>(PacketOp::Hello);
                       p = local_node.Write(p);
                     });
  }

  void StartHelloAck(Address address, NodeId node) {
    StartSimpleState(address, node, LinkState::AckingHello, 1, [](uint8_t* p) {
      *p = static_cast<uint8_t>(PacketOp::HelloAck);
    });
  }

  void StartSemiConnected(Address address) {
    StartSimpleState(
        address, Nothing, LinkState::SemiConnected, 1,
        [](uint8_t* p) { *p = static_cast<uint8_t>(PacketOp::Connected); });
  }

  void BecomePublished(Address address) {
    Link* link = link_for(address);
    assert(link->link == nullptr);
    assert(link->node_id);
    link->link = new NubLink(this, address, *link->node_id);
    Publish(LinkPtr<>(link->link));
  }

  Timer* const timer_;
  const TraceSink trace_sink_;
  const NodeId local_node_;
  std::unordered_map<Address, Link, HashAddress, EqAddress> links_;
  std::mt19937_64 rng_;
};

}  // namespace overnet
