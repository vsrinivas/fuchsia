// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include "src/camera/examples/camera_tool/buffer_collage.h"

#include <fuchsia/images/cpp/fidl.h>
#include <fuchsia/sysmem/cpp/fidl.h>
#include <lib/async-loop/default.h>
#include <lib/async/cpp/task.h>
#include <lib/async/cpp/wait.h>
#include <lib/async/dispatcher.h>
#include <lib/fit/bridge.h>
#include <lib/fit/promise.h>
#include <lib/syslog/cpp/logger.h>
#include <lib/ui/scenic/cpp/commands.h>
#include <lib/ui/scenic/cpp/resources.h>
#include <lib/ui/scenic/cpp/view_token_pair.h>
#include <lib/zx/eventpair.h>
#include <zircon/syscalls/object.h>
#include <zircon/types.h>

namespace camera {

// Returns an event such that when the event is signaled and the dispatcher executed, the provided
// eventpair is closed. This can be used to bridge event- and eventpair-based fence semantics. If
// this function returns an error, |eventpair| is closed immediately.
fit::result<zx::event, zx_status_t> MakeEventBridge(async_dispatcher_t* dispatcher,
                                                    zx::eventpair eventpair) {
  zx::event caller_event;
  zx::event waiter_event;
  zx_status_t status = zx::event::create(0, &caller_event);
  if (status != ZX_OK) {
    FX_PLOGS(ERROR, status);
    return fit::error(status);
  }
  status = caller_event.duplicate(ZX_RIGHT_SAME_RIGHTS, &waiter_event);
  if (status != ZX_OK) {
    FX_PLOGS(ERROR, status);
    return fit::error(status);
  }
  // A shared_ptr is necessary in order to begin the wait after setting the wait handler.
  auto wait = std::make_shared<async::Wait>(waiter_event.get(), ZX_EVENT_SIGNALED);
  wait->set_handler(
      [wait, waiter_event = std::move(waiter_event), eventpair = std::move(eventpair)](
          async_dispatcher_t*, async::Wait*, zx_status_t, const zx_packet_signal_t*) mutable {
        // Close the waiter along with its captures.
        wait = nullptr;
      });
  status = wait->Begin(dispatcher);
  if (status != ZX_OK) {
    FX_PLOGS(ERROR, status);
    return fit::error(status);
  }
  return fit::ok(std::move(caller_event));
}

BufferCollage::BufferCollage() : loop_(&kAsyncLoopConfigNoAttachToCurrentThread) {
  SetStopOnError(presenter_);
  SetStopOnError(scenic_);
  SetStopOnError(allocator_);
}

BufferCollage::~BufferCollage() {
  zx_status_t status =
      async::PostTask(loop_.dispatcher(), fit::bind_member(this, &BufferCollage::Stop));
  ZX_ASSERT(status == ZX_OK);
  loop_.JoinThreads();
}

fit::result<std::unique_ptr<BufferCollage>, zx_status_t> BufferCollage::Create(
    fuchsia::ui::policy::PresenterHandle presenter, fuchsia::ui::scenic::ScenicHandle scenic,
    fuchsia::sysmem::AllocatorHandle allocator, fit::closure stop_callback) {
  auto displayer = std::unique_ptr<BufferCollage>(new BufferCollage);

  // Bind interface handles and save the stop callback.
  zx_status_t status =
      displayer->presenter_.Bind(std::move(presenter), displayer->loop_.dispatcher());
  if (status != ZX_OK) {
    FX_PLOGS(ERROR, status);
    return fit::error(status);
  }
  status = displayer->scenic_.Bind(std::move(scenic), displayer->loop_.dispatcher());
  if (status != ZX_OK) {
    FX_PLOGS(ERROR, status);
    return fit::error(status);
  }
  status = displayer->allocator_.Bind(std::move(allocator), displayer->loop_.dispatcher());
  if (status != ZX_OK) {
    FX_PLOGS(ERROR, status);
    return fit::error(status);
  }
  displayer->stop_callback_ = std::move(stop_callback);

  // Create a scenic session and set its event handlers.
  displayer->session_ = std::make_unique<scenic::Session>(displayer->scenic_.get());
  displayer->session_->set_error_handler(
      fit::bind_member(displayer.get(), &BufferCollage::OnScenicError));
  displayer->session_->set_event_handler(
      fit::bind_member(displayer.get(), &BufferCollage::OnScenicEvent));

  // Create and present a scenic view.
  auto tokens = scenic::NewViewTokenPair();
  displayer->view_ = std::make_unique<scenic::View>(displayer->session_.get(),
                                                    std::move(tokens.first), "BufferCollage");
  displayer->presenter_->PresentOrReplaceView(std::move(tokens.second), nullptr);

  // Start a thread and begin processing messages.
  status = displayer->loop_.StartThread("BufferCollage Loop");
  if (status != ZX_OK) {
    FX_PLOGS(ERROR, status);
    return fit::error(status);
  }

  return fit::ok(std::move(displayer));
}

fit::promise<uint32_t> BufferCollage::AddCollection(
    fuchsia::sysmem::BufferCollectionTokenHandle token, fuchsia::sysmem::ImageFormat_2 image_format,
    std::string description) {
  if (!collection_views_.empty()) {
    FX_LOGS(ERROR) << "Multiple collections not supported yet.";
    Stop();
    return fit::make_result_promise<uint32_t>(fit::error());
  }
  auto collection_id = next_collection_id_++;
  FX_LOGS(DEBUG) << "Adding collection with ID " << collection_id << ".";
  ZX_ASSERT(collection_views_.find(collection_id) == collection_views_.end());
  auto& collection_view = collection_views_[collection_id];
  std::ostringstream oss;
  oss << " (" << collection_id << ")";
  SetStopOnError(collection_view.collection, "Collection" + oss.str());
  SetStopOnError(collection_view.image_pipe, "Image Pipe" + oss.str());

  // Bind and duplicate the token.
  fuchsia::sysmem::BufferCollectionTokenPtr token_ptr;
  SetStopOnError(token_ptr);
  zx_status_t status = token_ptr.Bind(std::move(token), loop_.dispatcher());
  if (status != ZX_OK) {
    FX_PLOGS(ERROR, status);
    Stop();
    return fit::make_result_promise<uint32_t>(fit::error());
  }
  fuchsia::sysmem::BufferCollectionTokenHandle scenic_token;
  token_ptr->Duplicate(ZX_RIGHT_SAME_RIGHTS, scenic_token.NewRequest());
  allocator_->BindSharedCollection(std::move(token_ptr),
                                   collection_view.collection.NewRequest(loop_.dispatcher()));

  // Sync the collection and create an image pipe using the scenic token.
  fit::bridge scenic_bridge;
  collection_view.collection->Sync([this, collection_id, token = std::move(scenic_token),
                                    result = std::move(scenic_bridge.completer)]() mutable {
    auto& view = collection_views_[collection_id];
    auto image_pipe_id = session_->AllocResourceId();
    auto command = scenic::NewCreateImagePipe2Cmd(image_pipe_id,
                                                  view.image_pipe.NewRequest(loop_.dispatcher()));
    session_->Enqueue(std::move(command));
    view.image_pipe->AddBufferCollection(1, std::move(token));
    view.material = std::make_unique<scenic::Material>(session_.get());
    view.material->SetTexture(image_pipe_id);
    view.rectangle = std::make_unique<scenic::Rectangle>(session_.get(), 1024, 640);
    view.node = std::make_unique<scenic::ShapeNode>(session_.get());
    view.node->SetShape(*view.rectangle);
    view.node->SetMaterial(*view.material);
    view.node->SetTranslation(512, 320, 0);
    view_->AddChild(*view.node);
    session_->Present(zx::clock::get_monotonic(), [](fuchsia::images::PresentationInfo info) {});
    result.complete_ok();
  });

  // Set minimal constraints then wait for buffer allocation.
  collection_view.collection->SetConstraints(true, {.usage{.none = fuchsia::sysmem::noneUsage}});
  fit::bridge sysmem_bridge;
  collection_view.collection->WaitForBuffersAllocated(
      [this, collection_id, result = std::move(sysmem_bridge.completer)](
          zx_status_t status, fuchsia::sysmem::BufferCollectionInfo_2 buffers) mutable {
        if (status != ZX_OK) {
          FX_PLOGS(ERROR, status) << "Failed to allocate buffers.";
          Stop();
          result.complete_error();
          return;
        }
        collection_views_[collection_id].buffers = std::move(buffers);
        result.complete_ok();
      });

  // Once both scenic and sysmem complete their operations, add the negotiated images to the image
  // pipe. Note that this continuation may be run on an arbitrary thread, so private actions must be
  // marshalled back to the collage thread.
  return fit::join_promises(scenic_bridge.consumer.promise(), sysmem_bridge.consumer.promise())
      .then([this, collection_id,
             image_format](fit::result<std::tuple<fit::result<>, fit::result<>>>& result)
                -> fit::promise<uint32_t> {
        if (result.is_error() || std::get<0>(result.value()).is_error() ||
            std::get<1>(result.value()).is_error()) {
          FX_LOGS(ERROR) << "Failed to add collection " << collection_id << ".";
          zx_status_t status = async::PostTask(loop_.dispatcher(), [this] { Stop(); });
          if (status != ZX_OK) {
            FX_PLOGS(ERROR, status) << "Failed to schedule task.";
          }
          return fit::make_result_promise<uint32_t>(fit::error());
        }

        fit::bridge<uint32_t> task_bridge;
        zx_status_t status = async::PostTask(
            loop_.dispatcher(), [this, collection_id, image_format,
                                 result = std::move(task_bridge.completer)]() mutable {
              auto& view = collection_views_[collection_id];
              for (uint32_t i = 0; i < view.buffers.buffer_count; ++i) {
                view.image_pipe->AddImage(i + 1, 1, i, image_format);
              }
              FX_LOGS(DEBUG) << "Successfully added collection " << collection_id << ".";
              result.complete_ok(collection_id);
            });
        if (status != ZX_OK) {
          FX_PLOGS(ERROR, status) << "Failed to schedule task.";
          return fit::make_result_promise<uint32_t>(fit::error());
        }

        return task_bridge.consumer.promise();
      });
}

void BufferCollage::RemoveCollection(uint32_t id) {
  async::PostTask(loop_.dispatcher(), [this, id]() {
    auto it = collection_views_.find(id);
    if (it == collection_views_.end()) {
      FX_LOGS(ERROR) << "Invalid collection ID " << id << ".";
      Stop();
      return;
    }
    view_->DetachChild(*it->second.node);
    it->second.collection->Close();
    collection_views_.erase(it);
    session_->Present(zx::clock::get_monotonic(), [](fuchsia::images::PresentationInfo info) {});
  });
}

void BufferCollage::ShowBuffer(uint32_t collection_id, uint32_t buffer_index,
                               zx::eventpair release_fence,
                               std::optional<fuchsia::math::Rect> subregion) {
  if (subregion) {
    FX_LOGS(ERROR) << "Subregion is not yet supported.";
    Stop();
    return;
  }
  auto it = collection_views_.find(collection_id);
  if (it == collection_views_.end()) {
    FX_LOGS(ERROR) << "Invalid collection ID " << collection_id << ".";
    Stop();
    return;
  }
  if (buffer_index >= it->second.buffers.buffer_count) {
    FX_LOGS(ERROR) << "Invalid buffer index " << buffer_index << ".";
    Stop();
    return;
  }

  auto result = MakeEventBridge(loop_.dispatcher(), std::move(release_fence));
  if (result.is_error()) {
    FX_PLOGS(ERROR, result.error());
    Stop();
    return;
  }
  std::vector<zx::event> scenic_fences;
  scenic_fences.push_back(result.take_value());
  it->second.image_pipe->PresentImage(buffer_index + 1, zx::clock::get_monotonic().get(), {},
                                      std::move(scenic_fences),
                                      [](fuchsia::images::PresentationInfo info) {});
}

void BufferCollage::Stop() {
  presenter_ = nullptr;
  scenic_ = nullptr;
  allocator_ = nullptr;
  view_ = nullptr;
  collection_views_.clear();
  loop_.Quit();
  if (stop_callback_) {
    stop_callback_();
    stop_callback_ = nullptr;
  }
}

template <typename T>
void BufferCollage::SetStopOnError(fidl::InterfacePtr<T>& p, std::string name) {
  p.set_error_handler([this, name, &p](zx_status_t status) {
    FX_PLOGS(ERROR, status) << name << " disconnected unexpectedly.";
    p = nullptr;
    Stop();
  });
}

void BufferCollage::OnScenicError(zx_status_t status) {
  FX_PLOGS(ERROR, status) << "Scenic session error.";
  Stop();
}

void BufferCollage::OnScenicEvent(std::vector<fuchsia::ui::scenic::Event> events) {
  for (const auto& event : events) {
    if (event.is_gfx() && event.gfx().is_view_properties_changed()) {
      view_extents_ = event.gfx().view_properties_changed().properties.bounding_box;
    }
  }
}

}  // namespace camera
