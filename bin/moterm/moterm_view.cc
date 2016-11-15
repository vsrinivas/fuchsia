// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/moterm/moterm_view.h"

#include <unistd.h>

#include "apps/fonts/services/font_provider.fidl.h"
#include "apps/moterm/command.h"
#include "apps/moterm/key_util.h"
#include "apps/moterm/moterm_model.h"
#include "apps/mozart/lib/skia/skia_vmo_surface.h"
#include "apps/mozart/services/input/cpp/formatting.h"
#include "lib/ftl/logging.h"
#include "lib/ftl/strings/string_printf.h"
#include "lib/ftl/time/time_delta.h"
#include "lib/mtl/tasks/message_loop.h"
#include "third_party/skia/include/core/SkPaint.h"

namespace moterm {

namespace {
constexpr uint32_t kContentImageResourceId = 1;
constexpr uint32_t kRootNodeId = mozart::kSceneRootNodeId;
constexpr ftl::TimeDelta kBlinkInterval = ftl::TimeDelta::FromMilliseconds(500);
}  // namespace

MotermView::MotermView(
    mozart::ViewManagerPtr view_manager,
    fidl::InterfaceRequest<mozart::ViewOwner> view_owner_request,
    modular::ApplicationContext* context,
    const MotermParams& moterm_params)
    : BaseView(std::move(view_manager),
               std::move(view_owner_request),
               "Moterm"),
      input_handler_(GetViewServiceProvider(), this),
      model_(MotermModel::Size(24, 80), this),
      context_(context),
      font_loader_(
          context_->ConnectToEnvironmentService<fonts::FontProvider>()),
      weak_ptr_factory_(this),
      task_runner_(mtl::MessageLoop::GetCurrent()->task_runner()),
      params_(moterm_params) {
  FTL_CHECK(!params_.command.empty());
  FTL_DCHECK(context_);

  auto font_request = fonts::FontRequest::New();
  font_request->family = "RobotoMono";
  font_loader_.LoadFont(std::move(font_request), [this](sk_sp<SkTypeface>
                                                            typeface) {
    FTL_CHECK(typeface);  // TODO(jpoichet): Fail gracefully.
    regular_typeface_ = std::move(typeface);

    // TODO(vtl): This duplicates some code.
    SkPaint fg_paint;
    fg_paint.setTypeface(regular_typeface_);
    fg_paint.setTextSize(params_.font_size);
    // Figure out appropriate metrics.
    SkPaint::FontMetrics fm = {};
    fg_paint.getFontMetrics(&fm);
    ascent_ = static_cast<int>(ceilf(-fm.fAscent));
    line_height_ = ascent_ + static_cast<int>(ceilf(fm.fDescent + fm.fLeading));
    FTL_DCHECK(line_height_ > 0);
    // To figure out the advance width, measure an X. Better hope the font
    // is monospace.
    advance_width_ = static_cast<int>(ceilf(fg_paint.measureText("X", 1)));
    FTL_DCHECK(advance_width_ > 0);

    const char* argv[]{params_.command.c_str(),
                       reinterpret_cast<const char*>(NULL)};
    command_.reset(new Command([this](const void* bytes, size_t num_bytes) {
      OnDataReceived(bytes, num_bytes);
    }));

    fidl::InterfaceHandle<modular::ApplicationEnvironment> child_environment;
    context_->environment()->Duplicate(fidl::GetProxy(&child_environment));
    command_->Start(params_.command.c_str(), 0, argv,
                    std::move(child_environment));
    Blink();
    Invalidate();
  });
}

MotermView::~MotermView() {}

void MotermView::Blink() {
  ftl::TimeDelta delta = ftl::TimePoint::Now() - last_key_;
  if (delta > kBlinkInterval) {
    blink_on_ = !blink_on_;
    Invalidate();
  }

  task_runner_->PostDelayedTask(
      [weak = weak_ptr_factory_.GetWeakPtr()] {
        if (weak) {
          weak->Blink();
        }
      },
      kBlinkInterval);
}

// |BaseView|:
void MotermView::OnDraw() {
  FTL_DCHECK(properties());

  auto update = mozart::SceneUpdate::New();

  const mozart::Size& size = *properties()->view_layout->size;
  if (size.width > 0 && size.height > 0) {
    mozart::RectF bounds;
    bounds.width = size.width;
    bounds.height = size.height;

    // Draw the contents of the scene to a surface.
    mozart::ImagePtr image;
    sk_sp<SkSurface> surface =
        mozart::MakeSkSurface(size, &buffer_producer_, &image);
    FTL_CHECK(surface);
    DrawContent(surface->getCanvas(), size);

    // Update the scene contents.
    auto content_resource = mozart::Resource::New();
    content_resource->set_image(mozart::ImageResource::New());
    content_resource->get_image()->image = std::move(image);
    update->resources.insert(kContentImageResourceId,
                             std::move(content_resource));

    auto root_node = mozart::Node::New();
    root_node->hit_test_behavior = mozart::HitTestBehavior::New();
    root_node->op = mozart::NodeOp::New();
    root_node->op->set_image(mozart::ImageNodeOp::New());
    root_node->op->get_image()->content_rect = bounds.Clone();
    root_node->op->get_image()->image_resource_id = kContentImageResourceId;
    update->nodes.insert(kRootNodeId, std::move(root_node));
  } else {
    update->nodes.insert(kRootNodeId, mozart::Node::New());
  }

  // Publish the updated scene contents.
  scene()->Update(std::move(update));
  scene()->Publish(CreateSceneMetadata());
}

// |BaseView|:
void MotermView::OnPropertiesChanged(mozart::ViewPropertiesPtr old_properties) {
  // Compute size
  uint32_t columns = properties()->view_layout->size->width / advance_width_;
  uint32_t rows = properties()->view_layout->size->height / line_height_;
  MotermModel::Size current = model_.GetSize();
  if (current.columns != columns || current.rows != rows) {
    model_.SetSize(MotermModel::Size(rows, columns), false);
  }
}

void MotermView::DrawContent(SkCanvas* canvas,
                             const mozart::Size& texture_size) {
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
          if ((ch.attributes & MotermModel::kAttributesUnderline))
            flags |= SkPaint::kUnderlineText_Flag;
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
    bg_paint.setARGB(64, 255, 255, 255);
    canvas->drawRect(SkRect::MakeXYWH(cursor_pos.column * advance_width_,
                                      cursor_pos.row * line_height_,
                                      advance_width_, line_height_),
                     bg_paint);
  }

  canvas->flush();
}

void MotermView::ScheduleDraw(bool force) {
  if (!properties() ||
      (!model_state_changes_.IsDirty() && !force && !force_next_draw_)) {
    force_next_draw_ |= force;
    return;
  }

  force_next_draw_ = false;
  Invalidate();
}

void MotermView::OnResponse(const void* buf, size_t size) {
  SendData(buf, size);
}

void MotermView::OnSetKeypadMode(bool application_mode) {
  keypad_application_mode_ = application_mode;
}

// |InputListener|:
void MotermView::OnEvent(mozart::EventPtr event,
                         const OnEventCallback& callback) {
  if (event->action == mozart::EventType::KEY_PRESSED) {
    OnKeyPressed(std::move(event));
    callback(true);
  } else {
    callback(false);
  }
}

void MotermView::OnKeyPressed(mozart::EventPtr key_event) {
  last_key_ = ftl::TimePoint::Now();
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

}  // namespace moterm
