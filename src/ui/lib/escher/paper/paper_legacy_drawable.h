// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_LIB_ESCHER_PAPER_PAPER_LEGACY_DRAWABLE_H_
#define SRC_UI_LIB_ESCHER_PAPER_PAPER_LEGACY_DRAWABLE_H_

#include "src/ui/lib/escher/paper/paper_drawable.h"
#include "src/ui/lib/escher/scene/object.h"

namespace escher {

// Wrapper which allows PaperRenderer to draw legacy escher::Objects.
// NOTE: see PaperRenderer::DrawLegacyObject(), which spares clients the
// inconvenience of explicitly wrapping each object in a PaperLegacyDrawable.
class PaperLegacyDrawable : public PaperDrawable {
 public:
  PaperLegacyDrawable(Object object) : object_(std::move(object)) {}

  // |PaperDrawable|
  void DrawInScene(const PaperScene* scene,
                   PaperDrawCallFactory* draw_call_factory,
                   PaperTransformStack* transform_stack, Frame* frame,
                   PaperDrawableFlags flags) override;

 private:
  Object object_;
};

}  // namespace escher

#endif  // SRC_UI_LIB_ESCHER_PAPER_PAPER_LEGACY_DRAWABLE_H_
