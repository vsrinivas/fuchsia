// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/feedback/testing/fakes/data_provider.h"

#include <fuchsia/feedback/cpp/fidl.h>
#include <fuchsia/mem/cpp/fidl.h>

#include <memory>
#include <vector>

#include "src/developer/feedback/utils/archive.h"
#include "src/lib/fsl/vmo/file.h"
#include "src/lib/fsl/vmo/sized_vmo.h"
#include "src/lib/fsl/vmo/strings.h"
#include "src/lib/fxl/logging.h"
#include "src/lib/fxl/strings/string_printf.h"

namespace feedback {
namespace fakes {
namespace {

using namespace fuchsia::feedback;

std::string AnnotationsToJSON(const std::vector<Annotation>& annotations) {
  std::string json = "{\n";
  for (const auto& annotation : annotations) {
    json +=
        fxl::StringPrintf("\t\"%s\": \"%s\"\n", annotation.key.c_str(), annotation.value.c_str());
  }
  json += "}\n";
  return json;
}

std::vector<Annotation> CreateAnnotations() {
  return {
      Annotation{.key = "annotation_key_1", .value = "annotation_value_1"},
      Annotation{.key = "annotation_key_2", .value = "annotation_value_2"},
      Annotation{.key = "annotation_key_3", .value = "annotation_value_3"},
  };
}

Attachment CreateBundleAttachment() {
  std::vector<Attachment> attachments;

  fsl::SizedVmo vmo;

  FXL_CHECK(fsl::VmoFromString("attachment_value_1", &vmo)) << "Failed to create attachment vmo";
  attachments.emplace_back(Attachment{
      .key = "attachment_key_1",
      .value = std::move(vmo).ToTransport(),
  });

  FXL_CHECK(fsl::VmoFromString(AnnotationsToJSON(CreateAnnotations()), &vmo))
      << "Failed to create attachment vmo";
  attachments.emplace_back(Attachment{
      .key = "annotations.json",
      .value = std::move(vmo).ToTransport(),
  });

  Attachment bundle;
  bundle.key = "attachment_bundle.zip";
  Archive(attachments, &bundle.value);

  return bundle;
}

std::unique_ptr<Screenshot> LoadPngScreenshot() {
  fsl::SizedVmo image;
  FXL_CHECK(fsl::VmoFromFilename("/pkg/data/checkerboard_100.png", &image))
      << "Failed to create image vmo";

  const size_t image_dim_in_px = 100u;
  fuchsia::math::Size dimensions;
  dimensions.width = image_dim_in_px;
  dimensions.height = image_dim_in_px;

  std::unique_ptr<Screenshot> screenshot = Screenshot::New();
  screenshot->image = std::move(image).ToTransport();
  screenshot->dimensions_in_px = dimensions;

  return screenshot;
}

}  // namespace

void DataProvider::GetData(GetDataCallback callback) {
  Data data;
  data.set_annotations(CreateAnnotations()).set_attachment_bundle(CreateBundleAttachment());

  DataProvider_GetData_Response response(std::move(data));
  callback(DataProvider_GetData_Result::WithResponse(std::move(response)));
}

void DataProvider::GetScreenshot(ImageEncoding encoding, GetScreenshotCallback callback) {
  switch (encoding) {
    case ImageEncoding::PNG:
      callback(LoadPngScreenshot());
    default:
      callback(nullptr);
  }
}

}  // namespace fakes
}  // namespace feedback
