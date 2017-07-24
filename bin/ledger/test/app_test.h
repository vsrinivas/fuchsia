// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_LEDGER_SRC_TEST_APP_TEST_H_
#define APPS_LEDGER_SRC_TEST_APP_TEST_H_

#include "application/lib/app/application_context.h"
#include "application/services/application_environment.fidl.h"
#include "apps/ledger/src/test/test_with_message_loop.h"

namespace test {

class AppTest : public TestWithMessageLoop {
 public:
  AppTest();
  ~AppTest() override;

 protected:
  app::ApplicationContext* application_context() {
    return application_context_.get();
  }

  std::unique_ptr<app::ApplicationContext> application_context_;
};

}  // namespace test

#endif  // APPS_LEDGER_SRC_TEST_APP_TEST_H_
