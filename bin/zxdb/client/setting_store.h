// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <map>
#include <memory>

#include "garnet/bin/zxdb/common/err.h"
#include "garnet/bin/zxdb/client/setting_value.h"

#include "lib/fxl/memory/ref_ptr.h"

namespace zxdb {

class SettingSchema;

// SettingStore is in charge of maintaining a structured group of settings.
// settings are indexed by a unique "path". Paths are dot (.) separated paths
// that point to a particular settings (eg. "this.is.a.path").
//
// These paths creates a hierarchical structure that can then be queried and
// shown to users.
class SettingStore {
 public:
   SettingStore(fxl::RefPtr<SettingSchema> schema, SettingStore* fallback);

   bool GetBool(const std::string& key) const;
   int GetInt(const std::string& key) const;
   std::string GetString(const std::string& key) const;
   std::vector<std::string> GetList(const std::string& key) const;

   // Mainly used for user defined settings. Normally we know zxdb defined
   // setting types, so we can confidently used the type getters. But frontend
   // code might want to check for dynamically defined settings and check
   // their type.
   // Returns a null value if the key is not found.
   SettingValue GetSetting(const std::string& key) const;

   Err SetBool(const std::string& key, bool);
   Err SetInt(const std::string& key, int);
   Err SetString(const std::string& key, std::string);
   Err SetList(const std::string& key, std::vector<std::string> list);

  private:
   // Actual function that traverses the path and creates the intermediate
   // nodes. |add_value_fn| is called to add the correct value to the newly
   // created node. These functions will be provided by the public interface
   // which is the one the user cals (eg. AddBool, AddString, etc.).

   // Adding a setting if the same, only that the value differs. This will call
   // the correct overload for the setting value and store it is valid.
   template <typename T>
   Err SetSetting(const std::string& key, T t);

   // Should always exist. All settings are validated against this.
   fxl::RefPtr<SettingSchema> schema_;

   // SettingStore this store lookup settings when it cannot find them locally.
   // Can be null.
   SettingStore* fallback_;

   std::map<std::string, SettingValue> settings_;
};

}  // namespace zxdg
