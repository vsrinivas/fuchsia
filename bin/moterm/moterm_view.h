// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_MOTERM_MOTERM_VIEW_H_
#define APPS_MOTERM_MOTERM_VIEW_H_

#include "apps/moterm/command.h"
#include "apps/moterm/moterm_model.h"
#include "apps/moterm/moterm_params.h"
#include "apps/moterm/moterm_params.h"
#include "apps/mozart/lib/skia/skia_font_loader.h"
#include "apps/mozart/lib/view_framework/base_view.h"
#include "apps/mozart/lib/view_framework/input_handler.h"
#include "lib/ftl/macros.h"
#include "third_party/skia/include/core/SkCanvas.h"
#include "third_party/skia/include/core/SkTypeface.h"

namespace moterm {
class MotermView : public mozart::BaseView,
                   public mozart::InputListener,
                   public MotermModel::Delegate {
 public:
  MotermView(mozart::ViewManagerPtr view_manager,
             fidl::InterfaceRequest<mozart::ViewOwner> view_owner_request,
             fonts::FontProviderPtr font_provider,
             const MotermParams& moterm_params);
  ~MotermView() override;

 private:
  // |BaseView|:
  void OnDraw() override;
  void OnPropertiesChanged(mozart::ViewPropertiesPtr old_properties) override;

  // |InputListener|:
  void OnEvent(mozart::EventPtr event,
               const OnEventCallback& callback) override;
  // |MotermModel::Delegate|:
  void OnResponse(const void* buf, size_t size) override;
  void OnSetKeypadMode(bool application_mode) override;

  void ScheduleDraw(bool force);
  void DrawContent(SkCanvas* canvas, const mozart::Size& size);
  void OnKeyPressed(mozart::EventPtr key_event);

  // stdin/stdout
  void OnDataReceived(const void* bytes, size_t num_bytes);
  void SendData(const void* bytes, size_t num_bytes);

  mozart::InputHandler input_handler_;

  // TODO(vtl): Consider the structure of this app. Do we really want the "view"
  // owning the model?
  // The terminal model.
  MotermModel model_;
  // State changes to the model since last draw.
  MotermModel::StateChanges model_state_changes_;

  // If we skip drawing despite being forced to, we should force the next draw.
  bool force_next_draw_;

  mozart::SkiaFontLoader font_loader_;
  sk_sp<SkTypeface> regular_typeface_;

  int ascent_;
  int line_height_;
  int advance_width_;
  // Keyboard state.
  bool keypad_application_mode_;

  MotermParams params_;
  std::unique_ptr<Command> command_;

  FTL_DISALLOW_COPY_AND_ASSIGN(MotermView);
};
}

#endif  // APPS_MOTERM_MOTERM_VIEW_H_
