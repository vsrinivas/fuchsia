// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/src/cobalt/cobalt.h"

namespace ledger {

ftl::AutoCall<ftl::Closure> InitializeCobalt(
    ftl::RefPtr<ftl::TaskRunner> task_runner,
    app::ApplicationContext* app_context) {
  return ftl::MakeAutoCall<ftl::Closure>([] {});
}

void ReportEvent(CobaltEvent event) {
  FTL_NOTIMPLEMENTED();
}

}  // namespace ledger
