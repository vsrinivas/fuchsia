// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_CLOUD_PROVIDER_FIRESTORE_FIRESTORE_FIRESTORE_SERVICE_H_
#define PERIDOT_BIN_CLOUD_PROVIDER_FIRESTORE_FIRESTORE_FIRESTORE_SERVICE_H_

#include <functional>

#include <google/firestore/v1beta1/document.pb.h>
#include <google/firestore/v1beta1/firestore.grpc.pb.h>

#include "lib/fxl/macros.h"
#include "peridot/bin/cloud_provider_firestore/firestore/listen_call_client.h"

namespace cloud_provider_firestore {

// Client library for Firestore.
//
// Manages access to a particular Firestore database.
//
// Requests methods are assumed to be called on the |main_runner| thread. All
// client callbacks are called on the |main_runner|.
class FirestoreService {
 public:
  FirestoreService() {}
  virtual ~FirestoreService() {}

  // Returns the Firestore path to the managed database.
  //
  // The returned value is in the format:
  // `projects/{project_id}/databases/{database_id}`.
  virtual const std::string& GetDatabasePath() = 0;

  // Returns the Firestore path to the root of the resource tree of the managed
  // database.
  //
  // The returned value is in the format:
  // `projects/{project_id}/databases/{database_id}/documents`.
  virtual const std::string& GetRootPath() = 0;

  // Gets a single document.
  virtual void GetDocument(
      google::firestore::v1beta1::GetDocumentRequest request,
      std::shared_ptr<grpc::CallCredentials> call_credentials,
      std::function<void(grpc::Status, google::firestore::v1beta1::Document)>
          callback) = 0;

  // Lists documents.
  virtual void ListDocuments(
      google::firestore::v1beta1::ListDocumentsRequest request,
      std::shared_ptr<grpc::CallCredentials> call_credentials,
      std::function<void(grpc::Status,
                         google::firestore::v1beta1::ListDocumentsResponse)>
          callback) = 0;

  // Creates a new document.
  virtual void CreateDocument(
      google::firestore::v1beta1::CreateDocumentRequest request,
      std::shared_ptr<grpc::CallCredentials> call_credentials,
      std::function<void(grpc::Status, google::firestore::v1beta1::Document)>
          callback) = 0;

  // Deletes a document.
  virtual void DeleteDocument(
      google::firestore::v1beta1::DeleteDocumentRequest request,
      std::shared_ptr<grpc::CallCredentials> call_credentials,
      std::function<void(grpc::Status)> callback) = 0;

  // Commits a transaction, while optionally updating documents.
  virtual void Commit(
      google::firestore::v1beta1::CommitRequest request,
      std::shared_ptr<grpc::CallCredentials> call_credentials,
      std::function<void(grpc::Status,
                         google::firestore::v1beta1::CommitResponse)>
          callback) = 0;

  // Runs a query.
  virtual void RunQuery(
      google::firestore::v1beta1::RunQueryRequest request,
      std::shared_ptr<grpc::CallCredentials> call_credentials,
      std::function<
          void(grpc::Status,
               std::vector<google::firestore::v1beta1::RunQueryResponse>)>
          callback) = 0;

  // Initiates a stream to watch for change notifications.
  virtual std::unique_ptr<ListenCallHandler> Listen(
      std::shared_ptr<grpc::CallCredentials> call_credentials,
      ListenCallClient* client) = 0;

  // Shuts the client down.
  //
  // It is only safe to delete the class after the callback is called.
  virtual void ShutDown(fxl::Closure callback) = 0;

 private:
  FXL_DISALLOW_COPY_AND_ASSIGN(FirestoreService);
};

}  // namespace cloud_provider_firestore

#endif  // PERIDOT_BIN_CLOUD_PROVIDER_FIRESTORE_FIRESTORE_FIRESTORE_SERVICE_H_
