// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_MOZART_EXAMPLES_LIB_VIEW_FRAMEWORK_SKIA_SKIA_FONT_LOADER_H_
#define APPS_MOZART_EXAMPLES_LIB_VIEW_FRAMEWORK_SKIA_SKIA_FONT_LOADER_H_

#include <functional>

#include "lib/ftl/macros.h"
#include "mojo/public/interfaces/application/application_connector.mojom.h"
#include "mojo/services/ui/fonts/interfaces/font_provider.mojom.h"
#include "third_party/skia/include/core/SkTypeface.h"

namespace mojo {
namespace ui {

// Loads fonts from the system font provider.
class SkiaFontLoader {
 public:
  using FontCallback = std::function<void(sk_sp<SkTypeface>)>;

  SkiaFontLoader(mojo::ApplicationConnector* connector);
  ~SkiaFontLoader();

  // Loads the requested font and invokes the callback when done.
  // If the request fails, the callback will receive a null typeface.
  void LoadFont(mojo::FontRequestPtr request, const FontCallback& callback);

  // Loads the default font and invokes the callback when done.
  // If the request fails, the callback will receive a null typeface.
  void LoadDefaultFont(const FontCallback& callback);

 private:
  mojo::FontProviderPtr font_provider_;

  FTL_DISALLOW_COPY_AND_ASSIGN(SkiaFontLoader);
};

}  // namespace ui
}  // namespace mojo

#endif  // APPS_MOZART_EXAMPLES_LIB_VIEW_FRAMEWORK_SKIA_SKIA_FONT_LOADER_H_
