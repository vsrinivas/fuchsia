// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/feedback/bugreport/bug_reporter.h"

#include <fuchsia/feedback/cpp/fidl.h>
#include <stdio.h>
#include <zircon/errors.h>
#include <zircon/status.h>

namespace fuchsia {
namespace bugreport {

bool MakeBugReport(std::shared_ptr<::sys::ServiceDirectory> services, const char* out_filename) {
  fuchsia::feedback::DataProviderSyncPtr feedback_data_provider;
  services->Connect(feedback_data_provider.NewRequest());

  fuchsia::feedback::DataProvider_GetData_Result result;
  const zx_status_t get_data_status = feedback_data_provider->GetData(&result);
  if (get_data_status != ZX_OK) {
    fprintf(stderr, "Failed to get data from fuchsia.feedback.DataProvider: %d (%s)\n",
            get_data_status, zx_status_get_string(get_data_status));
    return false;
  }

  if (result.is_err()) {
    fprintf(stderr, "fuchsia.feedback.DataProvider failed to get data: %d (%s) ", result.err(),
            zx_status_get_string(result.err()));
    return false;
  }

  if (!result.response().data.has_attachment_bundle()) {
    fprintf(stderr, "Failed to get attachment bundle from fuchsia.feedback.DataProvider");
    return false;
  }

  const auto& attachment = result.response().data.attachment_bundle();
  auto data = std::make_unique<uint8_t[]>(attachment.value.size);
  if (zx_status_t status = attachment.value.vmo.read(data.get(), 0u, attachment.value.size);
      status != ZX_OK) {
    fprintf(stderr, "Failed to read VMO attachment from fuchsia.feedback.DataProvider");
    return false;
  }

  if (out_filename) {
    FILE* out_file = fopen(out_filename, "w");
    if (!out_file) {
      fprintf(stderr, "Failed to open output file %s\n", out_filename);
      return false;
    }
    fwrite(data.get(), 1, attachment.value.size, out_file);
    fclose(out_file);
    return true;
  } else {
    fwrite(data.get(), 1, attachment.value.size, stdout);
    return true;
  }
}

}  // namespace bugreport
}  // namespace fuchsia
