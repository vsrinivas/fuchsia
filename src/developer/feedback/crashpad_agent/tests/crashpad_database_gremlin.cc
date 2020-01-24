// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/feedback/crashpad_agent/tests/crashpad_database_gremlin.h"

#include "src/lib/files/directory.h"
#include "src/lib/files/file.h"
#include "src/lib/files/path.h"

namespace feedback {
namespace {

bool ReplaceDirectoryWithFile(const std::string& path) {
  constexpr char kFileData[] = "this is a file!";

  files::DeletePath(path, /*recursive=*/true);
  return files::WriteFile(path, kFileData, sizeof(kFileData));
}

bool DeleteFile(const std::string& path) { return files::DeletePath(path, /*recursive=*/false); }

std::string AddExtension(const std::string& prefix, const std::string& extension) {
  return prefix + "." + extension;
}

}  // namespace

CrashpadDatabaseGremlin::CrashpadDatabaseGremlin(const std::string& path) : path_(path) {}

void CrashpadDatabaseGremlin::BreakInitialize() const { ReplaceDirectoryWithFile(path_); }

void CrashpadDatabaseGremlin::BreakPrepareNewCrashReport() const {
  ReplaceDirectoryWithFile(NewReportsPath());
}

void CrashpadDatabaseGremlin::BreakFinishedWritingCrashReport() const {
  ReplaceDirectoryWithFile(PendingReportsPath());
}

void CrashpadDatabaseGremlin::BreakRecordUploadComplete() const {
  ReplaceDirectoryWithFile(CompletedReportsPath());
}

void CrashpadDatabaseGremlin::BreakSkipReportUpload() const {
  ReplaceDirectoryWithFile(CompletedReportsPath());
}

void CrashpadDatabaseGremlin::DeletePendingReport(const crashpad::UUID& uuid) const {
  const std::string minidump =
      AddExtension(files::JoinPath(PendingReportsPath(), uuid.ToString()), "dmp");
  const std::string metadata =
      AddExtension(files::JoinPath(PendingReportsPath(), uuid.ToString()), "meta");

  DeleteFile(minidump);
  DeleteFile(metadata);
  ReplaceDirectoryWithFile(AttachmentsPath());
}

std::string CrashpadDatabaseGremlin::NewReportsPath() const {
  return files::JoinPath(path_, "new");
}

std::string CrashpadDatabaseGremlin::PendingReportsPath() const {
  return files::JoinPath(path_, "pending");
}

std::string CrashpadDatabaseGremlin::CompletedReportsPath() const {
  return files::JoinPath(path_, "completed");
}

std::string CrashpadDatabaseGremlin::AttachmentsPath() const {
  return files::JoinPath(path_, "attachments");
}

}  // namespace feedback
