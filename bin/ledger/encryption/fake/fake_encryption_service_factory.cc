// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/ledger/encryption/fake/fake_encryption_service_factory.h"

#include "peridot/bin/ledger/encryption/fake/fake_encryption_service.h"

namespace encryption {

FakeEncryptionServiceFactory::FakeEncryptionServiceFactory(async_dispatcher_t* dispatcher)
    : dispatcher_(dispatcher) {}

FakeEncryptionServiceFactory::~FakeEncryptionServiceFactory() {}

std::unique_ptr<EncryptionService>
FakeEncryptionServiceFactory::MakeEncryptionService(
    std::string /*namespace_id*/) {
  return std::make_unique<FakeEncryptionService>(dispatcher_);
}

}  // namespace encryption
