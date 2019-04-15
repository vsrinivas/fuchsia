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

OutputBuffer FormatSettingStore(const SettingStore& store);

// Outputs the detailed information about a particular setting.
OutputBuffer FormatSetting(const Setting&);

// Formats the setting to just show <name>:<value>.
OutputBuffer FormatSettingShort(const Setting&);

}  // namespace zxdb
