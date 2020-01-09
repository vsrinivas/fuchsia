// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_ZXDB_CLIENT_SETTING_STORE_H_
#define SRC_DEVELOPER_DEBUG_ZXDB_CLIENT_SETTING_STORE_H_

#include <map>
#include <memory>

#include "src/developer/debug/zxdb/client/setting_schema.h"
#include "src/developer/debug/zxdb/client/setting_value.h"
#include "src/developer/debug/zxdb/common/err.h"
#include "src/lib/fxl/memory/ref_ptr.h"

namespace zxdb {

// SettingStore is an interface for setting and querying values by a string-based key. It has a
// schema that describes the names and types of values it supports.
//
// This is an interface that does not store any actual data. Objects with settings can implement
// it, or use MapSettingStore which is a simple map-based implementation.
//
// This class does not implement observers. There are setting observers on the MapSettingStore which
// most implementations use. The lack of observers here is to make implementing SettingStores easier
// so they don't have to worry about notifying every time anything changes. And some SettingStore
// implementations might have different semantics that makes having a per-setting observer list
// undesirable.
class SettingStore {
 public:
  explicit SettingStore(fxl::RefPtr<SettingSchema> schema);

  fxl::RefPtr<SettingSchema> schema() const { return schema_; }

  Err SetBool(const std::string& key, bool);
  Err SetInt(const std::string& key, int);
  Err SetString(const std::string& key, std::string);
  Err SetExecutionScope(const std::string& key, const ExecutionScope&);
  Err SetInputLocations(const std::string& key, std::vector<InputLocation>);
  Err SetList(const std::string& key, std::vector<std::string> list);

  bool GetBool(const std::string& key) const;
  int GetInt(const std::string& key) const;
  std::string GetString(const std::string& key) const;
  const ExecutionScope& GetExecutionScope(const std::string& key) const;
  const std::vector<InputLocation>& GetInputLocations(const std::string& key) const;
  std::vector<std::string> GetList(const std::string& key) const;

  // General get/set for settings.
  SettingValue GetValue(const std::string& key) const;
  Err SetValue(const std::string& key, SettingValue value);

 protected:
  // Implemented by the override of the SettingStore to actually get/set values to/from the backend.
  // The key (and new value for the setter) will have been validated against the schema prior to
  // these calls.
  //
  // The implementation for GetStorageValue() can return a null SettingValue to indicate not found,
  // in which case GetValue() will return the default value from the schema.
  virtual SettingValue GetStorageValue(const std::string& key) const = 0;
  virtual Err SetStorageValue(const std::string& key, SettingValue value) = 0;

 private:
  // Should always exist. All settings are validated against this.
  fxl::RefPtr<SettingSchema> schema_;
};

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_CLIENT_SETTING_STORE_H_
