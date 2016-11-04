// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <mx/vmo.h>

#include <unordered_map>
#include <vector>

#include "apps/fonts/services/font_provider.fidl.h"
#include "lib/fidl/cpp/bindings/binding_set.h"
#include "lib/ftl/macros.h"

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

  // Map from font family name to vmo containing font data. Indices in
  // font_data_ point into font_vmos_.
  // TODO(kulakowski): We should be smarter than matching family
  // exactly.
  std::unordered_map<std::string, size_t> font_data_;
  std::vector<mx::vmo> font_vmos_;

  // VMO for a fallback font when font_data_ does not contain an exact
  // family match for a request.
  mx::vmo fallback_vmo_;

  FTL_DISALLOW_COPY_AND_ASSIGN(FontProviderImpl);
};

}  // namespace fonts
