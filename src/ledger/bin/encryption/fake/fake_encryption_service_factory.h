// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LEDGER_BIN_ENCRYPTION_FAKE_FAKE_ENCRYPTION_SERVICE_FACTORY_H_
#define SRC_LEDGER_BIN_ENCRYPTION_FAKE_FAKE_ENCRYPTION_SERVICE_FACTORY_H_

#include <lib/async/dispatcher.h>

#include "src/ledger/bin/encryption/public/encryption_service_factory.h"

namespace encryption {
class FakeEncryptionServiceFactory : public EncryptionServiceFactory {
 public:
  explicit FakeEncryptionServiceFactory(async_dispatcher_t* dispatcher);
  ~FakeEncryptionServiceFactory() override;

  // EncryptionServiceFactory
  std::unique_ptr<EncryptionService> MakeEncryptionService(std::string namespace_id) override;

 private:
  async_dispatcher_t* dispatcher_;
};
}  // namespace encryption

#endif  // SRC_LEDGER_BIN_ENCRYPTION_FAKE_FAKE_ENCRYPTION_SERVICE_FACTORY_H_
