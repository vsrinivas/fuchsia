// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/ledger/encryption/impl/encryption_service_factory_impl.h"

#include "gtest/gtest.h"
#include "lib/gtest/test_loop_fixture.h"

namespace encryption {
namespace {

using EncryptionServiceFactoryTest = gtest::TestLoopFixture;

TEST_F(EncryptionServiceFactoryTest, MakeEncryptionService) {
  EncryptionServiceFactoryImpl factory(dispatcher());
  EXPECT_TRUE(factory.MakeEncryptionService("namespace_id"));
}

}  // namespace
}  // namespace encryption
