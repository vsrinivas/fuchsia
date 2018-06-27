// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_CLOUD_PROVIDER_FIRESTORE_APP_CLOUD_PROVIDER_IMPL_H_
#define PERIDOT_BIN_CLOUD_PROVIDER_FIRESTORE_APP_CLOUD_PROVIDER_IMPL_H_

#include <fuchsia/ledger/cloud/cpp/fidl.h>
#include <fuchsia/ledger/cloud/firestore/cpp/fidl.h>
#include <lib/fit/function.h>

#include "lib/fidl/cpp/binding.h"
#include "lib/fxl/macros.h"
#include "peridot/bin/cloud_provider_firestore/app/device_set_impl.h"
#include "peridot/bin/cloud_provider_firestore/app/page_cloud_impl.h"
#include "peridot/bin/cloud_provider_firestore/firestore/firestore_service.h"
#include "peridot/lib/firebase_auth/firebase_auth_impl.h"

namespace cloud_provider_firestore {

// Implementation of cloud_provider::CloudProvider.
//
// If the |on_empty| callback is set, it is called when the client connection is
// closed.
class CloudProviderImpl : public cloud_provider::CloudProvider {
 public:
  CloudProviderImpl(
      std::string user_id,
      std::unique_ptr<firebase_auth::FirebaseAuth> firebase_auth,
      std::unique_ptr<FirestoreService> firestore_service,
      fidl::InterfaceRequest<cloud_provider::CloudProvider> request);
  ~CloudProviderImpl() override;

  void set_on_empty(fit::closure on_empty) { on_empty_ = std::move(on_empty); }

  // Shuts the class down and calls the on_empty callback, if set.
  //
  // It is only valid to delete the class after the on_empty callback is called.
  void ShutDownAndReportEmpty();

 private:
  void GetDeviceSet(
      fidl::InterfaceRequest<cloud_provider::DeviceSet> device_set,
      GetDeviceSetCallback callback) override;

  void GetPageCloud(
      fidl::VectorPtr<uint8_t> app_id, fidl::VectorPtr<uint8_t> page_id,
      fidl::InterfaceRequest<cloud_provider::PageCloud> page_cloud,
      GetPageCloudCallback callback) override;

  const std::string user_id_;

  std::unique_ptr<CredentialsProvider> credentials_provider_;
  std::unique_ptr<FirestoreService> firestore_service_;
  fidl::Binding<cloud_provider::CloudProvider> binding_;
  fit::closure on_empty_;

  callback::AutoCleanableSet<DeviceSetImpl> device_sets_;
  callback::AutoCleanableSet<PageCloudImpl> page_clouds_;

  FXL_DISALLOW_COPY_AND_ASSIGN(CloudProviderImpl);
};

}  // namespace cloud_provider_firestore

#endif  // PERIDOT_BIN_CLOUD_PROVIDER_FIRESTORE_APP_CLOUD_PROVIDER_IMPL_H_
