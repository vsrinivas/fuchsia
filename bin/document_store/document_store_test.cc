// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <sstream>
#include <string>
#include <utility>

#include "apps/ledger/services/ledger.fidl-sync.h"
#include "apps/modular/services/document_store/document_store.fidl-sync.h"
#include "apps/modular/src/document_store/documents.h"

#include "apps/modular/lib/app/application_context.h"
#include "apps/modular/lib/app/connect.h"
#include "apps/modular/lib/app/service_provider_impl.h"
#include "lib/fidl/cpp/bindings/binding_set.h"
#include "lib/fidl/cpp/bindings/interface_ptr_set.h"
#include "lib/fidl/cpp/bindings/synchronous_interface_ptr.h"
#include "lib/ftl/files/scoped_temp_dir.h"
#include "lib/ftl/macros.h"
#include "lib/mtl/tasks/message_loop.h"

namespace {

using namespace document_store;

// TODO(azani): Make more generally available.
std::string statustostr(const Status status) {
  std::string status_str;
  switch (status) {
    case Status::OK:
      status_str = "OK";
      break;
    case Status::PAGE_NOT_FOUND:
      status_str = "PAGE_NOT_FOUND";
      break;
    case Status::DOCUMENT_NOT_FOUND:
      status_str = "DOCUMENT_NOT_FOUND";
      break;
    case Status::DOCUMENT_DATA_ERROR:
      status_str = "DOCUMENT_DATA_ERROR";
      break;
    case Status::DOCUMENT_ALREADY_EXISTS:
      status_str = "DOCUMENT_ALREADY_EXISTS";
      break;
    case Status::TRANSACTION_ALREADY_IN_PROGRESS:
      status_str = "TRANSACTION_ALREADY_IN_PROGRESS";
      break;
    case Status::UNKNOWN_ERROR:
      status_str = "UNKNOWN_ERROR";
      break;
  };
  return status_str;
}

class DocumentStoreTest {
 public:
  DocumentStoreTest()
      : context_(modular::ApplicationContext::CreateFromStartupInfo()) {}

  void RunTests() {
    // TODO(azani): Make sure the data is not persisted unnecessarily across
    // runs.
    files::ScopedTempDir tmp_dir_;
    modular::ServiceProviderPtr child_services;

    fidl::SynchronousInterfacePtr<ledger::LedgerRepositoryFactory>
        ledger_repository_factory;
    auto launch_info = modular::ApplicationLaunchInfo::New();
    launch_info->url = "file:///system/apps/ledger";
    launch_info->services = fidl::GetProxy(&child_services);
    context_->launcher()->CreateApplication(
        std::move(launch_info),
        fidl::GetProxy(&ledger_repository_factory_controller_));
    modular::ConnectToService(
        child_services.get(),
        fidl::GetSynchronousProxy(&ledger_repository_factory));
    FTL_LOG(INFO) << "Connected to " << ledger::LedgerRepositoryFactory::Name_;
    FTL_CHECK(ledger_repository_factory.is_bound());

    // Currently, any name is valid as long as it's not the size-0 array.
    fidl::Array<uint8_t> ledger_name = fidl::Array<uint8_t>::New(1);

    ledger::Status ledger_status;

    fidl::SynchronousInterfacePtr<ledger::LedgerRepository> ledger_repository;
    FTL_CHECK(ledger_repository_factory->GetRepository(
        tmp_dir_.path(), fidl::GetSynchronousProxy(&ledger_repository),
        &ledger_status));

    ledger::LedgerPtr ledger;

    FTL_CHECK(ledger_repository->GetLedger(std::move(ledger_name),
                                           GetProxy(&ledger), &ledger_status));
    FTL_LOG(INFO) << "Got a ledger with status: " << ledger_status;
    FTL_CHECK(!ledger.encountered_error());
    FTL_CHECK(ledger_status == ledger::Status::OK);

    // Test that it is possible to connect to the document store factory.
    launch_info = modular::ApplicationLaunchInfo::New();
    launch_info->url = "file:///system/apps/document_store";
    launch_info->services = fidl::GetProxy(&child_services);
    context_->launcher()->CreateApplication(
        std::move(launch_info), fidl::GetProxy(&docstore_factory_controller_));
    modular::ConnectToService(child_services.get(),
                              fidl::GetSynchronousProxy(&docstore_factory_));
    FTL_LOG(INFO) << "Connected to mojo:document_store";
    FTL_CHECK(docstore_factory_.is_bound());

    FTL_CHECK(docstore_factory_->Initialize(std::move(ledger)));
    FTL_LOG(INFO) << "Sending ledger over!";

    NewDocstore();

    TestDocumentStoreManagement();
    ResetDocstore();

    TestReadWrite();
    ResetDocstore();

    TestDeletion();
    ResetDocstore();

    TestSimpleQueries();

    // TODO(azani): Clean up the data stored as part of the test.
    // DeleteDocstore();

    FTL_LOG(INFO) << "Tests passed.";
  }

  // Tests that document stores can be created, retrieved and deleted.
  void TestDocumentStoreManagement() {
    FTL_LOG(INFO) << "TestDocumentStoreManagement";
    document_store::Status docstore_status;
    fidl::InterfaceHandle<DocumentStore> docstore_handle;

    // Test that it is possible to create a new document store succesfully.
    docstore_factory_->NewDocumentStore(&docstore_status, &docstore_handle);
    FTL_CHECK(docstore_handle.is_valid());
    FTL_CHECK(docstore_status == Status::OK);

    // Test the most basic functionality of the new document store: GetId.
    fidl::SynchronousInterfacePtr<DocumentStore> docstore;
    docstore = fidl::SynchronousInterfacePtr<DocumentStore>::Create(
        std::move(docstore_handle));
    auto page_id = fidl::Array<uint8_t>::New(16);
    docstore->GetId(&page_id);

    // Check that it is possible to obtain an interface to an existing document
    // store.
    fidl::InterfaceHandle<DocumentStore> docstore_handle2;
    FTL_CHECK(docstore_factory_->GetDocumentStore(
        page_id.Clone(), &docstore_status, &docstore_handle2));
    FTL_LOG(INFO) << "GetDocumentStore status " << statustostr(docstore_status);

    FTL_CHECK(docstore_factory_->DeleteDocumentStore(page_id.Clone(),
                                                     &docstore_status));
    // TODO(azani): Figure out how to check that the docstore message pipe is
    // closed.

    // Check that the document store was deleted by the DeleteDocumentStore
    // call above.
    docstore_factory_->GetDocumentStore(page_id.Clone(), &docstore_status,
                                        &docstore_handle2);
    FTL_LOG(INFO) << "GetDocumentStore status after Delete "
                  << statustostr(docstore_status);
    FTL_CHECK(docstore_status == Status::PAGE_NOT_FOUND);
    FTL_CHECK(!docstore_handle2.is_valid());
  }

  // Tests that individual documents can be read and written.
  void TestReadWrite() {
    FTL_LOG(INFO) << "TestReadWrite";
    DocumentPtr document(Document::New());
    document->docid = "test_read_write_doc";
    document->properties["prop_string"] = Value::New();
    document->properties["prop_string"]->set_string_value("hello world!");
    document->properties["prop_iri"] = Value::New();
    document->properties["prop_iri"]->set_iri("hello iri!");
    document->properties["prop_int"] = Value::New();
    document->properties["prop_int"]->set_int_value(10);
    document->properties["prop_float"] = Value::New();
    document->properties["prop_float"]->set_float_value(10.5);
    fidl::Array<uint8_t> binary(fidl::Array<uint8_t>::New(2));
    binary[0] = 0xDE;
    binary[1] = 0xAD;
    document->properties["prop_binary"] = Value::New();
    document->properties["prop_binary"]->set_binary(std::move(binary));
    document->properties["prop_empty"] = Value::New();
    document->properties["prop_empty"]->set_empty(true);

    fidl::InterfaceHandle<Transaction> transaction_handle;
    FTL_CHECK(docstore_->BeginTransaction(&transaction_handle));
    FTL_CHECK(transaction_handle.is_valid());
    auto transaction = fidl::SynchronousInterfacePtr<Transaction>::Create(
        std::move(transaction_handle));
    FTL_CHECK(!document.is_null());
    transaction->PutOne(std::move(document));

    document_store::Status docstore_status;
    transaction->Commit(&docstore_status);

    fidl::InterfaceHandle<Snapshot> snapshot_handle;
    FTL_CHECK(docstore_->GetSnapshot(&snapshot_handle));

    auto snapshot = fidl::SynchronousInterfacePtr<Snapshot>::Create(
        std::move(snapshot_handle));
    document.reset();
    fidl::String docid = "test_read_write_doc";
    snapshot->GetOne(std::move(docid), &docstore_status, &document);

    // Test that each of the types can be deserialized.
    FTL_CHECK(document->properties["prop_string"]->get_string_value() ==
              "hello world!");
    FTL_CHECK(document->properties["prop_iri"]->get_iri() == "hello iri!");
    FTL_CHECK(document->properties["prop_int"]->get_int_value() == 10);
    FTL_CHECK(document->properties["prop_float"]->get_float_value() == 10.5);
    FTL_CHECK(document->properties["prop_binary"]->get_binary()[0] == 0xDE);
    FTL_CHECK(document->properties["prop_binary"]->get_binary()[1] == 0xAD);
    FTL_CHECK(document->properties["prop_empty"]->is_empty());
  }

  // Test that documents can be deleted.
  void TestDeletion() {
    FTL_LOG(INFO) << "TestDeletion";
    fidl::InterfaceHandle<Transaction> transaction_handle;
    FTL_CHECK(docstore_->BeginTransaction(&transaction_handle));
    auto transaction = fidl::SynchronousInterfacePtr<Transaction>::Create(
        std::move(transaction_handle));

    DocumentPtr document(Document::New());
    document->docid = "to_be_deleted";
    transaction->PutOne(std::move(document));

    document_store::Status docstore_status;
    transaction->Commit(&docstore_status);
    FTL_CHECK(Status::OK == docstore_status);

    fidl::InterfaceHandle<Snapshot> snapshot_handle;
    FTL_CHECK(docstore_->GetSnapshot(&snapshot_handle));
    auto snapshot = fidl::SynchronousInterfacePtr<Snapshot>::Create(
        std::move(snapshot_handle));
    document.reset();
    snapshot->GetOne("to_be_deleted", &docstore_status, &document);
    FTL_CHECK(Status::OK == docstore_status);

    FTL_CHECK(docstore_->BeginTransaction(&transaction_handle));
    transaction = fidl::SynchronousInterfacePtr<Transaction>::Create(
        std::move(transaction_handle));
    transaction->DeleteOne("to_be_deleted");
    transaction->Commit(&docstore_status);
    FTL_CHECK(Status::OK == docstore_status);

    FTL_CHECK(docstore_->GetSnapshot(&snapshot_handle));
    snapshot = fidl::SynchronousInterfacePtr<Snapshot>::Create(
        std::move(snapshot_handle));
    document.reset();
    snapshot->GetOne("to_be_deleted", &docstore_status, &document);
    FTL_CHECK(Status::DOCUMENT_NOT_FOUND == docstore_status);
  }

  // Test that simple queries work.
  void TestSimpleQueries() {
    FTL_LOG(INFO) << "TestSimpleQueries";
    fidl::InterfaceHandle<Transaction> transaction_handle;
    FTL_CHECK(docstore_->BeginTransaction(&transaction_handle));
    auto transaction = fidl::SynchronousInterfacePtr<Transaction>::Create(
        std::move(transaction_handle));

    DocumentPtr document(Document::New());
    document->docid = "docid1";

    document->properties["prop1"] = Value::New();
    document->properties["prop1"]->set_string_value("value1");
    transaction->PutOne(document.Clone());

    document->docid = "docid2";
    document->properties["prop1"]->set_string_value("value2");
    transaction->PutOne(document.Clone());

    document->docid = "docid3";
    document->properties.reset();
    document->properties["prop2"] = Value::New();
    document->properties["prop2"]->set_string_value("value2");
    transaction->PutOne(document.Clone());

    document_store::Status docstore_status;
    transaction->Commit(&docstore_status);
    FTL_CHECK(Status::OK == docstore_status);

    fidl::InterfaceHandle<Snapshot> snapshot_handle;
    FTL_CHECK(docstore_->GetSnapshot(&snapshot_handle));
    auto snapshot = fidl::SynchronousInterfacePtr<Snapshot>::Create(
        std::move(snapshot_handle));

    fidl::Array<DocumentPtr> documents;

    // Filter nothing out.
    QueryPtr query = Query::New();
    snapshot->ExecuteQuery(query.Clone(), &docstore_status, &documents);
    FTL_CHECK(Status::OK == docstore_status);
    FTL_CHECK(3 == documents.size());

    // Filter accepts only documents with a prop1 property.
    query->filter = Filter::New();
    SimpleFilterPtr simple_filter = SimpleFilter::New();
    simple_filter->property = "prop1";
    query->filter->set_simple(std::move(simple_filter));
    snapshot->ExecuteQuery(query.Clone(), &docstore_status, &documents);
    FTL_CHECK(Status::OK == docstore_status);
    FTL_CHECK(2 == documents.size());

    // Filter accepts documents where prop1 is equal to "value1".
    query->filter->get_simple()->value = Value::New();
    query->filter->get_simple()->value->set_string_value("value1");
    snapshot->ExecuteQuery(query.Clone(), &docstore_status, &documents);
    FTL_CHECK(Status::OK == docstore_status);
    FTL_CHECK(1 == documents.size());
  }

 private:
  std::unique_ptr<modular::ApplicationContext> context_;
  fidl::SynchronousInterfacePtr<DocumentStoreFactory> docstore_factory_;
  fidl::SynchronousInterfacePtr<DocumentStore> docstore_;
  modular::ApplicationControllerPtr ledger_repository_factory_controller_;
  modular::ApplicationControllerPtr docstore_factory_controller_;

  void ResetDocstore() {
    // TODO(azani): Re-enable when the case of deleting a page with open
    // transactions is fixed.
    // DeleteDocstore();
    NewDocstore();
  }

  void NewDocstore() {
    document_store::Status docstore_status;
    fidl::InterfaceHandle<DocumentStore> docstore_handle;

    docstore_factory_->NewDocumentStore(&docstore_status, &docstore_handle);

    FTL_CHECK(docstore_status == Status::OK);
    FTL_CHECK(docstore_handle.is_valid());

    docstore_ = fidl::SynchronousInterfacePtr<DocumentStore>::Create(
        std::move(docstore_handle));
  }

  void DeleteDocstore() {
    document_store::Status docstore_status;
    auto page_id = fidl::Array<uint8_t>::New(16);
    docstore_->GetId(&page_id);
    FTL_CHECK(docstore_factory_->DeleteDocumentStore(page_id.Clone(),
                                                     &docstore_status));
    FTL_CHECK(Status::OK == docstore_status);
  }
};
}  // namespace

int main(int argc, const char** argv) {
  FTL_LOG(INFO) << "document_store_test";
  mtl::MessageLoop loop;
  DocumentStoreTest app;
  app.RunTests();
  loop.Run();
  return 0;
}
