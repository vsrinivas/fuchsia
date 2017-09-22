// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_LEDGER_CLOUD_PROVIDER_FIREBASE_CLOUD_PROVIDER_IMPL_H_
#define APPS_LEDGER_CLOUD_PROVIDER_FIREBASE_CLOUD_PROVIDER_IMPL_H_

#include "apps/ledger/cloud_provider_firebase/device_set_impl.h"
#include "apps/ledger/cloud_provider_firebase/page_cloud_impl.h"
#include "apps/ledger/cloud_provider_firebase/services/factory.fidl.h"
#include "apps/ledger/services/cloud_provider/cloud_provider.fidl.h"
#include "apps/ledger/src/auth_provider/auth_provider_impl.h"
#include "apps/ledger/src/callback/auto_cleanable.h"
#include "apps/ledger/src/network/network_service.h"
#include "lib/auth/fidl/token_provider.fidl.h"
#include "lib/fidl/cpp/bindings/binding.h"
#include "lib/fxl/functional/closure.h"
#include "lib/fxl/macros.h"
#include "lib/fxl/tasks/task_runner.h"

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

  fxl::RefPtr<fxl::TaskRunner> main_runner_;
  ledger::NetworkService* const network_service_;
  const std::string user_id_;
  const std::string server_id_;
  std::unique_ptr<auth_provider::AuthProvider> auth_provider_;
  fidl::Binding<cloud_provider::CloudProvider> binding_;
  fxl::Closure on_empty_;

  callback::AutoCleanableSet<DeviceSetImpl> device_sets_;

  callback::AutoCleanableSet<PageCloudImpl> page_clouds_;

  FXL_DISALLOW_COPY_AND_ASSIGN(CloudProviderImpl);
};

}  // namespace cloud_provider_firebase

#endif  // APPS_LEDGER_CLOUD_PROVIDER_FIREBASE_CLOUD_PROVIDER_IMPL_H_
