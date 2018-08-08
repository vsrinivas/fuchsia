// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/fonts/font_provider_impl.h"

#include <string.h>

#include <zircon/status.h>
#include <zircon/syscalls.h>
#include <utility>

#include "lib/fxl/files/file.h"
#include "lib/fxl/logging.h"
#include "third_party/rapidjson/rapidjson/document.h"
#include "third_party/rapidjson/rapidjson/rapidjson.h"

namespace fonts {
namespace {

constexpr char kFontManifestPath[] = "/pkg/data/manifest.json";
constexpr char kVendorFontManifestPath[] =
    "/system/data/vendor/fonts/manifest.json";
constexpr char kFallback[] = "fallback";
constexpr char kFamilies[] = "families";

constexpr zx_rights_t kFontDataRights =
    ZX_RIGHTS_BASIC | ZX_RIGHT_READ | ZX_RIGHT_MAP;

}  // namespace

FontProviderImpl::FontProviderImpl() = default;

FontProviderImpl::~FontProviderImpl() = default;

bool FontProviderImpl::LoadFontsInternal(const char path[],
                                         bool fallback_required) {
  std::string json_data;
  if (!files::ReadFileToString(path, &json_data)) {
    FXL_LOG(ERROR) << "Failed to read font manifest from '" << path << "'.";
    return false;
  }

  rapidjson::Document document;
  document.Parse(json_data.data());
  if (document.HasParseError() || !document.IsObject()) {
    FXL_LOG(ERROR) << "Font manifest '" << path << "' was not vaild JSON.";
    return false;
  }

  const auto& fallback = document.FindMember(kFallback);
  if (fallback == document.MemberEnd() || !fallback->value.IsString()) {
    if (fallback_required) {
      FXL_LOG(ERROR) << "Font manifest '" << path
                     << "' did not contain a valid 'fallback' family.";
      return false;
    }
  } else {
    fallback_ = fallback->value.GetString();
  }

  const auto& families = document.FindMember(kFamilies);
  if (families == document.MemberEnd() || !families->value.IsArray()) {
    FXL_LOG(ERROR) << "Font manifest '" << path
                   << "' did not contain any families.";
    return false;
  }

  for (const auto& family : families->value.GetArray()) {
    auto parsed_family = std::make_unique<FontFamily>();
    if (!parsed_family->Load(family))
      return false;
    std::string name = parsed_family->name();
    families_.emplace(name, std::move(parsed_family));
  }

  if (families_.find(fallback_) == families_.end()) {
    FXL_LOG(ERROR) << "Font manifest did not contain '" << fallback_
                   << "', which is the fallback family.";
    return false;
  }

  return true;
}

bool FontProviderImpl::LoadFonts() {
  bool loaded_all = LoadFontsInternal(kFontManifestPath, true);
  if (!loaded_all)
    Reset();

  if (files::IsFile(kVendorFontManifestPath))
    loaded_all = LoadFontsInternal(kVendorFontManifestPath, false);

  if (!loaded_all)
    Reset();

  return loaded_all;
}

void FontProviderImpl::Reset() {
  fallback_.clear();
  families_.clear();
}

void FontProviderImpl::AddBinding(
    fidl::InterfaceRequest<fuchsia::fonts::FontProvider> request) {
  bindings_.AddBinding(this, std::move(request));
}

void FontProviderImpl::GetFont(fuchsia::fonts::FontRequest request,
                               GetFontCallback callback) {
  if (families_.empty()) {
    callback(nullptr);
    return;
  }

  auto it = families_.find(request.family);
  if (it == families_.end())
    it = families_.find(fallback_);

  if (it == families_.end()) {
    callback(nullptr);
    return;
  }

  fsl::SizedVmo* font_data = it->second->GetFontData(request);
  if (!font_data) {
    callback(nullptr);
    return;
  }

  fsl::SizedVmo duplicated_data;
  if (font_data->Duplicate(kFontDataRights, &duplicated_data) < 0) {
    callback(nullptr);
    return;
  }

  fuchsia::fonts::FontData data;
  data.buffer = std::move(duplicated_data).ToTransport();
  auto response = fuchsia::fonts::FontResponse::New();
  response->data = std::move(data);
  callback(std::move(response));
}

}  // namespace fonts
