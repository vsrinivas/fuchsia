// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#pragma once

#include <vector>

#include "apps/ledger/services/ledger.fidl.h"
#include "apps/modular/services/document_store/document.fidl.h"
#include "apps/modular/services/document_store/document_store.fidl.h"
#include "apps/modular/src/document_store/documents.h"

#include "lib/fidl/cpp/bindings/binding.h"
#include "lib/fidl/cpp/bindings/interface_ptr_set.h"
#include "lib/ftl/macros.h"

namespace document_store {

namespace internal {

// LedgerStatusTracker allows the return values of many ledger operations to
// be aggregated into a single return value: The latest error to be reported
// or ledger::Status::OK if there were no errors.
class LedgerStatusTracker {
 public:
  // callback is the function to be called when all the ledger statuses
  // have been reported.
  LedgerStatusTracker(int expected,
                      std::function<void(ledger::Status)> callback)
      : left_(expected), callback_(callback) {}

  void Run(ledger::Status status) {
    left_--;
    if (status != ledger::Status::OK) {
      status_ = status;
    }

    if (left_ <= 0) {
      callback_(status_);
      delete this;
    }
  }

 private:
  int left_;
  ledger::Status status_ = ledger::Status::OK;
  std::function<void(ledger::Status)> callback_;
};

// VoidCallbackTracker allows N calls to callback functions to translate to
// a single call to a callback functions. This is used when a single call with
// an empty return value is implemented as N calls and we want to make sure we
// don't return from the initial call before the other calls have returned.
class VoidCallbackTracker {
 public:
  VoidCallbackTracker(int expected, std::function<void()> callback)
      : left_(expected), callback_(callback) {}

  void Run() {
    left_--;
    if (left_ <= 0) {
      callback_();
      delete this;
    }
  }

 private:
  int left_;
  std::function<void()> callback_;
};

}  // namespace internal

// Implements the Transaction interface.
class TransactionImpl : public Transaction {
 public:
  TransactionImpl(fidl::InterfaceHandle<ledger::Page> page_handle)
      : page_(ledger::PagePtr::Create(std::move(page_handle))),
        binding_(this) {}

  ~TransactionImpl() {
    page_->Rollback([](ledger::Status ledger_status) {});
  }

  void Initialize(std::function<void(Status status)> callback) {
    page_->StartTransaction([this, callback](ledger::Status ledger_status) {
      if (ledger::Status::OK != ledger_status) {
        callback(internal::LedgerStatusToStatus(ledger_status));
        return;
      }
      page_->GetSnapshot(
          GetProxy(&snapshot_), [this, callback](ledger::Status ledger_status) {
            if (ledger::Status::OK != ledger_status) {
              snapshot_.reset();
            }
            callback(internal::LedgerStatusToStatus(ledger_status));
          });
    });
  }

  void Add(fidl::Array<document_store::DocumentPtr> docs,
           const AddCallback& callback) override {
    internal::VoidCallbackTracker* callback_tracker =
        new internal::VoidCallbackTracker(docs.size(),
                                          [callback]() { callback(); });
    for (size_t i = 0; i < docs.size(); ++i) {
      AddOne(std::move(docs.at(i)),
             [callback_tracker]() { callback_tracker->Run(); });
    }
  };

  void AddOne(document_store::DocumentPtr document,
              const AddOneCallback& callback) override {
    internal::VoidCallbackTracker* callback_tracker =
        new internal::VoidCallbackTracker(document->properties.size() + 1,
                                          [callback]() { callback(); });
    fidl::Array<uint8_t> key;
    fidl::Array<uint8_t> value;

    // Add the synthetic "docid" property.
    ValuePtr docid_value(Value::New());
    docid_value->set_iri(document->docid);
    internal::LedgerKeyValueForProperty(document->docid, "docid", docid_value,
                                        &key, &value);
    page_->Put(std::move(key), std::move(value),
               [callback_tracker](ledger::Status ledger_status) {
                 callback_tracker->Run();
               });

    for (auto it = document->properties.cbegin();
         document->properties.cend() != it; ++it) {
      internal::LedgerKeyValueForProperty(document->docid, it.GetKey(),
                                          it.GetValue(), &key, &value);
      if (!value.is_null()) {
        page_->Put(std::move(key), std::move(value),
                   [callback_tracker](ledger::Status ledger_status) {
                     callback_tracker->Run();
                   });
      } else {
        page_->Delete(std::move(key),
                      [callback_tracker](ledger::Status ledger_status) {
                        callback_tracker->Run();
                      });
      }
    }
  };

  void AddReplace(fidl::Array<document_store::DocumentPtr> docs,
                  const AddReplaceCallback& callback) override {
    FTL_LOG(FATAL) << "Not implemented yet!";
  };

  void AddReplaceOne(document_store::DocumentPtr doc,
                     const AddReplaceOneCallback& callback) override {
    FTL_LOG(FATAL) << "Not implemented yet!";
  };

  void Delete(fidl::Array<fidl::String> docids,
              const DeleteCallback& callback) override {
    internal::VoidCallbackTracker* callback_tracker =
        new internal::VoidCallbackTracker(docids.size(),
                                          [callback]() { callback(); });
    for (size_t i = 0; i < docids.size(); ++i) {
      DeleteOne(std::move(docids.at(i)),
                [callback_tracker]() { callback_tracker->Run(); });
    }
  };

  void DeleteOne(const fidl::String& docid,
                 const DeleteOneCallback& callback) override {
    fidl::Array<uint8_t> key_prefix;
    internal::DocumentLedgerKeyPrefix(docid, &key_prefix);
    // Get and queue-up for deletion all the keys for this document.
    snapshot_->GetKeys(
        std::move(key_prefix),
        NULL,  // token should be NULL on the first call to GetKeys.
        [this, callback](ledger::Status ledger_status,
                         fidl::Array<fidl::Array<uint8_t>> keys,
                         fidl::Array<uint8_t> next_token) {
          // TODO(azani): Surface the ledger error if there was one.
          if (keys.size() == 0) {
            callback();
            return;
          }
          internal::VoidCallbackTracker* callback_tracker =
              new internal::VoidCallbackTracker(keys.size() + 1,
                                                [callback]() { callback(); });

          fidl::String docid;
          internal::DocidFromLedgerKey(keys[0], &docid);

          for (size_t i = 0; i < keys.size(); ++i) {
            page_->Delete(std::move(keys[i]),
                          [callback_tracker](ledger::Status ledger_status) {
                            callback_tracker->Run();
                          });
          }

          // The docid property with an empty value is a tombstone for a
          // document.
          ValuePtr docid_value(Value::New());
          docid_value->set_empty(true);
          fidl::Array<uint8_t> tombstone_key;
          fidl::Array<uint8_t> tombstone_value;
          internal::LedgerKeyValueForProperty(docid, "docid", docid_value,
                                              &tombstone_key, &tombstone_value);
          page_->Put(std::move(tombstone_key), std::move(tombstone_value),
                     [callback_tracker](ledger::Status ledger_status) {
                       callback_tracker->Run();
                     });
        });
  };

  void ApplyStatementMutations(
      fidl::Array<StatementMutationPtr> mutations) override {
    FTL_LOG(FATAL) << "ApplyStatementMutations not implemented!";
  };

  void Commit(const CommitCallback& callback) override {
    page_->Commit([this, callback](ledger::Status ledger_status) {
      Initialize([this, callback, ledger_status](Status status) {
        callback(internal::LedgerStatusToStatus(ledger_status));

        // TODO(azani): Close the connection here if either of the checks fail.
        FTL_CHECK(ledger::Status::OK == ledger_status);
        FTL_CHECK(Status::OK == status);
      });
    });
  }

  void Bind(fidl::InterfaceRequest<Transaction> request) {
    binding_.Bind(std::move(request));
  }

 private:
  ledger::PageSnapshotPtr snapshot_;
  ledger::PagePtr page_;
  fidl::Binding<Transaction> binding_;
  FTL_DISALLOW_COPY_AND_ASSIGN(TransactionImpl);
};
}  // namespace document_store
