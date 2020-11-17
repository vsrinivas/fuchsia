// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/console/analytics.h"

#include "src/developer/debug/shared/message_loop.h"
#include "src/developer/debug/zxdb/client/setting_schema_definition.h"
#include "src/developer/debug/zxdb/common/version.h"
#include "src/lib/analytics/cpp/core_dev_tools/general_parameters.h"
#include "src/lib/analytics/cpp/core_dev_tools/system_info.h"

namespace zxdb {

namespace {

constexpr char kEventCategoryGeneral[] = "general";
constexpr char kEventActionInvoke[] = "invoke";

}  // namespace

using ::analytics::GetOsVersion;
using ::analytics::core_dev_tools::GeneralParameters;
using ::analytics::core_dev_tools::SubLaunchStatus;

bool Analytics::should_be_enabled_runtime_ = false;

void Analytics::Init(Session& session, SubLaunchStatus sub_launch_status) {
  Init(sub_launch_status);
  session.system().settings().SetBool(ClientSettings::System::kEnableAnalytics,
                                      should_be_enabled_runtime_);
}

void Analytics::RunTask(fit::pending_task task) {
  auto executor = debug_ipc::MessageLoop::Current();

  FX_DCHECK(executor);
  if (executor) {
    executor->schedule_task(std::move(task));
  }
}
bool Analytics::IsEnabled(Session* session) {
  return !ClientIsCleanedUp() &&
         session->system().settings().GetBool(ClientSettings::System::kEnableAnalytics);
}

void Analytics::IfEnabledSendInvokeEvent(Session* session) {
  if (IsEnabled(session)) {
    GeneralParameters parameters;
    parameters.SetOsVersion(GetOsVersion());
    parameters.SetApplicationVersion(kBuildVersion);

    // Set an empty application name (an) to make application version (av) usable. Otherwise, the
    // hit will be treated as invalid by Google Analytics.
    // See https://developers.google.com/analytics/devguides/collection/protocol/v1/parameters#an
    // for more information.
    parameters.SetApplicationName("");

    auto event = GoogleAnalyticsEvent(kEventCategoryGeneral, kEventActionInvoke);
    event.AddGeneralParameters(parameters);
    SendGoogleAnalyticsEvent(event);
  }
}

}  // namespace zxdb
