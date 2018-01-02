// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_CLOUD_PROVIDER_VALIDATION_VALIDATION_TEST_H_
#define LIB_CLOUD_PROVIDER_VALIDATION_VALIDATION_TEST_H_

#include "gtest/gtest.h"
#include "lib/app/cpp/application_context.h"
#include "lib/cloud_provider/fidl/cloud_provider.fidl.h"
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
  std::unique_ptr<app::ApplicationContext> application_context_;

  FXL_DISALLOW_COPY_AND_ASSIGN(ValidationTest);
};

}  // namespace cloud_provider

#endif  // LIB_CLOUD_PROVIDER_VALIDATION_VALIDATION_TEST_H_
