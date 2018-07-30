// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_CLOUD_PROVIDER_FIRESTORE_TESTING_CLOUD_PROVIDER_FACTORY_H_
#define PERIDOT_BIN_CLOUD_PROVIDER_FIRESTORE_TESTING_CLOUD_PROVIDER_FACTORY_H_

#include <fuchsia/ledger/cloud/cpp/fidl.h>
#include <fuchsia/ledger/cloud/firestore/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/component/cpp/startup_context.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/fxl/memory/ref_ptr.h>
#include <lib/network_wrapper/network_wrapper_impl.h>

#include "peridot/bin/cloud_provider_firestore/include/types.h"
#include "peridot/lib/firebase_auth/testing/service_account_token_provider.h"

namespace cloud_provider_firestore {

// Factory for real Firestore cloud provider binaries backed by fake token
// provider.
//
// This is used for end-to-end testing, including the validation test suite for
// the cloud provider.
class CloudProviderFactory {
 public:
  CloudProviderFactory(component::StartupContext* startup_context,
                       std::string server_id, std::string api_key,
                       std::string credentials);
  ~CloudProviderFactory();

  void Init();

  void MakeCloudProvider(
      fidl::InterfaceRequest<cloud_provider::CloudProvider> request);

  void MakeCloudProviderWithGivenUserId(
      std::string user_id,
      fidl::InterfaceRequest<cloud_provider::CloudProvider> request);

 private:
  class TokenProviderContainer;
  component::StartupContext* const startup_context_;
  const std::string server_id_;
  const std::string api_key_;
  const std::string credentials_;

  // Loop on which the token manager runs.
  async::Loop services_loop_;

  callback::AutoCleanableSet<TokenProviderContainer> token_providers_;

  fuchsia::sys::ComponentControllerPtr cloud_provider_controller_;
  FactoryPtr cloud_provider_factory_;

  FXL_DISALLOW_COPY_AND_ASSIGN(CloudProviderFactory);
};

}  // namespace cloud_provider_firestore

#endif  // PERIDOT_BIN_CLOUD_PROVIDER_FIRESTORE_TESTING_CLOUD_PROVIDER_FACTORY_H_
