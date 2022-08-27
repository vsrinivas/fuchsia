// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_ZXDB_CLIENT_FILTER_H_
#define SRC_DEVELOPER_DEBUG_ZXDB_CLIENT_FILTER_H_

#include <zircon/types.h>

#include <optional>
#include <string_view>

#include "src/developer/debug/ipc/records.h"
#include "src/developer/debug/zxdb/client/client_object.h"
#include "src/developer/debug/zxdb/client/setting_store.h"
#include "src/lib/fxl/memory/weak_ptr.h"

namespace zxdb {

class SettingSchema;

class Filter : public ClientObject {
 public:
  explicit Filter(Session* session);

  bool is_valid() const {
    return filter_.type != debug_ipc::Filter::Type::kUnset &&
           (filter_.job_koid != 0 || !filter_.pattern.empty());
  }

  void SetType(debug_ipc::Filter::Type type);
  debug_ipc::Filter::Type type() const { return filter_.type; }

  void SetPattern(const std::string& pattern);
  const std::string& pattern() const { return filter_.pattern; }

  void SetJobKoid(zx_koid_t job_koid);
  zx_koid_t job_koid() const { return filter_.job_koid; }

  // Accessing the underlying filter storage.
  const debug_ipc::Filter& filter() const { return filter_; }
  SettingStore& settings() { return settings_; }

  static fxl::RefPtr<SettingSchema> GetSchema();

 private:
  // Sync the filter to the debug_agent. Must be called when the filter changes.
  void Sync();

  // Implements the SettingStore interface for the Filter (uses composition instead of inheritance
  // to keep the Filter API simpler).
  class Settings : public SettingStore {
   public:
    explicit Settings(Filter* filter);

   protected:
    SettingValue GetStorageValue(const std::string& key) const override;
    Err SetStorageValue(const std::string& key, SettingValue value) override;

   private:
    Filter* filter_;  // Object that owns us.
  };

  Settings settings_;

  // The real filter.
  debug_ipc::Filter filter_;
};

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_CLIENT_FILTER_H_
