// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <iomanip>
#include <iostream>

#include "apps/netconnector/src/mdns/dns_formatting.h"
#include "apps/netconnector/src/mdns/dns_message.h"

namespace netconnector {
namespace mdns {
namespace {

int ostream_indent_index() {
  static int i = std::ios_base::xalloc();
  return i;
}

static constexpr size_t kBytesPerLine = 16;

}  // namespace

std::ostream& begl(std::ostream& os) {
  for (long i = 0; i < os.iword(ostream_indent_index()); i++) {
    os << "    ";
  }
  return os;
}

std::ostream& indent(std::ostream& os) {
  ++os.iword(ostream_indent_index());
  return os;
}

std::ostream& outdent(std::ostream& os) {
  --os.iword(ostream_indent_index());
  return os;
}

template <>
std::ostream& operator<<(std::ostream& os, const std::vector<uint8_t>& value) {
  os << indent;

  size_t line_offset = 0;
  size_t byte_count = value.size();
  const uint8_t* bytes = value.data();

  while (true) {
    os << std::endl
       << begl << std::hex << std::setw(4) << std::setfill('0') << line_offset
       << " ";

    std::string chars(kBytesPerLine, ' ');

    for (size_t i = 0; i < kBytesPerLine; ++i) {
      if (i == kBytesPerLine / 2) {
        os << " ";
      }

      if (i >= byte_count) {
        os << "   ";
        ++bytes;
      } else {
        os << " " << std::hex << std::setw(2) << std::setfill('0')
           << static_cast<uint16_t>(*bytes);
        if (*bytes >= ' ' && *bytes <= '~') {
          chars[i] = *bytes;
        } else {
          chars[i] = '.';
        }
        ++bytes;
      }
    }

    os << "  " << chars;

    if (byte_count <= kBytesPerLine) {
      break;
    }
    line_offset += kBytesPerLine;
    byte_count -= kBytesPerLine;
  }

  return os << std::dec << outdent;
}

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
  os << std::endl;
  os << indent;
  os << begl << "id: " << value.id_ << std::endl;
  os << begl << "flags: 0x" << std::hex << std::setw(4) << std::setfill('0')
     << value.flags_ << std::dec;
  return os << outdent;
}

std::ostream& operator<<(std::ostream& os, const DnsQuestion& value) {
  os << std::endl;
  os << indent;
  os << begl << "name: " << value.name_ << std::endl;
  os << begl << "type: " << value.type_ << std::endl;
  os << begl << "class: " << value.class_ << std::endl;
  os << begl << "unicast_response: " << value.unicast_response_;
  return os << outdent;
}

std::ostream& operator<<(std::ostream& os, const DnsResourceDataA& value) {
  return os << begl << "address: " << value.address_;
}

std::ostream& operator<<(std::ostream& os, const DnsResourceDataNs& value) {
  return os << begl
            << "name_server_domain_name: " << value.name_server_domain_name_;
}

std::ostream& operator<<(std::ostream& os, const DnsResourceDataCName& value) {
  return os << begl << "canonical_name: " << value.canonical_name_;
}

std::ostream& operator<<(std::ostream& os, const DnsResourceDataPtr& value) {
  return os << begl << "pointer_domain_name_: " << value.pointer_domain_name_;
}

std::ostream& operator<<(std::ostream& os, const DnsResourceDataTxt& value) {
  return os << begl << "text: ";
  os << indent;
  for (auto& string : value.strings_) {
    os << std::endl << begl << "\"" << string << "\"";
  }
  return os << outdent;
}

std::ostream& operator<<(std::ostream& os, const DnsResourceDataAaaa& value) {
  return os << begl << "address: " << value.address_;
}

std::ostream& operator<<(std::ostream& os, const DnsResourceDataSrv& value) {
  os << begl << "priority: " << value.priority_ << std::endl;
  os << begl << "weight: " << value.weight_ << std::endl;
  os << begl << "port: " << value.port_ << std::endl;
  return os << begl << "target: " << value.target_;
}

std::ostream& operator<<(std::ostream& os, const DnsResourceDataNSec& value) {
  os << begl << "next_domain: " << value.next_domain_ << std::endl;
  return os << begl << "bits: " << value.bits_;
}

std::ostream& operator<<(std::ostream& os, const DnsResource& value) {
  os << std::endl;
  os << indent;
  os << begl << "name: " << value.name_ << std::endl;
  os << begl << "type: " << value.type_ << std::endl;
  os << begl << "class: " << value.class_ << std::endl;
  os << begl << "cache_flush: " << value.cache_flush_ << std::endl;
  os << begl << "time_to_live: " << value.time_to_live_ << std::endl;
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
    case DnsType::kNSec:
      os << value.nsec_;
      break;
    default:
      break;
  }
  return os << outdent;
}

std::ostream& operator<<(std::ostream& os, const DnsMessage& value) {
  os << std::endl;
  os << indent;
  os << begl << "header: " << value.header_;
  if (!value.questions_.empty()) {
    os << std::endl << begl << "questions: " << value.questions_;
  }
  if (!value.answers_.empty()) {
    os << std::endl << begl << "answers: " << value.answers_;
  }
  if (!value.authorities_.empty()) {
    os << std::endl << begl << "authorities: " << value.authorities_;
  }
  if (!value.additionals_.empty()) {
    os << std::endl << begl << "additionals: " << value.additionals_;
  }
  return os << outdent;
}

}  // namespace mdns
}  // namespace netconnector
