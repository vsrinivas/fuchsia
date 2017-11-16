// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_CLOUD_PROVIDER_FIRESTORE_FIRESTORE_FIRESTORE_SERVICE_H_
#define PERIDOT_BIN_CLOUD_PROVIDER_FIRESTORE_FIRESTORE_FIRESTORE_SERVICE_H_

#include <thread>

#include <google/firestore/v1beta1/document.pb.h>
#include <google/firestore/v1beta1/firestore.grpc.pb.h>
#include <grpc++/grpc++.h>

#include "lib/fxl/functional/closure.h"
#include "lib/fxl/macros.h"
#include "lib/fxl/tasks/task_runner.h"
#include "peridot/bin/ledger/callback/auto_cleanable.h"

namespace cloud_provider_firestore {

// Wrapper over the Firestore connection exposing an asynchronous API.
//
// Requests methods are assumed to be called on the |main_runner| thread. All
// client callbacks are called on the |main_runner|.
//
// Internally, the class uses a polling thread to wait for request completion.
class FirestoreService {
 public:
  FirestoreService(fxl::RefPtr<fxl::TaskRunner> main_runner,
                   std::shared_ptr<grpc::Channel> channel);

  ~FirestoreService();

  void CreateDocument(
      google::firestore::v1beta1::CreateDocumentRequest request,
      std::function<void(grpc::Status status,
                         google::firestore::v1beta1::Document document)>
          callback);

 private:
  void Poll();

  fxl::RefPtr<fxl::TaskRunner> main_runner_;
  std::thread polling_thread_;

  std::unique_ptr<google::firestore::v1beta1::Firestore::Stub> firestore_;
  grpc::CompletionQueue cq_;

  struct DocumentResponseCall;
  callback::AutoCleanableSet<DocumentResponseCall> document_response_calls_;

  FXL_DISALLOW_COPY_AND_ASSIGN(FirestoreService);
};

}  // namespace cloud_provider_firestore

#endif  // PERIDOT_BIN_CLOUD_PROVIDER_FIRESTORE_FIRESTORE_FIRESTORE_SERVICE_H_
