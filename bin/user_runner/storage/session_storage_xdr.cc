// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/user_runner/storage/session_storage_xdr.h"

#include "peridot/lib/base64url/base64url.h"

namespace modular {

// Serialization and deserialization of fuchsia::modular::internal::StoryData
// and fuchsia::modular::StoryInfo to and from JSON.

namespace {

fuchsia::ledger::PageId PageIdFromBase64(const std::string& base64) {
  // Both base64 libraries available to us require that we allocate an output
  // buffer large enough to decode any base64 string of the input length, which
  // for us it does not know contains padding since our target size is 16, so we
  // have to allocate an intermediate buffer. Hex would not require this but
  // results in a slightly larger transport size.

  std::string decoded;
  fuchsia::ledger::PageId page_id;

  if (base64url::Base64UrlDecode(base64, &decoded)) {
    size_t size;
    if (decoded.length() != page_id.id.count()) {
      FXL_LOG(ERROR) << "Unexpected page ID length for " << base64
                     << " (decodes to " << decoded.length() << " bytes; "
                     << page_id.id.count() << " expected)";
      size = std::min(decoded.length(), page_id.id.count());
      memset(page_id.id.mutable_data(), 0, page_id.id.count());
    } else {
      size = page_id.id.count();
    }

    memcpy(page_id.id.mutable_data(), decoded.data(), size);
  } else {
    FXL_LOG(ERROR) << "Unable to decode page ID " << base64;
  }

  return page_id;
}

std::string PageIdToBase64(const fuchsia::ledger::PageId& page_id) {
  return base64url::Base64UrlEncode(
      {reinterpret_cast<const char*>(page_id.id.data()), page_id.id.count()});
}

// Serialization and deserialization of fuchsia::modular::internal::StoryData
// and fuchsia::modular::StoryInfo to and from JSON. We have different versions
// for backwards compatibilty.
//
// Version 0: Before FIDL2 conversion. ExtraInfo fields are stored as "key"
// and "value", page ids are stored as vector.
void XdrStoryInfoExtraEntry_v0(
    XdrContext* const xdr, fuchsia::modular::StoryInfoExtraEntry* const data) {
  xdr->Field("key", &data->key);
  xdr->Field("value", &data->value);
}

void XdrStoryInfo_v0(XdrContext* const xdr,
                     fuchsia::modular::StoryInfo* const data) {
  xdr->Field("last_focus_time", &data->last_focus_time);
  xdr->Field("url", &data->url);
  xdr->Field("id", &data->id);
  xdr->Field("extra", &data->extra, XdrStoryInfoExtraEntry_v0);
}

void XdrStoryData_v0(XdrContext* const xdr,
                     fuchsia::modular::internal::StoryData* const data) {
  FXL_CHECK(xdr->op() == XdrOp::FROM_JSON)
      << "A back version is never used for writing.";
  data->story_page_id = fuchsia::ledger::PageId::New();

  xdr->Field("story_info", &data->story_info, XdrStoryInfo_v0);
  xdr->Field("story_page_id", &data->story_page_id->id);
}

// Version 1: During FIDL2 conversion. ExtraInfo fields are stored as "key"
// and "value", page ids are stored as base64 string.
void XdrStoryInfoExtraEntry_v1(
    XdrContext* const xdr, fuchsia::modular::StoryInfoExtraEntry* const data) {
  xdr->Field("key", &data->key);
  xdr->Field("value", &data->value);
}

void XdrStoryInfo_v1(XdrContext* const xdr,
                     fuchsia::modular::StoryInfo* const data) {
  xdr->Field("last_focus_time", &data->last_focus_time);
  xdr->Field("url", &data->url);
  xdr->Field("id", &data->id);
  xdr->Field("extra", &data->extra, XdrStoryInfoExtraEntry_v1);
}

void XdrStoryData_v1(XdrContext* const xdr,
                     fuchsia::modular::internal::StoryData* const data) {
  static constexpr char kStoryPageId[] = "story_page_id";
  xdr->Field("story_info", &data->story_info, XdrStoryInfo_v1);
  switch (xdr->op()) {
    case XdrOp::FROM_JSON: {
      std::string page_id;
      xdr->Field(kStoryPageId, &page_id);
      if (page_id.empty()) {
        data->story_page_id = nullptr;
      } else {
        data->story_page_id = fuchsia::ledger::PageId::New();
        *data->story_page_id = PageIdFromBase64(page_id);
      }
      break;
    }
    case XdrOp::TO_JSON: {
      std::string page_id;
      if (data->story_page_id) {
        page_id = PageIdToBase64(*data->story_page_id);
      }
      xdr->Field(kStoryPageId, &page_id);
      break;
    }
  }
}

// Version 2: After FIDL2 conversion was complete. ExtraInfo fields are stored
// as @k and @v, page ids are stored as array wrapped in a struct.
void XdrStoryInfoExtraEntry_v2(
    XdrContext* const xdr, fuchsia::modular::StoryInfoExtraEntry* const data) {
  xdr->Field("@k", &data->key);
  xdr->Field("@v", &data->value);
}

void XdrStoryInfo_v2(XdrContext* const xdr,
                     fuchsia::modular::StoryInfo* const data) {
  xdr->Field("last_focus_time", &data->last_focus_time);
  xdr->Field("url", &data->url);
  xdr->Field("id", &data->id);
  xdr->Field("extra", &data->extra, XdrStoryInfoExtraEntry_v2);
}

void XdrPageId_v2(XdrContext* const xdr, fuchsia::ledger::PageId* const data) {
  xdr->Field("id", &data->id);
}

void XdrStoryData_v2(XdrContext* const xdr,
                     fuchsia::modular::internal::StoryData* const data) {
  xdr->Field("story_info", &data->story_info, XdrStoryInfo_v2);
  xdr->Field("story_page_id", &data->story_page_id, XdrPageId_v2);
}

// Version 3: ExtraInfo fields are stored as @k and @v, page ids are stored as
// array, and we set an explicit @version field.
void XdrStoryData_v3(XdrContext* const xdr,
                     fuchsia::modular::internal::StoryData* const data) {
  if (!xdr->Version(3)) {
    return;
  }
  // NOTE(mesch): We reuse subsidiary filters of previous versions as long as we
  // can. Only when they change too we create new versions of them.
  xdr->Field("story_info", &data->story_info, XdrStoryInfo_v2);
  xdr->Field("story_page_id", &data->story_page_id, XdrPageId_v2);
}

// Version 4: Includes is_kind_of_proto_story field.
void XdrStoryData_v4(XdrContext* const xdr,
                     fuchsia::modular::internal::StoryData* const data) {
  if (!xdr->Version(4)) {
    return;
  }
  // NOTE(mesch): We reuse subsidiary filters of previous versions as long as we
  // can. Only when they change too we create new versions of them.
  xdr->Field("story_info", &data->story_info, XdrStoryInfo_v2);
  xdr->Field("story_page_id", &data->story_page_id, XdrPageId_v2);
}

// Version 5: Includes story_name field.
void XdrStoryData_v5(XdrContext* const xdr,
                     fuchsia::modular::internal::StoryData* const data) {
  if (!xdr->Version(5)) {
    return;
  }
  // NOTE(mesch): We reuse subsidiary filters of previous versions as long as we
  // can. Only when they change too we create new versions of them.
  xdr->Field("story_info", &data->story_info, XdrStoryInfo_v2);
  xdr->Field("story_name", &data->story_name);
  xdr->Field("story_page_id", &data->story_page_id, XdrPageId_v2);
}

void XdrStoryOptions_v1(XdrContext* const xdr,
                        fuchsia::modular::StoryOptions* const data) {
  xdr->Field("kind_of_proto_story", &data->kind_of_proto_story);
}

// Version 6: Includes story_options field.
void XdrStoryData_v6(XdrContext* const xdr,
                     fuchsia::modular::internal::StoryData* const data) {
  if (!xdr->Version(6)) {
    return;
  }
  // NOTE(mesch): We reuse subsidiary filters of previous versions as long as we
  // can. Only when they change too we create new versions of them.
  xdr->Field("story_info", &data->story_info, XdrStoryInfo_v2);
  xdr->Field("story_name", &data->story_name);
  xdr->Field("story_options", &data->story_options, XdrStoryOptions_v1);
  xdr->Field("story_page_id", &data->story_page_id, XdrPageId_v2);
}

}  // namespace

XdrFilterType<fuchsia::modular::internal::StoryData> XdrStoryData[] = {
    XdrStoryData_v6,  // No clang-format, please.
    XdrStoryData_v5,  // No clang-format, please.
    XdrStoryData_v4,  // No clang-format, please.
    XdrStoryData_v3,  // No clang-format, please.
    XdrStoryData_v2,  // No clang-format, please.
    XdrStoryData_v1,  // No clang-format, please.
    XdrStoryData_v0,  // No clang-format, please.
    nullptr,
};

}  // namespace modular
