// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/ledger/encryption/fake/fake_encryption_service_factory.h"

#include "peridot/bin/ledger/encryption/fake/fake_encryption_service.h"

namespace encryption {

FakeEncryptionServiceFactory::FakeEncryptionServiceFactory(async_t* async)
    : async_(async) {}

FakeEncryptionServiceFactory::~FakeEncryptionServiceFactory() {}

std::unique_ptr<EncryptionService>
FakeEncryptionServiceFactory::MakeEncryptionService(
    std::string /*namespace_id*/) {
  return std::make_unique<FakeEncryptionService>(async_);
}

}  // namespace encryption
