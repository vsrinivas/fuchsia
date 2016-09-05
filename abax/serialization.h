// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_LEDGER_ABAX_SERIALIZATION_H_
#define APPS_LEDGER_ABAX_SERIALIZATION_H_

#include <map>

#include "apps/ledger/convert/convert.h"
#include "lib/ftl/macros.h"
#include "mojo/public/cpp/bindings/array.h"

namespace ledger {

// Defines the representation of entry keys and values in the database. Rows are
// serialized as follows.
//
// Metadata row is used to verify if the page exists:
//  - Key: "/page/<page-id>/__METADATA"
//  - Value: ""
//
// Reference rows store references to entry values:
//  - Key: "/page/<page-id>/reference/<entry-key>"
//  - Value: "/page/<page-id>/value/<reference-hash>"
//
// Value rows store values of entry value references.
//  - Key: "/page/<page-id>/value/<reference-hash>"
//  - Value: "<entry-value>"
class Serialization {
 public:
  Serialization(const mojo::Array<uint8_t>& page_id);
  ~Serialization();

  // Returns the key of the reference row for the given entry key.
  std::string GetReferenceRowKey(const mojo::Array<uint8_t>& entry_key);

  // Returns the entry key based on the reference row key.
  mojo::Array<uint8_t> GetEntryKey(const std::string& reference_row_key);

  // Returns the key of the value row for the given entry value. This is
  // computed based on a hash of the entry value.
  std::string GetValueRowKey(const convert::BytesReference& entry_value);

  // Returns the key representation of the metadata row.
  std::string MetaRowKey();

  // Returns the key prefix of keys in this page, including the metadata row.
  std::string PagePrefix();

  std::map<std::string, std::string>::const_iterator PrefixEnd(
      const std::map<std::string, std::string>& db, const std::string& prefix);

 private:
  // The prefix of all keys in this page. It uses the format: /page_<pageId>
  // where pageId is the string representation of the page id.
  const std::string page_prefix_;
  const std::string reference_row_prefix_;
  const std::string value_row_prefix_;
  const std::string meta_row_key_;

  FTL_DISALLOW_COPY_AND_ASSIGN(Serialization);
};

}  // namespace ledger

#endif  // APPS_LEDGER_ABAX_SERIALIZATION_H_
