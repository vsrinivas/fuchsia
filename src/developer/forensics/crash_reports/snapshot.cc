// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/forensics/crash_reports/snapshot.h"

#include <lib/syslog/cpp/macros.h>

namespace forensics {
namespace crash_reports {

Snapshot::Archive::Archive(const fuchsia::feedback::Attachment& attachment)
    : key(attachment.key), value() {
  const auto& archive = attachment.value;
  if (!archive.vmo.is_valid()) {
    return;
  }

  value = SizedData(archive.size, 0u);
  if (const zx_status_t status =
          archive.vmo.read(value.data(), /*offset=*/0u, /*len=*/value.size());
      status != ZX_OK) {
    FX_PLOGS(ERROR, status) << "Failed to read vmo";
    return;
  }
}

}  // namespace crash_reports
}  // namespace forensics
