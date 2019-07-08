// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Immutable packet filters. Given pointers to packet header data, a filter is an object
// that can perform a match operation on fields in the headers and outputs a verdict on
// whether the packet should be accepted.
//
// `FilterBase` is the abstract class providing the interface of a filter.
// Concrete subclasses of `FilterBase` are filters working at different network layers, or
// compositions of one or more filters. Data fields should be provided in network byte order.
//
// Filters cannot be mutated once created. In practical use, instances of `FilterBase` and its
// subclasses should always be wrapped in a `FilterPtr` for memory management.
// A complete filter is one or more sub-filters joined together as nodes in a filter tree,
// connected by logical negation, conjunction and/or disjunction operations.

#ifndef ZIRCON_SYSTEM_UAPP_NETDUMP_FILTER_H_
#define ZIRCON_SYSTEM_UAPP_NETDUMP_FILTER_H_

#include <algorithm>
#include <functional>
#include <memory>
#include <utility>
#include <variant>
#include <vector>

#include "filter_constants.h"

namespace netdump {

class Packet {
 public:
  Packet() { reset(); }

  void reset() {
    frame_length = 0;
    frame = nullptr;
    ip = nullptr;
    transport = nullptr;
  }

  // `frame_len` is supplied by the client user of filter, so it is expected in host byte order.
  uint16_t frame_length;
  const struct ethhdr* frame;
  union {
    const struct iphdr* ip;  // For both IPv4 and IPv6.
    const ip6_hdr_t* ipv6;
  };
  union {
    const struct tcphdr* tcp;
    const struct udphdr* udp;
    const void* transport;
  };
};

class FilterBase;
using FilterPtr = std::unique_ptr<FilterBase>;

class FilterBase {
 public:
  // Returns `true` if the packet matches the internal filter specification.
  // The storage and type of that specification is up to the particular filter subclasses.
  // If the a relevant pointer in `packet` is null, `false` is returned if the
  // filter specifies a basic match on a header field.
  virtual bool match(const Packet& packet) = 0;

  virtual ~FilterBase() = default;
  FilterBase(const FilterBase& other) = delete;
  FilterBase& operator=(const FilterBase& other) = delete;

 protected:
  FilterBase() = default;
};

// Filter on length of frame, including frame headers.
class FrameLengthFilter : public FilterBase {
 public:
  // If `comp` is `LEQ`, the filter matches if frame length is less than or
  // equal to `frame_len`. Otherwise the filter matches if it is greater than or
  // equal.
  explicit FrameLengthFilter(uint16_t frame_len, LengthComparator comp);

  bool match(const Packet& packet) override;

  FrameLengthFilter(const FrameLengthFilter& other) = delete;
  FrameLengthFilter& operator=(const FrameLengthFilter& other) = delete;

 private:
  std::function<bool(const Packet&)> match_fn_;
};

// Filter on Ethernet frames.
class EthFilter : public FilterBase {
 public:
  // A filter matching on Ethertype field in Ethernet II only.
  explicit EthFilter(uint16_t ethtype) : spec_(Spec(ethtype)) {}

  using MacAddress = std::array<uint8_t, ETH_ALEN>;
  // A filter matching on MAC address.
  EthFilter(const MacAddress& mac, AddressFieldType type);

  bool match(const Packet& packet) override;

  EthFilter(const EthFilter& other) = delete;
  EthFilter& operator=(const EthFilter& other) = delete;

 private:
  using EthType = uint16_t;
  using Address = struct {
    MacAddress mac;
    AddressFieldType type;
  };
  using Spec = std::variant<EthType, Address>;
  Spec spec_;
};

// Filter on IP headers. An IP version must be specified, which is always checked in the packet.
// The filter may additionally match on another field, which can be one of packet length,
// transport protocol, or IPv4 or IPv6 host address.
class IpFilter : public FilterBase {
 public:
  // A filter matching on IP version only.
  explicit IpFilter(uint8_t version);

  // A filter matching on IP packet length. If `comp` is `LEQ`,
  // the filter matches if packet length is less than or equal to `ip_pkt_len`.
  // Otherwise the filter matches if it is greater than or equal.
  explicit IpFilter(uint8_t version, uint16_t ip_pkt_len, LengthComparator comp);

  // A filter matching on transport protocol.
  explicit IpFilter(uint8_t version, uint8_t protocol);

  // A filter matching on IPv4 address. `ipv4_addr` should be in network byte order.
  explicit IpFilter(uint32_t ipv4_addr, AddressFieldType type);

  using IPv6Address = std::array<uint8_t, IP6_ADDR_LEN>;
  // A filter matching on IPv6 address. `ipv6_addr` should be in network byte order.
  IpFilter(const IPv6Address& ipv6_addr, AddressFieldType type);

  bool match(const Packet& packet) override;

  IpFilter(const IpFilter& other) = delete;
  IpFilter& operator=(const IpFilter& other) = delete;

 private:
  const uint8_t version_;
  std::function<bool(const Packet&)> match_fn_;
};

// Filter on transport layer ports.
class PortFilter : public FilterBase {
 public:
  explicit PortFilter(std::vector<PortRange> ports, PortFieldType type);

  bool match(const Packet& packet) override;

  PortFilter(const PortFilter& other) = delete;
  PortFilter& operator=(const PortFilter& other) = delete;

 private:
  // TODO(xianglong): Replace with e.g. interval tree guaranteeing logarithmic lookup.
  std::vector<PortRange> ports_;
  const PortFieldType type_;

  bool match_ports(uint16_t src_port, uint16_t dst_port);
};

// Logical `NOT` (negation) of the contained filter.
class NegFilter : public FilterBase {
 public:
  explicit NegFilter(FilterPtr filter) : filter_(std::move(filter)) {}

  bool match(const Packet& packet) override;

  NegFilter(const NegFilter& other) = delete;
  NegFilter& operator=(const NegFilter& other) = delete;

 private:
  const FilterPtr filter_;
};

// Logical `AND` (conjunction) of two contained filters.
class ConjFilter : public FilterBase {
 public:
  ConjFilter(FilterPtr left, FilterPtr right) : left_(std::move(left)), right_(std::move(right)) {}

  bool match(const Packet& packet) override;

  ConjFilter(const ConjFilter& other) = delete;
  ConjFilter& operator=(const ConjFilter& other) = delete;

 private:
  const FilterPtr left_;
  const FilterPtr right_;
};

// Logical `OR` (disjunction) of two contained filters.
class DisjFilter : public FilterBase {
 public:
  DisjFilter(FilterPtr left, FilterPtr right) : left_(std::move(left)), right_(std::move(right)) {}

  bool match(const Packet& packet) override;

  DisjFilter(const DisjFilter& other) = delete;
  DisjFilter& operator=(const DisjFilter& other) = delete;

 private:
  const FilterPtr left_;
  const FilterPtr right_;
};

}  // namespace netdump

#endif  // ZIRCON_SYSTEM_UAPP_NETDUMP_FILTER_H_
