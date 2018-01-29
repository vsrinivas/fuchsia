// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_CLOUD_PROVIDER_FIRESTORE_FIRESTORE_TESTING_TEST_FIRESTORE_SERVICE_H_
#define PERIDOT_BIN_CLOUD_PROVIDER_FIRESTORE_FIRESTORE_TESTING_TEST_FIRESTORE_SERVICE_H_

#include <string>

#include <google/firestore/v1beta1/document.pb.h>
#include <google/firestore/v1beta1/firestore.grpc.pb.h>

#include "peridot/bin/cloud_provider_firestore/firestore/firestore_service.h"

namespace cloud_provider_firestore {

struct GetDocumentRecord {
  google::firestore::v1beta1::GetDocumentRequest request;
  std::function<void(grpc::Status, google::firestore::v1beta1::Document)>
      callback;
};

struct CreateDocumentRecord {
  google::firestore::v1beta1::CreateDocumentRequest request;
  std::function<void(grpc::Status, google::firestore::v1beta1::Document)>
      callback;
};

class TestFirestoreService : public FirestoreService {
 public:
  TestFirestoreService();
  ~TestFirestoreService() override;

  void SetOnRequest(fxl::Closure on_request);

  // FirestoreService:
  const std::string& GetDatabasePath() override;

  const std::string& GetRootPath() override;

  void GetDocument(
      google::firestore::v1beta1::GetDocumentRequest request,
      std::shared_ptr<grpc::CallCredentials> call_credentials,
      std::function<void(grpc::Status, google::firestore::v1beta1::Document)>
          callback) override;

  void CreateDocument(
      google::firestore::v1beta1::CreateDocumentRequest request,
      std::shared_ptr<grpc::CallCredentials> call_credentials,
      std::function<void(grpc::Status, google::firestore::v1beta1::Document)>
          callback) override;

  void DeleteDocument(google::firestore::v1beta1::DeleteDocumentRequest request,
                      std::shared_ptr<grpc::CallCredentials> call_credentials,
                      std::function<void(grpc::Status)> callback) override;

  std::unique_ptr<ListenCallHandler> Listen(
      std::shared_ptr<grpc::CallCredentials> call_credentials,
      ListenCallClient* client) override;

  void ShutDown(fxl::Closure callback) override;

  std::vector<GetDocumentRecord> get_document_records;
  std::vector<CreateDocumentRecord> create_document_records;

  fxl::Closure shutdown_callback;

 private:
  const std::string db_path_;
  const std::string root_path_;

  fxl::Closure on_request_;
};

}  // namespace cloud_provider_firestore

#endif  // PERIDOT_BIN_CLOUD_PROVIDER_FIRESTORE_FIRESTORE_TESTING_TEST_FIRESTORE_SERVICE_H_
