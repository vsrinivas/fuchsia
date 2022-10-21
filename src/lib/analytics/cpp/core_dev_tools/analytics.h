// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_ANALYTICS_CPP_CORE_DEV_TOOLS_ANALYTICS_H_
#define SRC_LIB_ANALYTICS_CPP_CORE_DEV_TOOLS_ANALYTICS_H_

#include <memory>
#include <string>

#include "lib/fit/function.h"
#include "sdk/lib/syslog/cpp/macros.h"
#include "src/developer/debug/zxdb/common/version.h"
#include "src/lib/analytics/cpp/core_dev_tools/analytics_internal.h"
#include "src/lib/analytics/cpp/core_dev_tools/analytics_messages.h"
#include "src/lib/analytics/cpp/core_dev_tools/analytics_status.h"
#include "src/lib/analytics/cpp/core_dev_tools/command_line_options.h"
#include "src/lib/analytics/cpp/core_dev_tools/environment_status.h"
#include "src/lib/analytics/cpp/core_dev_tools/general_parameters.h"
#include "src/lib/analytics/cpp/core_dev_tools/google_analytics_client.h"
#include "src/lib/analytics/cpp/core_dev_tools/persistent_status.h"
#include "src/lib/analytics/cpp/core_dev_tools/system_info.h"
#include "src/lib/analytics/cpp/google_analytics/client.h"
#include "src/lib/analytics/cpp/google_analytics/event.h"
#include "src/lib/analytics/cpp/metric_properties/metric_properties.h"

namespace analytics::core_dev_tools {

// This class uses template following the pattern of CRTP
// (See https://en.wikipedia.org/wiki/Curiously_recurring_template_pattern).
// We use CRTP instead of the more common dynamic polymorphism via inheritance, in order to
// provide a simple, static interface for analytics, such that sending an event just looks like a
// one-line command without creating an object first. We choose static interface here because the
// action of sending analytics itself is rather static, without interacting with any internal
// status that changes from instance to instance.
//
// To use this class, one must inherit this class and specify required constants like below:
//
//     class ToolAnalytics : public Analytics<ToolAnalytics> {
//      public:
//       // ......
//
//      private:
//       friend class Analytics<ToolAnalytics>;
//       static constexpr char kToolName[] = "tool";
//       static constexpr int64_t kQuitTimeoutMs = 500; // wait for at most 500 ms before quitting
//       static constexpr char kTrackingId[] = "UA-XXXXX-Y";
//       static constexpr char kEnableArgs[] = "--analytics=enable";
//       static constexpr char kDisableArgs[] = "--analytics=disable";
//       static constexpr char kStatusArgs[] = "--show-analytics";
//       static constexpr char kAnalyticsList[] = R"(1. ...
//     2. ...)";
//     }
//
// One also needs to (if not already) add the following lines to the main() function before any
// threads are spawned and any use of Curl or Analytics:
//     debug_ipc::Curl::GlobalInit();
//     auto deferred_cleanup_curl = fit::defer(debug_ipc::Curl::GlobalCleanup);
//     auto deferred_cleanup_analytics = fit::defer(Analytics::CleanUp);
// and include related headers, e.g. <lib/fit/defer.h> and "src/developer/debug/zxdb/common/curl.h".
//
// The derived class can also define their own functions for sending analytics. For example
//
//     // The definition of a static public function in ToolAnalytics
//     void ToolAnalytics::IfEnabledSendExitEvent() {
//       if(<runtime analytics enabled>) {
//         SendGoogleAnalyticsHit(<...>);
//       }
//     }
//
template <class T>
class Analytics {
 public:
  // Same as Init() but will behave differently when run by bot
  static void InitBotAware(AnalyticsOption analytics_option, bool enable_on_bots = false) {
    metric_properties::MigrateMetricDirectory();
    if (IsDisabledByEnvironment()) {
      T::SetRuntimeAnalyticsStatus(AnalyticsStatus::kDisabled);
      return;
    }
    BotInfo bot = GetBotInfo();
    if (bot.IsRunByBot()) {
      if (enable_on_bots && (internal::PersistentStatus::IsFirstLaunchOfFirstTool() ||
                             !internal::PersistentStatus::IsEnabled())) {
        internal::PersistentStatus::Enable();
      }
      T::SetRuntimeAnalyticsStatus(enable_on_bots ? AnalyticsStatus::kEnabled
                                                  : AnalyticsStatus::kDisabled);
    } else {
      Init(analytics_option);
    }

    // Remove "&& bot.IsRunByBot()" if one wants to add "ds=user" parameter for non-bot users
    // Currently, non-bot users will have "(not set)" for the "ds" (data source) parameter, which
    // appears to be enough for now
    if (enable_on_bots && IsEnabled() && bot.IsRunByBot()) {
      FX_DCHECK(!client_ && !client_is_cleaned_up_);
      CreateAndPrepareGoogleAnalyticsClient(bot);
    }
  }

  static void PersistentEnable() {
    if (internal::PersistentStatus::IsEnabled()) {
      internal::ShowAlready(AnalyticsStatus::kEnabled);
    } else {
      internal::PersistentStatus::Enable();
      internal::ShowChangedTo(AnalyticsStatus::kEnabled);
      SendAnalyticsManualEnableEvent();
    }
  }

  static void PersistentDisable() {
    if (internal::PersistentStatus::IsEnabled()) {
      SendAnalyticsDisableEvent();
      internal::PersistentStatus::Disable();
      internal::ShowChangedTo(AnalyticsStatus::kDisabled);
    } else {
      internal::ShowAlready(AnalyticsStatus::kDisabled);
    }
  }

  // Show the persistent analytics status and the what is collected
  static void ShowAnalytics() {
    internal::ToolInfo tool_info{T::kToolName, T::kEnableArgs, T::kDisableArgs, T::kStatusArgs};
    internal::ShowAnalytics(tool_info,
                            internal::PersistentStatus::IsEnabled() ? AnalyticsStatus::kEnabled
                                                                    : AnalyticsStatus::kDisabled,
                            T::kAnalyticsList);
  }

  static void IfEnabledSendInvokeEvent() {
    if (IsEnabled()) {
      GeneralParameters parameters;
      parameters.SetOsVersion(analytics::GetOsVersion());
      parameters.SetApplicationVersion(zxdb::kBuildVersion);

      // Set an empty application name (an) to make application version (av) usable. Otherwise, the
      // hit will be treated as invalid by Google Analytics.
      // See https://developers.google.com/analytics/devguides/collection/protocol/v1/parameters#an
      // for more information.
      parameters.SetApplicationName("");

      google_analytics::Event event(kEventCategoryGeneral, kEventActionInvoke);
      event.AddGeneralParameters(parameters);
      SendGoogleAnalyticsHit(event);
    }
  }

  static void IfEnabledSendGoogleAnalyticsHit(const google_analytics::Hit& hit) {
    if (IsEnabled()) {
      SendGoogleAnalyticsHit(hit);
    }
  }

  static void CleanUp() {
    delete client_;
    client_ = nullptr;
    client_is_cleaned_up_ = true;
  }

 protected:
  static constexpr char kEventCategoryGeneral[] = "general";
  static constexpr char kEventActionInvoke[] = "invoke";

  static void SendGoogleAnalyticsHit(const google_analytics::Hit& hit) {
    if (!client_is_cleaned_up_) {
      if (!client_) {
        CreateAndPrepareGoogleAnalyticsClient();
      }
      client_->AddHit(hit);
    }
  }

  static bool ClientIsCleanedUp() { return client_is_cleaned_up_; }

  static void SetRuntimeAnalyticsStatus(AnalyticsStatus status) {
    enabled_runtime_ = (status == AnalyticsStatus::kEnabled);
  }

  static bool IsEnabled() { return !ClientIsCleanedUp() && enabled_runtime_; }

  inline static bool enabled_runtime_ = false;

 private:
  static constexpr char kEventCategoryAnalytics[] = "analytics";
  static constexpr char kEventActionEnable[] = "manual-enable";
  static constexpr char kEventActionDisable[] = "disable";

  // Init analytics status, and show suitable welcome messages if on the first run.
  static void Init(AnalyticsOption analytics_option) {
    internal::PersistentStatus persistent_status(T::kToolName);
    if (internal::PersistentStatus::IsFirstLaunchOfFirstTool()) {
      InitFirstRunOfFirstTool(persistent_status);
    } else if (analytics_option == AnalyticsOption::kSubLaunchFirst) {
      InitSubLaunchedFirst();
    } else if (analytics_option == AnalyticsOption::kSubLaunchNormal) {
      InitSubLaunchedNormal();
    } else if (persistent_status.IsFirstDirectLaunch()) {
      InitFirstRunOfOtherTool(persistent_status);
    } else {
      InitSubsequentRun();
    }
  }

  static void InitFirstRunOfFirstTool(internal::PersistentStatus& persistent_status) {
    internal::ToolInfo tool_info{T::kToolName, T::kEnableArgs, T::kDisableArgs, T::kStatusArgs};
    ShowMessageFirstRunOfFirstTool(tool_info);
    internal::PersistentStatus::Enable();
    persistent_status.MarkAsDirectlyLaunched();
    T::SetRuntimeAnalyticsStatus(AnalyticsStatus::kDisabled);
  }

  static void InitFirstRunOfOtherTool(internal::PersistentStatus& persistent_status) {
    internal::ToolInfo tool_info{T::kToolName, T::kEnableArgs, T::kDisableArgs, T::kStatusArgs};
    if (internal::PersistentStatus::IsEnabled()) {
      ShowMessageFirstRunOfOtherTool(tool_info, AnalyticsStatus::kEnabled);
      persistent_status.MarkAsDirectlyLaunched();
      T::SetRuntimeAnalyticsStatus(AnalyticsStatus::kEnabled);
    } else {
      ShowMessageFirstRunOfOtherTool(tool_info, AnalyticsStatus::kDisabled);
      persistent_status.MarkAsDirectlyLaunched();
      T::SetRuntimeAnalyticsStatus(AnalyticsStatus::kDisabled);
    }
  }

  static void InitSubsequentRun() {
    if (internal::PersistentStatus::IsEnabled()) {
      T::SetRuntimeAnalyticsStatus(AnalyticsStatus::kEnabled);
    } else {
      T::SetRuntimeAnalyticsStatus(AnalyticsStatus::kDisabled);
    }
  }

  static void InitSubLaunchedNormal() { InitSubsequentRun(); }

  static void InitSubLaunchedFirst() { T::SetRuntimeAnalyticsStatus(AnalyticsStatus::kDisabled); }

  static void CreateAndPrepareGoogleAnalyticsClient(std::optional<BotInfo> bot = std::nullopt) {
    client_ = new GoogleAnalyticsClient(T::kQuitTimeoutMs);
    internal::PrepareGoogleAnalyticsClient(*client_, T::kToolName, T::kTrackingId, bot);
  }

  static void SendAnalyticsManualEnableEvent() {
    SendGoogleAnalyticsHit(google_analytics::Event(kEventCategoryAnalytics, kEventActionEnable));
  }

  static void SendAnalyticsDisableEvent() {
    SendGoogleAnalyticsHit(google_analytics::Event(kEventCategoryAnalytics, kEventActionDisable));
  }

  inline static bool client_is_cleaned_up_ = false;
  // Instead of using an fbl::NoDestructor<std::unique_ptr<google_analytics::Client>>, a raw pointer
  // is used here, since
  // (1) there is no ownership transfer
  // (2) the life time of the pointed-to object is managed manually
  // (3) using a raw pointer here makes code simpler and easier to read
  inline static google_analytics::Client* client_ = nullptr;
};

}  // namespace analytics::core_dev_tools

#endif  // SRC_LIB_ANALYTICS_CPP_CORE_DEV_TOOLS_ANALYTICS_H_
