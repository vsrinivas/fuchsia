// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/modular/bin/sessionmgr/storage/session_storage_xdr.h"

#include <lib/fidl/cpp/optional.h>

#include <src/modular/bin/sessionmgr/storage/annotation_xdr.h>

#include "src/lib/fsl/vmo/strings.h"
#include "src/modular/lib/base64url/base64url.h"

namespace modular {

// Serialization and deserialization of fuchsia::modular::internal::StoryData
// and fuchsia::modular::StoryInfo to and from JSON.

namespace {

std::string BytesFromBase64(const std::string& base64) {
  std::string decoded;

  if (!base64url::Base64UrlDecode(base64, &decoded)) {
    FXL_LOG(ERROR) << "Unable to decode from Base64";
    return "";
  }

  return decoded;
}

std::string BytesToBase64(const std::string& bytes) {
  return base64url::Base64UrlEncode({reinterpret_cast<const char*>(bytes.data()), bytes.size()});
}

void XdrBase64Encoding(XdrContext* const xdr, std::string* value) {
  static constexpr char kBytesTag[] = "bytes";
  switch (xdr->op()) {
    case XdrOp::FROM_JSON: {
      std::string base64;
      xdr->Field(kBytesTag, &base64);
      *value = BytesFromBase64(base64);
      break;
    }
    case XdrOp::TO_JSON: {
      std::string base64 = BytesToBase64(*value);
      xdr->Field(kBytesTag, &base64);
      break;
    }
  }
}

// Serialization and deserialization of fuchsia::modular::internal::StoryData
// and fuchsia::modular::StoryInfo to and from JSON.

void XdrStoryInfo2(XdrContext* const xdr, fuchsia::modular::StoryInfo2* const data) {
  xdr->Field("id", data->mutable_id());
  xdr->Field("last_focus_time", data->mutable_last_focus_time());
  xdr->Field("annotations", data->mutable_annotations(), XdrAnnotation);
}

void XdrStoryData_v5(XdrContext* const xdr, fuchsia::modular::internal::StoryData* const data) {
  if (!xdr->Version(5)) {
    return;
  }
  // NOTE(mesch): We reuse subsidiary filters of previous versions as long as we
  // can. Only when they change too we create new versions of them.
  xdr->Field("story_info", data->mutable_story_info(), XdrStoryInfo2);
  xdr->Field("story_name", data->mutable_story_name());
  xdr->Field("story_page_id", data->mutable_story_page_id(), XdrBase64Encoding);
}

}  // namespace

// clang-format off
XdrFilterType<fuchsia::modular::internal::StoryData> XdrStoryData[] = {
    XdrStoryData_v5,
    nullptr,
};
// clang-format on

}  // namespace modular
