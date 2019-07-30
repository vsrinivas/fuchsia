// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Headers of test templates for exercising filters.
// The test functions take a filter construction function as parameter, so
// filters produced in different ways can be run through the same battery of
// tests. Data is passed to the function in network byte order. The function
// types match the signatures for the filter node constructors.

#ifndef SRC_CONNECTIVITY_NETWORK_NETDUMP_TEST_FILTER_TEST_H_
#define SRC_CONNECTIVITY_NETWORK_NETDUMP_TEST_FILTER_TEST_H_

#include <zxtest/zxtest.h>

#include <functional>

#include "filter.h"

namespace netdump::test {

// Test case function signatures.
using FrameLengthFn = std::function<FilterPtr(uint16_t, LengthComparator)>;
void FrameLengthTest(FrameLengthFn filter_fn);

using EthtypeFn = std::function<FilterPtr(uint16_t)>;
void EthtypeTest(EthtypeFn filter_fn);

using MacFn = std::function<FilterPtr(EthFilter::MacAddress, AddressFieldType)>;
void MacTest(MacFn filter_fn);

using VersionFn = std::function<FilterPtr(uint8_t)>;
void VersionTest(VersionFn filter_fn);

using IPLengthFn = std::function<FilterPtr(uint8_t, uint16_t, LengthComparator)>;
void IPLengthTest(IPLengthFn filter_fn);

using ProtocolFn = std::function<FilterPtr(uint8_t, uint8_t)>;
void ProtocolTest(ProtocolFn filter_fn);

using IPv4AddrFn = std::function<FilterPtr(uint32_t, AddressFieldType)>;
void IPv4AddrTest(IPv4AddrFn filter_fn);

using IPv6AddrFn = std::function<FilterPtr(IpFilter::IPv6Address, AddressFieldType)>;
void IPv6AddrTest(IPv6AddrFn filter_fn);

using PortFn = std::function<FilterPtr(std::vector<PortRange>, PortFieldType)>;
void IPv4PortsTest(PortFn filter_fn);
void IPv6PortsTest(PortFn filter_fn);

void UnsupportedIpVersionAssertTest(VersionFn version_fn, IPLengthFn length_fn,
                                    ProtocolFn protocol_fn);

using UnaryFn = std::function<FilterPtr(FilterPtr)>;
using BinaryFn = std::function<FilterPtr(FilterPtr, FilterPtr)>;
void CompositionTest(UnaryFn neg_fn, BinaryFn conj_fn, BinaryFn disj_fn);

}  // namespace netdump::test

#endif  // SRC_CONNECTIVITY_NETWORK_NETDUMP_TEST_FILTER_TEST_H_
