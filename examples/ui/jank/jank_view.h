// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_MOZART_EXAMPLES_JANK_JANK_VIEW_H_
#define APPS_MOZART_EXAMPLES_JANK_JANK_VIEW_H_

#include "apps/mozart/lib/skia/skia_font_loader.h"
#include "apps/mozart/lib/view_framework/skia_view.h"
#include "lib/ftl/macros.h"
#include "lib/ftl/time/time_point.h"
#include "third_party/skia/include/core/SkCanvas.h"
#include "third_party/skia/include/core/SkColor.h"
#include "third_party/skia/include/core/SkRect.h"
#include "third_party/skia/include/core/SkTypeface.h"

namespace examples {

class JankView : public mozart::SkiaView {
 public:
  JankView(mozart::ViewManagerPtr view_manager,
           fidl::InterfaceRequest<mozart::ViewOwner> view_owner_request,
           fonts::FontProviderPtr font_provider);
  ~JankView() override;

 private:
  enum class Action {
    kHang10,
    kStutter30,
    kCrash,
  };

  struct Button {
    const char* label;
    Action action;
  };

  static const Button kButtons[];

  // |BaseView|:
  void OnSceneInvalidated(
      scenic::PresentationInfoPtr presentation_info) override;
  bool OnInputEvent(mozart::InputEventPtr event) override;

  void DrawContent(SkCanvas* canvas);
  void DrawButton(SkCanvas* canvas, const char* label, const SkRect& bounds);
  void OnClick(const Button& button);

  mozart::SkiaFontLoader font_loader_;
  sk_sp<SkTypeface> typeface_;

  ftl::TimePoint stutter_end_time_;

  FTL_DISALLOW_COPY_AND_ASSIGN(JankView);
};

}  // namespace examples

#endif  // APPS_MOZART_EXAMPLES_JANK_JANK_VIEW_H_
