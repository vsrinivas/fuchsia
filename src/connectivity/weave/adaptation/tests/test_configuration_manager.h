// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_WEAVE_ADAPTATION_TESTS_TEST_CONFIGURATION_MANAGER_H_
#define SRC_CONNECTIVITY_WEAVE_ADAPTATION_TESTS_TEST_CONFIGURATION_MANAGER_H_

#include <gtest/gtest.h>

#include "configuration_manager_delegate_impl.h"

namespace weave::adaptation::testing {

class TestConfigurationManager final
    : public nl::Weave::DeviceLayer::ConfigurationManagerDelegateImpl {
 public:
  // Access underlying delegate implementation.
  using Impl = nl::Weave::DeviceLayer::ConfigurationManagerDelegateImpl;

  // Does the bare-minimum file initialization for tests. This should be used
  // instead of ConfigurationManager::Init() when tests invoke functions that
  // store data via ConfigurationManager but want to avoid accessing the
  // fuchsia.hwinfo and fuchsia.buildinfo FIDLs.
  void InitForTesting() {
    ASSERT_EQ(nl::Weave::DeviceLayer::Internal::EnvironmentConfig::Init(), WEAVE_NO_ERROR);
  }

  // Returns whether the device is paired to an account.
  bool IsPairedToAccount() override {
    return is_paired_to_account_ ? is_paired_to_account_.value() : Impl::IsPairedToAccount();
  }

  // Returns whether the device is a member of a fabric.
  bool IsMemberOfFabric() override {
    return is_member_of_fabric_ ? is_member_of_fabric_.value() : Impl::IsMemberOfFabric();
  }

  // Returns whether the device is thread-enabled.
  bool IsThreadEnabled() override {
    return is_thread_enabled_ ? is_thread_enabled_.value() : Impl::IsThreadEnabled();
  }

  // Returns the private key for signing.
  zx_status_t GetPrivateKeyForSigning(std::vector<uint8_t>* signing_key) override {
    if (use_signing_key_ && !use_signing_key_.value()) {
      return ZX_ERR_NOT_FOUND;
    }
    if (signing_key_) {
      std::copy(signing_key_.value().begin(), signing_key_.value().end(),
                std::back_inserter(*signing_key));
      return ZX_OK;
    }
    return Impl::GetPrivateKeyForSigning(signing_key);
  }

  // Set whether the device is paired to an account.
  TestConfigurationManager& set_is_paired_to_account(std::optional<bool> is_paired_to_account) {
    is_paired_to_account_ = is_paired_to_account;
    return *this;
  }

  // Set whether the device is a member of a fabric.
  TestConfigurationManager& set_is_member_of_fabric(std::optional<bool> is_member_of_fabric) {
    is_member_of_fabric_ = is_member_of_fabric;
    return *this;
  }

  // Set whether the device is thread-enabled.
  TestConfigurationManager& set_is_thread_enabled(std::optional<bool> is_thread_enabled) {
    is_thread_enabled_ = is_thread_enabled;
    return *this;
  }

  // Set whether or not to use the local private key.
  TestConfigurationManager& set_use_signing_key(std::optional<bool> use_signing_key) {
    use_signing_key_ = use_signing_key;
    return *this;
  }

  // Set the local private key when not using the fuchsia.weave.Signer protocol.
  TestConfigurationManager& set_signing_key(std::optional<std::vector<uint8_t>> signing_key) {
    signing_key_ = signing_key;
    return *this;
  }

 private:
  std::optional<bool> is_paired_to_account_;
  std::optional<bool> is_member_of_fabric_;
  std::optional<bool> is_thread_enabled_;
  std::optional<bool> use_signing_key_;
  std::optional<std::vector<uint8_t>> signing_key_;
};

}  // namespace weave::adaptation::testing

#endif  // SRC_CONNECTIVITY_WEAVE_ADAPTATION_TESTS_TEST_CONFIGURATION_MANAGER_H_
