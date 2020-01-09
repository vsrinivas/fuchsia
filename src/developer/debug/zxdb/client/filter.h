// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_ZXDB_CLIENT_FILTER_H_
#define SRC_DEVELOPER_DEBUG_ZXDB_CLIENT_FILTER_H_

#include <optional>

#include "src/developer/debug/zxdb/client/client_object.h"
#include "src/developer/debug/zxdb/client/job_context.h"
#include "src/developer/debug/zxdb/client/setting_store.h"
#include "src/lib/fxl/memory/weak_ptr.h"

namespace zxdb {

class SettingSchema;

class Filter : public ClientObject {
 public:
  explicit Filter(Session* session);
  virtual ~Filter();

  void SetPattern(const std::string& pattern);
  void SetJob(JobContext* job);

  const std::string& pattern() { return pattern_; }
  JobContext* job() { return job_ ? job_->get() : nullptr; }
  bool valid() { return !pattern_.empty() && (!job_ || job_->get()); }

  SettingStore& settings() { return settings_; }

  static fxl::RefPtr<SettingSchema> GetSchema();

 private:
  // Implements the SettingStore interface for the Filter (uses composition instead of inheritance
  // to keep the Filter API simpler).
  class Settings : public SettingStore {
   public:
    explicit Settings(Filter* filter);

   protected:
    virtual SettingValue GetStorageValue(const std::string& key) const override;
    virtual Err SetStorageValue(const std::string& key, SettingValue value) override;

   private:
    Filter* filter_;  // Object that owns us.
  };
  friend Settings;

  Settings settings_;

  std::string pattern_;

  // This exists in one of 3 states:
  //   1) Optional contains non-null pointer. That points to the job this applies to.
  //   2) Optional is a nullopt. This filter applies to all jobs.
  //   3) Optional contains a null pointer. This filter was meant to apply to a job that
  //      disappeared.
  std::optional<fxl::WeakPtr<JobContext>> job_;
};

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_CLIENT_FILTER_H_
