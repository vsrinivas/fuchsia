// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/console/analytics.h"

#include "src/developer/debug/zxdb/client/setting_schema_definition.h"
#include "src/developer/debug/zxdb/common/version.h"
#include "src/lib/analytics/cpp/core_dev_tools/general_parameters.h"
#include "src/lib/analytics/cpp/core_dev_tools/system_info.h"

namespace zxdb {

using ::analytics::GetOsVersion;
using ::analytics::core_dev_tools::AnalyticsOption;
using ::analytics::core_dev_tools::GeneralParameters;
using ::analytics::core_dev_tools::GoogleAnalyticsEvent;

void Analytics::Init(Session& session, AnalyticsOption analytics_option) {
  Init(analytics_option);
  session.system().settings().SetBool(ClientSettings::System::kEnableAnalytics, enabled_runtime_);
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

    GoogleAnalyticsEvent event(kEventCategoryGeneral, kEventActionInvoke);
    event.AddGeneralParameters(parameters);
    SendGoogleAnalyticsHit(event);
  }
}

}  // namespace zxdb
