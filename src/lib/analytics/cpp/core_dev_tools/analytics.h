// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_ANALYTICS_CPP_CORE_DEV_TOOLS_ANALYTICS_H_
#define SRC_LIB_ANALYTICS_CPP_CORE_DEV_TOOLS_ANALYTICS_H_

#include <string>

#include "lib/fit/function.h"
#include "src/lib/analytics/cpp/core_dev_tools/analytics_internal.h"
#include "src/lib/analytics/cpp/core_dev_tools/analytics_messages.h"
#include "src/lib/analytics/cpp/core_dev_tools/analytics_status.h"
#include "src/lib/analytics/cpp/core_dev_tools/persistent_status.h"
#include "src/lib/analytics/cpp/google_analytics/client.h"
#include "src/lib/analytics/cpp/google_analytics/event.h"
#include "src/lib/fxl/memory/weak_ptr.h"

namespace analytics::core_dev_tools {

enum class SubLaunchStatus {
  // sub-launched by the first run of the first tool
  kSubLaunchedFirst,
  // sub-launched otherwise
  kSubLaunchedNormal,

  kDirectlyLaunched
};

// This class uses template following the pattern of CRTP
// (See https://en.wikipedia.org/wiki/Curiously_recurring_template_pattern).
// We use CRTP instead of the more common dynamic polymorphism via inheritance, in order to
// provide a simple, static interface for analytics, such that sending an event just looks like a
// one-line command without creating an object first. We choose static interface here because the
// action of sending analytics itself is rather static, without interacting with any internal
// status that changes from instance to instance.
//
// To use this class, one must inherit this class and specify required constants and static methods
// as below:
//
//     class ToolAnalytics : public Analytics<ToolAnalytics> {
//       friend class Analytics<ToolAnalytics>;
//
//      public:
//       // ......
//
//      private:
//       static constexpr char kToolName[] = "tool";
//       static constexpr char kTrackingId[] = "UA-XXXXX-Y";
//       static constexpr char kEnableArgs[] = "--analytics=enable";
//       static constexpr char kDisableArgs[] = "--analytics=disable";
//       static constexpr char kStatusArgs[] = "--show-analytics";
//       static constexpr char kAnalyticsList[] = R"(1. ...
//     2. ...)";
//
//       static void SetRuntimeAnalyticsStatus(AnalyticsStatus status) {
//       // ......
//       }
//
//       static google_analytics::Client* CreateGoogleAnalyticsClient() {
//         return new ToolGoogleAnalyticsClient();
//       }
//
//       static void RunTask(fit::pending_task task) {
//       // ...
//       }
//     }
//
// Then the derived class can define their own functions for sending analytics. For example
//
//     // The definition of a static public function in ToolAnalytics
//     void ToolAnalytics::IfEnabledSendExitEvent() {
//       if(<runtime analytics enabled>) {
//         SendGoogleAnalyticsEvent(<...>);
//       }
//     }
//
template <class T>
class Analytics {
 public:
  // Init analytics status, and show suitable welcome messages if on the first run.
  static void Init(SubLaunchStatus sub_launch_status) {
    internal::PersistentStatus persistent_status(T::kToolName);
    if (internal::PersistentStatus::IsFirstLaunchOfFirstTool()) {
      InitFirstRunOfFirstTool(persistent_status);
    } else if (sub_launch_status == SubLaunchStatus::kSubLaunchedFirst) {
      InitSubLaunchedFirst();
    } else if (sub_launch_status == SubLaunchStatus::kSubLaunchedNormal) {
      InitSubLaunchedNormal();
    } else if (persistent_status.IsFirstDirectLaunch()) {
      InitFirstRunOfOtherTool(persistent_status);
    } else {
      InitSubsequentRun();
    }
  }

  static void PersistentEnable(fit::callback<void()> callback = nullptr) {
    if (internal::PersistentStatus::IsEnabled()) {
      internal::ShowAlready(AnalyticsStatus::kEnabled);
      if (callback) {
        T::RunTask(fit::make_promise([callback = std::move(callback)]() mutable { callback(); }));
      }
    } else {
      internal::PersistentStatus::Enable();
      internal::ShowChangedTo(AnalyticsStatus::kEnabled);
      SendAnalyticsManualEnableEvent(std::move(callback));
    }
  }

  static void PersistentDisable(fit::callback<void()> callback = nullptr) {
    if (internal::PersistentStatus::IsEnabled()) {
      SendAnalyticsDisableEvent(std::move(callback));
      internal::PersistentStatus::Disable();
      internal::ShowChangedTo(AnalyticsStatus::kDisabled);
    } else {
      internal::ShowAlready(AnalyticsStatus::kDisabled);
      if (callback) {
        T::RunTask(fit::make_promise([callback = std::move(callback)]() mutable { callback(); }));
      }
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

 protected:
  static void SendGoogleAnalyticsEvent(const google_analytics::Event& event,
                                       fit::callback<void()> callback = nullptr) {
    auto client = GetGoogleAnalyticsClient();
    if (client) {
      T::RunTask(client->AddEvent(event).then(
          [callback =
               std::move(callback)](fit::result<void, google_analytics::NetError>& result) mutable {
            if (callback) {
              callback();
            }
          }));
    } else {
      if (callback) {
        T::RunTask(fit::make_promise([callback = std::move(callback)]() mutable { callback(); }));
      }
    }
  }

  // Clean up the global pointer used by the Google Analytics client.
  static void CleanUpGoogleAnalyticsClient() {
    client_is_cleaned_up_ = true;
    auto* client = GetGoogleAnalyticsClient().get();
    delete client;
  }

  static bool ClientIsCleanedUp() { return client_is_cleaned_up_; }

 private:
  static constexpr char kEventCategoryAnalytics[] = "analytics";
  static constexpr char kEventActionEnable[] = "manual-enable";
  static constexpr char kEventActionDisable[] = "disable";

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

  static std::unique_ptr<google_analytics::Client> CreateAndPrepareGoogleAnalyticsClient() {
    std::unique_ptr<google_analytics::Client> client = T::CreateGoogleAnalyticsClient();
    internal::PrepareGoogleAnalyticsClient(*client, T::kToolName, T::kTrackingId);
    return client;
  };

  static fxl::WeakPtr<google_analytics::Client> GetGoogleAnalyticsClient() {
    // One can call CleanUpGoogleAnalyticsClient to free the pointer
    static google_analytics::Client* client = nullptr;
    if (!client_is_cleaned_up_ &&
        (client != nullptr || (client = CreateAndPrepareGoogleAnalyticsClient().release()))) {
      return client->GetWeakPtr();
    }
    client = nullptr;
    return fxl::WeakPtr<google_analytics::Client>();
  }

  static void SendAnalyticsManualEnableEvent(fit::callback<void()> callback = nullptr) {
    SendGoogleAnalyticsEvent(google_analytics::Event(kEventCategoryAnalytics, kEventActionEnable),
                             std::move(callback));
  }

  static void SendAnalyticsDisableEvent(fit::callback<void()> callback = nullptr) {
    SendGoogleAnalyticsEvent(google_analytics::Event(kEventCategoryAnalytics, kEventActionDisable),
                             std::move(callback));
  }

  inline static bool client_is_cleaned_up_ = false;
};

}  // namespace analytics::core_dev_tools

#endif  // SRC_LIB_ANALYTICS_CPP_CORE_DEV_TOOLS_ANALYTICS_H_
