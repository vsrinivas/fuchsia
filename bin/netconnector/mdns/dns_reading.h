// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <memory>

#include "apps/netconnector/src/mdns/dns_message.h"
#include "apps/netconnector/src/mdns/packet_reader.h"

namespace netconnector {
namespace mdns {

// Note: DnsResourceDataTxt, DnsResourceDataOpt and DnsResourceDataNSec must be
// read with a reader whose 'bytes remaining' has been set to the length of the
// item to be read. This is their size needs to be known in order to read them.
// See the overload for DnsResource to see how this is done.

template <typename T>
PacketReader& operator>>(PacketReader& reader, std::shared_ptr<T>& value) {
  value = std::make_shared<T>();
  return reader >> *value;
}

PacketReader& operator>>(PacketReader& reader, DnsName& value);
PacketReader& operator>>(PacketReader& reader, DnsV4Address& value);
PacketReader& operator>>(PacketReader& reader, DnsV6Address& value);
PacketReader& operator>>(PacketReader& reader, DnsType& value);
PacketReader& operator>>(PacketReader& reader, DnsClass& value);
PacketReader& operator>>(PacketReader& reader, DnsClassAndFlag& value);
PacketReader& operator>>(PacketReader& reader, DnsHeader& value);
PacketReader& operator>>(PacketReader& reader, DnsQuestion& value);
PacketReader& operator>>(PacketReader& reader, DnsResourceDataA& value);
PacketReader& operator>>(PacketReader& reader, DnsResourceDataNs& value);
PacketReader& operator>>(PacketReader& reader, DnsResourceDataCName& value);
PacketReader& operator>>(PacketReader& reader, DnsResourceDataPtr& value);
PacketReader& operator>>(PacketReader& reader, DnsResourceDataTxt& value);
PacketReader& operator>>(PacketReader& reader, DnsResourceDataAaaa& value);
PacketReader& operator>>(PacketReader& reader, DnsResourceDataSrv& value);
PacketReader& operator>>(PacketReader& reader, DnsResourceDataOpt& value);
PacketReader& operator>>(PacketReader& reader, DnsResourceDataNSec& value);
PacketReader& operator>>(PacketReader& reader, DnsResource& value);
PacketReader& operator>>(PacketReader& reader, DnsMessage& value);

}  // namespace mdns
}  // namespace netconnector
