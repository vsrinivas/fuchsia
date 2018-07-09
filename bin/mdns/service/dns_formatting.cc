// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/mdns/service/dns_formatting.h"

#include <iomanip>
#include <iostream>

#include "garnet/bin/mdns/service/dns_message.h"
#include "lib/fostr/hex_dump.h"

namespace mdns {

std::ostream& operator<<(std::ostream& os, DnsType value) {
  switch (value) {
    case DnsType::kA:
      return os << "A";
    case DnsType::kNs:
      return os << "NS";
    case DnsType::kCName:
      return os << "CNAME";
    case DnsType::kPtr:
      return os << "PTR";
    case DnsType::kTxt:
      return os << "TXT";
    case DnsType::kAaaa:
      return os << "AAAA";
    case DnsType::kSrv:
      return os << "SRV";
    case DnsType::kOpt:
      return os << "OPT";
    case DnsType::kNSec:
      return os << "NSEC";
    case DnsType::kAny:
      return os << "any";
    default:
      return os << "TYPE " << static_cast<uint16_t>(value);
  }
}

std::ostream& operator<<(std::ostream& os, DnsClass value) {
  switch (value) {
    case DnsClass::kIn:
      return os << "IN";
    case DnsClass::kCs:
      return os << "CS";
    case DnsClass::kCh:
      return os << "CH";
    case DnsClass::kHs:
      return os << "HS";
    case DnsClass::kNone:
      return os << "none";
    case DnsClass::kAny:
      return os << "any";
    default:
      return os << "CLASS " << static_cast<uint16_t>(value);
  }
}

std::ostream& operator<<(std::ostream& os, const DnsName& value) {
  return os << value.dotted_string_;
}

std::ostream& operator<<(std::ostream& os, const DnsV4Address& value) {
  return os << value.address_;
}

std::ostream& operator<<(std::ostream& os, const DnsV6Address& value) {
  return os << value.address_;
}

std::ostream& operator<<(std::ostream& os, const DnsHeader& value) {
  os << fostr::Indent;
  os << fostr::NewLine << "id: " << value.id_;
  os << fostr::NewLine << "flags: 0x" << std::hex << std::setw(4)
     << std::setfill('0') << value.flags_ << std::dec;
  return os << fostr::Outdent;
}

std::ostream& operator<<(std::ostream& os, const DnsQuestion& value) {
  os << fostr::Indent;
  os << fostr::NewLine << "name: " << value.name_;
  os << fostr::NewLine << "type: " << value.type_;
  os << fostr::NewLine << "class: " << value.class_;
  os << fostr::NewLine << "unicast_response: " << value.unicast_response_;
  return os << fostr::Outdent;
}

std::ostream& operator<<(std::ostream& os, const DnsResourceDataA& value) {
  return os << fostr::NewLine << "address: " << value.address_;
}

std::ostream& operator<<(std::ostream& os, const DnsResourceDataNs& value) {
  return os << fostr::NewLine
            << "name_server_domain_name: " << value.name_server_domain_name_;
}

std::ostream& operator<<(std::ostream& os, const DnsResourceDataCName& value) {
  return os << fostr::NewLine << "canonical_name: " << value.canonical_name_;
}

std::ostream& operator<<(std::ostream& os, const DnsResourceDataPtr& value) {
  return os << fostr::NewLine
            << "pointer_domain_name_: " << value.pointer_domain_name_;
}

std::ostream& operator<<(std::ostream& os, const DnsResourceDataTxt& value) {
  os << fostr::NewLine << "text: ";
  os << fostr::Indent;
  for (auto& string : value.strings_) {
    os << fostr::NewLine << "\"" << string << "\"";
  }
  return os << fostr::Outdent;
}

std::ostream& operator<<(std::ostream& os, const DnsResourceDataAaaa& value) {
  return os << fostr::NewLine << "address: " << value.address_;
}

std::ostream& operator<<(std::ostream& os, const DnsResourceDataSrv& value) {
  os << fostr::NewLine << "priority: " << value.priority_;
  os << fostr::NewLine << "weight: " << value.weight_;
  os << fostr::NewLine << "port: " << value.port_;
  return os << fostr::NewLine << "target: " << value.target_;
}

std::ostream& operator<<(std::ostream& os, const DnsResourceDataOpt& value) {
  return os << fostr::NewLine << "options: " << fostr::HexDump(value.options_);
}

std::ostream& operator<<(std::ostream& os, const DnsResourceDataNSec& value) {
  os << fostr::NewLine << "next_domain: " << value.next_domain_;
  return os << fostr::NewLine << "bits: " << fostr::HexDump(value.bits_);
}

std::ostream& operator<<(std::ostream& os, const DnsResource& value) {
  os << fostr::Indent;
  os << fostr::NewLine << "name: " << value.name_;
  os << fostr::NewLine << "type: " << value.type_;
  os << fostr::NewLine << "class: " << value.class_;
  os << fostr::NewLine << "cache_flush: " << value.cache_flush_;
  os << fostr::NewLine << "time_to_live: " << value.time_to_live_;
  switch (value.type_) {
    case DnsType::kA:
      os << value.a_;
      break;
    case DnsType::kNs:
      os << value.ns_;
      break;
    case DnsType::kCName:
      os << value.cname_;
      break;
    case DnsType::kPtr:
      os << value.ptr_;
      break;
    case DnsType::kTxt:
      os << value.txt_;
      break;
    case DnsType::kAaaa:
      os << value.aaaa_;
      break;
    case DnsType::kSrv:
      os << value.srv_;
      break;
    case DnsType::kOpt:
      os << value.opt_;
      break;
    case DnsType::kNSec:
      os << value.nsec_;
      break;
    default:
      break;
  }
  return os << fostr::Outdent;
}

std::ostream& operator<<(std::ostream& os, const DnsMessage& value) {
  os << fostr::Indent;
  os << fostr::NewLine << "header: " << value.header_;
  if (!value.questions_.empty()) {
    os << fostr::NewLine << "questions: " << value.questions_;
  }
  if (!value.answers_.empty()) {
    os << fostr::NewLine << "answers: " << value.answers_;
  }
  if (!value.authorities_.empty()) {
    os << fostr::NewLine << "authorities: " << value.authorities_;
  }
  if (!value.additionals_.empty()) {
    os << fostr::NewLine << "additionals: " << value.additionals_;
  }
  return os << fostr::Outdent;
}

}  // namespace mdns
