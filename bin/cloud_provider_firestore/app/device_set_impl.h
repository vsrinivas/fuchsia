// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_CLOUD_PROVIDER_FIRESTORE_APP_DEVICE_SET_IMPL_H_
#define PERIDOT_BIN_CLOUD_PROVIDER_FIRESTORE_APP_DEVICE_SET_IMPL_H_

#include <memory>

#include <fuchsia/ledger/cloud/cpp/fidl.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/fidl/cpp/vector.h>
#include <lib/fit/function.h>
#include <lib/fxl/macros.h>
#include <lib/fxl/memory/weak_ptr.h>

#include "peridot/bin/cloud_provider_firestore/app/credentials_provider.h"
#include "peridot/bin/cloud_provider_firestore/firestore/firestore_service.h"
#include "peridot/bin/cloud_provider_firestore/firestore/listen_call_client.h"
#include "peridot/bin/cloud_provider_firestore/include/types.h"

namespace cloud_provider_firestore {

// Implementation of cloud_provider::DeviceSet.
//
// If the |on_empty| callback is set, it is called when the client connection is
// closed.
class DeviceSetImpl : public cloud_provider::DeviceSet, ListenCallClient {
 public:
  DeviceSetImpl(std::string user_path,
                CredentialsProvider* credentials_provider,
                FirestoreService* firestore_service,
                fidl::InterfaceRequest<cloud_provider::DeviceSet> request);
  ~DeviceSetImpl() override;

  void set_on_empty(fit::closure on_empty) { on_empty_ = std::move(on_empty); }

 private:
  void ScopedGetCredentials(
      fit::function<void(std::shared_ptr<grpc::CallCredentials>)> callback);

  // cloud_provider::DeviceSet:
  void CheckFingerprint(fidl::VectorPtr<uint8_t> fingerprint,
                        CheckFingerprintCallback callback) override;

  void SetFingerprint(fidl::VectorPtr<uint8_t> fingerprint,
                      SetFingerprintCallback callback) override;

  void SetWatcher(
      fidl::VectorPtr<uint8_t> fingerprint,
      fidl::InterfaceHandle<cloud_provider::DeviceSetWatcher> watcher,
      SetWatcherCallback callback) override;

  void Erase(EraseCallback callback) override;

  void OnGotDocumentsToErase(
      std::shared_ptr<grpc::CallCredentials> call_credentials,
      google::firestore::v1beta1::ListDocumentsResponse documents_response,
      EraseCallback callback);

  // ListenCallClient:
  void OnConnected() override;

  void OnResponse(google::firestore::v1beta1::ListenResponse response) override;

  void OnFinished(grpc::Status status) override;

  const std::string user_path_;
  CredentialsProvider* const credentials_provider_;
  FirestoreService* const firestore_service_;

  fidl::Binding<cloud_provider::DeviceSet> binding_;
  fit::closure on_empty_;

  // Watcher set by the client.
  cloud_provider::DeviceSetWatcherPtr watcher_;
  std::string watched_fingerprint_;
  SetWatcherCallback set_watcher_callback_;
  std::unique_ptr<ListenCallHandler> listen_call_handler_;

  // Must be the last member.
  fxl::WeakPtrFactory<DeviceSetImpl> weak_ptr_factory_;

  FXL_DISALLOW_COPY_AND_ASSIGN(DeviceSetImpl);
};

}  // namespace cloud_provider_firestore

#endif  // PERIDOT_BIN_CLOUD_PROVIDER_FIRESTORE_APP_DEVICE_SET_IMPL_H_
