// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <string>

#include "garnet/bin/zxdb/client/setting_store.h"
#include "garnet/bin/zxdb/client/setting_value.h"

namespace zxdb {

class Err;
class OutputBuffer;

// Lists all the values for a store, highlighting any overriden values.
// Will error if the setting is not found.
Err FormatSettings(const SettingStore&, const std::string& setting_name,
                   OutputBuffer* out);

// Outputs the detailed information about a particular setting.
OutputBuffer FormatSetting(const StoredSetting&);

// Returns the pretty printable version of the value of a StoredSetting.
// Useful for feedback.
OutputBuffer FormatSettingValue(const StoredSetting&);

const char* SettingSchemaLevelToString(SettingSchema::Level);

}  // namespace zxdb
