// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_ZXDB_CLIENT_MAP_SETTING_STORE_H_
#define SRC_DEVELOPER_DEBUG_ZXDB_CLIENT_MAP_SETTING_STORE_H_

#include <map>
#include <memory>

#include "src/developer/debug/zxdb/client/setting_store.h"
#include "src/developer/debug/zxdb/client/setting_store_observer.h"
#include "src/lib/fxl/observer_list.h"

namespace zxdb {

// An implementation of SettingStore that just stores the values in a map. This is used for
// standalone settings where there is no separate object backing the storage.
//
// This type of SettingStore also implements fallback for hierarchical settings. If a value is
// not explicitly set in the current store, it will recursively query fallback stores until a
// value is found.
class MapSettingStore : public SettingStore {
 public:
  explicit MapSettingStore(fxl::RefPtr<SettingSchema> schema, MapSettingStore* fallback = nullptr);

  SettingStore* fallback() const { return fallback_; }
  void set_fallback(MapSettingStore* fallback) { fallback_ = fallback; }

  void AddObserver(const std::string& setting_name, SettingStoreObserver*);
  void RemoveObserver(const std::string& setting_name, SettingStoreObserver*);

 protected:
  // SettingStore implementation.
  virtual SettingValue GetStorageValue(const std::string& key) const override;
  virtual Err SetStorageValue(const std::string& key, SettingValue value) override;

 private:
  void NotifySettingChanged(const std::string& setting_name) const;

  // SettingStore this store lookup settings when it cannot find them locally. Can be null. If set,
  // must outlive |this|.
  MapSettingStore* fallback_;
  std::map<std::string, SettingValue> values_;

  std::map<std::string, fxl::ObserverList<SettingStoreObserver>> observer_map_;

  FXL_DISALLOW_COPY_AND_ASSIGN(MapSettingStore);
};

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_CLIENT_MAP_SETTING_STORE_H_
