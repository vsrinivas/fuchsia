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

  fuchsia::feedback::Bugreport bugreport;
  const zx_status_t get_data_status = feedback_data_provider->GetBugreport(
      std::move(fuchsia::feedback::GetBugreportParameters().set_collection_timeout_per_data(
          zx::min(5).get())),
      &bugreport);
  if (get_data_status != ZX_OK) {
    fprintf(stderr, "Failed to get data from fuchsia.feedback.DataProvider: %d (%s)\n",
            get_data_status, zx_status_get_string(get_data_status));
    return false;
  }

  if (!bugreport.has_bugreport()) {
    fprintf(stderr, "Failed to get bugreport from fuchsia.feedback.DataProvider");
    return false;
  }

  const auto size = bugreport.bugreport().value.size;
  auto data = std::make_unique<uint8_t[]>(bugreport.bugreport().value.size);
  if (const zx_status_t status = bugreport.bugreport().value.vmo.read(data.get(), 0u, size);
      status != ZX_OK) {
    fprintf(stderr, "Failed to read VMO bugreport from fuchsia.feedback.DataProvider");
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
