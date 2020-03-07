// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/feedback/feedback_agent/attachments/static_attachments.h"

#include <optional>
#include <string>

#include "src/developer/feedback/feedback_agent/attachments/aliases.h"
#include "src/developer/feedback/feedback_agent/constants.h"
#include "src/lib/files/file.h"
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

std::optional<AttachmentValue> BuildAttachmentValue(const AttachmentKey& key) {
  if (key == kAttachmentBuildSnapshot) {
    return ReadAttachmentValueFromFilepath(key, "/config/build-info/snapshot");
  } else if (key == kAttachmentLogSystemPrevious) {
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
