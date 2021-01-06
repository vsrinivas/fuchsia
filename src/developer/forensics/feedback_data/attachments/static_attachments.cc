// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/forensics/feedback_data/attachments/static_attachments.h"

#include <lib/syslog/cpp/macros.h>

#include <filesystem>
#include <string>

#include "src/developer/forensics/feedback_data/attachments/types.h"
#include "src/developer/forensics/feedback_data/constants.h"
#include "src/lib/files/file.h"
#include "src/lib/files/path.h"
#include "src/lib/fxl/strings/string_printf.h"

namespace forensics {
namespace feedback_data {
namespace {

const std::set<AttachmentKey> kStaticAttachmentKeys = {
    kAttachmentBuildSnapshot,
    kAttachmentLogSystemPrevious,
};

AttachmentValue ReadStringFromFilepath(const std::string& filepath) {
  std::string content;
  if (!files::ReadFileToString(filepath, &content)) {
    return AttachmentValue(Error::kFileReadFailure);
  }

  if (content.empty()) {
    return AttachmentValue(Error::kMissingValue);
  }

  return AttachmentValue(content);
}

AttachmentValue ReadAttachmentValueFromFilepath(const AttachmentKey& key,
                                                const std::string& filepath) {
  const auto value = ReadStringFromFilepath(filepath);
  if (!value.HasValue()) {
    FX_LOGS(WARNING) << "Failed to build attachment " << key;
  }
  return value;
}

AttachmentValue BuildAttachmentValue(const AttachmentKey& key) {
  if (key == kAttachmentBuildSnapshot) {
    return ReadAttachmentValueFromFilepath(key, "/config/build-info/snapshot");
  } else if (key == kAttachmentLogSystemPrevious) {
    return ReadAttachmentValueFromFilepath(key, kPreviousLogsFilePath);
  }
  // There are non-static attachments in the allowlist that we just skip here.
  FX_LOGS(FATAL) << "Invalid attachment key used: " << key;
  return AttachmentValue(Error::kNotSet);
}

AttachmentKeys RestrictAllowlist(const AttachmentKeys& allowlist) {
  AttachmentKeys intersection;
  std::set_intersection(allowlist.begin(), allowlist.end(), kStaticAttachmentKeys.begin(),
                        kStaticAttachmentKeys.end(),
                        std::inserter(intersection, intersection.begin()));

  return intersection;
}

}  // namespace

Attachments GetStaticAttachments(const AttachmentKeys& allowlist) {
  Attachments attachments;
  for (const auto& key : RestrictAllowlist(allowlist)) {
    attachments.insert({key, BuildAttachmentValue(key)});
  }
  return attachments;
}

}  // namespace feedback_data
}  // namespace forensics
