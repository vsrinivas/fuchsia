// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "lib/fxl/memory/ref_ptr.h"

namespace zxdb {

// This header contains the definitions for all the settings used within the
// client. They are within their own namespace to avoid collision.
// Usage:
//
//  system.GetString(settings::kSymbolPaths)

class SettingSchema;

class ClientSettings {
 public:
  // System Settings.
  static const char* kSymbolPaths;

};

fxl::RefPtr<SettingSchema> CreateSystemSchema();

}  // namespace zxdb
