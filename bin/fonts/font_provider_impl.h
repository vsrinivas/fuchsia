// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_FONTS_FONT_PROVIDER_IMPL_H_
#define GARNET_BIN_FONTS_FONT_PROVIDER_IMPL_H_

#include <unordered_map>
#include <vector>

#include <fuchsia/fonts/cpp/fidl.h>
#include <lib/zx/vmo.h>

#include "garnet/bin/fonts/font_family.h"
#include "lib/fidl/cpp/binding_set.h"
#include "lib/fxl/macros.h"

namespace fonts {

class FontProviderImpl : public fuchsia::fonts::FontProvider {
 public:
  FontProviderImpl();
  ~FontProviderImpl() override;

  // Return whether this function was able to successfully load the fonts from
  // persistent storage.
  bool LoadFonts();

  void AddBinding(fidl::InterfaceRequest<fuchsia::fonts::FontProvider> request);

 private:
  // |fuchsia::fonts::FontProvider| implementation:
  void GetFont(fuchsia::fonts::FontRequest request,
               GetFontCallback callback) override;

  // Load fonts. Returns true if all were loaded.
  bool LoadFontsInternal(const char path[], bool fallback_required);

  // Discard all font data.
  void Reset();

  fidl::BindingSet<fuchsia::fonts::FontProvider> bindings_;
  std::string fallback_;
  std::unordered_map<std::string, std::unique_ptr<FontFamily>> families_;

  FXL_DISALLOW_COPY_AND_ASSIGN(FontProviderImpl);
};

}  // namespace fonts

#endif  // GARNET_BIN_FONTS_FONT_PROVIDER_IMPL_H_
