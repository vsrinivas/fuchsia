// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/modular/bin/basemgr/session_context_impl.h"

#include <dirent.h>
#include <fcntl.h>
#include <lib/syslog/cpp/macros.h>

#include "src/lib/files/directory.h"
#include "src/lib/files/file.h"
#include "src/lib/files/unique_fd.h"
#include "src/lib/fsl/io/fd.h"
#include "src/modular/bin/basemgr/cobalt/cobalt.h"
#include "src/modular/lib/common/async_holder.h"
#include "src/modular/lib/common/teardown.h"
#include "src/modular/lib/modular_config/modular_config_constants.h"

namespace modular {

using cobalt_registry::ModularLifetimeEventsMetricDimensionEventType;

namespace {

// The path containing a subdirectory for each session.
constexpr char kSessionDirectoryLocation[] = "/data/modular";

// A standard prefix used on every session directory.
// Note: This is named "USER_" for legacy reasons. SESSION_ may have been more
// appropriate but a change would require a data migration.
constexpr char kSessionDirectoryPrefix[] = "USER_";

// A fixed session ID that is used for new persistent sessions. This is possible
// as basemanager never creates more than a single persistent session per device.
constexpr char kStandardSessionId[] = "0";

// Returns a fully qualified session directory path for |session_id|.
std::string GetSessionDirectory(const std::string& session_id) {
  return std::string(kSessionDirectoryLocation) + "/" + kSessionDirectoryPrefix + session_id;
}

// Returns the session IDs encoded in all existing session directories.
std::vector<std::string> GetExistingSessionIds() {
  std::vector<std::string> dirs;
  if (!files::ReadDirContents(kSessionDirectoryLocation, &dirs)) {
    FX_LOGS(WARNING) << "Could not open session directory location.";
    return std::vector<std::string>();
  }
  std::vector<std::string> output;
  for (const auto& dir : dirs) {
    if (strncmp(dir.c_str(), kSessionDirectoryPrefix, strlen(kSessionDirectoryPrefix)) == 0) {
      auto session_id = dir.substr(strlen(kSessionDirectoryPrefix));
      FX_LOGS(INFO) << "Found existing directory for session " << session_id;
      output.push_back(session_id);
    }
  }
  return output;
}

// Returns a randomly generated session ID and reports the case to cobalt.
std::string GetRandomSessionId() {
  FX_LOGS(INFO) << "Creating session using random ID.";
  ReportEvent(ModularLifetimeEventsMetricDimensionEventType::CreateSessionNewEphemeralAccount);
  uint32_t random_number = 0;
  zx_cprng_draw(&random_number, sizeof random_number);
  return std::to_string(random_number);
}

// Returns a stable session ID, using an ID extracted from the first session
// directory on disk if possible, and a fixed ID if not. The selected case is
// reported to cobalt.
std::string GetStableSessionId() {
  // TODO(50300): Once a sufficiently small number of devices are using legacy
  // non-zero session IDs, remove support for sniffing an existing directory and
  // just always use zero.
  auto existing_sessions = GetExistingSessionIds();
  if (existing_sessions.empty()) {
    FX_LOGS(INFO) << "Creating session using new persistent account.";
    ReportEvent(ModularLifetimeEventsMetricDimensionEventType::CreateSessionNewPersistentAccount);
    return kStandardSessionId;
  }

  if (existing_sessions.size() == 1) {
    if (existing_sessions[0] == std::string(kStandardSessionId)) {
      FX_LOGS(INFO) << "Creating session using existing account with fixed ID.";
      ReportEvent(ModularLifetimeEventsMetricDimensionEventType::CreateSessionExistingFixedAccount);
    } else {
      FX_LOGS(INFO) << "Creating session using existing account with legacy non-fixed ID.";
      ReportEvent(
          ModularLifetimeEventsMetricDimensionEventType::CreateSessionExistingPersistentAccount);
    }
    return existing_sessions[0];
  }

  // Walk through all existing sessions looking for the standard session id, which we use if we
  // find it, and the lexicographically minimum session id which we use otherwise.
  std::string lowest_session = existing_sessions[0];
  for (const auto& existing_session : existing_sessions) {
    if (existing_session == std::string(kStandardSessionId)) {
      FX_LOGS(WARNING) << "Creating session using one of multiple existing accounts with fixed ID.";
      ReportEvent(
          ModularLifetimeEventsMetricDimensionEventType::CreateSessionUnverifiableFixedAccount);
      return existing_session;
    }
    if (existing_session < lowest_session) {
      lowest_session = existing_session;
    }
  }
  FX_LOGS(WARNING) << "Creating session by picking the lowest of " << existing_sessions.size()
                   << " existing directories. Fixed ID was not found.";
  ReportEvent(
      ModularLifetimeEventsMetricDimensionEventType::CreateSessionUnverifiablePersistentAccount);
  return lowest_session;
}

}  // namespace

SessionContextImpl::SessionContextImpl(
    fuchsia::sys::Launcher* const launcher, bool use_random_id,
    fuchsia::modular::AppConfig sessionmgr_config, fuchsia::modular::AppConfig session_shell_config,
    fuchsia::modular::AppConfig story_shell_config, bool use_session_shell_for_story_shell_factory,
    fuchsia::ui::views::ViewToken view_token, fuchsia::sys::ServiceListPtr additional_services,
    zx::channel config_handle, GetPresentationCallback get_presentation,
    OnSessionShutdownCallback on_session_shutdown)
    : session_context_binding_(this),
      get_presentation_(std::move(get_presentation)),
      on_session_shutdown_(std::move(on_session_shutdown)),
      weak_factory_(this) {
  FX_CHECK(get_presentation_);
  FX_CHECK(on_session_shutdown_);

  // 0. Generate the path to map '/data' for the sessionmgr we are starting
  std::string session_id = use_random_id ? GetRandomSessionId() : GetStableSessionId();
  auto data_origin = GetSessionDirectory(session_id);

  // 1. Create a PseudoDir containing startup.config. This directory will be
  // injected into sessionmgr's namespace and sessionmgr will read its
  // configurations from there.
  auto flat_namespace = MakeConfigNamespace(std::move(config_handle));

  // 2. Launch Sessionmgr in the current environment.
  sessionmgr_app_ = std::make_unique<AppClient<fuchsia::modular::Lifecycle>>(
      launcher, std::move(sessionmgr_config), data_origin, std::move(additional_services),
      std::move(flat_namespace));

  // 3. Initialize the Sessionmgr service.
  sessionmgr_app_->services().ConnectToService(sessionmgr_.NewRequest());
  sessionmgr_->Initialize(session_id, std::move(session_shell_config),
                          std::move(story_shell_config), use_session_shell_for_story_shell_factory,
                          session_context_binding_.NewBinding(), std::move(view_token));

  sessionmgr_app_->SetAppErrorHandler([weak_this = weak_factory_.GetWeakPtr()] {
    FX_LOGS(ERROR) << "Sessionmgr seems to have crashed unexpectedly. "
                   << "Calling on_session_shutdown_().";
    // This prevents us from receiving any further requests.
    weak_this->session_context_binding_.Unbind();

    // Shutdown(), which expects a graceful shutdown of sessionmgr, does not
    // apply here because sessionmgr crashed. Move |on_session_shutdown_| on to the stack before
    // invoking it, in case the |on_session_shutdown_| deletes |this|.
    auto on_session_shutdown = std::move(weak_this->on_session_shutdown_);
    on_session_shutdown(ShutDownReason::CRASHED);
    // Don't touch |this|.
  });
}

fuchsia::sys::FlatNamespacePtr SessionContextImpl::MakeConfigNamespace(zx::channel config_handle) {
  // Determine where basemgr is reading configs from
  std::string config_dir = modular_config::kOverriddenConfigDir;
  if (!files::IsDirectory(config_dir)) {
    config_dir = modular_config::kDefaultConfigDir;
    if (!files::IsDirectory(config_dir)) {
      return nullptr;
    }
  }
  // Clone basemgr's config directory.
  fbl::unique_fd dir(open(config_dir.c_str(), O_DIRECTORY | O_RDONLY));
  auto flat_namespace = fuchsia::sys::FlatNamespace::New();
  flat_namespace->paths.push_back(modular_config::kOverriddenConfigDir);
  flat_namespace->directories.push_back(std::move(config_handle));
  return flat_namespace;
}

// TODO(MF-120): Replace method in favor of letting sessionmgr launch base
// shell via SessionUserProvider.
void SessionContextImpl::Shutdown(ShutDownReason reason, fit::function<void()> callback) {
  shutdown_callbacks_.push_back(std::move(callback));
  if (shutdown_callbacks_.size() > 1) {
    FX_LOGS(INFO) << "fuchsia::modular::internal::SessionContext::Shutdown() "
                     "already called, queuing callback while shutdown is in progress.";
    return;
  }

  // This should prevent us from receiving any further requests.
  session_context_binding_.Unbind();

  sessionmgr_app_->Teardown(kSessionmgrTimeout, [weak_this = weak_factory_.GetWeakPtr(), reason] {
    // One of the callbacks might delete |SessionContextImpl|, so always guard against
    // WeakPtr<SessionContextImpl>.
    for (const auto& callback : weak_this->shutdown_callbacks_) {
      callback();
      if (!weak_this) {
        return;
      }
    }
    auto on_session_shutdown = std::move(weak_this->on_session_shutdown_);
    on_session_shutdown(reason);
  });
}

void SessionContextImpl::GetPresentation(
    fidl::InterfaceRequest<fuchsia::ui::policy::Presentation> request) {
  get_presentation_(std::move(request));
}

void SessionContextImpl::Logout() {
  Shutdown(ShutDownReason::LOGGED_OUT, [] {});
}

void SessionContextImpl::Restart() {
  Shutdown(ShutDownReason::CRASHED, [] {});
}

void SessionContextImpl::Shutdown() {
  Shutdown(ShutDownReason::CRASHED, [] {});
}

}  // namespace modular
