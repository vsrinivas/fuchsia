// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#pragma once

#include "apps/ledger/services/ledger.fidl.h"
#include "apps/modular/src/document_store/values.h"
#include "apps/modular/services/document_store/document.fidl.h"
#include "lib/ftl/macros.h"

namespace document_store {
namespace internal {
// TODO(azani): Write unit tests.
// Constructs the key at which the specified property for the specified document
// is to be stored.
void LedgerKeyForProperty(const fidl::String& docid,
                          const fidl::String& property,
                          fidl::Array<uint8_t>* key) {
  // TODO(azani): Support collection index.
  size_t key_size = 3 + docid.size() + property.size();
  *key = fidl::Array<uint8_t>::New(key_size);
  uint8_t* key_raw = key->data();

  const char* docid_raw = docid.data();
  size_t docid_size = docid.size();
  const char* property_raw = property.data();
  size_t property_size = property.size();

  // First byte is a prefix to differentiate document values from metadata or
  // index data.
  key_raw[0] = 'v';

  std::copy(docid_raw, docid_raw + docid_size, &key_raw[1]);
  key_raw[docid_size + 2] = '\0';
  std::copy(property_raw, property_raw + property_size,
            &key_raw[2 + docid_size]);
  key_raw[3 + docid_size + property_size] = '\0';
}

// Construct the ledger key at which to store the specified property and
// serialize the property's value for storage on the ledger.
void LedgerKeyValueForProperty(const fidl::String& docid,
                               const fidl::String& property,
                               const ValuePtr& value,
                               fidl::Array<uint8_t>* ledger_key,
                               fidl::Array<uint8_t>* ledger_value) {
  LedgerKeyForProperty(docid, property, ledger_key);
  ledger_value->reset();
  if (!value.is_null()) {
    SerializeValue(value, ledger_value);
  }
}

// Every ledger key associated with a document has the same prefix derived from
// the document's id.
// This function computes the ledger key prefix for a particular document id.
void DocumentLedgerKeyPrefix(const fidl::String& docid,
                             fidl::Array<uint8_t>* key_prefix) {
  size_t key_size = 2 + docid.size();
  *key_prefix = fidl::Array<uint8_t>::New(key_size);
  uint8_t* key_raw = key_prefix->data();

  const char* docid_raw = docid.data();
  size_t docid_size = docid.size();

  key_raw[0] = 'v';
  std::copy(docid_raw, docid_raw + docid_size, &key_raw[1]);
  key_raw[docid_size + 2] = '\0';
}

// A ledger key encodes the document id and property name for a particular
// document-property pair.
// This function decodes the document id and property name from a particular
// ledger key.
bool PropertyFromLedgerKey(const fidl::Array<uint8_t>& key, fidl::String* docid,
                           fidl::String* property) {
  const uint8_t* key_raw = key.data();
  // Since the first byte is a prefix, it cannot be null.
  size_t first_null = 0;
  size_t second_null = 0;

  for (size_t i = 0; i < key.size(); ++i) {
    if (key_raw[i] == '\0') {
      if (first_null == 0) {
        first_null = i;
      } else {
        second_null = i;
        break;
      };
    }
  }

  if (first_null == 0 || second_null == 0) {
    FTL_LOG(ERROR) << "Invalid document key!";
    return false;
  }

  // We cheat and use the fact that we are null-terminating our docid and
  // property names.
  // TODO(azani): Do this without reinterpret_cast?
  *docid = reinterpret_cast<const char*>(&key_raw[1]);
  *property = reinterpret_cast<const char*>(&key_raw[first_null + 1]);

  return true;
}

bool DocidFromLedgerKey(const fidl::Array<uint8_t>& key, fidl::String* docid) {
  const uint8_t* key_raw = key.data();
  size_t null_pos = 0;
  for (size_t i = 0; i < key.size(); ++i) {
    if (key_raw[i] == '\0') {
      null_pos = i;
      break;
    }
  }

  if (null_pos == 0) {
    FTL_LOG(ERROR) << "Invalid document key!";
    return false;
  }

  *docid = reinterpret_cast<const char*>(&key_raw[1]);

  return true;
}

// Decodes a property from a ledger entry.
bool PropertyValueFromEntry(const ledger::EntryPtr& entry, fidl::String* docid,
                            fidl::String* property, ValuePtr* value) {
  if (!PropertyFromLedgerKey(entry->key, docid, property)) {
    return false;
  }
  if (!DeserializeValue(entry->value, value)) {
    return false;
  }
  return true;
}

// Checks whether a document that was found was deleted.
bool IsDocumentDeleted(const DocumentPtr& document) {
  return document->properties.at("docid")->is_empty();
}

// Decodes the next document in the list of ledger entries.
bool NextDocumentFromEntries(fidl::Array<ledger::EntryPtr>::Iterator* it,
                             const fidl::Array<ledger::EntryPtr>::Iterator& end,
                             DocumentPtr* doc) {
  *doc = Document::New();

  fidl::String docid;
  fidl::String property;
  ValuePtr value;
  while (*it != end) {
    bool result = PropertyValueFromEntry(**it, &docid, &property, &value);

    if (!(*doc)->docid.is_null() && (*doc)->docid != docid) {
      return true;
    }

    if (!result) {
      return false;
    }

    (*doc)->properties.insert(property, std::move(value));
    (*doc)->docid = std::move(docid);
    ++(*it);
  }

  return true;
}

}  // namespace internal
}  // namespace document_store
