// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/camera/bin/usb_device/stream_impl.h"

#include <fcntl.h>
#include <lib/async/cpp/task.h>
#include <lib/fit/function.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/trace/event.h>
#include <lib/zx/time.h>
#include <unistd.h>
#include <zircon/assert.h>
#include <zircon/errors.h>
#include <zircon/types.h>

#include <cerrno>

#include "src/camera/bin/usb_device/messages.h"
#include "src/camera/bin/usb_device/size_util.h"
#include "src/camera/bin/usb_device/uvc_hack.h"
#include "src/lib/fsl/handles/object_info.h"

namespace camera {

StreamImpl::StreamImpl(async_dispatcher_t* dispatcher,
                       const fuchsia::camera3::StreamProperties2& properties,
                       fidl::InterfaceRequest<fuchsia::camera3::Stream> request,
                       StreamRequestedCallback on_stream_requested,
                       AllocatorBindSharedCollectionCallback allocator_bind_shared_collection,
                       fit::closure on_no_clients, std::optional<std::string> description)
    : dispatcher_(dispatcher),
      properties_(properties),
      on_stream_requested_(std::move(on_stream_requested)),
      allocator_bind_shared_collection_(std::move(allocator_bind_shared_collection)),
      on_no_clients_(std::move(on_no_clients)),
      description_(description.value_or("<unknown>")) {
  current_resolution_ = ConvertToSize(properties.image_format());
  OnNewRequest(std::move(request));
}

StreamImpl::~StreamImpl() { StopStreaming(); }

inline size_t ROUNDUP(size_t size, size_t page_size) {
  // Caveat: Only works for power of 2 page sizes.
  return (size + (page_size - 1)) & ~(page_size - 1);
}

zx::result<fuchsia::sysmem::BufferCollectionInfo> StreamImpl::Gralloc(
    fuchsia::camera::VideoFormat video_format, uint32_t num_buffers) {
  fuchsia::sysmem::BufferCollectionInfo buffer_collection_info;
  size_t buffer_size =
      ROUNDUP(video_format.format.height * video_format.format.planes[0].bytes_per_row, PAGE_SIZE);
  buffer_collection_info.buffer_count = num_buffers;
  buffer_collection_info.vmo_size = buffer_size;
  buffer_collection_info.format.image = video_format.format;
  zx_status_t status;
  for (uint32_t i = 0; i < num_buffers; ++i) {
    status = zx::vmo::create(buffer_size, 0, &buffer_collection_info.vmos[i]);
    if (status != ZX_OK) {
      FX_PLOGS(ERROR, status) << "Failed to allocate Buffer Collection";
      return zx::error(status);
    }
  }
  return zx::ok(std::move(buffer_collection_info));
}

void StreamImpl::CloseAllClients(zx_status_t status) {
  while (clients_.size() > 1) {
    auto& [id, client] = *clients_.begin();
    client->CloseConnection(status);
  }

  if (clients_.size() == 1) {
    // After last client has been removed, on_no_clients_ will run and potentially delete 'this' so
    // handle last client on it's own and don't touch 'this' after.
    clients_.begin()->second->CloseConnection(status);
  }
}

fpromise::scope& StreamImpl::Scope() { return scope_; }

void StreamImpl::OnNewRequest(fidl::InterfaceRequest<fuchsia::camera3::Stream> request) {
  TRACE_DURATION("camera", "StreamImpl::OnNewRequest");
  auto client = std::make_unique<Client>(*this, client_id_next_, std::move(request));
  client->ReceiveResolution(current_resolution_);
  client->ReceiveCropRegion(nullptr);
  clients_.emplace(client_id_next_++, std::move(client));
}

void StreamImpl::OnLegacyStreamDisconnected(zx_status_t status) {
  FX_PLOGS(ERROR, status) << description_ << ":Legacy Stream disconnected unexpectedly.";
  clients_.clear();
  on_no_clients_();
}

void StreamImpl::RemoveClient(uint64_t id) {
  TRACE_DURATION("camera", "StreamImpl::RemoveClient");
  clients_.erase(id);
  if (clients_.empty()) {
    on_no_clients_();
  }
}

// A new incoming buffer (already processed to be client-facing format) is available to be sent.
// Enqueue this buffer into the outbounding queue, creating the necessary fence event pair.
void StreamImpl::OnFrameAvailable(fuchsia::camera::FrameAvailableEvent info) {
  TRACE_DURATION("camera", "StreamImpl::OnFrameAvailable");

  if (info.frame_status != fuchsia::camera::FrameStatus::OK) {
    FX_LOGS(WARNING) << description_
                     << ": Driver reported a bad frame. This will not be reported to clients.";
    return;
  }

  if (frame_waiters_.find(info.buffer_id) != frame_waiters_.end()) {
    FX_LOGS(WARNING) << description_
                     << ": Driver sent a frame that was already in use (ID = " << info.buffer_id
                     << "). This frame will not be sent to clients.";
    return;
  }

  // Faking timestamp here.
  // TODO(ernesthua) - Need to add timestamps in pipeline (maybe down in usb_video) on merge back.
  // Seems inappropriate to make up one here.
  uint64_t capture_timestamp = static_cast<uint64_t>(zx_clock_get_monotonic());

  // The frame is valid and camera is unmuted, so increment the frame counter.
  ++frame_counter_;

  // Discard the frame if there are too many frames outstanding.
  // TODO(fxbug.dev/64801): Recycle LRU frames.
  if (frame_waiters_.size() == max_camping_buffers_) {
    // record_.FrameDropped(cobalt::FrameDropReason::kTooManyFramesInFlight);
    ReleaseClientFrame(info.buffer_id);
    FX_LOGS(WARNING) << description_ << ": Max camping buffers!";
    return;
  }

  // Construct the frame info and create a release fence per client.
  std::vector<zx::eventpair> fences;
  for (auto& [id, client] : clients_) {
    if (!client->Participant()) {
      continue;
    }
    zx::eventpair fence;
    zx::eventpair release_fence;
    ZX_ASSERT(zx::eventpair::create(0u, &fence, &release_fence) == ZX_OK);
    fences.push_back(std::move(fence));
    fuchsia::camera3::FrameInfo2 frame;
    frame.set_buffer_index(info.buffer_id);
    frame.set_frame_counter(frame_counter_);
    frame.set_timestamp(capture_timestamp);
    frame.set_capture_timestamp(capture_timestamp);
    frame.set_release_fence(std::move(release_fence));
    client->AddFrame(std::move(frame));
  }

  // No participating clients exist. Release the frame immediately.
  if (fences.empty()) {
    return;
  }

  // Queue a waiter so that when the client end of the fence is released, the frame is released back
  // to the client-facing pool.
  // ZX_ASSERT(frame_waiters_.size() <= max_camping_buffers_);
  frame_waiters_[info.buffer_id] =
      std::make_unique<FrameWaiter>(dispatcher_, std::move(fences), [this, index = info.buffer_id] {
        ReleaseClientFrame(index);
        frame_waiters_.erase(index);
      });
}

void StreamImpl::SetBufferCollection(
    uint64_t id, fidl::InterfaceHandle<fuchsia::sysmem::BufferCollectionToken> token_handle) {
  TRACE_DURATION("camera", "StreamImpl::SetBufferCollection");
  auto it = clients_.find(id);
  if (it == clients_.end()) {
    FX_LOGS(ERROR) << description_ << ": Client " << id << " not found.";
    if (token_handle) {
      token_handle.BindSync()->Close();
    }
    ZX_DEBUG_ASSERT(false);
    return;
  }

  // Decide whether we are a parctipant or not.
  auto& client = it->second;
  client->Participant() = !!token_handle;

  if (token_handle) {
    InitializeClientBufferCollection(std::move(token_handle));
  }
}

void StreamImpl::InitializeClientBufferCollection(
    fidl::InterfaceHandle<fuchsia::sysmem::BufferCollectionToken> token_handle) {
  fuchsia::sysmem::BufferCollectionTokenPtr token;
  token.Bind(std::move(token_handle));
  token->Sync([this, token = std::move(token)]() mutable {
    InitializeClientSharedCollection(std::move(token));
  });
}

void StreamImpl::OnClientBufferCollectionError(zx_status_t status) {
  // TODO(ernesthua) - Need to handle this error path.
}

void StreamImpl::InitializeClientSharedCollection(fuchsia::sysmem::BufferCollectionTokenPtr token) {
  std::map<uint64_t, fuchsia::sysmem::BufferCollectionTokenHandle> client_tokens;
  for (auto& client_i : clients_) {
    if (client_i.second->Participant()) {
      token->Duplicate(ZX_RIGHT_SAME_RIGHTS, client_tokens[client_i.first].NewRequest());
    }
  }
  for (auto& [id, token] : client_tokens) {
    auto it = clients_.find(id);
    if (it == clients_.end()) {
      token.BindSync()->Close();
    } else {
      it->second->ReceiveBufferCollection(std::move(token));
    }
  }
  AllocatorBindSharedCollection(std::move(token), client_buffer_collection_.NewRequest());
  client_buffer_collection_.set_error_handler(
      fit::bind_member(this, &StreamImpl::OnClientBufferCollectionError));
  constexpr uint32_t kNamePriority = 30;
  std::string name("fake");
  client_buffer_collection_->SetName(kNamePriority, std::move(name));
  client_buffer_collection_->Sync([this]() { SetClientBufferCollectionConstraints(); });
}

void StreamImpl::SetClientBufferCollectionConstraints() {
  fuchsia::sysmem::BufferCollectionConstraints buffer_collection_constraints;
  UvcHackGetClientBufferCollectionConstraints(&buffer_collection_constraints);
  client_buffer_collection_->SetConstraints(true, std::move(buffer_collection_constraints));
  client_buffer_collection_->WaitForBuffersAllocated(
      [this](zx_status_t status, fuchsia::sysmem::BufferCollectionInfo_2 buffer_collection_info) {
        if (status != ZX_OK) {
          FX_PLOGS(ERROR, status) << "Failed to allocate sysmem buffer.";
          ZX_ASSERT(false);
        }
        InitializeClientBuffers(std::move(buffer_collection_info));
      });
}

void StreamImpl::InitializeClientBuffers(
    fuchsia::sysmem::BufferCollectionInfo_2 buffer_collection_info) {
  // Map all client buffers.
  {
    for (uint32_t buffer_id = 0; buffer_id < buffer_collection_info.buffer_count; buffer_id++) {
      // TODO(b/204456599) - Use VmoMapper helper class instead.
      const zx::vmo& vmo = buffer_collection_info.buffers[buffer_id].vmo;
      auto vmo_size = buffer_collection_info.settings.buffer_settings.size_bytes;
      uintptr_t vmo_virt_addr = 0;
      auto status =
          zx::vmar::root_self()->map(ZX_VM_PERM_READ | ZX_VM_PERM_WRITE, 0 /* vmar_offset */, vmo,
                                     0 /* vmo_offset */, vmo_size, &vmo_virt_addr);
      ZX_ASSERT(status == ZX_OK);
      client_buffer_id_to_virt_addr_[buffer_id] = vmo_virt_addr;
    }
  }

  // Initially place all buffers into free pool.
  {
    for (uint32_t buffer_id = 0; buffer_id < buffer_collection_info.buffer_count; buffer_id++) {
      ReleaseClientFrame(buffer_id);
    }
  }

  // Remember this buffer collection - must survive until no longer in use.
  client_buffer_collection_info_ = std::move(buffer_collection_info);

  // Kick off server-facing buffer collection allocation.
  AllocateDriverBufferCollection();
}

void StreamImpl::AllocateDriverBufferCollection() {
  fuchsia::camera::VideoFormat video_format;
  UvcHackGetServerBufferVideoFormat(&video_format);
  auto buffer_or = Gralloc(std::move(video_format), 8);
  if (buffer_or.is_error()) {
    FX_LOGS(ERROR) << "Couldn't allocate. status: " << buffer_or.error_value();
    return;
  }

  fuchsia::sysmem::BufferCollectionInfo buffer_collection_info = std::move(*buffer_or);

  // Map all driver buffers.
  {
    for (uint32_t buffer_id = 0; buffer_id < buffer_collection_info.buffer_count; buffer_id++) {
      // TODO(b/204456599) - Use VmoMapper helper class instead.
      const zx::vmo& vmo = buffer_collection_info.vmos[buffer_id];
      auto vmo_size = buffer_collection_info.vmo_size;
      uintptr_t vmo_virt_addr = 0;
      auto status =
          zx::vmar::root_self()->map(ZX_VM_PERM_READ | ZX_VM_PERM_WRITE, 0 /* vmar_offset */, vmo,
                                     0 /* vmo_offset */, vmo_size, &vmo_virt_addr);
      ZX_ASSERT(status == ZX_OK);
      driver_buffer_id_to_virt_addr_[buffer_id] = vmo_virt_addr;
      uint64_t offset = 0;
      status = vmo.op_range(ZX_VMO_OP_CACHE_CLEAN_INVALIDATE, offset, vmo_size, nullptr, 0);
      if (status != ZX_OK) {
        FX_LOGS(ERROR) << "vmo.op_range() failed";
      }
    }
  }

  ConnectAndStartStream(std::move(buffer_collection_info));
}

void StreamImpl::ConnectAndStartStream(
    fuchsia::sysmem::BufferCollectionInfo buffer_collection_info) {
  // Connect to stream.
  fuchsia::camera::FrameRate frame_rate;
  UvcHackGetServerFrameRate(&frame_rate);
  ConnectToStream(std::move(buffer_collection_info), std::move(frame_rate));

  // Start up the stream.
  StartStreaming();
}

void StreamImpl::ConnectToStream(fuchsia::sysmem::BufferCollectionInfo buffer_collection_info,
                                 fuchsia::camera::FrameRate frame_rate) {
  // Create event pair to know if we or the driver disconnects.
  zx::eventpair driver_token;
  auto status = zx::eventpair::create(0, &stream_token_, &driver_token);
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "Couldn't create driver token. status: " << status;
    return;
  }

  // Ask device side to connect to stream using given BufferCollectionInfo.
  fuchsia::camera::StreamHandle stream_handle;
  on_stream_requested_(std::move(buffer_collection_info), std::move(frame_rate),
                       stream_handle.NewRequest(), std::move(driver_token));
  stream_ = stream_handle.Bind();

  // Install frame call back.
  stream_.events().OnFrameAvailable = [this](fuchsia::camera::FrameAvailableEvent frame) {
    if (frame.frame_status != fuchsia::camera::FrameStatus::OK) {
      FX_LOGS(ERROR) << "Error set on incoming frame. Error: "
                     << static_cast<int>(frame.frame_status);
      return;
    }

    // Convert the frame for sending to the client.
    ProcessFrameForSend(frame.buffer_id);

    // Immediately return driver-facing buffer to free pool since it has been processed already.
    stream_->ReleaseFrame(frame.buffer_id);
  };

  // TODO(ernesthua): Should wait_one for SIGNALED | PEER_CLOSED here to tolerate driver closing the
  // connection.
}

void StreamImpl::StartStreaming() {
  if (streaming_) {
    FX_LOGS(WARNING) << "Already started streaming!";
    return;
  }
  streaming_ = true;
  stream_->Start();
}

void StreamImpl::StopStreaming() {
  if (!streaming_) {
    FX_LOGS(WARNING) << "Already stopped streaming!";
    return;
  }
  streaming_ = false;
  stream_->Stop();

  stream_ = nullptr;

  // Wait until Stop request completed/frames drain (PEER_CLOSED is signalled on the stream_token_).
  zx_signals_t peer_closed_signal = ZX_EVENTPAIR_PEER_CLOSED;
  zx_signals_t observed;
  auto status = zx_object_wait_one(stream_token_.get(), peer_closed_signal,
                                   zx_deadline_after(ZX_MSEC(3000)), &observed);
  if (status == ZX_OK) {
    ZX_ASSERT(observed == ZX_EVENTPAIR_PEER_CLOSED);
  } else if (status == ZX_ERR_TIMED_OUT) {
    FX_LOGS(ERROR) << "Timed out waiting for stream Stop request to complete!";
  } else {
    FX_LOGS(ERROR) << "Saw unexpected error while waiting for stream Stop request to complete: "
                   << status;
  }
}

void StreamImpl::AllocatorBindSharedCollection(
    fidl::InterfaceHandle<fuchsia::sysmem::BufferCollectionToken> token_handle,
    fidl::InterfaceRequest<fuchsia::sysmem::BufferCollection> request) {
  allocator_bind_shared_collection_(std::move(token_handle), std::move(request));
}

// Convert the incoming frame to the proper outgoing format. Caller is responsible for releasing the
// buffer back to the driver-facing buffer pool.
void StreamImpl::ProcessFrameForSend(uint32_t driver_buffer_id) {
  // Make sure driver_buffer_id valid.
  auto driver_it = driver_buffer_id_to_virt_addr_.find(driver_buffer_id);
  ZX_ASSERT(driver_it != driver_buffer_id_to_virt_addr_.end());

  // Grab a free client-facing buffer.
  uint32_t client_buffer_id = client_buffer_free_queue_.front();
  client_buffer_free_queue_.pop();

  // Make sure client_buffer_id valid.
  auto client_it = client_buffer_id_to_virt_addr_.find(client_buffer_id);
  ZX_ASSERT(client_it != client_buffer_id_to_virt_addr_.end());

  // Warning! Grotesquely hard coded for YUY2 server side & NV12 client side.
  uintptr_t client_frame_ptr = client_buffer_id_to_virt_addr_[client_buffer_id];
  uint8_t* client_frame = reinterpret_cast<uint8_t*>(client_frame_ptr);
  uintptr_t driver_frame_ptr = driver_buffer_id_to_virt_addr_[driver_buffer_id];
  uint8_t* driver_frame = reinterpret_cast<uint8_t*>(driver_frame_ptr);
  UvcHackConvertYUY2ToNV12(client_frame, driver_frame);

  // Construct frame metadata.
  fuchsia::camera::FrameAvailableEvent info;
  info.frame_status = fuchsia::camera::FrameStatus::OK;
  info.buffer_id = client_buffer_id;
  info.metadata.timestamp = static_cast<uint64_t>(zx_clock_get_monotonic());
  OnFrameAvailable(std::move(info));
}

void StreamImpl::ReleaseClientFrame(uint32_t buffer_id) {
  client_buffer_free_queue_.push(buffer_id);
}

}  // namespace camera
