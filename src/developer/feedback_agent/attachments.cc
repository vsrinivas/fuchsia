// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/feedback_agent/attachments.h"

#include <string>
#include <vector>

#include <fuchsia/mem/cpp/fidl.h>
#include <lib/fsl/vmo/file.h>
#include <lib/fsl/vmo/sized_vmo.h>
#include <lib/syslog/cpp/logger.h>

namespace fuchsia {
namespace feedback {
namespace {

Attachment BuildAttachment(const std::string& key, fuchsia::mem::Buffer value) {
  Attachment attachment;
  attachment.key = key;
  attachment.value = std::move(value);
  return attachment;
}

std::optional<fuchsia::mem::Buffer> VmoFromFilename(
    const std::string& filename) {
  fsl::SizedVmo vmo;
  if (fsl::VmoFromFilename(filename, &vmo)) {
    return std::move(vmo).ToTransport();
  }
  return std::nullopt;
}

void PushBackIfValuePresent(const std::string& key,
                            std::optional<fuchsia::mem::Buffer> value,
                            std::vector<Attachment>* attachments) {
  if (value.has_value()) {
    attachments->push_back(BuildAttachment(key, std::move(value.value())));
  } else {
    FX_LOGS(WARNING) << "missing attachment " << key;
  }
}

}  // namespace

std::vector<Attachment> GetAttachments() {
  std::vector<Attachment> attachments;
  PushBackIfValuePresent("build.snapshot",
                         VmoFromFilename("/config/build-info/snapshot"),
                         &attachments);
  return attachments;
}

}  // namespace feedback
}  // namespace fuchsia
