// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_CLOUD_PROVIDER_FIREBASE_CLOUD_PROVIDER_IMPL_H_
#define PERIDOT_BIN_CLOUD_PROVIDER_FIREBASE_CLOUD_PROVIDER_IMPL_H_

#include "lib/auth/fidl/token_provider.fidl.h"
#include "lib/cloud_provider/fidl/cloud_provider.fidl.h"
#include "lib/fidl/cpp/bindings/binding.h"
#include "lib/fxl/functional/closure.h"
#include "lib/fxl/macros.h"
#include "lib/fxl/tasks/task_runner.h"
#include "peridot/bin/cloud_provider_firebase/auth_provider/auth_provider_impl.h"
#include "peridot/bin/cloud_provider_firebase/device_set_impl.h"
#include "peridot/bin/cloud_provider_firebase/fidl/factory.fidl.h"
#include "peridot/bin/cloud_provider_firebase/firebase/firebase_impl.h"
#include "peridot/bin/cloud_provider_firebase/page_cloud_impl.h"
#include "peridot/bin/ledger/callback/auto_cleanable.h"
#include "peridot/bin/ledger/callback/cancellable.h"
#include "peridot/bin/ledger/network/network_service.h"

namespace cloud_provider_firebase {

// Implementation of cloud_provider::CloudProvider.
//
// If the |on_empty| callback is set, it is called when the client connection is
// closed.
class CloudProviderImpl : public cloud_provider::CloudProvider {
 public:
  CloudProviderImpl(
      fxl::RefPtr<fxl::TaskRunner> main_runner,
      ledger::NetworkService* network_service,
      std::string user_id,
      ConfigPtr config,
      std::unique_ptr<auth_provider::AuthProvider> auth_provider,
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

  fxl::RefPtr<fxl::TaskRunner> main_runner_;
  ledger::NetworkService* const network_service_;
  const std::string user_id_;
  const std::string server_id_;
  std::unique_ptr<auth_provider::AuthProvider> auth_provider_;
  firebase::FirebaseImpl user_firebase_;
  fidl::Binding<cloud_provider::CloudProvider> binding_;
  fxl::Closure on_empty_;

  callback::AutoCleanableSet<DeviceSetImpl> device_sets_;

  callback::AutoCleanableSet<PageCloudImpl> page_clouds_;

  // Pending auth token requests to be cancelled when this class goes away.
  callback::CancellableContainer auth_token_requests_;

  FXL_DISALLOW_COPY_AND_ASSIGN(CloudProviderImpl);
};

}  // namespace cloud_provider_firebase

#endif  // PERIDOT_BIN_CLOUD_PROVIDER_FIREBASE_CLOUD_PROVIDER_IMPL_H_
