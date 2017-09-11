// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/fxl/logging.h"
#include "lib/fxl/macros.h"
#include "mojo/public/cpp/application/application_test_base.h"
#include "mojo/services/icu_data/cpp/icu_data.h"

#ifndef LIB_URL_TEST_ICU_UNITTEST_BASE_H_
#define LIB_URL_TEST_ICU_UNITTEST_BASE_H_

namespace url {

namespace test {

class IcuUnitTestBase : public mojo::test::ApplicationTestBase {
 public:
  IcuUnitTestBase() {}
  ~IcuUnitTestBase() override {}
  void SetUp() override {
    mojo::test::ApplicationTestBase::SetUp();

    mojo::ApplicationConnectorPtr application_connector;
    shell()->CreateApplicationConnector(mojo::GetProxy(&application_connector));
    bool icu_success = icu_data::Initialize(application_connector.get());
    FXL_DCHECK(icu_success);
  }
  void TearDown() override {
    mojo::test::ApplicationTestBase::TearDown();
    bool icu_success = icu_data::Release();
    FXL_DCHECK(icu_success);
  }
 private:
  FXL_DISALLOW_COPY_AND_ASSIGN(IcuUnitTestBase);
};

}  // namespace test

}  // namespace url

#endif  // LIB_URL_TEST_ICU_UNITTEST_BASE_H_
