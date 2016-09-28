// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/mozart/lib/view_framework/skia/skia_font_loader.h"

#include "apps/mozart/lib/view_framework/skia/skia_vmo_data.h"
#include "mojo/public/cpp/application/connect.h"
#include "third_party/skia/include/ports/SkFontMgr.h"

namespace mojo {
namespace ui {

SkiaFontLoader::SkiaFontLoader(mojo::ApplicationConnector* connector) {
  mojo::ConnectToService(connector, "mojo:fonts",
                         mojo::GetProxy(&font_provider_));
  font_provider_.set_connection_error_handler([this] { /* TODO */ });
}

SkiaFontLoader::~SkiaFontLoader() {}

void SkiaFontLoader::LoadFont(mojo::FontRequestPtr request,
                              const FontCallback& callback) {
  font_provider_->GetFont(
      std::move(request), [this, callback](mojo::FontResponsePtr response) {
        if (response) {
          sk_sp<SkData> font_data =
              mojo::ui::MakeSkDataFromVMO(response->data->vmo.get().value());
          if (font_data) {
            callback(sk_sp<SkTypeface>(
                SkFontMgr::RefDefault()->createFromData(font_data.get())));
            return;
          }
        }
        callback(nullptr);
      });
}

void SkiaFontLoader::LoadDefaultFont(const FontCallback& callback) {
  auto font_request = mojo::FontRequest::New();
  font_request->family = "Roboto";
  LoadFont(std::move(font_request), callback);
}

}  // namespace ui
}  // namespace mojo
