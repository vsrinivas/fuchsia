// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <fbl/ref_counted.h>
#include <fbl/ref_ptr.h>

#include <random>
#include <unordered_map>
#include <unordered_set>

#include "src/connectivity/overnet/lib/environment/timer.h"
#include "src/connectivity/overnet/lib/labels/node_id.h"
#include "src/connectivity/overnet/lib/links/packet_link.h"
#include "src/connectivity/overnet/lib/vocabulary/slice.h"

namespace overnet {

template <class Address, uint32_t kMSS, class HashAddress = std::hash<Address>,
          class EqAddress = std::equal_to<Address>>
class PacketNub {
  enum class PacketOp : uint8_t {
    Connected = 0,
    CallMeMaybe = 1,
    Hello = 2,
    HelloAck = 3,
    GoAway = 4,
  };

  friend std::ostream& operator<<(std::ostream& out, PacketOp op) {
    switch (op) {
      case PacketOp::Connected: {
        return out << "Connected";
      }
      case PacketOp::CallMeMaybe: {
        return out << "CallMeMaybe";
      }
      case PacketOp::Hello: {
        return out << "Hello";
      }
      case PacketOp::HelloAck: {
        return out << "HelloAck";
      }
      case PacketOp::GoAway: {
        return out << "GoAway";
      }
    }
    return out << "UnknownPacketOp(" << static_cast<int>(op) << ")";
  }

  enum class LinkState : uint8_t {
    Initial,
    Announcing,
    SayingHello,
    AckingHello,
    SemiConnected,
    Connected,
  };

  friend std::ostream& operator<<(std::ostream& out, LinkState state) {
    switch (state) {
      case LinkState::Initial: {
        return out << "Initial";
      }
      case LinkState::Announcing: {
        return out << "Announcing";
      }
      case LinkState::SayingHello: {
        return out << "SayingHello";
      }
      case LinkState::AckingHello: {
        return out << "AckingHello";
      }
      case LinkState::SemiConnected: {
        return out << "SemiConnected";
      }
      case LinkState::Connected: {
        return out << "Connected";
      }
    }
    return out << "UnknownState(" << static_cast<int>(state) << ")";
  }

  struct LinkData;
  using LinkDataPtr = fbl::RefPtr<LinkData>;

  class NubLink final : public PacketLink {
   public:
    NubLink(PacketNub* nub, LinkDataPtr link, NodeId peer, uint64_t label)
        : PacketLink(nub->GetRouter(), peer, kMSS, label),
          nub_(nub),
          link_(link) {}

    ~NubLink() { Delist(); }

    void Emit(Slice packet) override {
      if (nub_ == nullptr) {
        return;
      }
      nub_->SendToLink(link_, std::move(packet));
    }

    void Tombstone() override {
      Delist();
      PacketLink::Tombstone();
    }

   private:
    void Delist() {
      if (nub_ == nullptr) {
        return;
      }
      nub_->RemoveLink(link_);
      nub_ = nullptr;
      link_->link = nullptr;
    }

    PacketNub* nub_;
    const LinkDataPtr link_;
  };

  struct LinkData : public fbl::RefCounted<LinkData> {
    ~LinkData() { assert(link == nullptr); }
    LinkData(std::vector<Address> addresses)
        : addresses(std::move(addresses)) {}

    std::vector<Address> addresses;
    Optional<Address> preferred_address;
    LinkState state = LinkState::Initial;
    Optional<NodeId> node_id;
    NubLink* link = nullptr;
    int ticks = -1;
    Optional<Timeout> next_timeout;

    Optional<int> SetState(LinkState st) {
      OVERNET_TRACE(DEBUG) << "SetState for " << AddrVecStr(addresses) << " to "
                           << st;
      next_timeout.Reset();
      if (state != st) {
        state = st;
        ticks = 0;
      } else {
        ticks++;
        if (ticks >= 5) {
          // Don't time out in semi-connected
          if (st == LinkState::SemiConnected) {
            ticks = 5;
          } else {
            return Nothing;
          }
        }
      }
      return ticks;
    }

    Optional<int> SetStateAndMaybeNode(LinkState st, Optional<NodeId> node) {
      if (node) {
        if (node_id) {
          if (*node_id != *node) {
            OVERNET_TRACE(DEBUG) << "Node id changed to " << *node;
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

 public:
  static constexpr inline auto kModule = Module::NUB;

  static constexpr size_t kCallMeMaybeSize = 256;
  static constexpr size_t kHelloSize = 256;
  static constexpr uint64_t kAnnounceResendMillis = 1000;

  PacketNub(Timer* timer, NodeId node) : timer_(timer), local_node_(node) {}
  virtual ~PacketNub() {}

  virtual void SendTo(Address dest, Slice slice) = 0;
  virtual Router* GetRouter() = 0;
  virtual void Publish(LinkPtr<> link) = 0;

  virtual void Process(TimeStamp received, Address src, Slice slice) {
    ScopedModule<PacketNub> in_nub(this);
    // Extract node id and op from slice... this code must be identical with
    // PacketLink.
    const uint8_t* const begin = slice.begin();
    const uint8_t* p = begin;
    const uint8_t* const end = slice.end();

    OVERNET_TRACE(DEBUG) << "INCOMING: from:" << src << " " << received << " "
                         << slice;

    if (p == end) {
      OVERNET_TRACE(INFO) << "Short packet received (no op code)";
      return;
    }
    const PacketOp op = static_cast<PacketOp>(*p++);

    auto op_state = [](PacketOp op, LinkState state) constexpr {
      return (static_cast<uint16_t>(op) << 8) | static_cast<uint16_t>(state);
    };

    while (true) {
      LinkDataPtr link = LinkForIncomingPacket(src);
      if (!link->preferred_address) {
        link->preferred_address = src;
      }
      uint64_t node_id;
      OVERNET_TRACE(DEBUG) << " op=" << op << " state=" << link->state;
      switch (op_state(op, link->state)) {
        case op_state(PacketOp::GoAway, LinkState::AckingHello):
        case op_state(PacketOp::GoAway, LinkState::SemiConnected):
        case op_state(PacketOp::GoAway, LinkState::Connected):
          OVERNET_TRACE(TRACE) << "Forget " << src << " due to goaway";
          if (link->link != nullptr) {
            link->link->Tombstone();
          } else {
            links_.erase(src);
          }
          break;
        case op_state(PacketOp::Connected, LinkState::AckingHello):
          if (!link->SetState(LinkState::Connected)) {
            OVERNET_TRACE(DEBUG)
                << "Forget " << src << " couldn't set connected";
            links_.erase(src);
            return;
          }
          link->next_timeout.Reset();
          BecomePublished(link);
          continue;
        case op_state(PacketOp::Connected, LinkState::SemiConnected):
          if (!link->SetState(LinkState::Connected)) {
            OVERNET_TRACE(DEBUG)
                << "Forget " << src << " couldn't set connected";
            RemoveLink(link);
            return;
          }
          link->next_timeout.Reset();
          continue;
        case op_state(PacketOp::Connected, LinkState::Connected):
          if (p == end && link->node_id && *link->node_id < local_node_) {
            // Empty connected packets get reflected to fully advance state
            // machine in the case of connecting when applications are idle.
            SendToLink(link, std::move(slice));
          } else {
            link->link->Process(received, std::move(slice));
          }
          return;
        case op_state(PacketOp::CallMeMaybe, LinkState::SayingHello):
          if (link->next_timeout.has_value()) {
            OVERNET_TRACE(INFO)
                << "Received packet op " << op << " from " << src
                << " on link state " << link->state << ": ignoring.";
            return;
          }
          [[fallthrough]];
        case op_state(PacketOp::CallMeMaybe, LinkState::Initial):
          if (end - begin != kCallMeMaybeSize) {
            OVERNET_TRACE(INFO) << "Received a mis-sized CallMeMaybe packet";
          } else if (!ParseLE64(&p, end, &node_id)) {
            OVERNET_TRACE(INFO)
                << "Failed to parse node id from CallMeMaybe packet";
          } else if (!AllZeros(p, end)) {
            OVERNET_TRACE(INFO) << "CallMeMaybe padding should be all zeros";
          } else if (NodeId(node_id) == local_node_) {
            OVERNET_TRACE(INFO) << "CallMeMaybe received for local node";
          } else if (NodeId(node_id) < local_node_) {
            OVERNET_TRACE(INFO)
                << "CallMeMaybe received from a smaller numbered node id";
          } else {
            StartHello(link, NodeId(node_id), ResendBehavior::kNever);
          }
          return;
        case op_state(PacketOp::Hello, LinkState::Initial):
        case op_state(PacketOp::Hello, LinkState::AckingHello):
        case op_state(PacketOp::Hello, LinkState::Announcing):
          if (end - begin != kHelloSize) {
            OVERNET_TRACE(INFO) << "Received a mis-sized Hello packet";
          } else if (!ParseLE64(&p, end, &node_id)) {
            OVERNET_TRACE(INFO) << "Failed to parse node id from Hello packet";
          } else if (!AllZeros(p, end)) {
            OVERNET_TRACE(INFO) << "Hello padding should be all zeros";
          } else if (local_node_ < NodeId(node_id)) {
            OVERNET_TRACE(INFO)
                << "Hello received from a larger numbered node id";
          } else if (NodeId(node_id) == local_node_) {
            OVERNET_TRACE(INFO) << "Hello received for local node";
          } else {
            StartHelloAck(link, NodeId(node_id));
          }
          return;
        case op_state(PacketOp::HelloAck, LinkState::SayingHello):
          if (end != p) {
            OVERNET_TRACE(INFO) << "Received a mis-sized HelloAck packet";
          } else {
            // Must BecomePublished *AFTER* the state change, else the link
            // could be dropped immediately during publishing forcing an
            // undetected state change that gets wiped out.
            StartSemiConnected(link);
            BecomePublished(link);
          }
          return;
        case op_state(PacketOp::Connected, LinkState::Initial):
        case op_state(PacketOp::HelloAck, LinkState::Initial):
        case op_state(PacketOp::Connected, LinkState::Announcing):
        case op_state(PacketOp::HelloAck, LinkState::Announcing):
          OVERNET_TRACE(INFO)
              << "Received packet op " << op << " from " << src
              << " on link state " << link->state << ": sending goaway.";
          SendToLink(link, Slice::FromContainer(
                               {static_cast<uint8_t>(PacketOp::GoAway)}));
          return;
        default:
          OVERNET_TRACE(INFO)
              << "Received packet op " << op << " from " << src
              << " on link state " << link->state << ": ignoring.";
          return;
      }
    }
  }

  static std::string AddrVecStr(const std::vector<Address>& addrs) {
    std::ostringstream out;
    bool first = true;
    out << "{";
    for (auto addr : addrs) {
      if (!first) {
        out << ", ";
      }
      first = false;
      out << addr;
    }
    out << "}";
    return out.str();
  }

  void Initiate(std::vector<Address> peer_addresses, NodeId node) {
    ScopedModule<PacketNub> in_nub(this);

    LinkDataPtr link;
    for (auto peer : peer_addresses) {
      if (auto it = links_.find(peer); it != links_.end()) {
        if (!link || link->state < it->second->state) {
          link = it->second;
        }
      }
    }

    if (!link) {
      link = CreateLink(peer_addresses);
    } else {
      for (auto old_addr : link->addresses) {
        links_.erase(old_addr);
      }
      for (auto addr : peer_addresses) {
        links_.emplace(addr, link);
      }
    }

    OVERNET_TRACE(INFO) << "Initiate peer=" << AddrVecStr(peer_addresses)
                        << " node=" << node << " state=" << link->state;
    assert(node != local_node_);
    if (link->state == LinkState::Initial ||
        (link->state == LinkState::SayingHello &&
         !link->next_timeout.has_value() && local_node_ < node)) {
      if (node < local_node_) {
        // To avoid duplicating links, we insist that lower indexed nodes
        // initiate the connection.
        StartAnnouncing(link, node);
      } else {
        StartHello(link, node, ResendBehavior::kResendable);
      }
    }
  }

  bool HasConnectionTo(Address peer) const {
    auto it = links_.find(peer);
    return it != links_.end() && it->second->link != nullptr;
  }

 private:
  static constexpr bool AllZeros(const uint8_t* begin, const uint8_t* end) {
    for (const uint8_t* p = begin; p != end; ++p) {
      if (*p != 0)
        return false;
    }
    return true;
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

  LinkDataPtr LinkForIncomingPacket(Address address) {
    if (auto it = links_.find(address); it != links_.end()) {
      return it->second;
    }
    return CreateLink({address});
  }

  LinkDataPtr CreateLink(std::vector<Address> addrs) {
    auto link = fbl::MakeRefCounted<LinkData>(std::move(addrs));
    for (auto addr : link->addresses) {
      links_.emplace(addr, link);
    }
    return link;
  }

  enum class ResendBehavior {
    kNever,
    kResendable,
  };

  friend std::ostream& operator<<(std::ostream& out, ResendBehavior resend) {
    switch (resend) {
      case ResendBehavior::kNever:
        return out << "Never";
      case ResendBehavior::kResendable:
        return out << "Resendable";
    }
    abort();
  }

  void SendToLink(LinkDataPtr link, Slice slice) {
    if (link->preferred_address) {
      OVERNET_TRACE(DEBUG) << "SendTo addr=" << *link->preferred_address
                           << " slice=" << slice;
      SendTo(*link->preferred_address, std::move(slice));
    } else {
      for (auto addr : link->addresses) {
        OVERNET_TRACE(DEBUG) << "SendTo addr=" << addr << " slice=" << slice;
        SendTo(addr, slice);
      }
    }
  }

  void RemoveLink(LinkDataPtr link) {
    for (auto addr : link->addresses) {
      if (auto it = links_.find(addr);
          it != links_.end() && it->second == link) {
        links_.erase(it);
      }
    }
  }

  template <class F>
  void StartSimpleState(LinkDataPtr link, Optional<NodeId> node,
                        LinkState state, ResendBehavior resend,
                        size_t packet_size, F packet_writer) {
    OVERNET_TRACE(DEBUG) << "StartState: addrs=" << AddrVecStr(link->addresses)
                         << " node=" << node << " linkstate=" << state
                         << " resend=" << resend
                         << " packet_size=" << packet_size;
    const Optional<int> ticks_or_nothing =
        link->SetStateAndMaybeNode(state, node);
    if (!ticks_or_nothing) {
      OVERNET_TRACE(TRACE) << "Forget " << AddrVecStr(link->addresses)
                           << " due to age";
      RemoveLink(link);
      return;
    }
    const int ticks = *ticks_or_nothing;
    SendToLink(link, Slice::WithInitializer(packet_size, packet_writer));
    switch (resend) {
      case ResendBehavior::kResendable:
        link->next_timeout.Reset(
            timer_, BackoffForTicks(kAnnounceResendMillis, ticks),
            StatusCallback(ALLOCATED_CALLBACK, [=](const Status& status) {
              ScopedModule<PacketNub> in_nub(this);
              if (status.is_error()) {
                return;
              }
              bool is_current = false;
              for (const auto& addr : link->addresses) {
                if (auto it = links_.find(addr);
                    it != links_.end() && it->second == link) {
                  is_current = true;
                  break;
                }
              }
              if (!is_current) {
                return;
              }
              StartSimpleState(link, node, state, resend, packet_size,
                               packet_writer);
            }));
        break;
      case ResendBehavior::kNever:
        link->next_timeout.Reset();
        break;
    }
  }

  void StartAnnouncing(LinkDataPtr link, NodeId node) {
    StartSimpleState(link, node, LinkState::Announcing,
                     ResendBehavior::kResendable, kCallMeMaybeSize,
                     [local_node = local_node_](uint8_t* p) {
                       memset(p, 0, kCallMeMaybeSize);
                       *p++ = static_cast<uint8_t>(PacketOp::CallMeMaybe);
                       p = local_node.Write(p);
                     });
  }

  void StartHello(LinkDataPtr link, NodeId node, ResendBehavior resend) {
    StartSimpleState(link, node, LinkState::SayingHello, resend, kHelloSize,
                     [local_node = local_node_](uint8_t* p) {
                       memset(p, 0, kHelloSize);
                       *p++ = static_cast<uint8_t>(PacketOp::Hello);
                       p = local_node.Write(p);
                     });
  }

  void StartHelloAck(LinkDataPtr link, NodeId node) {
    StartSimpleState(
        link, node, LinkState::AckingHello, ResendBehavior::kNever, 1,
        [](uint8_t* p) { *p = static_cast<uint8_t>(PacketOp::HelloAck); });
  }

  void StartSemiConnected(LinkDataPtr link) {
    StartSimpleState(
        link, Nothing, LinkState::SemiConnected, ResendBehavior::kResendable, 1,
        [](uint8_t* p) { *p = static_cast<uint8_t>(PacketOp::Connected); });
  }

  void BecomePublished(LinkDataPtr link) {
    assert(link->link == nullptr);
    assert(link->node_id);
    link->link = new NubLink(this, link, *link->node_id, next_label_++);
    Publish(LinkPtr<>(link->link));
  }

  Timer* const timer_;
  const NodeId local_node_;
  std::unordered_map<Address, LinkDataPtr, HashAddress, EqAddress> links_;
  std::mt19937_64 rng_;
  uint64_t next_label_ = 1;
};

}  // namespace overnet
