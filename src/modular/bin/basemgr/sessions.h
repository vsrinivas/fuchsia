// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/sys/cpp/fidl.h>

#include <string>
#include <string_view>
#include <vector>

namespace modular::sessions {

// Returns a fully qualified session directory path for |session_id|.
std::string GetSessionDirectory(std::string_view session_id);

// Returns the session IDs encoded in all existing session directories.
std::vector<std::string> GetExistingSessionIds();

// Returns a randomly generated session ID.
std::string GetRandomSessionId();

// Returns a fixed, stable session ID.
std::string GetStableSessionId();

// Reports that a new session with the given |session_id| was created to Cobalt.
void ReportNewSessionToCobalt(std::string_view session_id);

// Erases all existing sessions that use the legacy random ID.
// The stable session is never deleted.
//
// TODO(fxbug.dev/51752): Remove once there are no sessions with random IDs in use
void DeleteSessionsWithRandomIds(fuchsia::sys::Environment* base_environment);

}  // namespace modular::sessions
