// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/debug_adapter/handlers/request_scopes.h"

#include <dap/optional.h>
#include <dap/protocol.h>
#include <dap/session.h>

#include "lib/syslog/cpp/macros.h"
#include "src/developer/debug/zxdb/client/source_file_provider_impl.h"
#include "src/developer/debug/zxdb/debug_adapter/context.h"
#include "src/developer/debug/zxdb/symbols/function.h"
#include "src/developer/debug/zxdb/symbols/location.h"
#include "src/developer/debug/zxdb/symbols/variable.h"
#include "src/developer/debug/zxdb/symbols/visit_scopes.h"

namespace zxdb {

dap::ResponseOrError<dap::ScopesResponse> OnRequestScopes(DebugAdapterContext* ctx,
                                                          const dap::ScopesRequest& req) {
  dap::ScopesResponse response = {};
  auto frame = ctx->FrameforId(req.frameId);
  if (!frame) {
    return dap::Error("Invalid frame ID");
  }

  if (Err err = ctx->CheckStoppedThread(frame->GetThread()); err.has_error()) {
    return dap::Error(err.msg());
  }

  const Location& location = frame->GetLocation();
  if (!location.symbol())
    return dap::Error("There is no symbol information for the frame.");

  const Function* function = location.symbol().Get()->As<Function>();
  if (!function)
    return dap::Error("Symbols are corrupt.");

  dap::optional<dap::Source> source;
  auto file_provider = SourceFileProviderImpl(ctx->GetCurrentTarget()->settings());
  auto data_or =
      file_provider.GetFileData(location.file_line().file(), location.file_line().comp_dir());
  if (!data_or.has_error()) {
    dap::Source s;
    s.path = data_or.value().full_path;
    source = s;
  }

  for (auto i = 0; i < static_cast<int>(VariablesType::kVariablesTypeCount); i++) {
    dap::Scope scope;
    scope.source = source;
    scope.variablesReference = ctx->IdForVariables(req.frameId, static_cast<VariablesType>(i));
    switch (static_cast<VariablesType>(i)) {
      case VariablesType::kLocal:
        scope.name = "Locals";
        scope.presentationHint = "locals";
        break;
      case VariablesType::kArguments:
        scope.name = "Arguments";
        scope.presentationHint = "arguments";
        break;
      case VariablesType::kRegister:
        scope.name = "Registers";
        scope.presentationHint = "registers";
        break;
      default:
        // Internal variable types will not be reported in scopes. Skip it.
        continue;
    }
    response.scopes.push_back(std::move(scope));
  }
  return response;
}

}  // namespace zxdb
