// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string.h>

#include "apps/netconnector/src/mdns/dns_reading.h"
#include "lib/ftl/logging.h"

namespace netconnector {
namespace mdns {
namespace {

// Max record counts. These values are selected to prevent an attack that would
// cause is to allocate memory for large numbers of records.
static constexpr size_t kMaxQuestions = 1024;
static constexpr size_t kMaxAnswers = 1024;
static constexpr size_t kMaxAuthorities = 1024;
static constexpr size_t kMaxAdditionals = 1024;

void ReadNameLabels(PacketReader& reader, std::vector<char>& chars) {
  while (reader.healthy()) {
    uint8_t label_size;
    reader >> label_size;

    if (label_size & 0xc0) {
      // We have an offset rather than the actual name. The offset is in the
      // 14 bits following two 1's.
      uint16_t offset = (label_size & 0x3f) << 8;
      reader >> label_size;
      offset |= label_size;

      // Set the read position to that offset and rest of the name.
      size_t bytes_consumed = reader.bytes_consumed();
      reader.SetBytesConsumed(offset);
      ReadNameLabels(reader, chars);

      // Restore the read position.
      reader.SetBytesConsumed(bytes_consumed);
      break;
    }

    if (label_size > 63) {
      reader.MarkUnhealthy();
      break;
    }

    if (label_size == 0) {
      break;
    }

    size_t old_size = chars.size();
    chars.resize(old_size + label_size + 1);
    if (!reader.GetBytes(label_size, chars.data() + old_size)) {
      break;
    }

    chars[chars.size() - 1] = '.';
  }
}

}  // namespace

PacketReader& operator>>(PacketReader& reader, DnsName& value) {
  std::vector<char> chars;

  ReadNameLabels(reader, chars);

  if (chars.size() == 0) {
    FTL_DLOG(ERROR) << "zero domain name labels";
    reader.MarkUnhealthy();
  } else if (reader.healthy()) {
    value.dotted_string_ = std::string(chars.data(), chars.size());
  }

  return reader;
}

PacketReader& operator>>(PacketReader& reader, DnsV4Address& value) {
  in_addr addr;
  reader.GetBytes(sizeof(addr), &addr);
  value.address_ = IpAddress(addr);
  return reader;
}

PacketReader& operator>>(PacketReader& reader, DnsV6Address& value) {
  in6_addr addr;
  reader.GetBytes(sizeof(addr), &addr);
  value.address_ = IpAddress(addr);
  return reader;
}

PacketReader& operator>>(PacketReader& reader, DnsType& value) {
  uint16_t as_uint16;
  reader >> as_uint16;
  value = static_cast<DnsType>(as_uint16);
  return reader;
}

PacketReader& operator>>(PacketReader& reader, DnsClass& value) {
  uint16_t as_uint16;
  reader >> as_uint16;
  value = static_cast<DnsClass>(as_uint16);
  return reader;
}

PacketReader& operator>>(PacketReader& reader, DnsClassAndFlag& value) {
  uint16_t as_uint16;
  reader >> as_uint16;
  value.class_ = static_cast<DnsClass>(as_uint16 & 0x7fff);
  value.flag_ = (as_uint16 & 0x8000) != 0;
  return reader;
}

PacketReader& operator>>(PacketReader& reader, DnsHeader& value) {
  return reader >> value.id_ >> value.flags_ >> value.question_count_ >>
         value.answer_count_ >> value.authority_count_ >>
         value.additional_count_;
}

PacketReader& operator>>(PacketReader& reader, DnsQuestion& value) {
  DnsClassAndFlag class_and_flag;
  reader >> value.name_ >> value.type_ >> class_and_flag;
  value.class_ = class_and_flag.class_;
  value.unicast_response_ = class_and_flag.flag_;
  return reader;
}

PacketReader& operator>>(PacketReader& reader, DnsResourceDataA& value) {
  return reader >> value.address_;
}

PacketReader& operator>>(PacketReader& reader, DnsResourceDataNs& value) {
  return reader >> value.name_server_domain_name_;
}

PacketReader& operator>>(PacketReader& reader, DnsResourceDataCName& value) {
  return reader >> value.canonical_name_;
}

PacketReader& operator>>(PacketReader& reader, DnsResourceDataPtr& value) {
  return reader >> value.pointer_domain_name_;
}

PacketReader& operator>>(PacketReader& reader, DnsResourceDataTxt& value) {
  // |reader.bytes_remaining()| must be set to the length of the TXT data
  // before calling this operator overload.

  value.strings_.clear();

  while (reader.bytes_remaining() != 0) {
    const char* start = reinterpret_cast<const char*>(reader.Bytes(0));
    size_t length = strnlen(start, reader.bytes_remaining());

    if (length == reader.bytes_remaining()) {
      FTL_DLOG(ERROR) << "Unterminated string in TXT record.";
      reader.MarkUnhealthy();
      return reader;
    }

    std::string s(start);
    value.strings_.emplace_back(s);

    reader.Bytes(length + 1);
  }

  FTL_DCHECK(reader.healthy());
  FTL_DCHECK(reader.bytes_remaining() == 0);

  return reader;
}

PacketReader& operator>>(PacketReader& reader, DnsResourceDataAaaa& value) {
  return reader >> value.address_;
}

PacketReader& operator>>(PacketReader& reader, DnsResourceDataSrv& value) {
  uint16_t port;
  reader >> value.priority_ >> value.weight_ >> port >> value.target_;
  value.port_ = IpPort::From_uint16_t(port);
  return reader;
}

PacketReader& operator>>(PacketReader& reader, DnsResourceDataNSec& value) {
  // |reader.bytes_remaining()| must be set to the length of the NSEC data
  // before calling this operator overload.

  reader >> value.next_domain_;
  if (!reader.healthy()) {
    return reader;
  }

  value.bits_.resize(reader.bytes_remaining());
  reader.GetBytes(reader.bytes_remaining(), value.bits_.data());

  FTL_DCHECK(reader.healthy());
  FTL_DCHECK(reader.bytes_remaining() == 0);

  return reader;
}

PacketReader& operator>>(PacketReader& reader, DnsResource& value) {
  DnsClassAndFlag class_and_flag;
  uint16_t data_size;
  reader >> value.name_ >> value.type_ >> class_and_flag >>
      value.time_to_live_ >> data_size;
  value.class_ = class_and_flag.class_;
  value.cache_flush_ = class_and_flag.flag_;

  if (data_size > reader.bytes_remaining()) {
    reader.MarkUnhealthy();
    FTL_DLOG(ERROR) << "data_size is " << data_size << ", remaining is "
                    << reader.bytes_remaining();
    return reader;
  }

  switch (value.type_) {
    case DnsType::kA:
      new (&value.a_) DnsResourceDataA();
      reader >> value.a_;
      break;
    case DnsType::kNs:
      new (&value.ns_) DnsResourceDataNs();
      reader >> value.ns_;
      break;
    case DnsType::kCName:
      new (&value.cname_) DnsResourceDataCName();
      reader >> value.cname_;
      break;
    case DnsType::kPtr:
      new (&value.ptr_) DnsResourceDataPtr();
      reader >> value.ptr_;
      break;
    case DnsType::kTxt: {
      new (&value.txt_) DnsResourceDataTxt();
      size_t bytes_remaining = reader.bytes_remaining();
      reader.SetBytesRemaining(data_size);
      reader >> value.txt_;
      if (reader.healthy()) {
        FTL_DCHECK(reader.bytes_remaining() == 0);
        reader.SetBytesRemaining(bytes_remaining - data_size);
      }
    } break;
    case DnsType::kAaaa:
      new (&value.aaaa_) DnsResourceDataAaaa();
      reader >> value.aaaa_;
      break;
    case DnsType::kSrv:
      new (&value.srv_) DnsResourceDataSrv();
      reader >> value.srv_;
      break;
    case DnsType::kNSec: {
      new (&value.txt_) DnsResourceDataNSec();
      size_t bytes_remaining = reader.bytes_remaining();
      reader.SetBytesRemaining(data_size);
      reader >> value.nsec_;
      if (reader.healthy()) {
        FTL_DCHECK(reader.bytes_remaining() == 0);
        reader.SetBytesRemaining(bytes_remaining - data_size);
      }
    } break;
    default:
      FTL_DLOG(WARNING) << "Skipping data for unsupported resource type "
                        << static_cast<uint16_t>(value.type_);
      reader.Bytes(data_size);
      break;
  }

  return reader;
}

PacketReader& operator>>(PacketReader& reader, DnsMessage& value) {
  reader >> value.header_;

  if (value.header_.question_count_ > kMaxQuestions ||
      value.header_.answer_count_ > kMaxAnswers ||
      value.header_.authority_count_ > kMaxAuthorities ||
      value.header_.additional_count_ > kMaxAdditionals) {
    FTL_DLOG(ERROR) << "Max record count exceeded; rejecting message.";
    reader.MarkUnhealthy();
    return reader;
  }

  if (reader.healthy()) {
    value.questions_.resize(value.header_.question_count_);
    for (uint16_t i = 0; i < value.questions_.size(); ++i) {
      reader >> value.questions_[i];
    }
  }

  if (reader.healthy()) {
    value.answers_.resize(value.header_.answer_count_);
    for (uint16_t i = 0; i < value.answers_.size(); ++i) {
      reader >> value.answers_[i];
    }
  }

  if (reader.healthy()) {
    value.authorities_.resize(value.header_.authority_count_);
    for (uint16_t i = 0; i < value.authorities_.size(); ++i) {
      reader >> value.authorities_[i];
    }
  }

  if (reader.healthy()) {
    value.additionals_.resize(value.header_.additional_count_);
    for (uint16_t i = 0; i < value.additionals_.size(); ++i) {
      reader >> value.additionals_[i];
    }
  }

  return reader;
}

}  // namespace mdns
}  // namespace netconnector
