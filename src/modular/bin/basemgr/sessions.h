// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/sys/cpp/fidl.h>

namespace modular::sessions {

// A fixed session ID that is used for all sessions.
constexpr char kSessionId[] = "0";

// The path containing persistent storage for a single session with a fixed ID.
//
// Note: This is named "USER_" for legacy reasons. SESSION_ may have been more
// appropriate but a change would require a data migration.
constexpr char kSessionDirectoryPath[] = "/data/modular/USER_0";

// Reports that a new session was created to Cobalt.
void ReportNewSessionToCobalt();

}  // namespace modular::sessions
