// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/cloud_provider_firestore/firestore/testing/test_firestore_service.h"

namespace cloud_provider_firestore {

TestFirestoreService::TestFirestoreService() : db_path_(), root_path_() {}
TestFirestoreService::~TestFirestoreService() {}

void TestFirestoreService::SetOnRequest(fxl::Closure on_request) {
  on_request_ = std::move(on_request);
}

const std::string& TestFirestoreService::GetDatabasePath() {
  return db_path_;
}

const std::string& TestFirestoreService::GetRootPath() {
  return root_path_;
}

void TestFirestoreService::GetDocument(
    google::firestore::v1beta1::GetDocumentRequest request,
    std::shared_ptr<grpc::CallCredentials> call_credentials,
    std::function<void(grpc::Status, google::firestore::v1beta1::Document)>
        callback) {
  get_document_records.push_back({std::move(request), std::move(callback)});
  if (on_request_) {
    on_request_();
  }
}

void TestFirestoreService::CreateDocument(
    google::firestore::v1beta1::CreateDocumentRequest request,
    std::shared_ptr<grpc::CallCredentials> call_credentials,
    std::function<void(grpc::Status, google::firestore::v1beta1::Document)>
        callback) {
  create_document_records.push_back({std::move(request), std::move(callback)});
  if (on_request_) {
    on_request_();
  }
}

void TestFirestoreService::DeleteDocument(
    google::firestore::v1beta1::DeleteDocumentRequest /*request*/,
    std::shared_ptr<grpc::CallCredentials> call_credentials,
    std::function<void(grpc::Status)> /*callback*/) {}

std::unique_ptr<ListenCallHandler> TestFirestoreService::Listen(
    std::shared_ptr<grpc::CallCredentials> call_credentials,
    ListenCallClient* /*client*/) {
  return nullptr;
}

}  // namespace cloud_provider_firestore
