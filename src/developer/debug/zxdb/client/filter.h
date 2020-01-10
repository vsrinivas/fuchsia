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
  // This string "*" is used to indicate attaching to all processes.
  static const char kAllProcessesPattern[];

  explicit Filter(Session* session);
  virtual ~Filter();

  bool is_valid() const { return !pattern_.empty(); }

  // The pattern is a substring to match against launched processes in attached jobs. It is not a
  // glob or regular expression.
  //
  // When the pattern is empty, this filter is invalid. When it's |kAllProcessesPattern|, the
  // filter will match all processes.
  //
  // Note that the empty string behavior is different than the filter messages sent to the backend.
  void SetPattern(const std::string& pattern);
  const std::string& pattern() const { return pattern_; }

  // A null job matches all attached jobs. The System should automatically delete filters explicitly
  // associated with a job when the job is deleted which should prevent this pointer from dangling.
  void SetJob(JobContext* job);
  JobContext* job() const { return job_ ? job_->get() : nullptr; }

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
