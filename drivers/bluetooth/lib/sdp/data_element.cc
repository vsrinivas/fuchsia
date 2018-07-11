// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "data_element.h"

#include <endian.h>

#include <set>
#include <vector>

namespace btlib {

using common::MutableByteBuffer;
using common::Optional;
using common::UUID;

namespace sdp {

namespace {

// Size Descriptor occupies the lowest 3 bits of the header byte.
// v5.0, Vol 3, Part B, Sec 3.3.
constexpr uint8_t kDataElementSizeTypeMask = 0x07;

DataElement::Size SizeToSizeType(size_t size) {
  switch (size) {
    case 1:
      return DataElement::Size::kOneByte;
    case 2:
      return DataElement::Size::kTwoBytes;
    case 4:
      return DataElement::Size::kFourBytes;
    case 8:
      return DataElement::Size::kEightBytes;
    case 16:
      return DataElement::Size::kSixteenBytes;
    default:
      FXL_NOTREACHED();
  }
  return DataElement::Size::kNextFour;
}

size_t AggregateSize(const std::vector<DataElement>& aggregate) {
  size_t total_size = 0;
  for (const auto& elem : aggregate) {
    total_size += elem.WriteSize();
  }
  return total_size;
}

size_t WriteLength(MutableByteBuffer* buf, size_t length) {
  if (length <= std::numeric_limits<uint8_t>::max()) {
    uint8_t val = static_cast<uint8_t>(length);
    buf->Write(&val, sizeof(val));
    return sizeof(val);
  } else if (length <= std::numeric_limits<uint16_t>::max()) {
    uint16_t val = htobe16(static_cast<uint16_t>(length));
    buf->Write(reinterpret_cast<uint8_t*>(&val), sizeof(val));
    return sizeof(val);
  } else if (length <= std::numeric_limits<uint32_t>::max()) {
    uint32_t val = htobe32(static_cast<uint32_t>(length));
    buf->Write(reinterpret_cast<uint8_t*>(&val), sizeof(val));
    return sizeof(val);
  }
  return 0;
}

}  // namespace

DataElement::DataElement() : type_(Type::kNull), size_(Size::kOneByte) {}

template <>
void DataElement::Set<uint8_t>(uint8_t value) {
  type_ = Type::kUnsignedInt;
  size_ = SizeToSizeType(sizeof(uint8_t));
  uint_value_ = value;
}

template <>
void DataElement::Set<uint16_t>(uint16_t value) {
  type_ = Type::kUnsignedInt;
  size_ = SizeToSizeType(sizeof(uint16_t));
  uint_value_ = value;
}

template <>
void DataElement::Set<uint32_t>(uint32_t value) {
  type_ = Type::kUnsignedInt;
  size_ = SizeToSizeType(sizeof(uint32_t));
  uint_value_ = value;
}

template <>
void DataElement::Set<uint64_t>(uint64_t value) {
  type_ = Type::kUnsignedInt;
  size_ = SizeToSizeType(sizeof(uint64_t));
  uint_value_ = value;
}

template <>
void DataElement::Set<int8_t>(int8_t value) {
  type_ = Type::kSignedInt;
  size_ = SizeToSizeType(sizeof(int8_t));
  int_value_ = value;
}

template <>
void DataElement::Set<int16_t>(int16_t value) {
  type_ = Type::kSignedInt;
  size_ = SizeToSizeType(sizeof(int16_t));
  int_value_ = value;
}

template <>
void DataElement::Set<int32_t>(int32_t value) {
  type_ = Type::kSignedInt;
  size_ = SizeToSizeType(sizeof(int32_t));
  int_value_ = value;
}

template <>
void DataElement::Set<int64_t>(int64_t value) {
  type_ = Type::kSignedInt;
  size_ = SizeToSizeType(sizeof(int64_t));
  int_value_ = value;
}

template <>
void DataElement::Set<bool>(bool value) {
  type_ = Type::kBoolean;
  size_ = Size::kOneByte;
  int_value_ = (value ? 1 : 0);
}

template <>
void DataElement::Set<std::nullptr_t>(std::nullptr_t) {
  type_ = Type::kNull;
  size_ = Size::kOneByte;
}

template <>
void DataElement::Set<UUID>(UUID value) {
  type_ = Type::kUuid;
  size_ = SizeToSizeType(value.CompactSize());
  uuid_ = value;
}

template <>
void DataElement::Set<std::string>(std::string value) {
  type_ = Type::kString;
  SetVariableSize(value.size());
  string_ = value;
}

template <>
void DataElement::Set<std::vector<DataElement>>(
    std::vector<DataElement> value) {
  type_ = Type::kSequence;
  aggregate_ = value;
  SetVariableSize(AggregateSize(aggregate_));
}

void DataElement::SetAlternative(std::vector<DataElement> items) {
  type_ = Type::kAlternative;
  aggregate_ = items;
  SetVariableSize(AggregateSize(aggregate_));
}

template <>
Optional<uint8_t> DataElement::Get<uint8_t>() const {
  Optional<uint8_t> ret;
  if (type_ == Type::kUnsignedInt && size_ == SizeToSizeType(sizeof(uint8_t))) {
    ret = static_cast<uint8_t>(uint_value_);
  }
  return ret;
}

template <>
Optional<uint16_t> DataElement::Get<uint16_t>() const {
  Optional<uint16_t> ret;
  if (type_ == Type::kUnsignedInt &&
      size_ == SizeToSizeType(sizeof(uint16_t))) {
    ret = static_cast<uint16_t>(uint_value_);
  }
  return ret;
}

template <>
Optional<uint32_t> DataElement::Get<uint32_t>() const {
  Optional<uint32_t> ret;
  if (type_ == Type::kUnsignedInt &&
      size_ == SizeToSizeType(sizeof(uint32_t))) {
    ret = static_cast<uint32_t>(uint_value_);
  }
  return ret;
}

template <>
Optional<uint64_t> DataElement::Get<uint64_t>() const {
  Optional<uint64_t> ret;
  if (type_ == Type::kUnsignedInt &&
      size_ == SizeToSizeType(sizeof(uint64_t))) {
    ret = uint_value_;
  }
  return ret;
}

template <>
Optional<int8_t> DataElement::Get<int8_t>() const {
  Optional<int8_t> ret;
  if (type_ == Type::kUnsignedInt && size_ == SizeToSizeType(sizeof(int8_t))) {
    ret = static_cast<int8_t>(int_value_);
  }
  return ret;
}

template <>
Optional<int16_t> DataElement::Get<int16_t>() const {
  Optional<int16_t> ret;
  if (type_ == Type::kUnsignedInt && size_ == SizeToSizeType(sizeof(int16_t))) {
    ret = static_cast<int16_t>(int_value_);
  }
  return ret;
}

template <>
Optional<int32_t> DataElement::Get<int32_t>() const {
  Optional<int32_t> ret;
  if (type_ == Type::kUnsignedInt && size_ == SizeToSizeType(sizeof(int32_t))) {
    ret = static_cast<int32_t>(int_value_);
  }
  return ret;
}

template <>
Optional<int64_t> DataElement::Get<int64_t>() const {
  Optional<int64_t> ret;
  if (type_ == Type::kUnsignedInt && size_ == SizeToSizeType(sizeof(int64_t))) {
    ret = static_cast<int64_t>(int_value_);
  }
  return ret;
}

template <>
Optional<bool> DataElement::Get<bool>() const {
  Optional<bool> ret;
  if (type_ == Type::kBoolean) {
    ret = (int_value_ == 1);
  }
  return ret;
}

template <>
Optional<std::nullptr_t> DataElement::Get<std::nullptr_t>() const {
  Optional<std::nullptr_t> ret;
  if (type_ == Type::kNull) {
    ret = nullptr;
  }
  return ret;
}

template <>
Optional<std::string> DataElement::Get<std::string>() const {
  Optional<std::string> ret;
  if (type_ == Type::kString) {
    ret = string_;
  }
  return ret;
}

template <>
Optional<UUID> DataElement::Get<UUID>() const {
  Optional<UUID> ret;
  if (type_ == Type::kUuid) {
    ret = uuid_;
  }
  return ret;
}

template <>
Optional<std::vector<DataElement>> DataElement::Get<std::vector<DataElement>>()
    const {
  Optional<std::vector<DataElement>> ret;
  if (type_ == Type::kSequence) {
    ret = aggregate_;
  }
  return ret;
}

void DataElement::SetVariableSize(size_t length) {
  if (length <= std::numeric_limits<uint8_t>::max()) {
    size_ = Size::kNextOne;
  } else if (length <= std::numeric_limits<uint16_t>::max()) {
    size_ = Size::kNextTwo;
  } else {
    size_ = Size::kNextFour;
  }
}

size_t DataElement::Read(DataElement* elem, const common::ByteBuffer& buffer) {
  if (buffer.size() == 0) {
    return 0;
  }
  Type type_desc = static_cast<Type>(buffer[0] & kTypeMask);
  Size size_desc = Size(buffer[0] & kDataElementSizeTypeMask);
  size_t data_bytes = 0;
  size_t bytes_read = 1;
  common::BufferView cursor = buffer.view(bytes_read);
  switch (size_desc) {
    case DataElement::Size::kOneByte:
    case DataElement::Size::kTwoBytes:
    case DataElement::Size::kFourBytes:
    case DataElement::Size::kEightBytes:
    case DataElement::Size::kSixteenBytes:
      if (type_desc != Type::kNull) {
        data_bytes = (1 << uint8_t(size_desc));
      } else {
        data_bytes = 0;
      }
      break;
    case DataElement::Size::kNextOne: {
      if (cursor.size() < sizeof(uint8_t)) {
        return 0;
      }
      data_bytes = cursor.As<uint8_t>();
      bytes_read += sizeof(uint8_t);
      break;
    }
    case DataElement::Size::kNextTwo: {
      if (cursor.size() < sizeof(uint16_t)) {
        return 0;
      }
      data_bytes = betoh16(cursor.As<uint16_t>());
      bytes_read += sizeof(uint16_t);
      break;
    }
    case DataElement::Size::kNextFour: {
      if (cursor.size() < sizeof(uint32_t)) {
        return 0;
      }
      data_bytes = betoh32(cursor.As<uint32_t>());
      bytes_read += sizeof(uint32_t);
      break;
    }
  }
  cursor = buffer.view(bytes_read);
  if (cursor.size() < data_bytes) {
    return 0;
  }
  switch (type_desc) {
    case Type::kNull: {
      if (size_desc != Size::kOneByte) {
        return 0;
      }
      elem->Set(nullptr);
      return bytes_read + data_bytes;
    }
    case Type::kBoolean: {
      if (size_desc != Size::kOneByte) {
        return 0;
      }
      elem->Set(cursor.As<uint8_t>() != 0);
      return bytes_read + data_bytes;
    }
    case Type::kUnsignedInt: {
      if (size_desc == Size::kOneByte) {
        elem->Set(cursor.As<uint8_t>());
      } else if (size_desc == Size::kTwoBytes) {
        elem->Set(betoh16(cursor.As<uint16_t>()));
      } else if (size_desc == Size::kFourBytes) {
        elem->Set(betoh32(cursor.As<uint32_t>()));
      } else if (size_desc == Size::kEightBytes) {
        elem->Set(betoh64(cursor.As<uint64_t>()));
      } else {
        // TODO(jamuraa): support 128-bit uints
        // Invalid size.
        return 0;
      }
      return bytes_read + data_bytes;
    }
    case Type::kSignedInt: {
      if (size_desc == Size::kOneByte) {
        elem->Set(cursor.As<int8_t>());
      } else if (size_desc == Size::kTwoBytes) {
        elem->Set(betoh16(cursor.As<int16_t>()));
      } else if (size_desc == Size::kFourBytes) {
        elem->Set(betoh32(cursor.As<int32_t>()));
      } else if (size_desc == Size::kEightBytes) {
        elem->Set(betoh64(cursor.As<int64_t>()));
      } else {
        // TODO(jamuraa): support 128-bit uints
        // Invalid size.
        return 0;
      }
      return bytes_read + data_bytes;
    }
    case Type::kUuid: {
      if (size_desc == Size::kTwoBytes) {
        elem->Set(UUID(betoh16(cursor.As<uint16_t>())));
      } else if (size_desc == Size::kFourBytes) {
        elem->Set(UUID(betoh32(cursor.As<uint32_t>())));
      } else if (size_desc == Size::kSixteenBytes) {
        common::StaticByteBuffer<16> uuid_bytes;
        // UUID expects these to be in little-endian order.
        cursor.Copy(&uuid_bytes, 0, 16);
        std::reverse(uuid_bytes.mutable_data(), uuid_bytes.mutable_data() + 16);
        UUID uuid;
        UUID::FromBytes(uuid_bytes, &uuid);
        elem->Set(uuid);
      } else {
        return 0;
      }
      return bytes_read + data_bytes;
    }
    case Type::kString: {
      if (static_cast<uint8_t>(size_desc) < 5) {
        return 0;
      }
      std::string str(cursor.data(), cursor.data() + data_bytes);
      elem->Set(str);
      return bytes_read + data_bytes;
    }
    case Type::kSequence:
    case Type::kAlternative: {
      if (static_cast<uint8_t>(size_desc) < 5) {
        return 0;
      }
      common::BufferView sequence_buf = cursor.view(0, data_bytes);
      size_t remaining = data_bytes;
      FXL_CHECK(sequence_buf.size() == data_bytes);
      std::vector<DataElement> seq;
      while (remaining > 0) {
        DataElement next;
        size_t used = Read(&next, sequence_buf);
        if (used == 0) {
          return 0;
        }
        seq.push_back(std::move(next));
        remaining -= used;
      }
      if (type_desc == Type::kAlternative) {
        elem->SetAlternative(std::move(seq));
      } else {
        elem->Set(std::move(seq));
      }
      return bytes_read + data_bytes;
    }
    case Type::kUrl: {
      if (static_cast<uint8_t>(size_desc) < 5) {
        return 0;
      }
      // TODO(jamuraa): implement the URL type.
      return bytes_read + data_bytes;
    }
    default:
      return 0;
  }
}

size_t DataElement::WriteSize() const {
  switch (type_) {
    case Type::kNull:
      return 1;
    case Type::kBoolean:
      return 2;
    case Type::kUnsignedInt:
    case Type::kSignedInt:
    case Type::kUuid:
      return 1 + (1 << static_cast<uint8_t>(size_));
    case Type::kString:
    case Type::kUrl:
      return 1 + (1 << (static_cast<uint8_t>(size_) - 5)) + string_.size();
    case Type::kSequence:
    case Type::kAlternative:
      return 1 + (1 << (static_cast<uint8_t>(size_) - 5)) +
             AggregateSize(aggregate_);
  }
}

size_t DataElement::Write(MutableByteBuffer* buffer) const {
  if (buffer->size() < WriteSize()) {
    FXL_VLOG(4) << "sdp: DataElement: not enough space in buffer ("
                << buffer->size() << " < " << WriteSize() << ")";
    return 0;
  }

  uint8_t type_and_size =
      static_cast<uint8_t>(type_) | static_cast<uint8_t>(size_);
  buffer->Write(&type_and_size, 1);
  size_t pos = 1;

  common::MutableBufferView cursor = buffer->mutable_view(pos);

  switch (type_) {
    case Type::kNull: {
      return pos;
    }
    case Type::kBoolean: {
      uint8_t val = int_value_ != 0 ? 1 : 0;
      cursor.Write(reinterpret_cast<uint8_t*>(&val), sizeof(val));
      pos += 1;
      return pos;
    }
    case Type::kUnsignedInt: {
      if (size_ == Size::kOneByte) {
        uint8_t val = static_cast<uint8_t>(uint_value_);
        cursor.Write(reinterpret_cast<uint8_t*>(&val), sizeof(val));
        pos += sizeof(val);
      } else if (size_ == Size::kTwoBytes) {
        uint16_t val = htobe16(uint16_t(uint_value_));
        cursor.Write(reinterpret_cast<uint8_t*>(&val), sizeof(val));
        pos += sizeof(val);
      } else if (size_ == Size::kFourBytes) {
        uint32_t val = htobe32(uint32_t(uint_value_));
        cursor.Write(reinterpret_cast<uint8_t*>(&val), sizeof(val));
        pos += sizeof(val);
      } else if (size_ == Size::kFourBytes) {
        uint64_t val = htobe64(uint64_t(uint_value_));
        cursor.Write(reinterpret_cast<uint8_t*>(&val), sizeof(val));
        pos += sizeof(val);
      }
      return pos;
    }
    case Type::kSignedInt: {
      if (size_ == Size::kOneByte) {
        int8_t val = int8_t(int_value_);
        cursor.Write(reinterpret_cast<uint8_t*>(&val), sizeof(val));
        pos += sizeof(val);
      } else if (size_ == Size::kTwoBytes) {
        int16_t val = htobe16(int16_t(int_value_));
        cursor.Write(reinterpret_cast<uint8_t*>(&val), sizeof(val));
        pos += sizeof(val);
      } else if (size_ == Size::kFourBytes) {
        int32_t val = htobe32(int32_t(int_value_));
        cursor.Write(reinterpret_cast<uint8_t*>(&val), sizeof(val));
        pos += sizeof(val);
      } else if (size_ == Size::kFourBytes) {
        int64_t val = htobe64(int64_t(int_value_));
        cursor.Write(reinterpret_cast<uint8_t*>(&val), sizeof(val));
        pos += sizeof(val);
      }
      return pos;
    }
    case Type::kUuid: {
      size_t written = uuid_.ToBytes(&cursor);
      FXL_DCHECK(written);
      // SDP is big-endian, so reverse.
      std::reverse(cursor.mutable_data(), cursor.mutable_data() + written);
      pos += written;
      return pos;
    }
    case Type::kString: {
      size_t used = WriteLength(&cursor, string_.size());
      FXL_DCHECK(used);
      pos += used;
      cursor.Write(reinterpret_cast<const uint8_t*>(string_.c_str()),
                   string_.size(), used);
      pos += string_.size();
      return pos;
    }
    case Type::kSequence:
    case Type::kAlternative: {
      size_t used = WriteLength(&cursor, AggregateSize(aggregate_));
      FXL_DCHECK(used);
      pos += used;
      cursor = cursor.mutable_view(used);
      for (const auto& elem : aggregate_) {
        used = elem.Write(&cursor);
        FXL_DCHECK(used);
        pos += used;
        cursor = cursor.mutable_view(used);
      }
      return pos;
    }
    default: {
      // Fallthrough to error.
    }
  }
  return 0;
}

const DataElement* DataElement::At(size_t idx) const {
  if ((type_ != Type::kSequence) || (idx >= aggregate_.size())) {
    return nullptr;
  }
  return &aggregate_[idx];
}

}  // namespace sdp
}  // namespace btlib
