// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/moterm/moterm_view.h"

#include <unistd.h>

#include "lib/ui/input/cpp/formatting.h"
#include "garnet/bin/moterm/command.h"
#include "garnet/bin/moterm/key_util.h"
#include "garnet/bin/moterm/moterm_model.h"
#include "lib/fonts/fidl/font_provider.fidl.h"
#include "lib/fxl/logging.h"
#include "lib/fxl/strings/string_printf.h"
#include "lib/fxl/time/time_delta.h"
#include "lib/mtl/io/redirection.h"
#include "lib/mtl/tasks/message_loop.h"
#include "third_party/skia/include/core/SkPaint.h"

namespace moterm {

namespace {
constexpr fxl::TimeDelta kBlinkInterval = fxl::TimeDelta::FromMilliseconds(500);
}  // namespace

MotermView::MotermView(
    mozart::ViewManagerPtr view_manager,
    fidl::InterfaceRequest<mozart::ViewOwner> view_owner_request,
    app::ApplicationContext* context,
    History* history,
    const MotermParams& moterm_params)
    : SkiaView(std::move(view_manager),
               std::move(view_owner_request),
               "Moterm"),
      model_(MotermModel::Size(24, 80), this),
      context_(context),
      font_loader_(
          context_->ConnectToEnvironmentService<fonts::FontProvider>()),
      task_runner_(mtl::MessageLoop::GetCurrent()->task_runner()),
      history_(history),
      params_(moterm_params),
      weak_ptr_factory_(this) {
  FXL_DCHECK(context_);
  FXL_DCHECK(history_);

  auto font_request = fonts::FontRequest::New();
  font_request->family = "RobotoMono";
  font_loader_.LoadFont(
      std::move(font_request), [this](sk_sp<SkTypeface> typeface) {
        FXL_CHECK(typeface);  // TODO(jpoichet): Fail gracefully.
        regular_typeface_ = std::move(typeface);
        ComputeMetrics();
        StartCommand();
      });
}

MotermView::~MotermView() {}

void MotermView::ComputeMetrics() {
  if (!regular_typeface_)
    return;

  // TODO(vtl): This duplicates some code.
  SkPaint fg_paint;
  fg_paint.setTypeface(regular_typeface_);
  fg_paint.setTextSize(params_.font_size);
  // Figure out appropriate metrics.
  SkPaint::FontMetrics fm = {};
  fg_paint.getFontMetrics(&fm);
  ascent_ = static_cast<int>(ceilf(-fm.fAscent));
  line_height_ = ascent_ + static_cast<int>(ceilf(fm.fDescent + fm.fLeading));
  FXL_DCHECK(line_height_ > 0);
  // To figure out the advance width, measure an X. Better hope the font
  // is monospace.
  advance_width_ = static_cast<int>(ceilf(fg_paint.measureText("X", 1)));
  FXL_DCHECK(advance_width_ > 0);
}

void MotermView::StartCommand() {
  command_.reset(new Command());

  std::vector<std::string> command_to_run = params_.command;
  std::vector<mtl::StartupHandle> startup_handles;

  if (command_to_run.empty()) {
    shell_controller_ = std::make_unique<ShellController>(history_);
    command_to_run = shell_controller_->GetShellCommand();
    startup_handles = shell_controller_->GetStartupHandles();
    shell_controller_->Start();
  }

  bool success = command_->Start(command_to_run, std::move(startup_handles),
                                 [this](const void* bytes, size_t num_bytes) {
                                   OnDataReceived(bytes, num_bytes);
                                 },
                                 [this] { OnCommandTerminated(); });
  if (!success) {
    FXL_LOG(ERROR) << "Error starting command.";
    exit(1);
  }

  Blink(++blink_timer_id_);
  InvalidateScene();
}

void MotermView::Blink(uint64_t blink_timer_id) {
  if (blink_timer_id != blink_timer_id_)
    return;

  if (focused_) {
    fxl::TimeDelta delta = fxl::TimePoint::Now() - last_key_;
    if (delta > kBlinkInterval) {
      blink_on_ = !blink_on_;
      InvalidateScene();
    }
    task_runner_->PostDelayedTask(
        [ weak = weak_ptr_factory_.GetWeakPtr(), blink_timer_id ] {
          if (weak) {
            weak->Blink(blink_timer_id);
          }
        },
        kBlinkInterval);
  }
}

void MotermView::OnSceneInvalidated(
    scenic::PresentationInfoPtr presentation_info) {
  if (!regular_typeface_)
    return;

  SkCanvas* canvas = AcquireCanvas();
  if (canvas) {
    DrawContent(canvas);
    ReleaseAndSwapCanvas();
  }
}

void MotermView::OnPropertiesChanged(mozart::ViewPropertiesPtr old_properties) {
  ComputeMetrics();
  Resize();
}

void MotermView::Resize() {
  if (!has_logical_size() || !regular_typeface_)
    return;

  uint32_t columns = std::max(logical_size().width / advance_width_, 1.f);
  uint32_t rows = std::max(logical_size().height / line_height_, 1.f);
  MotermModel::Size current = model_.GetSize();
  if (current.columns != columns || current.rows != rows) {
    model_.SetSize(MotermModel::Size(rows, columns), false);
  }
  InvalidateScene();
}

void MotermView::DrawContent(SkCanvas* canvas) {
  canvas->clear(SK_ColorBLACK);

  SkPaint bg_paint;
  bg_paint.setStyle(SkPaint::kFill_Style);

  SkPaint fg_paint;
  fg_paint.setTypeface(regular_typeface_);
  fg_paint.setTextSize(params_.font_size);
  fg_paint.setTextEncoding(SkPaint::kUTF32_TextEncoding);

  MotermModel::Size size = model_.GetSize();
  int y = 0;
  for (unsigned i = 0; i < size.rows; i++, y += line_height_) {
    int x = 0;
    for (unsigned j = 0; j < size.columns; j++, x += advance_width_) {
      MotermModel::CharacterInfo ch =
          model_.GetCharacterInfoAt(MotermModel::Position(i, j));

      // Paint the background.
      bg_paint.setColor(SkColorSetRGB(ch.background_color.red,
                                      ch.background_color.green,
                                      ch.background_color.blue));
      canvas->drawRect(SkRect::MakeXYWH(x, y, advance_width_, line_height_),
                       bg_paint);

      // Paint the foreground.
      if (ch.code_point) {
        if (!(ch.attributes & MotermModel::kAttributesBlink) || blink_on_) {
          uint32_t flags = SkPaint::kAntiAlias_Flag;
          // TODO(jpoichet): Use real bold font
          if ((ch.attributes & MotermModel::kAttributesBold))
            flags |= SkPaint::kFakeBoldText_Flag;
          // TODO(jpoichet): Account for MotermModel::kAttributesUnderline
          // without using the deprecated flag SkPaint::kUnderlineText_Flag
          fg_paint.setFlags(flags);
          fg_paint.setColor(SkColorSetRGB(ch.foreground_color.red,
                                          ch.foreground_color.green,
                                          ch.foreground_color.blue));

          canvas->drawText(&ch.code_point, sizeof(ch.code_point), x,
                           y + ascent_, fg_paint);
        }
      }
    }
  }

  if (model_.GetCursorVisibility() && blink_on_) {
    // Draw the cursor.
    MotermModel::Position cursor_pos = model_.GetCursorPosition();
    // TODO(jpoichet): Vary how we draw the cursor, depending on if we're
    // focused and/or active.
    SkPaint caret_paint;
    caret_paint.setStyle(SkPaint::kFill_Style);
    if (!focused_) {
      caret_paint.setStyle(SkPaint::kStroke_Style);
      caret_paint.setStrokeWidth(2);
    }

    caret_paint.setARGB(64, 255, 255, 255);
    canvas->drawRect(SkRect::MakeXYWH(cursor_pos.column * advance_width_,
                                      cursor_pos.row * line_height_,
                                      advance_width_, line_height_),
                     caret_paint);
  }
}

void MotermView::ScheduleDraw(bool force) {
  if (!properties() ||
      (!model_state_changes_.IsDirty() && !force && !force_next_draw_)) {
    force_next_draw_ |= force;
    return;
  }

  force_next_draw_ = false;
  InvalidateScene();
}

void MotermView::OnResponse(const void* buf, size_t size) {
  SendData(buf, size);
}

void MotermView::OnSetKeypadMode(bool application_mode) {
  keypad_application_mode_ = application_mode;
}

bool MotermView::OnInputEvent(mozart::InputEventPtr event) {
  bool handled = false;
  if (event->is_keyboard()) {
    const mozart::KeyboardEventPtr& keyboard = event->get_keyboard();
    if (keyboard->phase == mozart::KeyboardEvent::Phase::PRESSED ||
        keyboard->phase == mozart::KeyboardEvent::Phase::REPEAT) {
      if (keyboard->code_point == '+' &&
          keyboard->modifiers & mozart::kModifierAlt) {
        params_.font_size++;
        ComputeMetrics();
        Resize();
      } else if (keyboard->code_point == '-' &&
                 keyboard->modifiers & mozart::kModifierAlt) {
        params_.font_size--;
        ComputeMetrics();
        Resize();
      }
      OnKeyPressed(std::move(event));
      handled = true;
    }
  } else if (event->is_focus()) {
    const mozart::FocusEventPtr& focus = event->get_focus();
    focused_ = focus->focused;
    blink_on_ = true;
    if (focused_) {
      Blink(++blink_timer_id_);
    } else {
      InvalidateScene();
    }
    handled = true;
  }
  return handled;
}

void MotermView::OnKeyPressed(mozart::InputEventPtr key_event) {
  last_key_ = fxl::TimePoint::Now();
  blink_on_ = true;

  std::string input_sequence =
      GetInputSequenceForKeyPressedEvent(*key_event, keypad_application_mode_);
  if (input_sequence.empty())
    return;

  SendData(input_sequence.data(), input_sequence.size());
}

void MotermView::SendData(const void* bytes, size_t num_bytes) {
  if (command_) {
    command_->SendData(bytes, num_bytes);
  }
}

void MotermView::OnDataReceived(const void* bytes, size_t num_bytes) {
  model_.ProcessInput(bytes, num_bytes, &model_state_changes_);
  ScheduleDraw(false);
}

void MotermView::OnCommandTerminated() {
  FXL_LOG(INFO) << "Command terminated.";
  if (shell_controller_) {
    shell_controller_->Terminate();
  }
  mtl::MessageLoop::GetCurrent()->PostQuitTask();
}

}  // namespace moterm
