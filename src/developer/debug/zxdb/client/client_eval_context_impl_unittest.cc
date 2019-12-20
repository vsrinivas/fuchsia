// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/client/client_eval_context_impl.h"

#include "src/developer/debug/zxdb/client/remote_api_test.h"
#include "src/developer/debug/zxdb/client/session.h"
#include "src/developer/debug/zxdb/client/setting_schema_definition.h"
#include "src/developer/debug/zxdb/client/system.h"

namespace zxdb {

namespace {

class ClientEvalContextImplTest : public RemoteAPITest {};

}  // namespace

TEST_F(ClientEvalContextImplTest, AutoCastToDerived) {
  // Default setting to false.
  auto& system_settings = session().system().settings();
  system_settings.SetBool(ClientSettings::System::kAutoCastToDerived, false);

  Target* target = session().system().GetTargets()[0];

  auto context = fxl::MakeRefCounted<ClientEvalContextImpl>(target, std::nullopt);
  EXPECT_FALSE(context->ShouldPromoteToDerived());

  // Change to true, the context should pick up the change.
  system_settings.SetBool(ClientSettings::System::kAutoCastToDerived, true);
  EXPECT_TRUE(context->ShouldPromoteToDerived());
}

}  // namespace zxdb
