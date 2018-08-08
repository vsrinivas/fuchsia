// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/fonts/font_family.h"

#include <algorithm>

#include "lib/fsl/vmo/file.h"
#include "lib/fxl/logging.h"

namespace fonts {
namespace {

constexpr char kFamily[] = "family";
constexpr char kFonts[] = "fonts";
constexpr char kAsset[] = "asset";
constexpr char kSlant[] = "slant";
constexpr char kWeight[] = "weight";
constexpr char kItalic[] = "italic";
constexpr char kUpright[] = "upright";

struct Matcher {
  Matcher(fuchsia::fonts::FontSlant slant, int weight)
      : slant_(slant), weight_(weight) {}

  bool operator()(const FontFamily::Font& a, const FontFamily::Font& b) {
    if (a.slant != b.slant) {
      if (a.slant == slant_)
        return true;
      if (b.slant == slant_)
        return false;
    }

    return abs(a.weight - weight_) < abs(b.weight - weight_);
  }

 private:
  fuchsia::fonts::FontSlant slant_;
  int weight_;
};

}  // namespace

FontFamily::Font::Font()
    : slant(fuchsia::fonts::FontSlant::UPRIGHT), weight(400) {}

FontFamily::Font::Font(Font&& other)
    : asset(std::move(other.asset)),
      slant(std::move(other.slant)),
      weight(std::move(other.weight)),
      data(std::move(other.data)) {}

FontFamily::FontFamily() = default;

FontFamily::~FontFamily() = default;

bool FontFamily::Load(const rapidjson::Document::ValueType& family) {
  if (!family.IsObject()) {
    FXL_LOG(ERROR) << "Font manifest contained an invalid family.";
    return false;
  }

  const auto& name = family.FindMember(kFamily);
  if (name == family.MemberEnd() || !name->value.IsString()) {
    FXL_LOG(ERROR) << "Font manifest contained a family without a valid name.";
    return false;
  }

  name_ = name->value.GetString();
  const auto& fonts = family.FindMember(kFonts);
  if (fonts == family.MemberEnd() || !fonts->value.IsArray()) {
    FXL_LOG(ERROR) << "Font family '" << name_
                   << "' did not contain any fonts.";
    return false;
  }
  for (const auto& font : fonts->value.GetArray()) {
    if (!font.IsObject()) {
      FXL_LOG(ERROR) << "Font family '" << name_
                     << "' contained an invalid font.";
      return false;
    }
    Font record;

    const auto& asset = font.FindMember(kAsset);
    if (asset == font.MemberEnd() || !asset->value.IsString()) {
      FXL_LOG(ERROR) << "Font family '" << name_
                     << "' contained a font without a valid asset.";
      return false;
    }
    record.asset = asset->value.GetString();

    const auto& slant = font.FindMember(kSlant);
    if (slant != font.MemberEnd()) {
      if (!slant->value.IsString()) {
        FXL_LOG(ERROR) << "Font family '" << name_
                       << "' contained a font whose slant was not a string.";
        return false;
      }
      std::string slant_string = slant->value.GetString();
      if (slant_string == kItalic) {
        record.slant = fuchsia::fonts::FontSlant::ITALIC;
      } else if (slant_string != kUpright) {
        FXL_LOG(ERROR) << "Font family '" << name_
                       << "' contained a font with slant '" << slant_string
                       << "', which is not valid.";
        return false;
      }
    }

    const auto& weight = font.FindMember(kWeight);
    if (weight != font.MemberEnd()) {
      if (!weight->value.IsInt()) {
        FXL_LOG(ERROR) << "Font family '" << name_
                       << "' contained a font whose weight was not an integer.";
        return false;
      }
      record.weight = weight->value.GetInt();
    }

    fonts_.push_back(std::move(record));
  }

  return true;
}

fsl::SizedVmo* FontFamily::GetFontData(
    const fuchsia::fonts::FontRequest& request) {
  Matcher matcher(request.slant, request.weight);
  auto it = std::min_element(fonts_.begin(), fonts_.end(), matcher);
  if (it == fonts_.end())
    return nullptr;
  if (it->data)
    return &it->data;
  if (fsl::VmoFromFilename(it->asset, &it->data))
    return &it->data;
  return nullptr;
}

}  // namespace fonts
