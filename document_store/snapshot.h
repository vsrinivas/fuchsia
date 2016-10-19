// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/api/ledger.mojom.h"
#include "apps/maxwell/document_store/interfaces/document.mojom.h"
#include "apps/maxwell/document_store/interfaces/document_store.mojom.h"

namespace document_store {

namespace internal {

// Every ledger key associated with a document has the same prefix derived from
// the document's id.
// This function computes the ledger key prefix for a particular document id.
void DocumentLedgerKeyPrefix(const mojo::String& docid,
                             mojo::Array<uint8_t>* key_prefix) {
  size_t key_size = 2 + docid.size();
  *key_prefix = mojo::Array<uint8_t>::New(key_size);
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
bool PropertyFromLedgerKey(const mojo::Array<uint8_t>& key, mojo::String* docid,
                           mojo::String* property) {
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
    MOJO_LOG(ERROR) << "Invalid document key!";
    return false;
  }

  // We cheat and use the fact that we are null-terminating our docid and
  // property names.
  // TODO(azani): Do this without reinterpret_cast?
  *docid = reinterpret_cast<const char*>(&key_raw[1]);
  *property = reinterpret_cast<const char*>(&key_raw[first_null + 1]);

  return true;
}

// Deserialize a value stored on the ledger.
bool DeserializeValue(const mojo::Array<uint8_t>& serialized, ValuePtr* value) {
  *value = Value::New();

  switch (serialized[0]) {
    case 's':
      mojo::String string(reinterpret_cast<const char*>(serialized.data() + 1),
                          serialized.size() - 1);
      (*value)->set_string_value(std::move(string));
      return true;
  };
  MOJO_LOG(ERROR) << "Unrecognized data type.";
  return false;
}

// Decodes a property from a ledger entry.
bool PropertyFromEntry(const ledger::EntryPtr& entry, mojo::String* docid,
                       PropertyPtr* property) {
  *property = Property::New();
  if (!PropertyFromLedgerKey(entry->key, docid, &((*property)->property))) {
    return false;
  }
  if (!DeserializeValue(entry->value, &((*property)->value))) {
    return false;
  }
  return true;
}

// Decodes a whole document from a list of ledger entries.
bool DocumentFromEntries(const mojo::Array<ledger::EntryPtr>& entries,
                         DocumentPtr* doc) {
  *doc = Document::New();
  (*doc)->properties = mojo::Array<PropertyPtr>::New(entries.size());

  mojo::String docid;
  PropertyPtr property;
  for (size_t i = 0; i < entries.size(); i++) {
    if (!PropertyFromEntry(entries[i], &docid, &property)) {
      return false;
    }
    (*doc)->properties[i] = std::move(property);
  }

  (*doc)->docid = std::move(docid);

  return true;
}

}  // namespace internal

// Implements the Snapshot interface.
class SnapshotImpl : public Snapshot {
 public:
  SnapshotImpl(
      mojo::InterfaceHandle<ledger::PageSnapshot> ledger_snapshot_handle)
      : snapshot_(ledger::PageSnapshotPtr::Create(
            std::move(ledger_snapshot_handle))) {}

  void GetOne(const mojo::String& docid,
              const GetOneCallback& callback) override {
    mojo::Array<uint8_t> key_prefix;
    internal::DocumentLedgerKeyPrefix(docid, &key_prefix);
    snapshot_->GetAll(
        std::move(key_prefix),
        [callback](ledger::Status ledger_status,
                   mojo::Array<ledger::EntryPtr> entries) {
          DocumentPtr doc;
          if (ledger_status != ledger::Status::OK) {
            callback.Run(internal::LedgerStatusToStatus(ledger_status),
                         std::move(doc));
          } else if (entries.size() == 0) {
            callback.Run(Status::DOCUMENT_NOT_FOUND, std::move(doc));
          } else if (!internal::DocumentFromEntries(entries, &doc)) {
            doc.reset();
            callback.Run(Status::DOCUMENT_DATA_ERROR, std::move(doc));
          } else {
            callback.Run(Status::OK, std::move(doc));
          }
        });
  };

  void Get(mojo::Array<mojo::String> docids,
           const GetCallback& callback) override {
    MOJO_LOG(FATAL) << "Not implemented yet!";
  };

  void ExecuteQuery(QueryPtr query,
                    const ExecuteQueryCallback& callback) override {
    MOJO_LOG(FATAL) << "Haha! As if that was implemented yet!";
  };

 private:
  ledger::PageSnapshotPtr snapshot_;
  MOJO_DISALLOW_COPY_AND_ASSIGN(SnapshotImpl);
};
}  // namespace document_store
