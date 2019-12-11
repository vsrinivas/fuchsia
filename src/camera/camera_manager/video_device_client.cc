// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/camera/camera_manager/video_device_client.h"

#include <fcntl.h>
#include <fuchsia/hardware/camera/c/fidl.h>
#include <lib/async/cpp/task.h>
#include <lib/fzl/fdio.h>

#include <fbl/unique_fd.h>

#include "src/lib/syslog/cpp/logger.h"

namespace camera {
using fuchsia::camera::VideoFormat;
uint64_t VideoDeviceClient::camera_id_counter_ = 0;

// We'll be dumping our connection to the driver, but it may have connections
// to clients.  We need to tell the driver to drop those connections.
VideoDeviceClient::~VideoDeviceClient() {
  // Clearing this list will signal the device to close all of the streams
  // when their stream_tokens are closed. The individual VideoStreams should
  // all have waiters in the pending state, so their destruction will result in
  // normal cancelation.
  // It is helpful to do this before closing the control channel, because
  // closing the control channel will result in the driver closing the streams,
  // which will result in the stream tokens in the active_streams_ being
  // signalled.
  active_streams_.clear();
  // Close the channel to the device, which should put it in the init state.
  camera_control_.Unbind();
}

std::unique_ptr<VideoDeviceClient> VideoDeviceClient::Create(int dir_fd, const std::string& name) {
  // Open the device node.
  fbl::unique_fd dev_node{openat(dir_fd, name.c_str(), O_RDONLY)};
  if (!dev_node.is_valid()) {
    FX_LOGS(WARNING) << "VideoDeviceClient failed to open device node at \"" << name << "\". ("
                     << strerror(errno) << " : " << errno << ")";
    return nullptr;
  }

  zx::channel local, remote;
  auto err = zx::channel::create(0u, &local, &remote);
  if (err) {
    FX_PLOGS(ERROR, err) << "Failed to create channel";
    return nullptr;
  }

  fzl::FdioCaller dev(std::move(dev_node));
  err = fuchsia_hardware_camera_DeviceGetChannel(dev.borrow_channel(), remote.release());
  if (err) {
    FX_PLOGS(ERROR, err) << "Failed to obtain channel";
    return nullptr;
  }

  std::unique_ptr<VideoDeviceClient> device(new VideoDeviceClient);
  device->camera_control_.Bind(std::move(local));
  device->device_info_.camera_id = camera_id_counter_++;
  return device;
}

void VideoDeviceClient::OnGetFormatsResp(std::vector<fuchsia::camera::VideoFormat> formats,
                                         uint32_t total_format_count,
                                         zx_status_t /*device_status*/) {
  auto& new_formats = formats;
  formats_.insert(formats_.end(), new_formats.begin(), new_formats.end());
  if (formats_.size() < total_format_count) {
    camera_control_->GetFormats(formats.size(),
                                fbl::BindMember(this, &VideoDeviceClient::OnGetFormatsResp));
  } else {
    ready_callback_->Signal(ReadyCallbackHandler::kFormatsReady);
  }
}

void VideoDeviceClient::Startup(StartupCallback callback) {
  // Start up an aggregator that waits until both the formats and the
  // device info are retrieved.  When both bits of info come back,
  // signal that the device is ready.
  // A timeout is also given which will call the callback with an error.
  ready_callback_ = std::make_unique<ReadyCallbackHandler>(std::move(callback),
                                                           zx::sec(kDriverStartupTimeoutSec));
  camera_control_->GetFormats(0, fbl::BindMember(this, &VideoDeviceClient::OnGetFormatsResp));
  camera_control_->GetDeviceInfo([this](fuchsia::camera::DeviceInfo device_info) {
    // Save the camera id, because that is assigned by this class.
    device_info.camera_id = id();
    device_info_ = device_info;
    ready_callback_->Signal(ReadyCallbackHandler::kDeviceInfoReady);
  });
}

void VideoDeviceClient::CreateStream(fuchsia::sysmem::BufferCollectionInfo buffer_collection,
                                     fuchsia::camera::FrameRate frame_rate,
                                     fidl::InterfaceRequest<fuchsia::camera::Stream> stream,
                                     zx::eventpair client_token) {
  /*
    NOTE: Why do we want to do this in the first place?
    Why don't we let the driver police its own streams instead of doing double
    bookeeping?
  */

  // Limit the number of stream connections to a camera by only allowing
  // one connection per available output stream.  This doesn't support
  // multiple applications connecting to the same stream, or having multiple
  // types of streams.
  // TODO(CAM-14): Add more logic around when we can create a stream.
  if (active_streams_.size() >= device_info_.max_stream_count) {
    FX_LOGS(ERROR) << "Failed to creat stream: active streams (" << active_streams_.size()
                   << ") >= max_stream_count (" << device_info_.max_stream_count << ")";
    // If we deny the request, we just return.  That drops the InterfaceRequest
    // on the floor, so no connection is made.
    return;
  }
  // Assume now that we can make a new stream connection.

  zx::eventpair driver_token;
  auto video_stream = VideoStream::Create(this, &driver_token);
  if (!video_stream) {
    // If VideoStream::Create failed, drop everything
    FX_LOGS(ERROR) << "Failed to create stream bookeeping structure.";
    return;
  }

  // We do the push_back first, because if CreateStream fails,
  // it will destruct the driver token at the end of the context,
  // which will then turn around and try to delete the VideoStream
  // from active_streams_.
  active_streams_.push_back(std::move(video_stream));
  camera_control_->CreateStream(std::move(buffer_collection), frame_rate, std::move(stream),
                                std::move(client_token));
}

const std::vector<fuchsia::camera::VideoFormat>* VideoDeviceClient::GetFormats() const {
  return &formats_;
}

// static
std::unique_ptr<VideoDeviceClient::VideoStream> VideoDeviceClient::VideoStream::Create(
    VideoDeviceClient* owner, zx::eventpair* driver_token) {
  auto stream = std::unique_ptr<VideoDeviceClient::VideoStream>(new VideoDeviceClient::VideoStream);
  // Create stream token.  The stream token is used to close the stream,
  // since the camera manager does not retain control of the stream channel.
  zx_status_t status = zx::eventpair::create(0, &stream->stream_token_, driver_token);
  if (status != ZX_OK) {
    FX_PLOGS(ERROR, status) << "Couldn't create driver token";
    return nullptr;
  }
  // Create a waiter that waits for the stream_token to be closed.
  // When it is, trigger the owner to destroy this object, and remove it from
  // the list of active devices.
  // This callback won't run if stream is deleted before the wait triggers.
  // The wait won't trigger at least until this thread has returned to the
  // dispatcher.  Before returning to the dispatcher, the caller must either
  // delete stream or put stream in the list of active streams.
  stream->stream_token_waiter_ = std::make_unique<async::Wait>(
      stream->stream_token_.get(), ZX_EVENTPAIR_PEER_CLOSED, 0,
      std::bind([owner, stream_ptr = stream.get()]() {
        FX_LOGS(INFO) << "ZX_EVENTPAIR_PEER_CLOSED received, removing active stream.";
        owner->RemoveActiveStream(stream_ptr);
      }));

  status = stream->stream_token_waiter_->Begin(async_get_default_dispatcher());
  if (status != ZX_OK) {
    FX_PLOGS(ERROR, status) << "Couldn't begin stream_token_waiter_ wait";
    return nullptr;
  }

  return stream;
}

// The stream was shut down in the driver.
void VideoDeviceClient::RemoveActiveStream(VideoStream* stream) {
  for (auto iter = active_streams_.begin(); iter != active_streams_.end(); ++iter) {
    if (iter->get() == stream) {
      active_streams_.erase(iter);
      return;
    }
  }
}

void VideoDeviceClient::ReadyCallbackHandler::Signal(int signal) {
  if (!callback_) {
    return;
  }
  if (signal == kFormatsReady) {
    has_formats_ = true;
  }
  if (signal == kDeviceInfoReady) {
    has_device_info_ = true;
  }
  if (has_formats_ && has_device_info_) {
    callback_(ZX_OK);
    callback_ = nullptr;
    // Intentionally leaving timeout in deprecated class. See TODO(39822): delete CameraManager1
    timeout_task_.Cancel();
  }
}

}  // namespace camera
