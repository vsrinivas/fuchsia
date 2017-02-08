// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <cstddef>

#include "apps/bluetooth/gap/gap.h"

// This file contains convenience classes for reading and writing the contents
// of Advertising Data, Scan Response Data, or Extended Inquiry Response Data
// payloads. The format in which data is stored looks like the following:
//
//    [1-octet LENGTH][1-octet TYPE][LENGTH-1 octets DATA]
//
// See Core Spec v5.0, Vol 3, Part C, Section 11, and the Core Specification
// Supplement v7 for more information.

namespace bluetooth {
namespace common {

class BufferView;
class ByteBuffer;

}  // namespace common

namespace gap {

// Class for extracting EIR, AD, and Scan Rsp.
class AdvertisingDataReader {
 public:
  // |data| must point to a valid piece of memory for the duration in which this
  // object is to remain alive.
  explicit AdvertisingDataReader(const common::ByteBuffer& data);

  // Returns false if the fields of |data| have been formatted incorrectly. For
  // example, this could happen if the length of an advertising data structure
  // would exceed the bounds of the buffer.
  inline bool is_valid() const { return is_valid_; }

  // Returns the data and type fields of the next advertising data structure in
  // |out_data| and |out_type|. Returns false if there is no more data to read
  // or if the data is formatted incorrectly.
  bool GetNextField(DataType* out_type, common::BufferView* out_data);

  // Returns true if there is more data to read. Returns false if the end of
  // data has been reached or if the current segment is malformed in a way that
  // would exceed the bounds of the data this reader was initialized with.
  bool HasMoreData() const;

 private:
  bool is_valid_;
  const uint8_t* ptr_;
  size_t remaining_bytes_;
};

// TODO(armansito): Add AdvertisingDataWriter

}  // namespace gap
}  // namespace bluetooth
