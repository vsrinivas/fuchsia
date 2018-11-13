// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <string>

#include "garnet/bin/zxdb/client/setting_store.h"
#include "garnet/bin/zxdb/client/setting_value.h"

namespace zxdb {

class Command;
class Err;
class OutputBuffer;


// When formatting settings, the values will depend on the current context (what
// activated target, thread, jobs, etc.).
// For that, we pass in the command which holds the current context the user is
// using.
void FormatSettings(const Command& cmd, OutputBuffer* out);


// Will err out if the setting is not found in the current context.
Err FormatSetting(const Command& cmd, const std::string& setting_name,
                  OutputBuffer* out);
Err FormatSetting(const SettingStore& store, const std::string& setting_name,
                  OutputBuffer* out);

// Outputs the detailed information about a particular setting.
OutputBuffer FormatSettingValue(const StoredSetting&);

const char* SettingSchemaLevelToString(SettingSchema::Level);

}  // namespace zxdb
