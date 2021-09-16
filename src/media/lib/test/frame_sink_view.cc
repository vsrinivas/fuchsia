// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/media/test/frame_sink_view.h"

#include <lib/async/cpp/wait.h>
#include <lib/media/codec_impl/fourcc.h>
#include <lib/media/test/frame_sink.h>
#include <lib/ui/scenic/cpp/commands.h>
#include <zircon/errors.h>
#include <zircon/syscalls/object.h>

#include <iomanip>
#include <memory>
#include <type_traits>

constexpr uint32_t kShapeWidth = 640;
constexpr uint32_t kShapeHeight = 480;
constexpr float kDisplayHeight = 50;
constexpr float kInitialWindowXPos = 320;
constexpr float kInitialWindowYPos = 240;

// Context for a frame's async lifetime.  When the presentation wait is done,
// the WaitHandler deletes this.
//
// As with async::WaitMethod, this relies on all this code running on the
// dispatcher's single thread.
class Frame {
 public:
  Frame(FrameSinkView* owner, async_dispatcher_t* dispatcher, ::zx::event& release_event, fit::closure on_done)
      : owner_(owner), wait_(this), on_done_(std::move(on_done)) {
    zx_status_t status = release_event.duplicate(ZX_RIGHT_SAME_RIGHTS, &release_event_);
    if (status != ZX_OK) {
      FX_PLOGS(FATAL, status) << "::zx::event::duplicate() failed";
      FX_NOTREACHED();
    }
    wait_.set_object(release_event_.get());
    wait_.set_trigger(ZX_EVENT_SIGNALED);
    status = wait_.Begin(dispatcher);
    if (status != ZX_OK) {
      FX_PLOGS(FATAL, status) << "Begin() failed";
      FX_NOTREACHED();
    }
    owner_->RegisterFrame(this);
  }

  ~Frame() {
    owner_->UnregisterFrame(this);
    ZX_ASSERT(!on_done_);
  }

  void CancelFrame() {
    zx_status_t cancel_status = wait_.Cancel();
    ZX_DEBUG_ASSERT(cancel_status != ZX_ERR_NOT_SUPPORTED);
    if (cancel_status != ZX_OK) {
      // WaitHandler() will run.
      return;
    }
    // WaitHandler won't run.
    FX_LOGS(INFO) << "CancelFrame() ended wait early. (scenic died?)";
    on_done_();
    on_done_ = nullptr;

    // Frame is self-deleting when done.
    delete this;
  }

  // Normally this runs when the remote end of the eventpair is closed.  In
  // addition, when the dispatcher shuts down, if this Frame is still waiting,
  // this callback runs and is passed status ZX_ERR_CANCELED.
  void WaitHandler(async_dispatcher_t* dispatcher, async::WaitBase* wait, zx_status_t status,
                   const zx_packet_signal_t* signal) {
    if (status != ZX_OK) {
      if (status == ZX_ERR_CANCELED) {
        // Probably normal if this is not happening until dispatcher shutdown.
        //
        // TODO(dustingreen): Mute this output if |dispatcher| is shutting down.
        FX_LOGS(INFO) << "WaitHandler() sees ZX_ERR_CANCELED (normal if shutting down)";
      } else {
        FX_PLOGS(INFO, status) << "WaitHandler() sees failure";
      }
    }

    // Regardless of status or signal, this frame is done.  The callee here
    // knows nothing about |Packet|, so cannot delete |this|.
    on_done_();
    on_done_ = nullptr;

    // If the handler is called it means the handler needs to delete the frame.
    // This applies even if the status is ZX_ERR_CANCELED; that'll only happen
    // if the dispatcher is shutting down, not if a canceller manually cancels,
    // which is done in CancelFrame().
    delete this;
  }

  FrameSinkView* owner_ = nullptr;
  ::zx::event release_event_;
  async::WaitMethod<Frame, &Frame::WaitHandler> wait_;
  fit::closure on_done_;
};

std::unique_ptr<FrameSinkView> FrameSinkView::Create(scenic::ViewContext context, FrameSink* parent,
                                                     async::Loop* main_loop) {
  return std::unique_ptr<FrameSinkView>(new FrameSinkView(std::move(context), parent, main_loop));
}

FrameSinkView::~FrameSinkView() {
  while (!registered_frames_.empty()) {
    (*registered_frames_.begin())->CancelFrame();
  }
  parent_->RemoveFrameSinkView(this);
}

void FrameSinkView::PutFrame(uint32_t image_id, zx_time_t present_time, const zx::vmo& vmo,
                             uint64_t vmo_offset,
                             const fuchsia::media::VideoUncompressedFormat& video_format,
                             fit::closure on_done) {
  FX_DCHECK((image_id != FrameSink::kBlankFrameImageId) || !on_done);

  fuchsia::images::PixelFormat pixel_format = fuchsia::images::PixelFormat::BGRA_8;
  uint32_t fourcc = video_format.fourcc;
  switch (fourcc) {
    case make_fourcc('N', 'V', '1', '2'):
      pixel_format = fuchsia::images::PixelFormat::NV12;
      break;
    case make_fourcc('B', 'G', 'R', 'A'):
      pixel_format = fuchsia::images::PixelFormat::BGRA_8;
      break;
    case make_fourcc('Y', 'V', '1', '2'):
      pixel_format = fuchsia::images::PixelFormat::YV12;
      break;
    default:
      FX_CHECK(false) << "fourcc conversion not implemented - fourcc: " << fourcc_to_string(fourcc)
                       << " in hex: 0x" << std::hex << std::setw(8) << fourcc;
      FX_NOTREACHED();
      pixel_format = fuchsia::images::PixelFormat::BGRA_8;
      break;
  }
  fuchsia::images::ImageInfo image_info{
      .width = video_format.primary_width_pixels,
      .height = video_format.primary_height_pixels,
      .stride = video_format.primary_line_stride_bytes,
      .pixel_format = pixel_format,
  };

  FX_VLOGS(3) << "#### image_id: " << image_id << " width: " << image_info.width
              << " height: " << image_info.height << " stride: " << image_info.stride
              << " pixel_format: " <<
              static_cast<std::underlying_type_t<decltype(pixel_format)>>(pixel_format);

  ::zx::vmo image_vmo;
  zx_status_t status = vmo.duplicate(ZX_RIGHT_SAME_RIGHTS, &image_vmo);
  if (status != ZX_OK) {
    FX_PLOGS(FATAL, status) << "vmo.duplicate() failed";
    FX_NOTREACHED();
  }

  size_t image_vmo_size;
  status = image_vmo.get_size(&image_vmo_size);
  if (status != ZX_OK) {
    FX_PLOGS(FATAL, status) << "vmo.get_size() failed";
    FX_NOTREACHED();
  }

  image_pipe_->AddImage(image_id, image_info, std::move(image_vmo), vmo_offset, image_vmo_size,
                        fuchsia::images::MemoryType::HOST_MEMORY);

  ::zx::event release_frame;
  status = ::zx::event::create(0, &release_frame);
  if (status != ZX_OK) {
    FX_PLOGS(FATAL, status) << "::zx::event::create() failed";
    FX_NOTREACHED();
  }

  // There is no expectation that ~frame would be run from this method, but
  // avoid just having a raw pointer since that may be considered harmful-ish.
  //
  // If ~frame were to run, we rely on ~frame being able to cancel the frame's
  // wait without the frame's handler running yet, which is among the ways in
  // which we're relying on running on the dispatcher's single thread here.
  auto on_done_wrapper = [this, image_id, on_done = std::move(on_done)] {
    if (image_id == FrameSink::kBlankFrameImageId) {
      // The image_pipe_ may already be gone, so don't touch ImagePipe.  There
      // is no on_done callback, so no worries re. not running it.
      FX_DCHECK(!on_done);
      return;
    }
    // According to ImagePipe .fidl, this RemoveImage() doesn't impact the
    // present queue or the currently-displayed image.  However, it might
    // help avoid some complaining in the log from Scenic, for now, if we
    // wait this long, instead of removing earlier.
    image_pipe_->RemoveImage(image_id);
    on_done();
  };
  auto frame = std::make_unique<Frame>(this, main_loop_->dispatcher(), release_frame,
                                       std::move(on_done_wrapper));

  ::std::vector<::zx::event> acquire_fences;
  ::std::vector<::zx::event> release_fences;
  release_fences.push_back(std::move(release_frame));

  // For the moment we just display every frame asap.  This will hopefully tend
  // to show frames faster than real-time, and provide some general impression
  // of how fast they're decoding - it's fairly useless for determining the max
  // presentation frame rate - that's not the point here.
  image_pipe_->PresentImage(
      image_id, present_time, std::move(acquire_fences), std::move(release_fences),
      [image_id](fuchsia::images::PresentationInfo presentation_info) {
        FX_VLOGS(3) << "PresentImageCallback() called - presentation_time: "
                    << presentation_info.presentation_time
                    << " presenation_interval: " << presentation_info.presentation_interval
                    << " image_id: " << image_id;
      });

  // The frame will self-delete when its wait is done, so don't ~frame here.
  (void)frame.release();
}

FrameSinkView::FrameSinkView(scenic::ViewContext context, FrameSink* parent, async::Loop* main_loop)
    : BaseView(std::move(context), "FrameSinkView"),
      parent_(parent),
      main_loop_(main_loop),
      node_(session()) {
  FX_VLOGS(3) << "Creating View";

  // Create an ImagePipe and use it.
  uint32_t image_pipe_id = session()->AllocResourceId();
  session()->Enqueue(scenic::NewCreateImagePipeCmd(
      image_pipe_id, image_pipe_.NewRequest(main_loop_->dispatcher())));

  // Create a material that has our image pipe mapped onto it:
  scenic::Material material(session());
  material.SetTexture(image_pipe_id);
  session()->ReleaseResource(image_pipe_id);

  // Create a rectangle shape to display the YUV on.
  scenic::Rectangle shape(session(), kShapeWidth, kShapeHeight);

  node_.SetShape(shape);
  node_.SetMaterial(material);
  root_node().AddChild(node_);

  // Translation of 0, 0 is the middle of the screen
  node_.SetTranslation(kInitialWindowXPos, kInitialWindowYPos, -kDisplayHeight);
  InvalidateScene();

  parent_->AddFrameSinkView(this);
}

void FrameSinkView::OnSceneInvalidated(fuchsia::images::PresentationInfo presentation_info) {
  if (!has_logical_size()) {
    return;
  }

  float width = logical_size().x;
  float height = logical_size().y;
  scenic::Rectangle shape(session(), width, height);
  node_.SetShape(shape);
  float half_width = width * 0.5f;
  float half_height = height * 0.5f;
  node_.SetTranslation(half_width, half_height, -kDisplayHeight);
}

void FrameSinkView::RegisterFrame(Frame* frame) {
  registered_frames_.insert(frame);
}

void FrameSinkView::UnregisterFrame(Frame* frame) {
  registered_frames_.erase(frame);
}
