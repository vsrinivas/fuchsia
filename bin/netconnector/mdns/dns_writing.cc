// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/netconnector/src/mdns/dns_writing.h"
#include "lib/ftl/logging.h"

namespace netconnector {
namespace mdns {

PacketWriter& operator<<(PacketWriter& writer, const DnsName& value) {
  std::string s = value.dotted_string_;

  while (!s.empty()) {
    size_t position = writer.GetBookmarkPosition(s);
    if (position != PacketWriter::npos) {
      // Write a name offset.
      uint16_t offset = static_cast<uint16_t>(position) | 0xc000;
      writer << offset;
      return writer;
    }

    writer.CreateBookmark(s);

    size_t dot_pos = s.find('.');

    if (dot_pos == std::string::npos) {
      // There really should be a dot at the end, but there is not.
      writer << static_cast<uint8_t>(s.size());
      writer.PutBytes(s.size(), s.data());
      break;
    }

    writer << static_cast<uint8_t>(dot_pos);
    writer.PutBytes(dot_pos, s.data());
    s = s.substr(dot_pos + 1);
  }

  return writer << static_cast<uint8_t>(0);
}

PacketWriter& operator<<(PacketWriter& writer, const DnsV4Address& value) {
  FTL_DCHECK(value.address_.is_v4());
  writer.PutBytes(value.address_.byte_count(), value.address_.as_bytes());
  return writer;
}

PacketWriter& operator<<(PacketWriter& writer, const DnsV6Address& value) {
  FTL_DCHECK(value.address_.is_v6());
  writer.PutBytes(value.address_.byte_count(), value.address_.as_bytes());
  return writer;
}

PacketWriter& operator<<(PacketWriter& writer, const DnsType& value) {
  uint16_t as_uint16 = static_cast<uint16_t>(value);
  return writer << as_uint16;
}

PacketWriter& operator<<(PacketWriter& writer, const DnsClass& value) {
  uint16_t as_uint16 = static_cast<uint16_t>(value);
  return writer << as_uint16;
}

PacketWriter& operator<<(PacketWriter& writer, const DnsClassAndFlag& value) {
  uint16_t as_uint16 = static_cast<uint16_t>(value.class_);
  if (value.flag_) {
    as_uint16 |= 0x8000;
  }
  return writer << as_uint16;
}

PacketWriter& operator<<(PacketWriter& writer, const DnsHeader& value) {
  return writer << value.id_ << value.flags_ << value.question_count_
                << value.answer_count_ << value.authority_count_
                << value.additional_count_;
}

PacketWriter& operator<<(PacketWriter& writer, const DnsQuestion& value) {
  DnsClassAndFlag class_and_flag;
  class_and_flag.class_ = value.class_;
  class_and_flag.flag_ = value.unicast_response_;
  return writer << value.name_ << value.type_ << class_and_flag;
}

PacketWriter& operator<<(PacketWriter& writer, const DnsResourceDataA& value) {
  return writer << value.address_;
}

PacketWriter& operator<<(PacketWriter& writer, const DnsResourceDataNs& value) {
  return writer << value.name_server_domain_name_;
}

PacketWriter& operator<<(PacketWriter& writer,
                         const DnsResourceDataCName& value) {
  return writer << value.canonical_name_;
}

PacketWriter& operator<<(PacketWriter& writer,
                         const DnsResourceDataPtr& value) {
  return writer << value.pointer_domain_name_;
}

PacketWriter& operator<<(PacketWriter& writer,
                         const DnsResourceDataTxt& value) {
  for (auto& string : value.strings_) {
    writer.PutBytes(string.size() + 1, string.data());
  }

  return writer;
}

PacketWriter& operator<<(PacketWriter& writer,
                         const DnsResourceDataAaaa& value) {
  return writer << value.address_;
}

PacketWriter& operator<<(PacketWriter& writer,
                         const DnsResourceDataSrv& value) {
  return writer << value.priority_ << value.weight_ << value.port_.as_uint16_t()
                << value.target_;
}

PacketWriter& operator<<(PacketWriter& writer,
                         const DnsResourceDataNSec& value) {
  return writer << value.next_domain_ << value.bits_;
}

PacketWriter& operator<<(PacketWriter& writer, const DnsResource& value) {
  DnsClassAndFlag class_and_flag;
  class_and_flag.class_ = value.class_;
  class_and_flag.flag_ = value.cache_flush_;

  writer << value.name_ << value.type_ << class_and_flag << value.time_to_live_;

  // Note where the data size goes and write a placeholder.
  size_t data_size_position = writer.position();
  uint16_t data_size = 0;

  writer << data_size;

  switch (value.type_) {
    case DnsType::kA:
      writer << value.a_;
      break;
    case DnsType::kNs:
      writer << value.ns_;
      break;
    case DnsType::kCName:
      writer << value.cname_;
      break;
    case DnsType::kPtr:
      writer << value.ptr_;
      break;
    case DnsType::kTxt:
      writer << value.txt_;
      break;
    case DnsType::kAaaa:
      writer << value.aaaa_;
      break;
    case DnsType::kSrv:
      writer << value.srv_;
      break;
    case DnsType::kNSec:
      writer << value.nsec_;
      break;
    default:
      FTL_DCHECK(false) << "Unsupported resource type "
                        << static_cast<uint16_t>(value.type_);
      break;
  }

  // Determine the size of the data and prefix the data with it.
  size_t end_position = writer.position();
  data_size = end_position - data_size_position - sizeof(data_size);
  writer.SetPosition(data_size_position);
  writer << data_size;
  writer.SetPosition(end_position);

  return writer;
}

PacketWriter& operator<<(PacketWriter& writer, const DnsMessage& value) {
  FTL_DCHECK(value.header_.question_count_ == value.questions_.size());
  FTL_DCHECK(value.header_.answer_count_ == value.answers_.size());
  FTL_DCHECK(value.header_.authority_count_ == value.authorities_.size());
  FTL_DCHECK(value.header_.additional_count_ == value.additionals_.size());

  writer << value.header_;

  for (uint16_t i = 0; i < value.questions_.size(); ++i) {
    writer << value.questions_[i];
  }

  for (uint16_t i = 0; i < value.answers_.size(); ++i) {
    writer << value.answers_[i];
  }

  for (uint16_t i = 0; i < value.authorities_.size(); ++i) {
    writer << value.authorities_[i];
  }

  for (uint16_t i = 0; i < value.additionals_.size(); ++i) {
    writer << value.additionals_[i];
  }

  return writer;
}

}  // namespace mdns
}  // namespace netconnector
