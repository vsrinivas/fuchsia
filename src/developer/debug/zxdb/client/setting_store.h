// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_ZXDB_CLIENT_SETTING_STORE_H_
#define SRC_DEVELOPER_DEBUG_ZXDB_CLIENT_SETTING_STORE_H_

#include <map>
#include <memory>

#include "src/developer/debug/zxdb/client/setting_schema.h"
#include "src/developer/debug/zxdb/client/setting_store_observer.h"
#include "src/developer/debug/zxdb/client/setting_value.h"
#include "src/developer/debug/zxdb/common/err.h"
#include "src/lib/fxl/memory/ref_ptr.h"
#include "src/lib/fxl/observer_list.h"

namespace zxdb {

struct StoredSetting;

// SettingStore is in charge of maintaining a structured group of settings. settings are indexed by
// a unique key.
class SettingStore {
 public:
  SettingStore(fxl::RefPtr<SettingSchema> schema, SettingStore* fallback);

  SettingStore* fallback() const { return fallback_; }
  void set_fallback(SettingStore* fallback) { fallback_ = fallback; }

  fxl::RefPtr<SettingSchema> schema() const { return schema_; }

  void AddObserver(const std::string& setting_name, SettingStoreObserver*);
  void RemoveObserver(const std::string& setting_name, SettingStoreObserver*);

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

  // Returns null setting/value if key is not found.
  SettingValue GetValue(const std::string& key) const;

  bool HasSetting(const std::string& key) const;

  bool empty() const { return values_.empty(); }

  const char* name() const { return name_; }
  void set_name(const char* name) { name_ = name; }

 protected:
  std::map<std::string, fxl::ObserverList<SettingStoreObserver>>& observers() {
    return observer_map_;
  }

 private:
  template <typename T>
  Err SetSetting(const std::string& key, T t);

  void NotifySettingChanged(const std::string& setting_name) const;

  // Should always exist. All settings are validated against this.
  fxl::RefPtr<SettingSchema> schema_;

  // SettingStore this store lookup settings when it cannot find them locally. Can be null. If set,
  // must outlive |this|.
  SettingStore* fallback_;
  std::map<std::string, SettingValue> values_;

  std::map<std::string, fxl::ObserverList<SettingStoreObserver>> observer_map_;

  // Useful for debugging.
  const char* name_ = "<not-set>";

  FXL_DISALLOW_COPY_AND_ASSIGN(SettingStore);
};

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_CLIENT_SETTING_STORE_H_
