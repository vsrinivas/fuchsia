// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/forensics/bugreport/bug_reporter.h"

#include <fuchsia/feedback/cpp/fidl.h>
#include <lib/zx/time.h>
#include <zircon/errors.h>
#include <zircon/status.h>

#include <cstdio>

namespace forensics {
namespace bugreport {

bool MakeBugReport(std::shared_ptr<sys::ServiceDirectory> services, const char* out_filename) {
  fuchsia::feedback::DataProviderSyncPtr feedback_data_provider;
  services->Connect(feedback_data_provider.NewRequest());

  fuchsia::feedback::Snapshot snapshot;
  if (const zx_status_t status = feedback_data_provider->GetSnapshot(
          std::move(fuchsia::feedback::GetSnapshotParameters().set_collection_timeout_per_data(
              zx::min(5).get())),
          &snapshot);
      status != ZX_OK) {
    fprintf(stderr, "Failed to get data from fuchsia.feedback.DataProvider: %d (%s)\n", status,
            zx_status_get_string(status));
    return false;
  }

  if (!snapshot.has_archive()) {
    fprintf(stderr, "Failed to get snapshot from fuchsia.feedback.DataProvider");
    return false;
  }

  const auto size = snapshot.archive().value.size;
  auto data = std::make_unique<uint8_t[]>(snapshot.archive().value.size);
  if (const zx_status_t status = snapshot.archive().value.vmo.read(data.get(), 0u, size);
      status != ZX_OK) {
    fprintf(stderr, "Failed to read VMO archive from fuchsia.feedback.DataProvider");
    return false;
  }

  if (out_filename) {
    FILE* out_file = fopen(out_filename, "w");
    if (!out_file) {
      fprintf(stderr, "Failed to open output file %s\n", out_filename);
      return false;
    }
    fwrite(data.get(), 1, size, out_file);
    fclose(out_file);
    return true;
  } else {
    fwrite(data.get(), 1, size, stdout);
    return true;
  }
}

}  // namespace bugreport
}  // namespace forensics
