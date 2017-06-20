// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "advertising_data.h"

#include "apps/bluetooth/lib/common/byte_buffer.h"
#include "lib/ftl/logging.h"

namespace bluetooth {
namespace gap {

AdvertisingDataReader::AdvertisingDataReader(const common::ByteBuffer& data)
    : is_valid_(true), remaining_(data) {
  if (!remaining_.size()) {
    is_valid_ = false;
    return;
  }

  // Do a validity check.
  common::BufferView tmp(remaining_);
  while (tmp.size()) {
    size_t tlv_len = tmp[0];

    // A struct can have 0 as its length. In that case its valid to terminate.
    if (!tlv_len) break;

    // The full struct includes the length octet itself.
    size_t struct_size = tlv_len + 1;
    if (struct_size > tmp.size()) {
      is_valid_ = false;
      break;
    }

    tmp = tmp.view(struct_size);
  }
}

bool AdvertisingDataReader::GetNextField(DataType* out_type, common::BufferView* out_data) {
  FTL_DCHECK(out_type);
  FTL_DCHECK(out_data);

  if (!HasMoreData()) return false;

  size_t tlv_len = remaining_[0];
  size_t cur_struct_size = tlv_len + 1;
  FTL_DCHECK(cur_struct_size <= remaining_.size());

  *out_type = static_cast<DataType>(remaining_[1]);
  *out_data = remaining_.view(2, tlv_len - 1);

  // Update |remaining_|.
  remaining_ = remaining_.view(cur_struct_size);
  return true;
}

bool AdvertisingDataReader::HasMoreData() const {
  if (!is_valid_ || !remaining_.size()) return false;

  // If the buffer is valid and has remaining bytes but the length of the next segment is zero, then
  // we terminate.
  return !!remaining_[0];
}

AdvertisingDataWriter::AdvertisingDataWriter(common::MutableByteBuffer* buffer)
    : buffer_(buffer), bytes_written_(0u) {
  FTL_DCHECK(buffer_);
}

bool AdvertisingDataWriter::WriteField(DataType type, const common::ByteBuffer& data) {
  size_t next_size = data.size() + 2;  // 2 bytes for [length][type].
  if (bytes_written_ + next_size > buffer_->size() || next_size > 255) return false;

  (*buffer_)[bytes_written_++] = static_cast<uint8_t>(next_size) - 1;
  (*buffer_)[bytes_written_++] = static_cast<uint8_t>(type);

  // Get a view into the offset we want to write to.
  auto target = buffer_->mutable_view(bytes_written_);

  // Copy the data into the view.
  size_t written = data.Copy(&target);
  FTL_DCHECK(written == data.size());

  bytes_written_ += written;

  return true;
}

}  // namespace gap
}  // namespace bluetooth
