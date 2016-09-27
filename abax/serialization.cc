// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/abax/serialization.h"

#include <string>

#include "apps/ledger/abax/constants.h"
#include "apps/ledger/convert/convert.h"
#include "apps/ledger/glue/crypto/hash.h"
#include "lib/ftl/logging.h"

namespace ledger {

namespace {

const char kPagePrefix[] = "/page/";
const char kReferenceRowPrefix[] = "/reference/";
const char kValueRowPrefix[] = "/value/";
const char kMetaRowKey[] = "/__METADATA";

}  // namespace

Serialization::Serialization(const mojo::Array<uint8_t>& page_id)
    : page_prefix_(kPagePrefix + convert::ToString(page_id)),
      reference_row_prefix_(page_prefix_ + kReferenceRowPrefix),
      value_row_prefix_(page_prefix_ + kValueRowPrefix),
      meta_row_key_(page_prefix_ + kMetaRowKey) {}
Serialization::~Serialization() {}

std::string Serialization::GetReferenceRowKey(
    const mojo::Array<uint8_t>& entry_key) {
  return reference_row_prefix_ + convert::ToString(entry_key);
}

mojo::Array<uint8_t> Serialization::GetEntryKey(
    const std::string& reference_row_key) {
  const char* data = reference_row_key.data() + reference_row_prefix_.length();
  const int size = reference_row_key.size() - reference_row_prefix_.length();
  mojo::Array<uint8_t> result = mojo::Array<uint8_t>::New(size);
  memcpy(result.data(), data, size);
  return result;
}

std::string Serialization::GetValueRowKey(
    const convert::BytesReference& entry_value) {
  return value_row_prefix_ +
         glue::SHA256Hash(entry_value.data(), entry_value.size());
}

std::string Serialization::MetaRowKey() {
  return meta_row_key_;
}

std::string Serialization::PagePrefix() {
  return page_prefix_;
}

std::map<std::string, std::string>::const_iterator Serialization::PrefixEnd(
    const std::map<std::string, std::string>& db,
    const std::string& prefix) {
  std::string next_prefix = prefix;
  bool incremented = false;
  for (std::string::reverse_iterator c = next_prefix.rbegin();
       c != next_prefix.rend(); ++c) {
    if (*c != std::numeric_limits<char>::max()) {
      ++*c;
      incremented = true;
      break;
    }
    *c = std::numeric_limits<char>::min();
  }
  return incremented ? db.lower_bound(next_prefix) : db.end();
}

}  // namespace ledger
