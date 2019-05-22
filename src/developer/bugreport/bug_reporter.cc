// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/bugreport/bug_reporter.h"

#include <fuchsia/feedback/cpp/fidl.h>
#include <lib/fsl/vmo/strings.h>
#include <stdio.h>
#include <zircon/errors.h>
#include <zircon/status.h>

#include "src/developer/bugreport/bug_report_schema.h"
#include "third_party/rapidjson/include/rapidjson/document.h"
#include "third_party/rapidjson/include/rapidjson/filewritestream.h"
#include "third_party/rapidjson/include/rapidjson/prettywriter.h"

namespace fuchsia {
namespace bugreport {
namespace {

void AddAnnotations(
    const std::vector<fuchsia::feedback::Annotation>& annotations,
    rapidjson::Document* document) {
  rapidjson::Value json_annotations(rapidjson::kObjectType);
  for (const auto& annotation : annotations) {
    json_annotations.AddMember(rapidjson::StringRef(annotation.key),
                               annotation.value, document->GetAllocator());
  }
  document->AddMember("annotations", json_annotations,
                      document->GetAllocator());
}

void AddAttachments(
    const std::vector<fuchsia::feedback::Attachment>& attachments,
    rapidjson::Document* document) {
  rapidjson::Value json_attachments(rapidjson::kObjectType);
  for (const auto& attachment : attachments) {
    // TODO(DX-1551): interpret the attachment value as a JSON object for
    // the "inspect" key.
    std::string value;
    if (!fsl::StringFromVmo(attachment.value, &value)) {
      fprintf(stderr, "Failed to parse attachment VMO as string for key %s\n",
              attachment.key.c_str());
      continue;
    }
    json_attachments.AddMember(rapidjson::StringRef(attachment.key), value,
                               document->GetAllocator());
  }
  document->AddMember("attachments", json_attachments,
                      document->GetAllocator());
}

bool MakeAndWriteJson(const fuchsia::feedback::Data& feedback_data,
                      FILE* out_file) {
  rapidjson::Document document;
  document.SetObject();
  AddAnnotations(feedback_data.annotations(), &document);
  AddAttachments(feedback_data.attachments(), &document);

  char buffer[65536];
  rapidjson::FileWriteStream output_stream(out_file, buffer, sizeof(buffer));
  rapidjson::PrettyWriter<rapidjson::FileWriteStream> writer(output_stream);
  document.Accept(writer);
  if (!writer.IsComplete()) {
    fprintf(stderr, "Failed to write JSON\n");
    return false;
  }

  return true;
}

}  // namespace

bool MakeBugReport(std::shared_ptr<::sys::ServiceDirectory> services,
                   const char* out_filename) {
  fuchsia::feedback::DataProviderSyncPtr feedback_data_provider;
  services->Connect(feedback_data_provider.NewRequest());

  fuchsia::feedback::DataProvider_GetData_Result result;
  const zx_status_t get_data_status = feedback_data_provider->GetData(&result);
  if (get_data_status != ZX_OK) {
    fprintf(stderr,
            "Failed to get data from fuchsia.feedback.DataProvider: %d (%s)\n",
            get_data_status, zx_status_get_string(get_data_status));
    return false;
  }

  if (result.is_err()) {
    fprintf(stderr,
            "fuchsia.feedback.DataProvider failed to get data: %d (%s) ",
            result.err(), zx_status_get_string(result.err()));
    return false;
  }

  if (out_filename) {
    FILE* out_file = fopen(out_filename, "w");
    if (!out_file) {
      fprintf(stderr, "Failed to open output file %s\n", out_filename);
      return false;
    }
    const bool success = MakeAndWriteJson(result.response().data, out_file);
    fclose(out_file);
    return success;
  } else {
    return MakeAndWriteJson(result.response().data, stdout);
  }
}

}  // namespace bugreport
}  // namespace fuchsia
