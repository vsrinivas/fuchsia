// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LEDGER_BIN_ENCRYPTION_PUBLIC_ENCRYPTION_SERVICE_FACTORY_H_
#define SRC_LEDGER_BIN_ENCRYPTION_PUBLIC_ENCRYPTION_SERVICE_FACTORY_H_

#include <memory>

#include "src/ledger/bin/encryption/public/encryption_service.h"

namespace encryption {

// Factory for building EncryptionService per namespace.
class EncryptionServiceFactory {
 public:
  EncryptionServiceFactory() {}
  virtual ~EncryptionServiceFactory() {}

  // Creates the encryption service for the given namespace.
  virtual std::unique_ptr<EncryptionService> MakeEncryptionService(std::string namespace_id) = 0;
};

}  // namespace encryption

#endif  // SRC_LEDGER_BIN_ENCRYPTION_PUBLIC_ENCRYPTION_SERVICE_FACTORY_H_
