// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/ui/skia/skia_font_loader.h"

#include <utility>

#include "lib/ui/skia/skia_vmo_data.h"
#include "third_party/skia/include/ports/SkFontMgr.h"

namespace mozart {

SkiaFontLoader::SkiaFontLoader(fonts::FontProviderPtr font_provider)
    : font_provider_(std::move(font_provider)) {}

SkiaFontLoader::~SkiaFontLoader() {}

void SkiaFontLoader::LoadFont(fonts::FontRequestPtr request,
                              const FontCallback& callback) {
  // TODO(jeffbrown): Handle errors in case the font provider itself dies.
  font_provider_->GetFont(
      std::move(request), [this, callback](fonts::FontResponsePtr response) {
        if (response) {
          sk_sp<SkData> font_data =
              MakeSkDataFromVMO(std::move(response->data->vmo));
          if (font_data) {
            callback(
                SkFontMgr::RefDefault()->makeFromData(std::move(font_data)));
            return;
          }
        }
        callback(nullptr);
      });
}

void SkiaFontLoader::LoadDefaultFont(const FontCallback& callback) {
  auto font_request = fonts::FontRequest::New();
  font_request->family = "Roboto";
  LoadFont(std::move(font_request), callback);
}

}  // namespace mozart
