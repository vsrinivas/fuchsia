// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_LEDGER_ENCRYPTION_IMPL_ENCRYPTION_SERVICE_FACTORY_IMPL_H_
#define PERIDOT_BIN_LEDGER_ENCRYPTION_IMPL_ENCRYPTION_SERVICE_FACTORY_IMPL_H_

#include "lib/fxl/tasks/task_runner.h"
#include "peridot/bin/ledger/encryption/public/encryption_service_factory.h"

namespace encryption {
class EncryptionServiceFactoryImpl : public EncryptionServiceFactory {
 public:
  explicit EncryptionServiceFactoryImpl(
      fxl::RefPtr<fxl::TaskRunner> task_runner);
  ~EncryptionServiceFactoryImpl() override;

  // EncryptionServiceFactory
  std::unique_ptr<EncryptionService> MakeEncryptionService(
      std::string namespace_id) override;

 private:
  fxl::RefPtr<fxl::TaskRunner> task_runner_;
};
}  // namespace encryption

#endif  // PERIDOT_BIN_LEDGER_ENCRYPTION_IMPL_ENCRYPTION_SERVICE_FACTORY_IMPL_H_
