// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/ledger/encryption/impl/encryption_service_factory_impl.h"

#include "peridot/bin/ledger/encryption/impl/encryption_service_impl.h"

namespace encryption {

EncryptionServiceFactoryImpl::EncryptionServiceFactoryImpl(
    fxl::RefPtr<fxl::TaskRunner> task_runner)
    : task_runner_(std::move(task_runner)) {}

EncryptionServiceFactoryImpl::~EncryptionServiceFactoryImpl() {}

std::unique_ptr<EncryptionService>
EncryptionServiceFactoryImpl::MakeEncryptionService(std::string namespace_id) {
  return std::make_unique<EncryptionServiceImpl>(task_runner_,
                                                 std::move(namespace_id));
}

}  // namespace encryption
