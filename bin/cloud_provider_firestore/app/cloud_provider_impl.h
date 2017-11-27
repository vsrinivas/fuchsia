// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_CLOUD_PROVIDER_FIRESTORE_APP_CLOUD_PROVIDER_IMPL_H_
#define PERIDOT_BIN_CLOUD_PROVIDER_FIRESTORE_APP_CLOUD_PROVIDER_IMPL_H_

#include "lib/cloud_provider/fidl/cloud_provider.fidl.h"
#include "lib/fidl/cpp/bindings/binding.h"
#include "lib/fxl/functional/closure.h"
#include "lib/fxl/macros.h"
#include "peridot/bin/cloud_provider_firebase/firebase_auth/firebase_auth_impl.h"
#include "peridot/bin/cloud_provider_firestore/fidl/factory.fidl.h"

namespace cloud_provider_firestore {

// Implementation of cloud_provider::CloudProvider.
//
// If the |on_empty| callback is set, it is called when the client connection is
// closed.
class CloudProviderImpl : public cloud_provider::CloudProvider {
 public:
  CloudProviderImpl(
      std::unique_ptr<firebase_auth::FirebaseAuth> firebase_auth,
      fidl::InterfaceRequest<cloud_provider::CloudProvider> request);
  ~CloudProviderImpl() override;

  void set_on_empty(const fxl::Closure& on_empty) { on_empty_ = on_empty; }

 private:
  void GetDeviceSet(
      fidl::InterfaceRequest<cloud_provider::DeviceSet> device_set,
      const GetDeviceSetCallback& callback) override;

  void GetPageCloud(
      fidl::Array<uint8_t> app_id,
      fidl::Array<uint8_t> page_id,
      fidl::InterfaceRequest<cloud_provider::PageCloud> page_cloud,
      const GetPageCloudCallback& callback) override;

  void EraseAllData(const EraseAllDataCallback& callback) override;

  std::unique_ptr<firebase_auth::FirebaseAuth> firebase_auth_;
  fidl::Binding<cloud_provider::CloudProvider> binding_;
  fxl::Closure on_empty_;

  FXL_DISALLOW_COPY_AND_ASSIGN(CloudProviderImpl);
};

}  // namespace cloud_provider_firestore

#endif  // PERIDOT_BIN_CLOUD_PROVIDER_FIRESTORE_APP_CLOUD_PROVIDER_IMPL_H_
