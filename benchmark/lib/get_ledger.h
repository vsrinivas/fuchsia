// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_LEDGER_BENCHMARK_LIB_GET_LEDGER_H_
#define APPS_LEDGER_BENCHMARK_LIB_GET_LEDGER_H_

#include "application/lib/app/application_context.h"
#include "apps/ledger/services/public/ledger.fidl.h"
#include "lib/ftl/strings/string_view.h"

namespace benchmark {

ledger::LedgerPtr GetLedger(app::ApplicationContext* context,
                            app::ApplicationControllerPtr* controller,
                            std::string ledger_name,
                            std::string ledger_repository_path);

}  // namespace benchmark

#endif  // APPS_LEDGER_BENCHMARK_LIB_GET_LEDGER_H_
