// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_ZXDB_CLIENT_SETTING_SCHEMA_DEFINITION_H_
#define SRC_DEVELOPER_DEBUG_ZXDB_CLIENT_SETTING_SCHEMA_DEFINITION_H_

#include <string>
#include <vector>

namespace zxdb {

// This header contains the definitions for all the settings used within the client. They are within
// their own namespace to avoid collision. Usage:
//
//  system.GetString(settings::kSymbolPaths)

class SettingSchema;

// This is the global declaration of the setting names, so that we have a symbol for each of them.
// The definition of these symbols are in the appropiate context: (System for system, Target for
// target, etc.).
//
// Settings that appear at multiple levels should be declared (with their help message) at the
// most specific level they're needed.
struct ClientSettings {
  struct System {
    static const char* kDebugMode;
    static const char* kSymbolPaths;
    static const char* kSymbolRepoPaths;
    static const char* kPauseOnLaunch;
    static const char* kPauseOnAttach;
    static const char* kQuitAgentOnExit;
    static const char* kShowFilePaths;
    static const char* kShowStdout;
    static const char* kSymbolServers;
    static const char* kSymbolCache;
  };

  struct Job {};

  struct Target {
    static const char* kStoreBacktraces;
    static const char* kBuildDirs;
    static const char* kBuildDirsDescription;  // Help for kBuildDirs.

    static const char* kVectorFormat;  // Possible values are the kVectorRegisterFormatStr_*.
    static const char* kVectorFormatDescription;  // Help for kBuildDirs.

    // Returns the possible options for kVectorFormat.
    static std::vector<std::string> GetVectorFormatOptions();
  };

  struct Thread {
    static const char* kDebugStepping;
    static const char* kDebugSteppingDescription;
  };
};

// Schemas need to be initialized together because some schemas can add settings to other schemas.
// If we made it completely lazy, when the first thread is spun up, it could make new settings
// appear which is not what the user would expect.
void InitializeSchemas();

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_CLIENT_SETTING_SCHEMA_DEFINITION_H_
