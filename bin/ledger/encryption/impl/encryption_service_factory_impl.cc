// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/ledger/encryption/impl/encryption_service_factory_impl.h"

#include "peridot/bin/ledger/encryption/impl/encryption_service_impl.h"

namespace encryption {

EncryptionServiceFactoryImpl::EncryptionServiceFactoryImpl(async_dispatcher_t* dispatcher)
    : dispatcher_(dispatcher) {}

EncryptionServiceFactoryImpl::~EncryptionServiceFactoryImpl() {}

std::unique_ptr<EncryptionService>
EncryptionServiceFactoryImpl::MakeEncryptionService(std::string namespace_id) {
  return std::make_unique<EncryptionServiceImpl>(dispatcher_,
                                                 std::move(namespace_id));
}

}  // namespace encryption
