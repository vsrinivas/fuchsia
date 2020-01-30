// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FEEDBACK_CRASHPAD_AGENT_TESTS_CRASHPAD_DATABASE_GREMLIN_H_
#define SRC_DEVELOPER_FEEDBACK_CRASHPAD_AGENT_TESTS_CRASHPAD_DATABASE_GREMLIN_H_

#include <string>

#include "third_party/crashpad/util/misc/uuid.h"

namespace feedback {

// Manipulates Crashpad's crash report database to cause failures.
class CrashpadDatabaseGremlin {
 public:
  CrashpadDatabaseGremlin(const std::string& path);

  void BreakInitialize() const;
  void BreakPrepareNewCrashReport() const;
  void BreakFinishedWritingCrashReport() const;
  void BreakRecordUploadComplete() const;
  void BreakSkipReportUpload() const;
  void DeletePendingReport(const crashpad::UUID& uuid) const;

 private:
  std::string NewReportsPath() const;
  std::string PendingReportsPath() const;
  std::string CompletedReportsPath() const;
  std::string AttachmentsPath() const;

  const std::string path_;
};

}  // namespace feedback

#endif  // SRC_DEVELOPER_FEEDBACK_CRASHPAD_AGENT_TESTS_CRASHPAD_DATABASE_GREMLIN_H_
