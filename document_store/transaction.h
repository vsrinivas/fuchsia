// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <vector>

#include "apps/ledger/api/ledger.mojom.h"
#include "apps/maxwell/document_store/interfaces/document.mojom.h"
#include "apps/maxwell/document_store/interfaces/document_store.mojom.h"

#include "mojo/public/cpp/bindings/callback.h"
#include "mojo/public/cpp/bindings/interface_ptr_set.h"
#include "mojo/public/cpp/bindings/strong_binding_set.h"

namespace document_store {

namespace internal {
// TODO(azani): Write unit tests.
// Constructs the key at which the specified property for the specified document
// is to be stored.
void LedgerKeyForProperty(const mojo::String& docid,
                          const PropertyPtr& property,
                          mojo::Array<uint8_t>* key) {
  // TODO(azani): Support collection index.
  size_t key_size = 3 + docid.size() + property->property.size();
  *key = mojo::Array<uint8_t>::New(key_size);
  uint8_t* key_raw = key->data();

  const char* docid_raw = docid.data();
  size_t docid_size = docid.size();
  const char* property_raw = property->property.data();
  size_t property_size = property->property.size();

  // First byte is a prefix to differentiate document values from metadata or
  // index data.
  key_raw[0] = 'v';

  std::copy(docid_raw, docid_raw + docid_size, &key_raw[1]);
  key_raw[docid_size + 2] = '\0';
  std::copy(property_raw, property_raw + property_size,
            &key_raw[2 + docid_size]);
  key_raw[3 + docid_size + property_size] = '\0';
}

// Serialize a string value for storage on the ledger.
void SerializeString(const mojo::String& value,
                     mojo::Array<uint8_t>* serialized) {
  *serialized = mojo::Array<uint8_t>::New(1 + value.size());

  uint8_t* raw = serialized->data();
  raw[0] = 's';

  const char* value_raw = value.data();
  std::copy(value_raw, value_raw + value.size(), &raw[1]);
}

// Serialize a value for storage on the ledger.
void SerializeValue(const ValuePtr& value, mojo::Array<uint8_t>* serialized) {
  mojo::String string;
  switch (value->which()) {
    case Value::Tag::STRING_VALUE:
      SerializeString(value->get_string_value(), serialized);
      return;
    default:
      MOJO_LOG(FATAL) << "Your data was not a string. Your mistake!";
  };
}

// Construct the ledger key at which to store the specified property and
// serialize the property's value for storage on the ledger.
void LedgerKeyValueForProperty(const mojo::String& docid,
                               const PropertyPtr& property,
                               mojo::Array<uint8_t>* key,
                               mojo::Array<uint8_t>* value) {
  LedgerKeyForProperty(docid, property, key);
  value->reset();
  if (!property->value.is_null()) {
    SerializeValue(property->value, value);
  }
}

// Keeps track of a single operation on a DocumentStore.
struct Operation {
 public:
  enum class Type {
    ADD,
    ADD_REPLACE,
    DELETE,
  };

  Operation(const Operation& op)
      : docid_(op.docid()), doc_(op.document().Clone()), type_(op.type()) {}

  Operation(mojo::String docid, Type type) : docid_(docid), type_(type) {}

  Operation(DocumentPtr doc, Type type) : doc_(std::move(doc)), type_(type) {}

  Type type() const { return type_; }
  const DocumentPtr& document() const { return doc_; }
  const mojo::String& docid() const { return docid_; }

 private:
  mojo::String docid_;
  DocumentPtr doc_;
  Type type_;
};

// Transaction accumulates Operations to be later committed.
class Transaction {
 public:
  void Add(DocumentPtr doc) {
    Operation op(std::move(doc), Operation::Type::ADD);
    operations_.push_back(op);
  }

  void AddReplace(DocumentPtr doc) {
    Operation op(std::move(doc), Operation::Type::ADD_REPLACE);
    operations_.push_back(op);
  }

  void Delete(const mojo::String& docid) {
    Operation op(docid, Operation::Type::DELETE);
    operations_.push_back(op);
  }

  std::vector<Operation>::const_iterator begin() const noexcept {
    return operations_.begin();
  }

  std::vector<Operation>::const_iterator end() const noexcept {
    return operations_.end();
  }

 private:
  std::vector<Operation> operations_;
};

// LedgerStatusTracker allows the return values of many ledger operations to
// be aggregated into a single return value: The latest error to be resported
// or ledger::Status::OK if there were no errors.
class LedgerStatusTracker {
 public:
  // callback is the function to be called when all the ledger statuses
  // have been reported.
  LedgerStatusTracker(std::function<void(ledger::Status)> callback)
      : callback_(callback) {}

  void ReportStatus(ledger::Status status) {
    received_++;
    if (status != ledger::Status::OK) {
      status_ = status;
    }

    if (expected_ >= 0 && received_ >= expected_) {
      Done();
    }
  }

  // Tells the ledger status tracker how many ledger statuses are expected.
  void SetExpected(int expected) {
    expected_ = expected;
    if (received_ >= expected) {
      Done();
    }
  }

 private:
  void Done() {
    callback_(status_);
    delete this;
  }

  int expected_ = -1;
  int received_ = 0;
  ledger::Status status_ = ledger::Status::OK;
  std::function<void(ledger::Status)> callback_;
};

}  // namespace internal

// Implements the Transaction interface.
class TransactionImpl : public Transaction {
 public:
  TransactionImpl(mojo::InterfaceHandle<ledger::Page> page_handle)
      : page_(ledger::PagePtr::Create(std::move(page_handle))),
        transaction_(new internal::Transaction){};

  void Add(mojo::Array<document_store::DocumentPtr> docs) override {
    for (size_t i = 0; i < docs.size(); ++i) {
      AddOne(std::move(docs.at(i)));
    }
  };

  void AddOne(document_store::DocumentPtr doc) override {
    transaction_->Add(std::move(doc));
  };

  void AddReplace(mojo::Array<document_store::DocumentPtr> docs) override {
    MOJO_LOG(FATAL) << "Not implemented yet!";
  };

  void AddReplaceOne(document_store::DocumentPtr doc) override {
    MOJO_LOG(FATAL) << "Not implemented yet!";
  };

  void Delete(mojo::Array<mojo::String> docids) override {
    MOJO_LOG(FATAL) << "Not implemented yet!";
  };

  void DeleteOne(const mojo::String& docid) override {
    MOJO_LOG(FATAL) << "Not implemented yet!";
  };

  void ApplyStatementMutations(
      mojo::Array<StatementMutationPtr> mutations) override {
    MOJO_LOG(FATAL) << "ApplyStatementMutations not implemented!";
  };

  void Commit(const CommitCallback& callback) override {
    // TODO(azani): Use a ledger transaction when implemented.
    std::unique_ptr<internal::Transaction> transaction(
        new internal::Transaction());
    transaction_.swap(transaction);

    int expected = 0;
    internal::LedgerStatusTracker* tracker = new internal::LedgerStatusTracker(
        [callback](ledger::Status ledger_status) {
          callback.Run(internal::LedgerStatusToStatus(ledger_status));
        });

    auto ledger_status_callback = [tracker](ledger::Status ledger_status) {
      tracker->ReportStatus(ledger_status);
    };

    for (auto op = transaction->begin(); op != transaction->end(); ++op) {
      switch (op->type()) {
        case internal::Operation::Type::ADD:
          expected += HandleAddOperation(*op, ledger_status_callback);
          break;
        default:
          MOJO_LOG(FATAL) << "Only ADD operations permitted at this time!";
      }
    }
    tracker->SetExpected(expected);
  };

 private:
  // Translates an Add operation to method calls on the ledger.
  int HandleAddOperation(const internal::Operation& op,
                         std::function<void(ledger::Status)> callback) {
    mojo::Array<uint8_t> key;
    mojo::Array<uint8_t> value;

    // Add the synthetic "docid" property.
    PropertyPtr docid(Property::New());
    docid->property = "docid";
    docid->value = Value::New();
    docid->value->set_string_value(op.document()->docid);
    internal::LedgerKeyValueForProperty(op.document()->docid, docid, &key,
                                        &value);
    page_->Put(std::move(key), std::move(value), callback);

    for (size_t i = 0; i < op.document()->properties.size(); i++) {
      internal::LedgerKeyValueForProperty(
          op.document()->docid, op.document()->properties[i], &key, &value);
      if (!value.is_null()) {
        page_->Put(std::move(key), std::move(value), callback);
      } else {
        page_->Delete(std::move(key), callback);
      }
    }
    return op.document()->properties.size() + 1;
  }

  ledger::PagePtr page_;
  std::unique_ptr<internal::Transaction> transaction_;
  MOJO_DISALLOW_COPY_AND_ASSIGN(TransactionImpl);
};
}  // namespace document_store
