// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <mx/vmo.h>

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

  fidl::BindingSet<FontProvider> bindings_;

  // TODO(abarth): We should support more than one font.
  mx::vmo roboto_regular_vmo_;

  FTL_DISALLOW_COPY_AND_ASSIGN(FontProviderImpl);
};

}  // namespace fonts
