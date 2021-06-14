// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FORENSICS_FEEDBACK_MIGRATION_UTILS_LOG_H_
#define SRC_DEVELOPER_FORENSICS_FEEDBACK_MIGRATION_UTILS_LOG_H_

#include <set>
#include <string>

namespace forensics::feedback {

// Utility class for persisting whether a component's namespace has been migrated.
class MigrationLog {
 public:
  // Deserializes the file at |path| into a MigrationLog. A new file is created if noting exists at
  // |path|.
  //
  // Returns a null option if deserialization fails and the file at |path| should be deleted.
  static std::optional<MigrationLog> FromFile(std::string path);

  enum class Component { kLastReboot, kCrashReports, kFeedbackData };

  bool Contains(Component component) const;

  // Sets |component| as being migrated and persists the log.
  //
  // The in-memory log is updated in the event persisting fails.
  void Set(Component component);

 private:
  MigrationLog(std::string path, std::set<Component> migrated);

  std::string path_;
  std::set<Component> migrated_;
};

}  // namespace forensics::feedback

#endif  // SRC_DEVELOPER_FORENSICS_FEEDBACK_MIGRATION_UTILS_LOG_H_
