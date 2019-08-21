// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#include "src/developer/feedback/bugreport/bug_reporter.h"

#include <fuchsia/feedback/cpp/fidl.h>
#include <lib/fsl/vmo/strings.h>
#include <stdio.h>
#include <zircon/errors.h>
#include <zircon/status.h>

#include <vector>

#include "src/developer/feedback/bugreport/bug_report_schema.h"
#include "third_party/rapidjson/include/rapidjson/document.h"
#include "third_party/rapidjson/include/rapidjson/filewritestream.h"
#include "third_party/rapidjson/include/rapidjson/prettywriter.h"

namespace fuchsia {
namespace bugreport {
namespace {

rapidjson::Document MakeJson(const std::vector<fuchsia::feedback::Attachment>& attachments,
                             const std::set<std::string>& attachment_allowlist) {
  rapidjson::Document json;
  json.SetObject();
  for (const auto& attachment : attachments) {
    if (!attachment_allowlist.empty() &&
        attachment_allowlist.find(attachment.key) == attachment_allowlist.end()) {
      continue;
    }

    std::string value;
    if (!fsl::StringFromVmo(attachment.value, &value)) {
      fprintf(stderr, "Failed to parse attachment VMO as string for key %s\n",
              attachment.key.c_str());
      continue;
    }
    json.AddMember(rapidjson::StringRef(attachment.key), value, json.GetAllocator());
  }
  return json;
}

bool MakeAndWriteJson(const fuchsia::feedback::Data& feedback_data,
                      const std::set<std::string>& attachment_allowlist, FILE* out_file) {
  rapidjson::Document json = MakeJson(feedback_data.attachments(), attachment_allowlist);

  char buffer[65536];
  rapidjson::FileWriteStream output_stream(out_file, buffer, sizeof(buffer));
  rapidjson::PrettyWriter<rapidjson::FileWriteStream> writer(output_stream);
  json.Accept(writer);
  if (!writer.IsComplete()) {
    fprintf(stderr, "Failed to write JSON\n");
    return false;
  }

  return true;
}

}  // namespace

bool MakeBugReport(std::shared_ptr<::sys::ServiceDirectory> services,
                   const std::set<std::string>& attachment_allowlist, const char* out_filename) {
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

  if (out_filename) {
    FILE* out_file = fopen(out_filename, "w");
    if (!out_file) {
      fprintf(stderr, "Failed to open output file %s\n", out_filename);
      return false;
    }
    const bool success = MakeAndWriteJson(result.response().data, attachment_allowlist, out_file);
    fclose(out_file);
    return success;
  } else {
    return MakeAndWriteJson(result.response().data, attachment_allowlist, stdout);
  }
}

}  // namespace bugreport
}  // namespace fuchsia
