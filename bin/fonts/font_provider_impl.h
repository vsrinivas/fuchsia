// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_FONTS_FONT_PROVIDER_IMPL_H_
#define APPS_FONTS_FONT_PROVIDER_IMPL_H_

#include <mx/vmo.h>

#include <unordered_map>
#include <vector>

#include "garnet/bin/fonts/font_family.h"
#include "lib/fonts/fidl/font_provider.fidl.h"
#include "lib/fidl/cpp/bindings/binding_set.h"
#include "lib/fxl/macros.h"

namespace fonts {

class FontProviderImpl : public FontProvider {
 public:
  FontProviderImpl();
  ~FontProviderImpl() override;

  // Return whether this function was able to successfully load the fonts from
  // persistent storage.
  bool LoadFonts();

  void AddBinding(fidl::InterfaceRequest<FontProvider> request);

 private:
  // |FontProvider| implementation:
  void GetFont(FontRequestPtr request,
               const GetFontCallback& callback) override;

  // Load fonts. Returns true if all were loaded.
  bool LoadFontsInternal();

  // Discard all font data.
  void Reset();

  fidl::BindingSet<FontProvider> bindings_;
  std::string fallback_;
  std::unordered_map<std::string, std::unique_ptr<FontFamily>> families_;

  FXL_DISALLOW_COPY_AND_ASSIGN(FontProviderImpl);
};

}  // namespace fonts

#endif  // APPS_FONTS_FONT_PROVIDER_IMPL_H_
