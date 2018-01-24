// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/ledger/encryption/fake/fake_encryption_service_factory.h"

#include "peridot/bin/ledger/encryption/fake/fake_encryption_service.h"

namespace encryption {

FakeEncryptionServiceFactory::FakeEncryptionServiceFactory(
    fxl::RefPtr<fxl::TaskRunner> task_runner)
    : task_runner_(std::move(task_runner)) {}

FakeEncryptionServiceFactory::~FakeEncryptionServiceFactory() {}

std::unique_ptr<EncryptionService>
FakeEncryptionServiceFactory::MakeEncryptionService(
    std::string /*namespace_id*/) {
  return std::make_unique<FakeEncryptionService>(task_runner_);
}

}  // namespace encryption
