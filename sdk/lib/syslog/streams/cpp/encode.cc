// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "encode.h"

#include <fuchsia/diagnostics/cpp/fidl.h>
#include <fuchsia/diagnostics/stream/cpp/fidl.h>
#include <lib/zx/clock.h>
#include <zircon/status.h>

#include <iostream>
#include <vector>

#include "fields.h"

namespace streams {
namespace {

const int WORD_SIZE = 8;

size_t padding_8byte(std::vector<uint8_t>* out) {
  size_t length = out->size();
  if (length % WORD_SIZE != 0) {
    out->insert(out->end(), WORD_SIZE - length % WORD_SIZE, 0);
    return WORD_SIZE - length % WORD_SIZE;
  }

  return 0;
}

size_t write_string(const std::string& str, std::vector<uint8_t>* out) {
  out->insert(out->end(), std::begin(str), std::end(str));
  size_t extra_padding = padding_8byte(out);
  return (str.size() + extra_padding) / WORD_SIZE;
}

size_t write_signed_int(int64_t signed_int, std::vector<uint8_t>* out) {
  size_t orig_length = out->size();
  out->resize(out->size() + sizeof(int64_t));
  auto* val_ptr = reinterpret_cast<int64_t*>(out->data() + orig_length);
  *val_ptr = signed_int;
  return sizeof(int64_t) / WORD_SIZE;
}

size_t write_unsigned_int(uint64_t unsigned_int, std::vector<uint8_t>* out) {
  size_t orig_length = out->size();
  out->resize(out->size() + sizeof(uint64_t));
  auto* val_ptr = reinterpret_cast<uint64_t*>(out->data() + orig_length);
  *val_ptr = unsigned_int;
  return sizeof(uint64_t) / WORD_SIZE;
}

size_t write_float(double f, std::vector<uint8_t>* out) {
  size_t orig_length = out->size();
  out->resize(out->size() + sizeof(double));
  auto* val_ptr = reinterpret_cast<double*>(out->data() + orig_length);
  *val_ptr = f;
  return sizeof(double) / WORD_SIZE;
}

size_t log_argument(const std::string& name, const fuchsia::diagnostics::stream::Value& value,
                    std::vector<uint8_t>* out) {
  size_t header_idx = out->size();
  out->resize(header_idx + WORD_SIZE);  // WORD_SIZE=8 is for the header
  size_t s_size = write_string(name, out);
  size_t arg_size = s_size + 1;  // 1 is for the header size

  int type = 0;
  uint64_t value_ref = 0;
  switch (value.Which()) {
    case fuchsia::diagnostics::stream::Value::Tag::kSignedInt:
      type = 3;
      arg_size += write_signed_int(value.signed_int(), out);
      break;

    case fuchsia::diagnostics::stream::Value::Tag::kUnsignedInt:
      type = 4;
      arg_size += write_unsigned_int(value.unsigned_int(), out);
      break;

    case fuchsia::diagnostics::stream::Value::Tag::kFloating:
      type = 5;
      arg_size += write_float(value.floating(), out);
      break;

    case fuchsia::diagnostics::stream::Value::Tag::kText:
      type = 6;
      arg_size += write_string(value.text(), out);
      value_ref = value.text().length() > 0 ? (1 << 15) | value.text().length() : 0;
      break;

    case fuchsia::diagnostics::stream::Value::Tag::kUnknown:
      break;
    default:
      break;
  }
  uint64_t header = ArgumentFields::Type::Make(type) | ArgumentFields::SizeWords::Make(arg_size) |
                    ArgumentFields::NameRefVal::Make(name.length()) |
                    ArgumentFields::NameRefMSB::Make(name.length() > 0 ? 1 : 0) |
                    ArgumentFields::ValueRef::Make(value_ref) | ArgumentFields::Reserved::Make(0);

  std::memcpy(out->data() + header_idx, &header, WORD_SIZE);
  return arg_size;
}

}  // namespace

zx_status_t log_record(const fuchsia::diagnostics::stream::Record& record,
                       std::vector<uint8_t>* out) {
  // Keep index to write header at the end
  size_t idx = out->size();
  out->resize(out->size() + WORD_SIZE);  // WORD_SIZE=8 is header size in bytes

  // Add timstamp
  size_t time_index = out->size();
  zx_time_t time = record.timestamp;
  out->resize(out->size() + sizeof(time));
  std::memcpy(out->data() + time_index, &time, sizeof(time));
  // Add the arguments
  size_t record_size = 2;  // 2 words for Record Header
  for (unsigned long i = 0; i < record.arguments.size(); i++) {
    record_size += log_argument(record.arguments[i].name, record.arguments[i].value, out);
  }
  uint64_t header = HeaderFields::Type::Make(9) | HeaderFields::SizeWords::Make(record_size) |
                    HeaderFields::Reserved::Make(0) | HeaderFields::Severity::Make(record.severity);
  // Set size and write header
  std::memcpy(out->data() + idx, &header, WORD_SIZE);
  return ZX_OK;
}

}  // namespace streams
