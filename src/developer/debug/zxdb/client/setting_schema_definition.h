// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

namespace zxdb {

// This header contains the definitions for all the settings used within the
// client. They are within their own namespace to avoid collision.
// Usage:
//
//  system.GetString(settings::kSymbolPaths)

class SettingSchema;

// This is the global declaration of the setting names, so that we have a symbol
// for each of them. The definition of these symbols are in the appropiate
// context: (System for system, Target for target, etc.).
struct ClientSettings {
  struct System {
    static const char* kDebugMode;
    static const char* kSymbolPaths;
    static const char* kPauseNewProcesses;
    static const char* kQuitAgentOnExit;
  };

  struct Job {
    static const char* kFilters;
  };

  struct Target {};

  struct Thread {};
};

// Schemas need to be initialized together because some schemas can add settings
// to other schemas. If we made it completely lazy, when the first thread is
// spun up, it could make new settings appear which is not what the user would
// expect.
void InitializeSchemas();

}  // namespace zxdb
