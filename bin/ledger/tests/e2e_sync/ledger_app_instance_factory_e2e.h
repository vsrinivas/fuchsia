// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_LEDGER_TESTS_E2E_SYNC_LEDGER_APP_INSTANCE_FACTORY_E2E_H_
#define PERIDOT_BIN_LEDGER_TESTS_E2E_SYNC_LEDGER_APP_INSTANCE_FACTORY_E2E_H_

#include <memory>
#include <string>

#include <lib/component/cpp/startup_context.h>

#include "peridot/bin/ledger/testing/cloud_provider_firebase_factory.h"
#include "peridot/bin/ledger/testing/ledger_app_instance_factory.h"

namespace test {

class LedgerAppInstanceFactoryImpl : public LedgerAppInstanceFactory {
 public:
  LedgerAppInstanceFactoryImpl(std::string server_id);
  ~LedgerAppInstanceFactoryImpl() override;

  void Init();

  std::unique_ptr<LedgerAppInstance> NewLedgerAppInstance(
      LoopController* loop_controller) override;

 private:
  std::unique_ptr<component::StartupContext> startup_context_;
  CloudProviderFirebaseFactory cloud_provider_firebase_factory_;

  const std::string server_id_;
};

}  // namespace test

#endif  // PERIDOT_BIN_LEDGER_TESTS_E2E_SYNC_LEDGER_APP_INSTANCE_FACTORY_E2E_H_
