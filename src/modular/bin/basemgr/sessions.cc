// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/modular/bin/basemgr/sessions.h"

#include <lib/syslog/cpp/macros.h>

#include "src/lib/files/directory.h"
#include "src/lib/files/path.h"
#include "src/modular/bin/basemgr/cobalt/cobalt.h"

namespace modular::sessions {

// The path containing a subdirectory for each session.
constexpr char kSessionDirectoryLocation[] = "/data/modular";

// A standard prefix used on every session directory.
// Note: This is named "USER_" for legacy reasons. SESSION_ may have been more
// appropriate but a change would require a data migration.
constexpr char kSessionDirectoryPrefix[] = "USER_";

// A fixed session ID that is used for new persistent sessions. This is possible
// as basemanager never creates more than a single persistent session per device.
constexpr char kStandardSessionId[] = "0";

std::string GetSessionDirectory(std::string_view session_id) {
  return std::string(kSessionDirectoryLocation)
      .append("/")
      .append(kSessionDirectoryPrefix)
      .append(session_id);
}

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

std::string GetRandomSessionId() {
  uint32_t random_number = 0;
  while (random_number == 0) {
    zx_cprng_draw(&random_number, sizeof random_number);
  }
  return std::to_string(random_number);
}

std::string GetStableSessionId() { return kStandardSessionId; }

void ReportNewSessionToCobalt(std::string_view session_id) {
  if (session_id != GetStableSessionId()) {
    FX_LOGS(INFO) << "Creating session using random ID.";
    ReportEvent(cobalt_registry::ModularLifetimeEventsMetricDimensionEventType::
                    CreateSessionNewEphemeralAccount);
    return;
  }

  auto existing_sessions = GetExistingSessionIds();

  auto stable_session_exists = std::find(existing_sessions.begin(), existing_sessions.end(),
                                         GetStableSessionId()) != existing_sessions.end();

  if (stable_session_exists) {
    if (existing_sessions.size() > 1) {
      FX_LOGS(ERROR) << "Creating session using existing account with fixed ID, but others exist. "
                     << "Non-stable sessions should be deleted.";
      ReportEvent(cobalt_registry::ModularLifetimeEventsMetricDimensionEventType::
                      CreateSessionUnverifiableFixedAccount);
    } else {
      FX_LOGS(INFO) << "Creating session using existing account with fixed ID.";
      ReportEvent(cobalt_registry::ModularLifetimeEventsMetricDimensionEventType::
                      CreateSessionExistingFixedAccount);
    }
  } else {
    FX_LOGS(INFO) << "Creating session using new persistent account with fixed ID.";
    ReportEvent(cobalt_registry::ModularLifetimeEventsMetricDimensionEventType::
                    CreateSessionNewPersistentAccount);
  }
}

// Deletes a session with the given ID, including its isolated storage and session directory.
// Does nothing if the Modular session directory for this session ID does not exist.
//
// |session_id| must not be the stable session ID.
//
// TODO(fxbug.dev/51752): Remove once there are no sessions with random IDs in use
void DeleteSession(std::string_view session_id, fuchsia::sys::Environment* const base_environment) {
  FX_DCHECK(session_id != modular::sessions::GetStableSessionId())
      << "The standard stable session cannot be deleted.";

  static constexpr char kSessionEnvironmentLabelPrefix[] = "session-";
  const auto label = std::string(kSessionEnvironmentLabelPrefix).append(session_id);

  auto session_dir = GetSessionDirectory(session_id);

  if (!files::IsDirectory(session_dir)) {
    FX_LOGS(WARNING) << "Modular session directory does not exist, not deleting session: "
                     << session_dir;
    return;
  }

  FX_LOGS(WARNING) << "DELETING LEGACY STABLE SESSION: " << label;

  // Erase the isolated storage for the session by creating an environment with
  // |delete_storage_on_death| and immediately killing it.
  {
    fuchsia::sys::EnvironmentPtr session_environment;
    fuchsia::sys::EnvironmentControllerPtr session_environment_controller;
    base_environment->CreateNestedEnvironment(
        session_environment.NewRequest(), session_environment_controller.NewRequest(), label,
        /*additional_services=*/nullptr,
        {.inherit_parent_services = true, .delete_storage_on_death = true});
    // The nested environment will be killed when the controller goes out of scope.
  }

  // Cleanup the entry under the modular sessions directory.
  if (auto is_dir_deleted = files::DeletePath(session_dir, /*recursive=*/true); !is_dir_deleted) {
    FX_LOGS(ERROR) << "Failed to delete Modular session directory: " << session_dir;
  }
}

void DeleteSessionsWithRandomIds(fuchsia::sys::Environment* const base_environment) {
  auto existing_sessions = GetExistingSessionIds();

  // Enumerate the sessions erasing the contents of all but the standard one.
  for (const auto& existing_session : existing_sessions) {
    if (existing_session != std::string_view(kStandardSessionId)) {
      DeleteSession(existing_session, base_environment);
    }
  }
}

}  // namespace modular::sessions
