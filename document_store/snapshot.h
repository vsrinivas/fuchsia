// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#pragma once

#include <unordered_map>

#include "apps/ledger/services/ledger.fidl.h"
#include "apps/modular/document_store/documents.h"
#include "apps/modular/services/document_store/document.fidl.h"
#include "apps/modular/services/document_store/document_store.fidl.h"
#include "lib/fidl/cpp/bindings/binding.h"
#include "lib/ftl/macros.h"

namespace document_store {

namespace internal {

// Implements the logic of a document_store.Filter.
class DocumentFilter {
 public:
  DocumentFilter(FilterPtr filter) : filter_(std::move(filter)) {
    // We currently only support null and simple filters.
    FTL_CHECK(filter_.is_null() || filter_->is_simple());
  }

  bool Matches(const DocumentPtr& document) {
    // A null filter matches all documents.
    if (filter_.is_null()) {
      return true;
    }

    auto entry = document->properties.find(filter_->get_simple()->property);

    if (entry == document->properties.end()) {
      return false;
    }

    if (filter_->get_simple()->value.is_null()) {
      return true;
    }

    return (filter_->get_simple()->value.Equals(entry.GetValue()));
  }

 private:
  FilterPtr filter_;
};

}  // namespace internal

// Implements the Snapshot interface.
class SnapshotImpl : public Snapshot {
 public:
  SnapshotImpl(
      fidl::InterfaceHandle<ledger::PageSnapshot> ledger_snapshot_handle)
      : snapshot_(
            ledger::PageSnapshotPtr::Create(std::move(ledger_snapshot_handle))),
        binding_(this) {}

  void GetOne(const fidl::String& docid,
              const GetOneCallback& callback) override {
    fidl::Array<uint8_t> key_prefix;
    internal::DocumentLedgerKeyPrefix(docid, &key_prefix);
    snapshot_->GetEntries(
        std::move(key_prefix),
        NULL,  // token should be NULL on the first call to GetEntries.
        [callback](ledger::Status ledger_status,
                   fidl::Array<ledger::EntryPtr> entries,
                   fidl::Array<uint8_t> next_token) {
          DocumentPtr doc;
          if (ledger_status != ledger::Status::OK) {
            callback(internal::LedgerStatusToStatus(ledger_status),
                     std::move(doc));
            return;
          }

          if (entries.size() == 0) {
            callback(Status::DOCUMENT_NOT_FOUND, std::move(doc));
            return;
          }

          auto entries_begin = entries.begin();
          auto entries_end = entries.end();
          if (!internal::NextDocumentFromEntries(&entries_begin, entries_end,
                                                 &doc)) {
            doc.reset();
            callback(Status::DOCUMENT_DATA_ERROR, std::move(doc));
            return;
          }

          if (internal::IsDocumentDeleted(doc)) {
            doc.reset();
            callback(Status::DOCUMENT_NOT_FOUND, std::move(doc));
            return;
          }

          callback(Status::OK, std::move(doc));
        });
  };

  void Get(fidl::Array<fidl::String> docids,
           const GetCallback& callback) override {
    FTL_LOG(FATAL) << "Not implemented yet!";
  };

  void ExecuteQuery(QueryPtr query,
                    const ExecuteQueryCallback& callback) override {
    Filter query_filter;
    internal::DocumentFilter* filter =
        new internal::DocumentFilter(std::move(query->filter));
    // We fetch all documents in the store and filter them for those that match
    // the filter.
    snapshot_->GetEntries(
        NULL,  // NULL key prefix to get all entries.
        NULL,  // token should be NULL on the first call to GetEntries.
        [filter, callback](ledger::Status ledger_status,
                           fidl::Array<ledger::EntryPtr> entries,
                           fidl::Array<uint8_t> next_token) {
          fidl::Array<DocumentPtr> documents;
          std::unique_ptr<internal::DocumentFilter> cleanup_filter(filter);

          if (ledger_status != ledger::Status::OK) {
            callback(internal::LedgerStatusToStatus(ledger_status),
                     std::move(documents));
            return;
          }

          documents = fidl::Array<DocumentPtr>::New(0);
          auto entries_it = entries.begin();
          DocumentPtr doc;
          while (entries_it != entries.end()) {
            if (!internal::NextDocumentFromEntries(&entries_it, entries.end(),
                                                   &doc)) {
              documents.reset();
              callback(Status::DOCUMENT_DATA_ERROR, std::move(documents));
              return;
            }

            if (!internal::IsDocumentDeleted(doc) && filter->Matches(doc)) {
              documents.push_back(std::move(doc));
            }
          }
          callback(Status::OK, std::move(documents));
        });
  };

  void Bind(fidl::InterfaceRequest<Snapshot> request) {
    binding_.Bind(std::move(request));
  }

 private:
  ledger::PageSnapshotPtr snapshot_;
  fidl::Binding<Snapshot> binding_;
  FTL_DISALLOW_COPY_AND_ASSIGN(SnapshotImpl);
};
}  // namespace document_store
