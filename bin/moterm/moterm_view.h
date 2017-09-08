// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_MOTERM_MOTERM_VIEW_H_
#define APPS_MOTERM_MOTERM_VIEW_H_

#include "application/lib/app/application_context.h"
#include "application/services/application_environment.fidl.h"
#include "lib/ui/skia/skia_font_loader.h"
#include "lib/ui/view_framework/skia_view.h"
#include "garnet/bin/moterm/command.h"
#include "garnet/bin/moterm/history.h"
#include "garnet/bin/moterm/moterm_model.h"
#include "garnet/bin/moterm/moterm_params.h"
#include "garnet/bin/moterm/shell_controller.h"
#include "lib/ftl/macros.h"
#include "lib/ftl/memory/ref_ptr.h"
#include "lib/ftl/memory/weak_ptr.h"
#include "lib/ftl/tasks/task_runner.h"
#include "lib/ftl/time/time_point.h"
#include "third_party/skia/include/core/SkCanvas.h"
#include "third_party/skia/include/core/SkTypeface.h"

namespace moterm {
class MotermView : public mozart::SkiaView, public MotermModel::Delegate {
 public:
  MotermView(mozart::ViewManagerPtr view_manager,
             fidl::InterfaceRequest<mozart::ViewOwner> view_owner_request,
             app::ApplicationContext* context,
             History* history,
             const MotermParams& moterm_params);
  ~MotermView() override;

 private:
  // |BaseView|:
  void OnPropertiesChanged(mozart::ViewPropertiesPtr old_properties) override;
  void OnSceneInvalidated(
      scenic::PresentationInfoPtr presentation_info) override;
  bool OnInputEvent(mozart::InputEventPtr event) override;

  // |MotermModel::Delegate|:
  void OnResponse(const void* buf, size_t size) override;
  void OnSetKeypadMode(bool application_mode) override;

  void ScheduleDraw(bool force);
  void DrawContent(SkCanvas* canvas);
  void OnKeyPressed(mozart::InputEventPtr key_event);

  // stdin/stdout
  void OnDataReceived(const void* bytes, size_t num_bytes);
  void SendData(const void* bytes, size_t num_bytes);

  void ComputeMetrics();
  void StartCommand();
  void Blink(uint64_t blink_timer_id);
  void Resize();
  void OnCommandTerminated();

  // TODO(vtl): Consider the structure of this app. Do we really want the "view"
  // owning the model?
  // The terminal model.
  MotermModel model_;
  // State changes to the model since last draw.
  MotermModel::StateChanges model_state_changes_;
  std::unique_ptr<ShellController> shell_controller_;

  // If we skip drawing despite being forced to, we should force the next draw.
  bool force_next_draw_;

  app::ApplicationContext* context_;
  mozart::SkiaFontLoader font_loader_;
  sk_sp<SkTypeface> regular_typeface_;

  int ascent_;
  int line_height_;
  int advance_width_;
  // Keyboard state.
  bool keypad_application_mode_;

  ftl::RefPtr<ftl::TaskRunner> task_runner_;
  ftl::TimePoint last_key_;
  bool blink_on_ = true;
  uint64_t blink_timer_id_ = 0;
  bool focused_ = false;

  History* history_;

  MotermParams params_;
  std::unique_ptr<Command> command_;

  ftl::WeakPtrFactory<MotermView> weak_ptr_factory_;

  FTL_DISALLOW_COPY_AND_ASSIGN(MotermView);
};
}  // namespace moterm

#endif  // APPS_MOTERM_MOTERM_VIEW_H_
