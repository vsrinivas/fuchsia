// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/bin/encryption/impl/encryption_service_factory_impl.h"

#include "src/ledger/bin/encryption/impl/encryption_service_impl.h"

namespace encryption {

EncryptionServiceFactoryImpl::EncryptionServiceFactoryImpl(ledger::Environment* environment)
    : environment_(environment) {}

EncryptionServiceFactoryImpl::~EncryptionServiceFactoryImpl() {}

std::unique_ptr<EncryptionService> EncryptionServiceFactoryImpl::MakeEncryptionService(
    std::string namespace_id) {
  return std::make_unique<EncryptionServiceImpl>(environment_, std::move(namespace_id));
}

}  // namespace encryption
