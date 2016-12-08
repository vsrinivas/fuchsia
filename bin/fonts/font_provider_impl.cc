// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/fonts/font_provider_impl.h"

#include <string.h>

#include <magenta/status.h>
#include <magenta/syscalls.h>
#include <utility>

#include "lib/ftl/files/file.h"
#include "lib/ftl/logging.h"
#include "third_party/rapidjson/rapidjson/document.h"
#include "third_party/rapidjson/rapidjson/rapidjson.h"

namespace fonts {
namespace {

constexpr char kFontManifestPath[] = "/system/data/fonts/manifest.json";
constexpr char kFallbackFontFamily[] = "Roboto";
constexpr mx_rights_t kFontDataRights =
    MX_RIGHT_DUPLICATE | MX_RIGHT_TRANSFER | MX_RIGHT_READ | MX_RIGHT_MAP;

void LogInvalidJson() {
  FTL_LOG(ERROR) << "Font manifest at '" << kFontManifestPath << "' is invalid."
                 << " Specifically, it must be an object, with keys from font"
                 << " family names to paths containing the corresponding"
                 << " font data.";
}

}  // namespace

FontProviderImpl::FontProviderImpl() = default;

FontProviderImpl::~FontProviderImpl() = default;

bool FontProviderImpl::LoadFontsInternal() {
  std::string json_data;
  if (!files::ReadFileToString(kFontManifestPath, &json_data)) {
    FTL_LOG(ERROR) << "Failed to read font manifest from '" << kFontManifestPath
                   << "'.";
    return false;
  }

  rapidjson::Document json;
  json.Parse(json_data.data());
  if (!json.IsObject()) {
    LogInvalidJson();
    return false;
  }

  mx::vmo fallback_vmo;
  for (const auto& entry : json.GetObject()) {
    // The iterator here is from string (family) to json type (path),
    // so we have to check the second type.
    const auto& family = entry.name.GetString();
    const auto& json_path = entry.value;
    if (!json_path.IsString()) {
      LogInvalidJson();
      return false;
    }
    auto path = json_path.GetString();

    std::string data;
    if (!files::ReadFileToString(path, &data)) {
      FTL_LOG(ERROR) << "Failed to read " << family << " font data from '"
                     << path << "'.";
      return false;
    }

    mx::vmo vmo;
    mx_status_t rv = mx::vmo::create(data.size(), 0, &vmo);
    if (rv != NO_ERROR) {
      FTL_LOG(ERROR) << "Failed to create VMO for " << family << ":"
                     << mx_status_get_string(rv);
      return false;
    }

    size_t actual = 0;
    rv = vmo.write(data.data(), 0, data.size(), &actual);
    if (rv < 0) {
      FTL_LOG(ERROR) << "Failed to write data to VMO for " << family << ":"
                     << mx_status_get_string(rv);
      return false;
    }

    if (!strcmp(family, kFallbackFontFamily)) {
      rv = vmo.duplicate(kFontDataRights, &fallback_vmo);
      if (rv != NO_ERROR) {
        FTL_LOG(ERROR) << "Failed to create fallback vmo."
                       << mx_status_get_string(rv);
        return false;
      }
    }

    font_vmos_.push_back(std::move(vmo));
    font_data_[family] = font_vmos_.size() - 1;
  }

  if (!fallback_vmo) {
    FTL_LOG(ERROR) << "Failed to find fallback family " << kFallbackFontFamily
                   << ".";
    return false;
  }

  fallback_vmo_ = std::move(fallback_vmo);
  return true;
}

bool FontProviderImpl::LoadFonts() {
  bool loaded_all = LoadFontsInternal();
  if (!loaded_all)
    Reset();
  return loaded_all;
}

void FontProviderImpl::Reset() {
  font_data_.clear();
  font_vmos_.clear();
  fallback_vmo_.reset();
}

void FontProviderImpl::AddBinding(
    fidl::InterfaceRequest<FontProvider> request) {
  bindings_.AddBinding(this, std::move(request));
}

void FontProviderImpl::GetFont(FontRequestPtr request,
                               const GetFontCallback& callback) {
  if (!fallback_vmo_) {
    callback(nullptr);
    return;
  }

  // TODO(kulakowski) Do something smarter than just lookup by family.
  mx::vmo* font_vmo = &fallback_vmo_;
  auto font_data = font_data_.find(request->family);
  if (font_data != font_data_.end()) {
    font_vmo = &font_vmos_[font_data->second];
  }

  auto data = FontData::New();
  if (font_vmo->duplicate(kFontDataRights, &data->vmo) < 0) {
    callback(nullptr);
    return;
  }

  auto response = FontResponse::New();
  response->data = std::move(data);
  callback(std::move(response));
}

}  // namespace fonts
