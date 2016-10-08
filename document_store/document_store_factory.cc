// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <mojo/system/main.h>

#include <unordered_set>
#include <utility>

#include "apps/ledger/api/ledger.mojom.h"
#include "apps/maxwell/document_store/interfaces/document_store.mojom.h"
#include "mojo/public/cpp/application/application_impl_base.h"
#include "mojo/public/cpp/application/run_application.h"
#include "mojo/public/cpp/application/service_provider_impl.h"
#include "mojo/public/cpp/bindings/binding_set.h"
#include "mojo/public/cpp/bindings/callback.h"
#include "mojo/public/cpp/bindings/interface_ptr_set.h"
#include "mojo/public/cpp/bindings/strong_binding_set.h"
#include "mojo/public/cpp/bindings/synchronous_interface_ptr.h"

namespace {

using mojo::ApplicationImplBase;
using mojo::BindingSet;
using mojo::ConnectionContext;
using mojo::InterfaceHandle;
using mojo::InterfacePtrSet;
using mojo::InterfaceRequest;
using mojo::ServiceProviderImpl;

using namespace document_store;

class DocumentStoreImpl : public DocumentStore {
 public:
  DocumentStoreImpl(mojo::InterfaceHandle<ledger::Page> page)
      : page_(ledger::PagePtr::Create(std::move(page))) {}

  void GetId(const GetIdCallback& callback) override {
    // GetId returns the ledger page's id.
    page_->GetId(callback);
  }

  void Watch(mojo::InterfaceHandle<DocumentStoreWatcher> water,
             const WatchCallback& callback) override {
    MOJO_LOG(FATAL) << "Watch is not implemented yet!";
  }

  void GetSnapshot(mojo::InterfaceRequest<Snapshot> snapshot) override {
    MOJO_LOG(FATAL) << "GetSnapshot is not implemented yet!";
  }

  void BeginTransaction(
      mojo::InterfaceRequest<Transaction> transaction) override {
    MOJO_LOG(FATAL) << "BeginTransaction is not implemented yet!";
  }

  void GetIndexManager(mojo::InterfaceRequest<IndexManager> manager) override {
    MOJO_LOG(FATAL) << "GetIndexManager is not implemented yet!";
  }

 private:
  ledger::PagePtr page_;
  MOJO_DISALLOW_COPY_AND_ASSIGN(DocumentStoreImpl);
};

class DocumentStoreFactoryImpl : public DocumentStoreFactory {
 public:
  DocumentStoreFactoryImpl() {}

  void Initialize(mojo::InterfaceHandle<ledger::Ledger> ledger) override {
    ledger_ = ledger::LedgerPtr::Create(std::move(ledger));
  }

  void NewDocumentStore(const NewDocumentStoreCallback& callback) override {
    auto lambda = [this, callback](
        ledger::Status ledger_status,
        mojo::InterfaceHandle<ledger::Page> page_handle) {
      Status docstore_status = Status::OK;
      mojo::InterfaceHandle<DocumentStore> handle;
      if (ledger_status == ledger::Status::OK) {
        NewDocumentStoreImpl(std::move(page_handle), &handle);
      } else {
        docstore_status = Status::UNKNOWN_ERROR;
      }
      callback.Run(docstore_status, std::move(handle));
    };

    ledger_->NewPage(lambda);
  }

  void GetDocumentStore(mojo::Array<uint8_t> page_id,
                        const GetDocumentStoreCallback& callback) override {
    auto lambda = [this, callback](
        ledger::Status ledger_status,
        mojo::InterfaceHandle<ledger::Page> page_handle) {
      Status docstore_status = Status::OK;
      mojo::InterfaceHandle<DocumentStore> handle;
      switch (ledger_status) {
        case ledger::Status::OK:
          NewDocumentStoreImpl(std::move(page_handle), &handle);
          break;
        case ledger::Status::PAGE_NOT_FOUND:
          docstore_status = Status::PAGE_NOT_FOUND;
          break;
        default:
          docstore_status = Status::UNKNOWN_ERROR;
          break;
      };
      callback.Run(docstore_status, std::move(handle));
    };

    ledger_->GetPage(std::move(page_id), lambda);
  }

  void DeleteDocumentStore(
      mojo::Array<uint8_t> page_id,
      const DeleteDocumentStoreCallback& callback) override {
    auto lambda = [this, callback](ledger::Status ledger_status) {
      Status docstore_status;
      switch (ledger_status) {
        case ledger::Status::OK:
          docstore_status = Status::OK;
          break;
        case ledger::Status::PAGE_NOT_FOUND:
          docstore_status = Status::PAGE_NOT_FOUND;
          break;
        default:
          docstore_status = Status::UNKNOWN_ERROR;
          break;
      };
      callback.Run(docstore_status);
    };

    ledger_->DeletePage(std::move(page_id), lambda);
  }

 private:
  mojo::InterfacePtr<ledger::Ledger> ledger_;
  mojo::StrongBindingSet<DocumentStore> docstore_bindings_;
  MOJO_DISALLOW_COPY_AND_ASSIGN(DocumentStoreFactoryImpl);

  void NewDocumentStoreImpl(
      mojo::InterfaceHandle<ledger::Page> page_handle,
      mojo::InterfaceHandle<DocumentStore>* docstore_handle) {
    auto request = mojo::GetProxy(docstore_handle);
    auto impl = new DocumentStoreImpl(std::move(page_handle));
    docstore_bindings_.AddBinding(impl, std::move(request));
  }
};

class DocumentStoreFactoryApp : public ApplicationImplBase {
 public:
  DocumentStoreFactoryApp() {}

  bool OnAcceptConnection(ServiceProviderImpl* service_provider_impl) override {
    // Singleton service
    service_provider_impl->AddService<DocumentStoreFactory>(
        [this](const ConnectionContext& connection_context,
               InterfaceRequest<DocumentStoreFactory> request) {
          doc_store_factory_bindings_.AddBinding(&doc_store_factory_impl_,
                                                 std::move(request));
        });
    return true;
  }

 private:
  DocumentStoreFactoryImpl doc_store_factory_impl_;
  BindingSet<DocumentStoreFactory> doc_store_factory_bindings_;
  MOJO_DISALLOW_COPY_AND_ASSIGN(DocumentStoreFactoryApp);
};

}  // namespace

MojoResult MojoMain(MojoHandle request) {
  DocumentStoreFactoryApp app;
  return mojo::RunApplication(request, &app);
};
