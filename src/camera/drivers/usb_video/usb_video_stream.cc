// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/camera/drivers/usb_video/usb_video_stream.h"

#include <lib/async/cpp/task.h>
#include <lib/ddk/debug.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/fit/defer.h>
#include <lib/zircon-internal/align.h>
#include <lib/zx/vmar.h>
#include <stdlib.h>
#include <string.h>

#include <algorithm>
#include <memory>

#include <fbl/algorithm.h>
#include <fbl/auto_lock.h>
#include <usb/usb-request.h>

#include "src/camera/drivers/usb_video/bind.h"
#include "src/camera/drivers/usb_video/descriptors.h"
namespace camera::usb_video {

RequestTask::RequestTask(usb_request_t* req, UsbVideoStream* context)
    : req_(*req), context_(context) {
  this->state = ASYNC_STATE_INIT;
  this->handler = RequestTask::AsyncTaskHandler;

  if (req->response.actual == 0 || req->response.status != ZX_OK) {
    return;
  }
  data_.resize(req->response.actual);
  __UNUSED auto copy_result = usb_request_copy_from(req, data_.data(), req->response.actual, 0);
  req_.virt = reinterpret_cast<uintptr_t>(data_.data());
  req_.offset = 0;
  req_.vmo_handle = ZX_HANDLE_INVALID;
}

//  static
void RequestTask::AsyncTaskHandler(async_dispatcher_t* dispatcher, async_task_t* task,
                                   zx_status_t status) {
  auto request_task = static_cast<RequestTask*>(task);
  request_task->context_->HandleRequest(request_task);
}

UsbVideoStream::UsbVideoStream(zx_device_t* parent, usb_protocol_t usb, StreamingSetting settings)
    : UsbVideoStreamBase(parent),
      unbind_task_(this),
      control_binding_(this),
      usb_state_(usb, std::move(settings)),
      stream_binding_(this) {
  fidl_dispatch_loop_ = std::make_unique<async::Loop>(&kAsyncLoopConfigNoAttachToCurrentThread);
  fidl_dispatch_loop_->StartThread();
}

zx_status_t UsbVideoStream::Bind(void* ctx, zx_device_t* device) {
  usb_protocol_t usb;
  zx_status_t status = device_get_protocol(device, ZX_PROTOCOL_USB, &usb);
  if (status != ZX_OK) {
    return status;
  }

  usb_desc_iter_t iter;
  status = usb_desc_iter_init(&usb, &iter);
  if (status != ZX_OK) {
    return status;
  }

  auto streams_or = LoadStreamingSettings(&iter);
  // Release iterator regardless of whether parsing succeeded.
  usb_desc_iter_release(&iter);
  if (streams_or.is_error()) {
    zxlogf(ERROR, "Failed to load stream settings.");
    return streams_or.error_value();
  }
  // Parsed all descriptors successfully.

  auto dev = std::make_unique<UsbVideoStream>(device, usb, std::move(*streams_or));

  zxlogf(INFO, "Adding UsbVideoStream");
  status = dev->DdkAdd("usb-video-source");
  if (status == ZX_OK) {
    // devmgr is now in charge of the memory for dev
    __UNUSED auto ptr = dev.release();
  }
  return status;
}

void UsbVideoStream::GetChannel(GetChannelRequestView request,
                                GetChannelCompleter::Sync& completer) {
  fbl::AutoLock lock(&lock_);
  zxlogf(DEBUG, "Getting Channel");
  if (unbind_txn_.has_value()) {
    zxlogf(ERROR, "Request for camera control after Unbind has been called.");
    completer.Close(ZX_ERR_ACCESS_DENIED);
    return;
  }
  if (control_binding_.is_bound()) {
    zxlogf(ERROR, "Camera Control already running");
    completer.Close(ZX_ERR_ACCESS_DENIED);
    return;
  }
  control_binding_.Bind(std::move(request->ch), fidl_dispatch_loop_->dispatcher());
  completer.Close(ZX_OK);
}

void UsbVideoStream::GetDeviceInfo(GetDeviceInfoCallback callback) {
  zxlogf(DEBUG, "Received GetDeviceInfo call");
  // This is just a conversion from the format internal to the device driver
  // to the FIDL DeviceInfo struct.
  const auto& usb_device_info = usb_state_.GetDeviceInfo();
  fuchsia::camera::DeviceInfo camera_device_info;
  camera_device_info.vendor_name = usb_device_info.manufacturer;
  camera_device_info.vendor_id = usb_device_info.vendor_id;
  camera_device_info.product_name = usb_device_info.product_name;
  camera_device_info.product_id = usb_device_info.product_id;

  camera_device_info.output_capabilities = fuchsia::camera::CAMERA_OUTPUT_STREAM;
  camera_device_info.max_stream_count = 1;
  callback(std::move(camera_device_info));
}

void UsbVideoStream::GetFormats(uint32_t index, GetFormatsCallback callback) {
  auto uvc_formats = usb_state_.GetUvcFormats();
  std::vector<fuchsia::camera::VideoFormat> formats;
  while ((index < uvc_formats.size()) &&
         (formats.size() < fuchsia::camera::MAX_FORMATS_PER_RESPONSE)) {
    formats.push_back(uvc_formats[index].ToFidl());
    ++index;
  }
  static_assert(fuchsia::camera::MAX_FORMATS_PER_RESPONSE < std::numeric_limits<uint32_t>::max());
  callback(std::move(formats), static_cast<uint32_t>(formats.size()), ZX_OK);
}

// Fidl call:
void UsbVideoStream::CreateStream(fuchsia::sysmem::BufferCollectionInfo buffer_collection,
                                  fuchsia::camera::FrameRate frame_rate,
                                  fidl::InterfaceRequest<fuchsia::camera::Stream> stream,
                                  zx::eventpair stream_token) {
  fbl::AutoLock lock(&lock_);
  // Check the current state:
  if (stream_binding_.is_bound()) {
    zxlogf(ERROR, "CreateStream called while currently streaming!");
    return;
  }
  // messages on the stream channel will not be processed until this function completes.
  stream_token_ = std::move(stream_token);
  stream_binding_.Bind(std::move(stream), fidl_dispatch_loop_->dispatcher());
  auto status = CreateStream(std::move(buffer_collection), frame_rate);
  if (status != ZX_OK) {
    CloseStreamOnError(status, "create stream");
    control_binding_.Unbind();  // Close the channel on error.
  } else {
    stream_binding_.set_error_handler([this](zx_status_t status) { OnStreamingShutdown(); });
  }
}

// Internal implementation:
zx_status_t UsbVideoStream::CreateStream(fuchsia::sysmem::BufferCollectionInfo buffer_collection,
                                         fuchsia::camera::FrameRate frame_rate) {
  fuchsia::camera::VideoFormat video_format{.format = buffer_collection.format.image,
                                            .rate = frame_rate};

  // Try setting the format on the device.
  if (zx_status_t status = usb_state_.SetFormat(video_format); status != ZX_OK) {
    zxlogf(ERROR, "setting format failed, err: %d", status);
    return status;
  }

  if (usb_state_.MaxFrameSize() > buffer_collection.vmo_size) {
    zxlogf(ERROR, "buffer provided %lu is less than max size %u.", buffer_collection.vmo_size,
           usb_state_.MaxFrameSize());
    return ZX_ERR_INVALID_ARGS;
  }

  // Clear any previous frame state.
  current_frame_ = nullptr;
  frame_number_ = 0;
  // Now to set the buffers:
  {
    zx::unowned_vmo vmos[std::size(buffer_collection.vmos)];
    for (size_t i = 0; i < buffer_collection.buffer_count; ++i) {
      vmos[i] = buffer_collection.vmos[i].borrow();
    }
    if (zx_status_t status = buffers_.Init(cpp20::span(vmos, buffer_collection.buffer_count));
        status != ZX_OK) {
      zxlogf(ERROR, "Failed to initialize VmoPool, err: %d", status);
      return status;
    }
  }
  if (zx_status_t status = buffers_.MapVmos(); status != ZX_OK) {
    zxlogf(ERROR, "Failed to map vmos, err: %d", status);
    return status;
  }

  // Now tell the usb state to start streaming, but set the "pause" mode
  // so we won't send any frames.
  is_streaming_ = false;
  return usb_state_.StartStreaming(
      fit::bind_member(this, &UsbVideoStream::PostRequestCompleteTask));
}

void UsbVideoStream::CloseStreamOnError(zx_status_t status, const char* call) {
  if (status != ZX_OK) {
    zxlogf(ERROR, "Failed to %s: status %s. Closing stream channel.", call,
           zx_status_get_string(status));
    SetStreaming(false);
    stream_binding_.Unbind();
    OnStreamingShutdown();
  }
}

zx_status_t UsbVideoStream::SetStreaming(bool stream) {
  // This cannot be called unless our state is either STREAMING or READY.
  ZX_ASSERT(stream_binding_.is_bound());
  if (stream == is_streaming_) {
    zxlogf(ERROR, "%s streaming was called when streaming was already %s!",
           stream ? "Start" : "Stop", stream ? "started" : "stopped");
    return ZX_ERR_BAD_STATE;
  }
  is_streaming_ = stream;
  return ZX_OK;
}

void UsbVideoStream::HandleUnbind(async_dispatcher_t* dispatcher, async::TaskBase* task,
                                  zx_status_t status) {
  fbl::AutoLock lock(&lock_);
  if (control_binding_.is_bound()) {
    control_binding_.Unbind();
  }
  if (stream_binding_.is_bound()) {
    stream_binding_.Unbind();
  }
  OnStreamingShutdown();
  unbind_txn_->Reply();
}

zx_status_t UsbVideoStream::FrameRelease(uint32_t buffer_id) {
  // This cannot be called unless our state is either STREAMING or READY.
  ZX_ASSERT(stream_binding_.is_bound());
  return buffers_.ReleaseBuffer(buffer_id);
}

// Ensures that current_frame_ exists, and is backed by a buffer if one is available.
void UsbVideoStream::CheckCurrentFrame() {
  if (!current_frame_) {
    std::optional<fzl::VmoPool::Buffer> buffer = buffers_.LockBufferForWrite();
    if (!buffer) {
      zxlogf(ERROR, "no available buffers to store image data!");
      // If no buffers, we still need to track the data stream from the camera.
      // Initialize current_frame_ with an invalid buffer.
      buffer = fzl::VmoPool::Buffer();
    }
    current_frame_ = std::make_unique<VideoFrame>(std::move(*buffer), usb_state_, frame_number_++);
  }
}

void UsbVideoStream::HandleRequest(RequestTask* task) {
  RequestComplete(task->req());
  fbl::AutoLock lock(&req_list_lock_);
  for (auto it = req_list_.begin(); it != req_list_.end(); ++it) {
    if (&(*it) == task) {
      req_list_.erase(it);
      return;
    }
  }
}

void UsbVideoStream::PostRequestCompleteTask(usb_request_t* req) {
  fbl::AutoLock lock(&req_list_lock_);
  req_list_.emplace_back(req, this);
  req_list_.back().deadline = async_now(fidl_dispatch_loop_->dispatcher());
  if (async_post_task(fidl_dispatch_loop_->dispatcher(), &req_list_.back()) != ZX_OK) {
    req_list_.pop_back();
  }
}

void UsbVideoStream::RequestComplete(usb_request_t* req) {
  // Check the current state:
  if (!stream_binding_.is_bound()) {
    zxlogf(ERROR, "Received usb request but we are no longer streaming!");
    return;
  }

  CheckCurrentFrame();
  auto status = current_frame_->ProcessPayload(req);
  // ZX_ERR_STOP and ZX_ERR_NEXT both indicate that the current frame is complete.
  if (status == ZX_ERR_STOP || status == ZX_ERR_NEXT) {
    zxlogf(DEBUG, "Process payload returned %s, the the frame is complete.",
           zx_status_get_string(status));
    // VideoFrame::Release handles the case of frame errors, or if we didn't have a buffer.
    // The only reason result would not be valid is if there was a logical issue with the frame,
    // in which case we should not send it.
    auto result = current_frame_->Release();
    if (result.is_ok()) {
      if (is_streaming_) {
        zxlogf(DEBUG, "Sending frame %u", current_frame_->FrameNumber());
        stream_binding_.events().OnFrameAvailable(result.value());
      } else {
        // If we are in the READY (paused) state, just don't send the message.
        // We do need to release the buffer though, if it was valid.
        if (result->frame_status != fuchsia::camera::FrameStatus::ERROR_BUFFER_FULL) {
          buffers_.ReleaseBuffer(result->buffer_id);
        }
      }
    }  // if result.is_ok()
    // Now that the frame is done, delete it. It no longer owns the buffer.
    current_frame_ = nullptr;
    // If we received ZX_ERR_NEXT, this request actually contains the first bit of data
    // for the next frame.
    if (status == ZX_ERR_NEXT) {
      zxlogf(DEBUG, "reprocess request, because new frame.");
      // make another VideoFrame to populate current_frame_:
      CheckCurrentFrame();
      // need to re-process request:
      zx_status_t next_status = current_frame_->ProcessPayload(req);
      // This is the first frame, and one request is not allowed to span two frames,
      // So we should never get ZX_ERR_NEXT here.
      ZX_ASSERT(next_status != ZX_ERR_NEXT);
      // We can ignore other return codes. Even if this was somehow a tiny frame,
      // and we got ZX_ERR_STOP, the next request will return ZX_ERR_NEXT, and process it
      // correctly.
    }
  }  // if current frame is complete
}

}  // namespace camera::usb_video

static zx_driver_ops_t driver_ops = []() {
  zx_driver_ops_t ops = {};
  ops.version = DRIVER_OPS_VERSION;
  ops.bind = camera::usb_video::UsbVideoStream::Bind;
  return ops;
}();

// clang-format off
ZIRCON_DRIVER(usb_video, driver_ops, "zircon", "0.1");
// clang-format on
