// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "apps/ledger/services/ledger.fidl.h"
#include "apps/modular/lib/app/application_context.h"
#include "apps/modular/lib/app/connect.h"
#include "apps/modular/lib/app/service_provider_impl.h"
#include "apps/modular/services/document_store/document_store.fidl.h"
#include "apps/modular/src/document_store/ledger.h"
#include "apps/modular/src/document_store/snapshot.h"
#include "apps/modular/src/document_store/transaction.h"
#include "lib/fidl/cpp/bindings/binding.h"
#include "lib/fidl/cpp/bindings/interface_ptr_set.h"
#include "lib/ftl/functional/make_copyable.h"
#include "lib/ftl/macros.h"
#include "lib/mtl/tasks/message_loop.h"

namespace document_store {

// Implementation of the DocumentStore interface.
class DocumentStoreImpl : public DocumentStore {
  using PageGetter = std::function<void(
      fidl::Array<uint8_t> page_id, fidl::InterfaceRequest<ledger::Page>,
      const ledger::Ledger::GetPageCallback&)>;

 public:
  // DocumentStoreImpl does not take ownership of ledger.
  DocumentStoreImpl(ledger::PagePtr page, PageGetter get_page)
      : page_(std::move(page)), get_page_(get_page), binding_(this) {}

  // GetId returns the ledger page's id.
  void GetId(const GetIdCallback& callback) override {
    if (page_id_.is_null()) {
      page_->GetId(callback);
    } else {
      callback(page_id_.Clone());
    }
  }

  void Watch(fidl::InterfaceHandle<DocumentStoreWatcher> watcher,
             const WatchCallback& callback) override {
    FTL_LOG(FATAL) << "Watch is not implemented yet!";
  }

  // Create an return a Snapshot interface handle to the caller.
  // Snapshots allow reading from the document store.
  void GetSnapshot(const GetSnapshotCallback& callback) override {
    ledger::PageSnapshotPtr ledger_snapshot;
    page_->GetSnapshot(GetProxy(&ledger_snapshot),
                       [](ledger::Status ledger_status) {
                         FTL_CHECK(ledger_status == ledger::Status::OK);
                       });
    auto impl = new SnapshotImpl(std::move(ledger_snapshot));
    fidl::InterfaceHandle<Snapshot> docstore_snapshot_handle;
    auto request = fidl::GetProxy(&docstore_snapshot_handle);
    impl->Bind(std::move(request));
    callback(std::move(docstore_snapshot_handle));
  }

  // Returns a Transaction interface handle to the caller. Transactions allow
  // modifications to be batched and commited to the document store.
  void BeginTransaction(const BeginTransactionCallback& callback) override {
    // Since every ledger page can support only one transaction at a time,
    // we give each of our transactions its own page so they don't step on each
    // other.
    fidl::InterfaceHandle<ledger::Page> page_handle;
    auto request = GetProxy(&page_handle);
    get_page_(page_id_.Clone(), std::move(request), ftl::MakeCopyable([
                this, callback, page_handle = std::move(page_handle)
              ](ledger::Status ledger_status) mutable {
                TransactionImpl* impl =
                    new TransactionImpl(std::move(page_handle));
                impl->Initialize([this, callback, impl](Status status) {
                  // TODO(azani): Return an error if status is an error.
                  fidl::InterfaceHandle<Transaction> transaction_handle;
                  if (Status::OK == status) {
                    auto request = fidl::GetProxy(&transaction_handle);
                    impl->Bind(std::move(request));
                  }
                  callback(std::move(transaction_handle));
                });
              }));
  }

  void GetIndexManager(fidl::InterfaceRequest<IndexManager> manager) override {
    FTL_LOG(FATAL) << "GetIndexManager is not implemented yet!";
  }

  // SetPageId must be called before DocumentStoreImpl can be bound.
  void SetPageId(fidl::Array<uint8_t> page_id) {
    page_id_ = std::move(page_id);
  }

  void Bind(fidl::InterfaceRequest<DocumentStore> request) {
    binding_.Bind(std::move(request));
  }

 private:
  ledger::PagePtr page_;
  fidl::Array<uint8_t> page_id_;
  PageGetter get_page_;
  fidl::Binding<DocumentStore> binding_;
  FTL_DISALLOW_COPY_AND_ASSIGN(DocumentStoreImpl);
};

// Implementation of the DocumentStoreFactory interface.
class DocumentStoreFactoryImpl : public DocumentStoreFactory {
 public:
  DocumentStoreFactoryImpl() {}

  void Initialize(fidl::InterfaceHandle<ledger::Ledger> ledger) override {
    ledger_ = ledger::LedgerPtr::Create(std::move(ledger));
  }

  // Creates a new document store.
  void NewDocumentStore(const NewDocumentStoreCallback& callback) override {
    fidl::InterfaceHandle<ledger::Page> page_handle;
    auto request = GetProxy(&page_handle);
    ledger_->NewPage(std::move(request), ftl::MakeCopyable([
                       this, callback, page_handle = std::move(page_handle)
                     ](ledger::Status ledger_status) mutable {
                       if (ledger_status == ledger::Status::OK) {
                         NewDocumentStoreImpl(std::move(page_handle), callback);
                       } else {
                         fidl::InterfaceHandle<DocumentStore> handle;
                         callback(internal::LedgerStatusToStatus(ledger_status),
                                  std::move(handle));
                       }
                     }));
  }

  // Gets an existing document store.
  void GetDocumentStore(fidl::Array<uint8_t> page_id,
                        const GetDocumentStoreCallback& callback) override {
    fidl::InterfaceHandle<ledger::Page> page_handle;
    auto request = GetProxy(&page_handle);
    ledger_->GetPage(std::move(page_id), std::move(request), ftl::MakeCopyable([
                       this, callback, page_handle = std::move(page_handle)
                     ](ledger::Status ledger_status) mutable {
                       fidl::InterfaceHandle<DocumentStore> handle;
                       if (ledger_status == ledger::Status::OK) {
                         NewDocumentStoreImpl(std::move(page_handle), callback);
                       } else {
                         callback(internal::LedgerStatusToStatus(ledger_status),
                                  std::move(handle));
                       }
                     }));
  }

  // Deletes an existing document store.
  void DeleteDocumentStore(
      fidl::Array<uint8_t> page_id,
      const DeleteDocumentStoreCallback& callback) override {
    ledger_->DeletePage(
        std::move(page_id), [this, callback](ledger::Status ledger_status) {
          callback(internal::LedgerStatusToStatus(ledger_status));
        });
  }

 private:
  ledger::LedgerPtr ledger_;
  FTL_DISALLOW_COPY_AND_ASSIGN(DocumentStoreFactoryImpl);

  // Constructs a new DocumentStoreImpl, binds it to a DocumentStore handle and
  // passes the newly created handle to the callback.
  void NewDocumentStoreImpl(fidl::InterfaceHandle<ledger::Page> page_handle,
                            const GetDocumentStoreCallback& callback) {
    auto page = ledger::PagePtr::Create(std::move(page_handle));
    auto page_getter = [this](fidl::Array<uint8_t> page_id,
                              fidl::InterfaceRequest<ledger::Page> page,
                              const ledger::Ledger::GetPageCallback& callback) {
      // TODO(azani): Lock the ledger_.
      ledger_->GetPage(std::move(page_id), std::move(page), callback);
    };
    auto impl = new DocumentStoreImpl(std::move(page), page_getter);
    impl->GetId([this, impl, callback](fidl::Array<uint8_t> page_id) {
      fidl::InterfaceHandle<DocumentStore> docstore_handle;
      auto request = fidl::GetProxy(&docstore_handle);
      // TODO(azani): We may leak memory here if page_handle breaks in the
      // middle of GetId. This seems unlikely, fix later.
      impl->SetPageId(std::move(page_id));
      impl->Bind(std::move(request));
      callback(Status::OK, std::move(docstore_handle));
    });
  }
};

class DocumentStoreFactoryApp {
 public:
  DocumentStoreFactoryApp()
      : context_(modular::ApplicationContext::CreateFromStartupInfo()) {
    // Singleton service
    context_->outgoing_services()->AddService<DocumentStoreFactory>(
        [this](fidl::InterfaceRequest<DocumentStoreFactory> request) {
          doc_store_factory_bindings_.AddBinding(&doc_store_factory_impl_,
                                                 std::move(request));
        });
  }

 private:
  std::unique_ptr<modular::ApplicationContext> context_;
  DocumentStoreFactoryImpl doc_store_factory_impl_;
  fidl::BindingSet<DocumentStoreFactory> doc_store_factory_bindings_;

  FTL_DISALLOW_COPY_AND_ASSIGN(DocumentStoreFactoryApp);
};

}  // namespace document_store

int main(int argc, const char** argv) {
  mtl::MessageLoop loop;
  document_store::DocumentStoreFactoryApp app;
  loop.Run();
  return 0;
};
