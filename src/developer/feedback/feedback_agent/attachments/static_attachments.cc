// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/feedback/feedback_agent/attachments/static_attachments.h"

#include <filesystem>
#include <optional>
#include <string>

#include "src/developer/feedback/feedback_agent/attachments/aliases.h"
#include "src/developer/feedback/feedback_agent/constants.h"
#include "src/developer/feedback/utils/rotating_file_set.h"
#include "src/lib/files/file.h"
#include "src/lib/files/path.h"
#include "src/lib/syslog/cpp/logger.h"

namespace feedback {
namespace {

std::optional<std::string> ReadStringFromFilepath(const std::string& filepath) {
  std::string content;
  if (!files::ReadFileToString(filepath, &content)) {
    return std::nullopt;
  }
  return content;
}

std::optional<AttachmentValue> ReadAttachmentValueFromFilepath(const AttachmentKey& key,
                                                               const std::string& filepath) {
  const auto value = ReadStringFromFilepath(filepath);
  if (!value.has_value()) {
    FX_LOGS(WARNING) << "Failed to build attachment " << key;
  }
  return value;
}

void CreatePreviousLogsFile() {
  // We read the set of /cache files into a single /tmp file.
  RotatingFileSetReader log_reader(kCurrentLogsFilePaths);
  if (log_reader.Concatenate(kPreviousLogsFilePath)) {
    FX_LOGS(INFO) << "Found logs from previous boot cycle, available at " << kPreviousLogsFilePath;

    // Clean up the /cache files now that they have been concatenated into a single /tmp file.
    for (const auto& file : kCurrentLogsFilePaths) {
      files::DeletePath(file, /*recursive=*/false);
    }
  } else {
    FX_LOGS(WARNING) << "No logs found from previous boot cycle";
  }
}

std::optional<AttachmentValue> BuildAttachmentValue(const AttachmentKey& key) {
  if (key == kAttachmentBuildSnapshot) {
    return ReadAttachmentValueFromFilepath(key, "/config/build-info/snapshot");
  } else if (key == kAttachmentLogSystemPrevious) {
    // If the single /tmp file for the logs from the previous boot cycle does not exist yet, we need
    // to create it by aggregating the content stored in the /cache files for the current boot
    // cycle that are still containing the content from the previous boot cycle.
    //
    // This assumes that the static attachments are fetched before any log persistence for the
    // current boot cycle as this would overwrite these /cache files with the content for the
    // current boot cycle.
    if (!std::filesystem::exists(kPreviousLogsFilePath)) {
      CreatePreviousLogsFile();
    }
    return ReadAttachmentValueFromFilepath(key, kPreviousLogsFilePath);
  }
  // There are non-static attachments in the allowlist that we just skip here.
  return std::nullopt;
}

}  // namespace

Attachments GetStaticAttachments(const AttachmentKeys& allowlist) {
  Attachments attachments;
  for (const auto& key : allowlist) {
    const auto value = BuildAttachmentValue(key);
    if (value.has_value()) {
      attachments[key] = value.value();
    }
  }
  return attachments;
}

}  // namespace feedback
