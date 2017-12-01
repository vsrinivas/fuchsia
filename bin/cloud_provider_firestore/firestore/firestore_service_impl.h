// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_CLOUD_PROVIDER_FIRESTORE_FIRESTORE_FIRESTORE_SERVICE_IMPL_H_
#define PERIDOT_BIN_CLOUD_PROVIDER_FIRESTORE_FIRESTORE_FIRESTORE_SERVICE_IMPL_H_

#include <memory>
#include <thread>

#include "lib/fxl/functional/closure.h"
#include "peridot/bin/cloud_provider_firestore/firestore/firestore_service.h"
#include "peridot/bin/cloud_provider_firestore/firestore/listen_call.h"
#include "peridot/lib/callback/auto_cleanable.h"

namespace cloud_provider_firestore {

// Implementation of the FirestoreService interface.
//
// This class is implemented as a wrapper over the Firestore connection. We use
// a polling thread to wait for request completion on the completion queue and
// expose a callback-based API to the client.
class FirestoreServiceImpl : public FirestoreService {
 public:
  FirestoreServiceImpl(std::string server_id,
                       fxl::RefPtr<fxl::TaskRunner> main_runner,
                       std::shared_ptr<grpc::Channel> channel);

  ~FirestoreServiceImpl() override;

  // FirestoreService:
  const std::string& GetDatabasePath() override { return database_path_; }

  const std::string& GetRootPath() override { return root_path_; }

  void CreateDocument(
      google::firestore::v1beta1::CreateDocumentRequest request,
      std::function<void(grpc::Status status,
                         google::firestore::v1beta1::Document document)>
          callback) override;

  std::unique_ptr<ListenCallHandler> Listen(ListenCallClient* client) override;

 private:
  struct DocumentResponseCall;

  void Poll();

  const std::string server_id_;
  const std::string database_path_;
  const std::string root_path_;

  fxl::RefPtr<fxl::TaskRunner> main_runner_;
  std::thread polling_thread_;

  std::unique_ptr<google::firestore::v1beta1::Firestore::Stub> firestore_;
  grpc::CompletionQueue cq_;

  callback::AutoCleanableSet<DocumentResponseCall> document_response_calls_;

  callback::AutoCleanableSet<ListenCall> listen_calls_;
};

}  // namespace cloud_provider_firestore

#endif  // PERIDOT_BIN_CLOUD_PROVIDER_FIRESTORE_FIRESTORE_FIRESTORE_SERVICE_IMPL_H_
