// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <mojo/system/main.h>

#include <functional>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#include "apps/ledger/api/ledger.mojom.h"
#include "apps/maxwell/document_store/interfaces/document_store.mojom.h"
#include "apps/maxwell/document_store/ledger.h"
#include "apps/maxwell/document_store/snapshot.h"
#include "apps/maxwell/document_store/transaction.h"

#include "mojo/public/cpp/application/application_impl_base.h"
#include "mojo/public/cpp/application/run_application.h"
#include "mojo/public/cpp/application/service_provider_impl.h"
#include "mojo/public/cpp/bindings/binding_set.h"
#include "mojo/public/cpp/bindings/callback.h"
#include "mojo/public/cpp/bindings/interface_ptr_set.h"
#include "mojo/public/cpp/bindings/strong_binding_set.h"

namespace {

using namespace document_store;

// Implementation of the DocumentStore interface.
class DocumentStoreImpl : public DocumentStore {
  using PageGetter =
      std::function<void(const ledger::Ledger::GetPageCallback&)>;

 public:
  // DocumentStoreImpl does not take ownership of ledger.
  DocumentStoreImpl(ledger::PagePtr page, ledger::LedgerPtr* ledger)
      : page_(std::move(page)), ledger_(ledger) {}

  // GetId returns the ledger page's id.
  void GetId(const GetIdCallback& callback) override {
    if (page_id_.is_null()) {
      page_->GetId(callback);
    } else {
      callback.Run(page_id_.Clone());
    }
  }

  void Watch(mojo::InterfaceHandle<DocumentStoreWatcher> watcher,
             const WatchCallback& callback) override {
    MOJO_LOG(FATAL) << "Watch is not implemented yet!";
  }

  // Create an return a Snapshot interface handle to the caller.
  // Snapshots allow reading from the document store.
  void GetSnapshot(const GetSnapshotCallback& callback) override {
    page_->GetSnapshot([this, callback](
        ledger::Status ledger_status,
        mojo::InterfaceHandle<ledger::PageSnapshot> ledger_snapshot_handle) {
      MOJO_CHECK(ledger_status == ledger::Status::OK);
      auto impl = new SnapshotImpl(std::move(ledger_snapshot_handle));
      mojo::InterfaceHandle<Snapshot> docstore_snapshot_handle;
      auto request = mojo::GetProxy(&docstore_snapshot_handle);
      snapshot_bindings_.AddBinding(impl, std::move(request));
      callback.Run(std::move(docstore_snapshot_handle));
    });
  }

  // Returns a Transaction interface handle to the caller. Transactions allow
  // modifications to be batched and commited to the document store.
  void BeginTransaction(const BeginTransactionCallback& callback) override {
    // Since every ledger page can support only one transaction at a time,
    // we give each of our transactions its own page so they don't step on each
    // other.
    (*ledger_)->GetPage(
        page_id_.Clone(),
        [this, callback](ledger::Status ledger_status,
                         mojo::InterfaceHandle<ledger::Page> page_handle) {
          MOJO_CHECK(ledger_status == ledger::Status::OK);
          auto impl = new TransactionImpl(std::move(page_handle));
          mojo::InterfaceHandle<Transaction> transaction_handle;
          auto request = mojo::GetProxy(&transaction_handle);
          transaction_bindings_.AddBinding(impl, std::move(request));
          callback.Run(std::move(transaction_handle));
        });
  }

  void GetIndexManager(mojo::InterfaceRequest<IndexManager> manager) override {
    MOJO_LOG(FATAL) << "GetIndexManager is not implemented yet!";
  }

  // SetPageId must be called before DocumentStoreImpl can be bound.
  void SetPageId(mojo::Array<uint8_t> page_id) {
    page_id_ = std::move(page_id);
  }

 private:
  ledger::PagePtr page_;
  ledger::LedgerPtr* ledger_;
  mojo::Array<uint8_t> page_id_;
  mojo::StrongBindingSet<Transaction> transaction_bindings_;
  mojo::StrongBindingSet<Snapshot> snapshot_bindings_;
  MOJO_DISALLOW_COPY_AND_ASSIGN(DocumentStoreImpl);
};

// Implementation of the DocumentStoreFactory interface.
class DocumentStoreFactoryImpl : public DocumentStoreFactory {
 public:
  DocumentStoreFactoryImpl() {}

  void Initialize(mojo::InterfaceHandle<ledger::Ledger> ledger) override {
    ledger_ = ledger::LedgerPtr::Create(std::move(ledger));
  }

  // Creates a new document store.
  void NewDocumentStore(const NewDocumentStoreCallback& callback) override {
    ledger_->NewPage(
        [this, callback](ledger::Status ledger_status,
                         mojo::InterfaceHandle<ledger::Page> page_handle) {
          if (ledger_status == ledger::Status::OK) {
            NewDocumentStoreImpl(std::move(page_handle), callback);
          } else {
            mojo::InterfaceHandle<DocumentStore> handle;
            callback.Run(internal::LedgerStatusToStatus(ledger_status),
                         std::move(handle));
          }
        });
  }

  // Gets an existing document store.
  void GetDocumentStore(mojo::Array<uint8_t> page_id,
                        const GetDocumentStoreCallback& callback) override {
    ledger_->GetPage(
        std::move(page_id),
        [this, callback](ledger::Status ledger_status,
                         mojo::InterfaceHandle<ledger::Page> page_handle) {
          mojo::InterfaceHandle<DocumentStore> handle;
          if (ledger_status == ledger::Status::OK) {
            NewDocumentStoreImpl(std::move(page_handle), callback);
          } else {
            callback.Run(internal::LedgerStatusToStatus(ledger_status),
                         std::move(handle));
          }
        });
  }

  // Deletes an existing document store.
  void DeleteDocumentStore(
      mojo::Array<uint8_t> page_id,
      const DeleteDocumentStoreCallback& callback) override {
    ledger_->DeletePage([this, callback](ledger::Status ledger_status) {
      callback.Run(internal::LedgerStatusToStatus(ledger_status));
    });
  }

 private:
  ledger::LedgerPtr ledger_;
  mojo::StrongBindingSet<DocumentStore> docstore_bindings_;
  MOJO_DISALLOW_COPY_AND_ASSIGN(DocumentStoreFactoryImpl);

  // Constructs a new DocumentStoreImpl, binds it to a DocumentStore handle and
  // passes the newly created handle to the callback.
  void NewDocumentStoreImpl(mojo::InterfaceHandle<ledger::Page> page_handle,
                            const GetDocumentStoreCallback& callback) {
    auto page = ledger::PagePtr::Create(std::move(page_handle));
    // DocumentStoreFactoryImpl retains ownership of ledger_.
    auto impl = new DocumentStoreImpl(std::move(page), &ledger_);
    impl->GetId([this, impl, callback](mojo::Array<uint8_t> page_id) {
      mojo::InterfaceHandle<DocumentStore> docstore_handle;
      auto request = mojo::GetProxy(&docstore_handle);
      // TODO(azani): We may leak memory here if page_handle breaks in the
      // middle of GetId. This seems unlikely, fix later.
      impl->SetPageId(std::move(page_id));
      docstore_bindings_.AddBinding(impl, std::move(request));
      callback.Run(Status::OK, std::move(docstore_handle));
    });
  }
};

class DocumentStoreFactoryApp : public mojo::ApplicationImplBase {
 public:
  DocumentStoreFactoryApp() {}

  bool OnAcceptConnection(
      mojo::ServiceProviderImpl* service_provider_impl) override {
    // Singleton service
    service_provider_impl->AddService<DocumentStoreFactory>(
        [this](const mojo::ConnectionContext& connection_context,
               mojo::InterfaceRequest<DocumentStoreFactory> request) {
          doc_store_factory_bindings_.AddBinding(&doc_store_factory_impl_,
                                                 std::move(request));
        });
    return true;
  }

 private:
  DocumentStoreFactoryImpl doc_store_factory_impl_;
  mojo::BindingSet<DocumentStoreFactory> doc_store_factory_bindings_;
  MOJO_DISALLOW_COPY_AND_ASSIGN(DocumentStoreFactoryApp);
};

}  // namespace

MojoResult MojoMain(MojoHandle request) {
  DocumentStoreFactoryApp app;
  return mojo::RunApplication(request, &app);
};
