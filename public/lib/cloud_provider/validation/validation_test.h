// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_CLOUD_PROVIDER_VALIDATION_VALIDATION_TEST_H_
#define LIB_CLOUD_PROVIDER_VALIDATION_VALIDATION_TEST_H_

#include <fuchsia/cpp/cloud_provider.h>

#include "gtest/gtest.h"
#include "lib/app/cpp/application_context.h"
#include "lib/fsl/tasks/message_loop.h"
#include "lib/fxl/macros.h"

namespace cloud_provider {

class ValidationTest : public ::testing::Test {
 public:
  ValidationTest();
  ~ValidationTest() override;

  void SetUp() override;

 protected:
  CloudProviderPtr cloud_provider_;

 private:
  fsl::MessageLoop message_loop_;
  std::unique_ptr<component::ApplicationContext> application_context_;

  FXL_DISALLOW_COPY_AND_ASSIGN(ValidationTest);
};

}  // namespace cloud_provider

#endif  // LIB_CLOUD_PROVIDER_VALIDATION_VALIDATION_TEST_H_
