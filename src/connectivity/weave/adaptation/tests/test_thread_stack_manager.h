// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_WEAVE_ADAPTATION_TESTS_TEST_THREAD_STACK_MANAGER_H_
#define SRC_CONNECTIVITY_WEAVE_ADAPTATION_TESTS_TEST_THREAD_STACK_MANAGER_H_

#include <gtest/gtest.h>

#include "thread_stack_manager_delegate_impl.h"

namespace weave::adaptation::testing {

class TestThreadStackManager final : public nl::Weave::DeviceLayer::ThreadStackManagerDelegateImpl {
 public:
  static constexpr char kThreadInterfaceName[] = "lowpan0";

  // Access underlying delegate implementation.
  using Impl = nl::Weave::DeviceLayer::ThreadStackManagerDelegateImpl;

  // Returns whether thread is provisioned.
  bool IsThreadProvisioned() override {
    return is_thread_provisioned_.value_or(Impl::IsThreadProvisioned());
  }

  // Returns whether thread is supported.
  bool IsThreadSupported() const override {
    return is_thread_supported_.value_or(Impl::IsThreadSupported());
  }

  // Returns the thread interface name.
  std::string GetInterfaceName() const override { return kThreadInterfaceName; }

  // Set whether thread is provisioned.
  TestThreadStackManager& set_is_thread_provisioned(std::optional<bool> is_thread_provisioned) {
    is_thread_provisioned_ = is_thread_provisioned;
    return *this;
  }

  // Set whether thread is supported.
  TestThreadStackManager& set_is_thread_supported(std::optional<bool> is_thread_supported) {
    is_thread_supported_ = is_thread_supported;
    return *this;
  }

 private:
  std::optional<bool> is_thread_provisioned_;
  std::optional<bool> is_thread_supported_;
};

}  // namespace weave::adaptation::testing

#endif  // SRC_CONNECTIVITY_WEAVE_ADAPTATION_TESTS_TEST_THREAD_STACK_MANAGER_H_
