// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <string>

#include "src/developer/debug/zxdb/client/setting_store.h"
#include "src/developer/debug/zxdb/client/setting_value.h"

namespace zxdb {

class Command;
class Err;
class OutputBuffer;

// Will err out if the setting is not found in the given store.
Err FormatSetting(const SettingStore& store, const std::string& setting_name,
                  OutputBuffer* out);

// Outputs the detailed information about a particular setting.
OutputBuffer FormatSettingValue(const StoredSetting&);

OutputBuffer FormatSettingStore(const SettingStore& store);

const char* SettingSchemaLevelToString(SettingSchema::Level);

}  // namespace zxdb
