// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_CLOUD_PROVIDER_FIREBASE_APP_CLOUD_PROVIDER_IMPL_H_
#define PERIDOT_BIN_CLOUD_PROVIDER_FIREBASE_APP_CLOUD_PROVIDER_IMPL_H_

#include <fuchsia/ledger/cloud/cpp/fidl.h>
#include <fuchsia/ledger/cloud/firebase/cpp/fidl.h>
#include <fuchsia/modular/auth/cpp/fidl.h>
#include <lib/fit/function.h>

#include "lib/callback/auto_cleanable.h"
#include "lib/callback/cancellable.h"
#include "lib/fidl/cpp/binding.h"
#include "lib/fxl/macros.h"
#include "lib/network_wrapper/network_wrapper.h"
#include "peridot/bin/cloud_provider_firebase/app/device_set_impl.h"
#include "peridot/bin/cloud_provider_firebase/app/page_cloud_impl.h"
#include "peridot/lib/firebase/firebase_impl.h"
#include "peridot/lib/firebase_auth/firebase_auth_impl.h"

namespace cloud_provider_firebase {

// Implementation of cloud_provider::CloudProvider.
//
// If the |on_empty| callback is set, it is called when the client connection is
// closed.
class CloudProviderImpl : public cloud_provider::CloudProvider {
 public:
  CloudProviderImpl(
      network_wrapper::NetworkWrapper* network_wrapper, std::string user_id,
      Config config, std::unique_ptr<firebase_auth::FirebaseAuth> firebase_auth,
      fidl::InterfaceRequest<cloud_provider::CloudProvider> request);
  ~CloudProviderImpl() override;

  void set_on_empty(fit::closure on_empty) { on_empty_ = std::move(on_empty); }

 private:
  void GetDeviceSet(
      fidl::InterfaceRequest<cloud_provider::DeviceSet> device_set,
      GetDeviceSetCallback callback) override;

  void GetPageCloud(
      fidl::VectorPtr<uint8_t> app_id, fidl::VectorPtr<uint8_t> page_id,
      fidl::InterfaceRequest<cloud_provider::PageCloud> page_cloud,
      GetPageCloudCallback callback) override;

  network_wrapper::NetworkWrapper* const network_wrapper_;
  const std::string user_id_;
  const std::string server_id_;
  std::unique_ptr<firebase_auth::FirebaseAuth> firebase_auth_;
  fidl::Binding<cloud_provider::CloudProvider> binding_;
  fit::closure on_empty_;

  callback::AutoCleanableSet<DeviceSetImpl> device_sets_;

  callback::AutoCleanableSet<PageCloudImpl> page_clouds_;

  // Pending auth token requests to be cancelled when this class goes away.
  callback::CancellableContainer auth_token_requests_;

  FXL_DISALLOW_COPY_AND_ASSIGN(CloudProviderImpl);
};

}  // namespace cloud_provider_firebase

#endif  // PERIDOT_BIN_CLOUD_PROVIDER_FIREBASE_APP_CLOUD_PROVIDER_IMPL_H_
