// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <string>

namespace zxdb {

class Err;
class OutputBuffer;
class SettingStore;
struct StoredSetting;

// Lists all the values for a store, highlighting any overriden values.
// Will error if the setting is not found.
Err FormatSettings(const SettingStore&, const std::string& setting_name,
                   OutputBuffer* out);

}  // namespace zxdb
