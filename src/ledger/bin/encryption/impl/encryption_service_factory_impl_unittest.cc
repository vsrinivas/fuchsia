// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/bin/encryption/impl/encryption_service_factory_impl.h"

#include "gtest/gtest.h"
#include "src/ledger/bin/testing/test_with_environment.h"

namespace encryption {
namespace {

using EncryptionServiceFactoryTest = ledger::TestWithEnvironment;

TEST_F(EncryptionServiceFactoryTest, MakeEncryptionService) {
  EncryptionServiceFactoryImpl factory(&environment_);
  EXPECT_TRUE(factory.MakeEncryptionService("namespace_id"));
}

}  // namespace
}  // namespace encryption
