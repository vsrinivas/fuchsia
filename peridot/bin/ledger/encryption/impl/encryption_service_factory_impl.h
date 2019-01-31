// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_LEDGER_ENCRYPTION_IMPL_ENCRYPTION_SERVICE_FACTORY_IMPL_H_
#define PERIDOT_BIN_LEDGER_ENCRYPTION_IMPL_ENCRYPTION_SERVICE_FACTORY_IMPL_H_

#include "peridot/bin/ledger/encryption/public/encryption_service_factory.h"
#include "peridot/bin/ledger/environment/environment.h"

namespace encryption {
class EncryptionServiceFactoryImpl : public EncryptionServiceFactory {
 public:
  explicit EncryptionServiceFactoryImpl(ledger::Environment* environment);
  ~EncryptionServiceFactoryImpl() override;

  // EncryptionServiceFactory
  std::unique_ptr<EncryptionService> MakeEncryptionService(
      std::string namespace_id) override;

 private:
  ledger::Environment* const environment_;
};
}  // namespace encryption

#endif  // PERIDOT_BIN_LEDGER_ENCRYPTION_IMPL_ENCRYPTION_SERVICE_FACTORY_IMPL_H_
