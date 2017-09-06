// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_LEDGER_CLOUD_PROVIDER_FIREBASE_CLOUD_PROVIDER_IMPL_H_
#define APPS_LEDGER_CLOUD_PROVIDER_FIREBASE_CLOUD_PROVIDER_IMPL_H_

#include "apps/ledger/cloud_provider_firebase/services/factory.fidl.h"
#include "apps/ledger/services/cloud_provider/cloud_provider.fidl.h"
#include "apps/ledger/src/auth_provider/auth_provider_impl.h"
#include "apps/modular/services/auth/token_provider.fidl.h"
#include "garnet/public/lib/fidl/cpp/bindings/binding.h"
#include "garnet/public/lib/ftl/functional/closure.h"
#include "garnet/public/lib/ftl/macros.h"
#include "garnet/public/lib/ftl/tasks/task_runner.h"

namespace cloud_provider_firebase {

class CloudProviderImpl : public cloud_provider::CloudProvider {
 public:
  CloudProviderImpl(
      ftl::RefPtr<ftl::TaskRunner> main_runner,
      fidl::InterfaceRequest<cloud_provider::CloudProvider> request,
      ConfigPtr config,
      fidl::InterfaceHandle<modular::auth::TokenProvider> token_provider);
  ~CloudProviderImpl() override;

  void set_on_empty(const ftl::Closure& on_empty) { on_empty_ = on_empty; }

 private:
  void GetDeviceSet(
      fidl::InterfaceRequest<cloud_provider::DeviceSet> device_set,
      const GetDeviceSetCallback& callback) override;

  void GetPageCloud(
      fidl::Array<uint8_t> app_id,
      fidl::Array<uint8_t> page_id,
      fidl::InterfaceRequest<cloud_provider::PageCloud> page_cloud,
      const GetPageCloudCallback& callback) override;

  ftl::RefPtr<ftl::TaskRunner> main_runner_;
  fidl::Binding<cloud_provider::CloudProvider> binding_;
  std::unique_ptr<auth_provider::AuthProviderImpl> auth_provider_;
  ftl::Closure on_empty_;

  FTL_DISALLOW_COPY_AND_ASSIGN(CloudProviderImpl);
};

}  // namespace cloud_provider_firebase

#endif  // APPS_LEDGER_CLOUD_PROVIDER_FIREBASE_CLOUD_PROVIDER_IMPL_H_
