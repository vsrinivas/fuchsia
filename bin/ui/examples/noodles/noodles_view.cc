// Copyright 2015 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/mozart/examples/noodles/noodles_view.h"

#include <math.h>

#include <cstdlib>
#include <utility>

#include "apps/mozart/examples/noodles/frame.h"
#include "apps/mozart/examples/noodles/rasterizer.h"
#include "lib/ftl/functional/make_copyable.h"
#include "lib/ftl/logging.h"
#include "third_party/skia/include/core/SkCanvas.h"
#include "third_party/skia/include/core/SkColor.h"
#include "third_party/skia/include/core/SkPath.h"
#include "third_party/skia/include/core/SkPicture.h"
#include "third_party/skia/include/core/SkPictureRecorder.h"

namespace examples {

namespace {
constexpr double kSecondsBetweenChanges = 10.0;

void Lissajous(SkPath* path, double ax, double ay, int wx, int wy, double p) {
  uint32_t segments = ceil(fabs(ax) + fabs(ay)) / 2u + 1u;
  for (uint32_t i = 0; i < segments; ++i) {
    double t = M_PI * 2.0 * i / segments;
    double x = ax * sin(t * wx);
    double y = ay * sin(t * wy + p);
    if (i == 0u)
      path->moveTo(x, y);
    else
      path->lineTo(x, y);
  }
  path->close();
}
}  // namespace

NoodlesView::NoodlesView(
    mozart::ViewManagerPtr view_manager,
    fidl::InterfaceRequest<mozart::ViewOwner> view_owner_request)
    : BaseView(std::move(view_manager),
               std::move(view_owner_request),
               "Noodles"),
      frame_queue_(std::make_shared<FrameQueue>()),
      rasterizer_delegate_(new RasterizerDelegate(frame_queue_)) {
  // TODO(jeffbrown): Give this thread a name.
  rasterizer_thread_ = mtl::CreateThread(&rasterizer_task_runner_);

  rasterizer_task_runner_->PostTask(ftl::MakeCopyable([
    d = rasterizer_delegate_.get(), scene = TakeScene().PassInterfaceHandle()
  ]() mutable { d->CreateRasterizer(std::move(scene)); }));
}

NoodlesView::~NoodlesView() {
  rasterizer_task_runner_->PostTask([this] {
    rasterizer_delegate_.reset();
    mtl::MessageLoop::GetCurrent()->QuitNow();
  });
  rasterizer_thread_.join();
}

void NoodlesView::OnDraw() {
  FTL_DCHECK(properties());

  const mozart::Size& size = *properties()->view_layout->size;

  // Update the animation.
  alpha_ += frame_tracker().presentation_time_delta().ToSecondsF();

  // Create and post a new frame to the renderer.
  std::unique_ptr<Frame> frame(
      new Frame(size, CreatePicture(), CreateSceneMetadata()));
  if (frame_queue_->PutFrame(std::move(frame))) {
    rasterizer_task_runner_->PostTask(
        [d = rasterizer_delegate_.get()] { d->PublishNextFrame(); });
  }

  // Animate!
  Invalidate();
}

sk_sp<SkPicture> NoodlesView::CreatePicture() {
  constexpr int count = 4;
  constexpr int padding = 1;

  if (alpha_ > kSecondsBetweenChanges) {
    alpha_ = 0.0;
    wx_ = rand() % 9 + 1;
    wy_ = rand() % 9 + 1;
  }

  const mozart::Size& size = *properties()->view_layout->size;
  SkPictureRecorder recorder;
  SkCanvas* canvas = recorder.beginRecording(size.width, size.height);

  double cx = size.width * 0.5;
  double cy = size.height * 0.5;
  canvas->translate(cx, cy);

  double phase = alpha_;
  for (int i = 0; i < count; i++, phase += 0.1) {
    SkPaint paint;
    SkScalar hsv[3] = {static_cast<SkScalar>(fmod(phase * 120, 360)), 1, 1};
    paint.setColor(SkHSVToColor(hsv));
    paint.setStyle(SkPaint::kStroke_Style);
    paint.setAntiAlias(true);

    SkPath path;
    Lissajous(&path, cx - padding, cy - padding, wx_, wy_, phase);
    canvas->drawPath(path, paint);
  }

  return recorder.finishRecordingAsPicture();
}

NoodlesView::FrameQueue::FrameQueue() {}

NoodlesView::FrameQueue::~FrameQueue() {}

bool NoodlesView::FrameQueue::PutFrame(std::unique_ptr<Frame> frame) {
  std::lock_guard<std::mutex> lock(mutex_);
  bool was_empty = !next_frame_.get();
  next_frame_.swap(frame);
  return was_empty;
}

std::unique_ptr<Frame> NoodlesView::FrameQueue::TakeFrame() {
  std::lock_guard<std::mutex> lock(mutex_);
  return std::move(next_frame_);
}

NoodlesView::RasterizerDelegate::RasterizerDelegate(
    const std::shared_ptr<FrameQueue>& frame_queue)
    : frame_queue_(frame_queue) {
  FTL_DCHECK(frame_queue_);
}

NoodlesView::RasterizerDelegate::~RasterizerDelegate() {}

void NoodlesView::RasterizerDelegate::CreateRasterizer(
    fidl::InterfaceHandle<mozart::Scene> scene_info) {
  rasterizer_.reset(
      new Rasterizer(mozart::ScenePtr::Create(std::move(scene_info))));
}

void NoodlesView::RasterizerDelegate::PublishNextFrame() {
  std::unique_ptr<Frame> frame(frame_queue_->TakeFrame());
  FTL_DCHECK(frame);
  rasterizer_->PublishFrame(std::move(frame));
}

}  // namespace examples
