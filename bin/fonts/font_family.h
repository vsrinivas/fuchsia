// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_FONTS_FONT_FAMILY_H_
#define GARNET_BIN_FONTS_FONT_FAMILY_H_

#include <vector>

#include <fonts/cpp/fidl.h>
#include "lib/fsl/vmo/sized_vmo.h"
#include "lib/fxl/macros.h"
#include "third_party/rapidjson/rapidjson/document.h"
#include "third_party/rapidjson/rapidjson/rapidjson.h"

namespace fonts {

class FontFamily {
 public:
  FontFamily();
  ~FontFamily();

  bool Load(const rapidjson::Document::ValueType& family);

  const std::string& name() const { return name_; }
  fsl::SizedVmo* GetFontData(const FontRequest& request);

  class Font {
   public:
    Font();
    Font(Font&& other);

    std::string asset;
    FontSlant slant;
    int weight;
    fsl::SizedVmo data;
  };

 private:
  std::string name_;
  std::vector<Font> fonts_;

  FXL_DISALLOW_COPY_AND_ASSIGN(FontFamily);
};

}  // namespace fonts

#endif  // GARNET_BIN_FONTS_FONT_FAMILY_H_
