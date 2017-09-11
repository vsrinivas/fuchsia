// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_MOZART_LIB_SKIA_SKIA_FONT_LOADER_H_
#define APPS_MOZART_LIB_SKIA_SKIA_FONT_LOADER_H_

#include <functional>

#include "lib/fonts/fidl/font_provider.fidl.h"
#include "lib/fxl/macros.h"
#include "third_party/skia/include/core/SkTypeface.h"

namespace mozart {

// Loads fonts from a font provider.
class SkiaFontLoader {
 public:
  using FontCallback = std::function<void(sk_sp<SkTypeface>)>;

  SkiaFontLoader(fonts::FontProviderPtr font_provider);
  ~SkiaFontLoader();

  // Loads the requested font and invokes the callback when done.
  // If the request fails, the callback will receive a null typeface.
  void LoadFont(fonts::FontRequestPtr request, const FontCallback& callback);

  // Loads the default font and invokes the callback when done.
  // If the request fails, the callback will receive a null typeface.
  void LoadDefaultFont(const FontCallback& callback);

 private:
  fonts::FontProviderPtr font_provider_;

  FXL_DISALLOW_COPY_AND_ASSIGN(SkiaFontLoader);
};

}  // namespace mozart

#endif  // APPS_MOZART_LIB_SKIA_SKIA_FONT_LOADER_H_
