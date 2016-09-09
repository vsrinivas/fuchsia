// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "mojo/services/ui/fonts/interfaces/font_provider.mojom.h"

#include "lib/mtl/handles/unique_handle.h"
#include "mojo/public/cpp/bindings/binding_set.h"

namespace fonts {

class FontProviderImpl : public mojo::FontProvider {
 public:
  FontProviderImpl();
  ~FontProviderImpl() override;

  // Return whether this function was able to successfully load the fonts from
  // persistent storage.
  bool LoadFonts();

  void AddBinding(mojo::InterfaceRequest<mojo::FontProvider> request);

 private:
  // |FontProvider| implementation:
  void GetFont(mojo::FontRequestPtr request,
               const GetFontCallback& callback) override;

  mojo::BindingSet<mojo::FontProvider> bindings_;

  // TODO(abarth): We should support more than one font.
  mtl::UniqueHandle roboto_regular_vmo_;

  MOJO_DISALLOW_COPY_AND_ASSIGN(FontProviderImpl);
};

}  // namespace fonts
