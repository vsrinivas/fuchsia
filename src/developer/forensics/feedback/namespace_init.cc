// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/forensics/feedback/namespace_init.h"

#include <lib/syslog/cpp/macros.h>

#include "src/developer/forensics/feedback_data/system_log_recorder/encoding/production_encoding.h"
#include "src/developer/forensics/feedback_data/system_log_recorder/encoding/version.h"
#include "src/developer/forensics/feedback_data/system_log_recorder/reader.h"
#include "src/lib/files/file.h"
#include "src/lib/files/path.h"
#include "src/lib/fxl/strings/string_printf.h"

namespace forensics::feedback {
namespace {

void MoveFile(const std::string& from, const std::string& to) {
  // Bail if the file doesn't exist.
  if (!files::IsFile(from)) {
    return;
  }

  // Bail if the file can't be read.
  std::string content;
  if (!files::ReadFileToString(from, &content)) {
    FX_LOGS(ERROR) << "Failed to read file " << from;
    return;
  }

  // Copy the file content – we cannot move as the two files are under different namespaces.
  if (!files::WriteFile(to, content)) {
    FX_LOGS(ERROR) << "Failed to write file " << to;
    return;
  }

  // Delete the original file.
  if (!files::DeletePath(from, /*recursive=*/true)) {
    FX_LOGS(ERROR) << "Failed to delete " << from;
  }
}

}  // namespace

bool TestAndSetNotAFdr(const std::string& not_a_fdr_file) {
  if (files::IsFile(not_a_fdr_file)) {
    return true;
  }

  if (!files::WriteFile(not_a_fdr_file, "", 0u)) {
    FX_LOGS(ERROR) << "Failed to create " << not_a_fdr_file;
  }

  return false;
}

void MovePreviousRebootReason(const std::string& from, const std::string& legacy_from,
                              const std::string& to) {
  if (files::IsFile(from)) {
    MoveFile(from, to);
  } else {
    MoveFile(legacy_from, to);
  }
}

void CreatePreviousLogsFile(cobalt::Logger* cobalt, const std::string& dir,
                            const std::string& write_path) {
  // We read the set of /cache files into a single /tmp file.
  feedback_data::system_log_recorder::ProductionDecoder decoder;
  float compression_ratio;
  if (!feedback_data::system_log_recorder::Concatenate(dir, &decoder, write_path,
                                                       &compression_ratio)) {
    return;
  }
  FX_LOGS(INFO) << fxl::StringPrintf(
      "Found logs from previous boot cycle (compression ratio %.2f), available at %s\n",
      compression_ratio, write_path.c_str());

  cobalt->LogCount(feedback_data::system_log_recorder::ToCobalt(decoder.GetEncodingVersion()),
                   (uint64_t)(compression_ratio * 100));

  // Clean up the /cache files now that they have been concatenated into a single /tmp file.
  files::DeletePath(dir, /*recursive=*/true);
}

void MoveAndRecordBootId(const std::string& new_boot_id, const std::string& previous_boot_id_path,
                         const std::string& current_boot_id_path) {
  MoveFile(/*from=*/current_boot_id_path, /*to=*/previous_boot_id_path);
  files::WriteFile(current_boot_id_path, new_boot_id);
}

void MoveAndRecordBuildVersion(const std::string& current_build_version,
                               const std::string& previous_build_version_path,
                               const std::string& current_build_version_path) {
  MoveFile(/*from=*/current_build_version_path, /*to=*/previous_build_version_path);
  files::WriteFile(current_build_version_path, current_build_version);
}

}  // namespace forensics::feedback
