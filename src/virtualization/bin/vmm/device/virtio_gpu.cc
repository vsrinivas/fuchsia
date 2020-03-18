// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/ui/policy/cpp/fidl.h>
#include <fuchsia/ui/scenic/cpp/fidl.h>
#include <fuchsia/ui/views/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fit/defer.h>
#include <lib/trace-provider/provider.h>
#include <lib/ui/scenic/cpp/session.h>
#include <lib/ui/scenic/cpp/view_token_pair.h>

#include <unordered_map>

#include <virtio/gpu.h>

#include "src/virtualization/bin/vmm/device/device_base.h"
#include "src/virtualization/bin/vmm/device/gpu_resource.h"
#include "src/virtualization/bin/vmm/device/gpu_scanout.h"
#include "src/virtualization/bin/vmm/device/guest_view.h"
#include "src/virtualization/bin/vmm/device/stream_base.h"

#define CHECK_LEN_OR_CONTINUE(request_type, response_type)                           \
  if (request_len < sizeof(request_type) || response_len < sizeof(response_type)) {  \
    FX_LOGS(ERROR) << "Invalid GPU control command 0x" << std::hex << request->type; \
    continue;                                                                        \
  }                                                                                  \
  *Used() += sizeof(response_type)

#define GET_RESOURCE_OR_RETURN(resource)                      \
  auto it = resources_.find(request->resource_id);            \
  if (it == resources_.end()) {                               \
    response->type = VIRTIO_GPU_RESP_ERR_INVALID_RESOURCE_ID; \
    return;                                                   \
  }                                                           \
  auto& resource = it->second

using GpuResourceMap = std::unordered_map<uint32_t, GpuResource>;

enum class Queue : uint16_t {
  CONTROL = 0,
  CURSOR = 1,
};

// Stream for control queue.
class ControlStream : public StreamBase {
 public:
  ControlStream(GpuScanout* scanout, GpuResourceMap* resources)
      : scanout_(*scanout), resources_(*resources) {}

  void Init(const PhysMem& phys_mem, VirtioQueue::InterruptFn interrupt) {
    phys_mem_ = &phys_mem;
    StreamBase::Init(phys_mem, std::move(interrupt));
  }

  void DoControl() {
    for (; queue_.NextChain(&chain_); chain_.Return()) {
      if (!chain_.NextDescriptor(&desc_)) {
        FX_LOGS(ERROR) << "GPU control command is missing request";
        continue;
      }
      const auto request = static_cast<virtio_gpu_ctrl_hdr_t*>(desc_.addr);
      const uint32_t request_len = desc_.len;
      if (!chain_.NextDescriptor(&desc_)) {
        FX_LOGS(ERROR) << "GPU control command is missing response";
        continue;
      }
      auto response = static_cast<virtio_gpu_ctrl_hdr_t*>(desc_.addr);
      const uint32_t response_len = desc_.len;

      // Virtio 1.0 (GPU) Section 5.7.6.7:
      //
      // If the driver sets the VIRTIO_GPU_FLAG_FENCE bit in the request flags
      // field the device MUST:
      //
      // * set VIRTIO_GPU_FLAG_FENCE bit in the response,
      // * copy the content of the fence_id field from the request to the
      //   response, and
      // * send the response only after command processing is complete.
      //
      // NOTE: The control stream runs sequentially so fences are enforced.
      if (request->flags & VIRTIO_GPU_FLAG_FENCE) {
        response->flags |= VIRTIO_GPU_FLAG_FENCE;
        response->fence_id = request->fence_id;
      }

      switch (request->type) {
        case VIRTIO_GPU_CMD_GET_DISPLAY_INFO:
          CHECK_LEN_OR_CONTINUE(virtio_gpu_ctrl_hdr_t, virtio_gpu_resp_display_info_t);
          GetDisplayInfo(request, reinterpret_cast<virtio_gpu_resp_display_info_t*>(response));
          break;
        case VIRTIO_GPU_CMD_RESOURCE_CREATE_2D:
          CHECK_LEN_OR_CONTINUE(virtio_gpu_resource_create_2d_t, virtio_gpu_ctrl_hdr_t);
          ResourceCreate2d(reinterpret_cast<const virtio_gpu_resource_create_2d_t*>(request),
                           response);
          break;
        case VIRTIO_GPU_CMD_RESOURCE_UNREF:
          CHECK_LEN_OR_CONTINUE(virtio_gpu_resource_unref_t, virtio_gpu_ctrl_hdr_t);
          ResourceUnref(reinterpret_cast<const virtio_gpu_resource_unref_t*>(request), response);
          break;
        case VIRTIO_GPU_CMD_SET_SCANOUT:
          CHECK_LEN_OR_CONTINUE(virtio_gpu_set_scanout_t, virtio_gpu_ctrl_hdr_t);
          SetScanout(reinterpret_cast<const virtio_gpu_set_scanout_t*>(request), response);
          break;
        case VIRTIO_GPU_CMD_RESOURCE_FLUSH:
          CHECK_LEN_OR_CONTINUE(virtio_gpu_resource_flush_t, virtio_gpu_ctrl_hdr_t);
          ResourceFlush(reinterpret_cast<const virtio_gpu_resource_flush_t*>(request), response);
          break;
        case VIRTIO_GPU_CMD_TRANSFER_TO_HOST_2D:
          CHECK_LEN_OR_CONTINUE(virtio_gpu_transfer_to_host_2d_t, virtio_gpu_ctrl_hdr_t);
          TransferToHost2d(reinterpret_cast<const virtio_gpu_transfer_to_host_2d_t*>(request),
                           response);
          break;
        case VIRTIO_GPU_CMD_RESOURCE_ATTACH_BACKING:
          CHECK_LEN_OR_CONTINUE(virtio_gpu_resource_attach_backing_t, virtio_gpu_ctrl_hdr_t);
          ResourceAttachBacking(
              reinterpret_cast<const virtio_gpu_resource_attach_backing_t*>(request), response,
              request_len - sizeof(virtio_gpu_ctrl_hdr_t));
          break;
        case VIRTIO_GPU_CMD_RESOURCE_DETACH_BACKING:
          CHECK_LEN_OR_CONTINUE(virtio_gpu_resource_detach_backing_t, virtio_gpu_ctrl_hdr_t);
          ResourceDetachBacking(
              reinterpret_cast<const virtio_gpu_resource_detach_backing_t*>(request), response);
          break;
        default:
          FX_LOGS(ERROR) << "Unknown GPU control command 0x" << std::hex << request->type;
          *Used() += sizeof(*response);
          response->type = VIRTIO_GPU_RESP_ERR_UNSPEC;
          break;
      }
    }
  }

 private:
  GpuScanout& scanout_;
  GpuResourceMap& resources_;
  const PhysMem* phys_mem_;

  void GetDisplayInfo(const virtio_gpu_ctrl_hdr_t* request,
                      virtio_gpu_resp_display_info_t* response) {
    response->pmodes[0] = {
        .r = scanout_.extents(),
        .enabled = 1,
        .flags = 0,
    };
    response->hdr.type = VIRTIO_GPU_RESP_OK_DISPLAY_INFO;
    *Used() += sizeof(*response);
  }

  void ResourceCreate2d(const virtio_gpu_resource_create_2d_t* request,
                        virtio_gpu_ctrl_hdr_t* response) {
    GpuResource resource(*phys_mem_, request->format, request->width, request->height);
    resources_.insert_or_assign(request->resource_id, std::move(resource));
    response->type = VIRTIO_GPU_RESP_OK_NODATA;
  }

  void ResourceUnref(const virtio_gpu_resource_unref_t* request, virtio_gpu_ctrl_hdr_t* response) {
    size_t num_erased = resources_.erase(request->resource_id);
    if (num_erased == 0) {
      response->type = VIRTIO_GPU_RESP_ERR_INVALID_RESOURCE_ID;
      return;
    }
    response->type = VIRTIO_GPU_RESP_OK_NODATA;
  }

  void SetScanout(const virtio_gpu_set_scanout_t* request, virtio_gpu_ctrl_hdr_t* response) {
    if (request->resource_id == 0) {
      // Resource ID 0 is a special case and means the provided scanout should
      // be disabled.
      scanout_.OnSetScanout(nullptr, {});
      response->type = VIRTIO_GPU_RESP_OK_NODATA;
      return;
    } else if (request->scanout_id != 0) {
      // Only a single scanout is supported.
      response->type = VIRTIO_GPU_RESP_ERR_INVALID_SCANOUT_ID;
      return;
    }

    GET_RESOURCE_OR_RETURN(resource);
    scanout_.OnSetScanout(&resource, request->r);
    response->type = VIRTIO_GPU_RESP_OK_NODATA;
  }

  void ResourceFlush(const virtio_gpu_resource_flush_t* request, virtio_gpu_ctrl_hdr_t* response) {
    GET_RESOURCE_OR_RETURN(resource);
    scanout_.OnResourceFlush(&resource, request->r);
    response->type = VIRTIO_GPU_RESP_OK_NODATA;
  }

  void TransferToHost2d(const virtio_gpu_transfer_to_host_2d_t* request,
                        virtio_gpu_ctrl_hdr_t* response) {
    GET_RESOURCE_OR_RETURN(resource);
    resource.TransferToHost2d(request->r, request->offset);
    response->type = VIRTIO_GPU_RESP_OK_NODATA;
  }

  void ResourceAttachBacking(const virtio_gpu_resource_attach_backing_t* request,
                             virtio_gpu_ctrl_hdr_t* response, uint32_t extra_len) {
    // Entries may be stored in the next descriptor.
    const virtio_gpu_mem_entry_t* mem_entries;
    if (chain_.NextDescriptor(&desc_)) {
      mem_entries = reinterpret_cast<const virtio_gpu_mem_entry_t*>(response);
      response = static_cast<virtio_gpu_ctrl_hdr_t*>(desc_.addr);
    } else if (extra_len >= request->nr_entries * sizeof(virtio_gpu_mem_entry_t)) {
      mem_entries = reinterpret_cast<const virtio_gpu_mem_entry_t*>(request + 1);
    } else {
      FX_LOGS(ERROR) << "Invalid GPU memory entries command";
      response->type = VIRTIO_GPU_RESP_ERR_INVALID_PARAMETER;
      return;
    }

    GET_RESOURCE_OR_RETURN(resource);
    resource.AttachBacking(mem_entries, request->nr_entries);
    response->type = VIRTIO_GPU_RESP_OK_NODATA;
  }

  void ResourceDetachBacking(const virtio_gpu_resource_detach_backing_t* request,
                             virtio_gpu_ctrl_hdr_t* response) {
    GET_RESOURCE_OR_RETURN(resource);
    resource.DetachBacking();
    response->type = VIRTIO_GPU_RESP_OK_NODATA;
  }
};

// Stream for cursor queue.
class CursorStream : public StreamBase {
 public:
  CursorStream(GpuScanout* scanout, GpuResourceMap* resources)
      : scanout_(*scanout), resources_(*resources) {}

  void DoCursor() {
    for (; queue_.NextChain(&chain_); chain_.Return()) {
      if (!chain_.NextDescriptor(&desc_) || desc_.len != sizeof(virtio_gpu_ctrl_hdr_t)) {
        continue;
      }
      // In the Linux driver, cursor commands do not send a response.
      const auto request = static_cast<virtio_gpu_ctrl_hdr_t*>(desc_.addr);

      switch (request->type) {
        case VIRTIO_GPU_CMD_UPDATE_CURSOR:
          UpdateCursor(reinterpret_cast<const virtio_gpu_update_cursor_t*>(request));
          // fall-through
        case VIRTIO_GPU_CMD_MOVE_CURSOR:
          MoveCursor(reinterpret_cast<const virtio_gpu_update_cursor_t*>(request));
          break;
        default:
          FX_LOGS(ERROR) << "Unknown GPU cursor command 0x" << std::hex << request->type;
          break;
      }
    }
  }

 private:
  GpuScanout& scanout_;
  GpuResourceMap& resources_;

  void UpdateCursor(const virtio_gpu_update_cursor_t* request) {
    if (request->resource_id == 0) {
      scanout_.OnUpdateCursor(nullptr, 0, 0);
      return;
    }

    auto it = resources_.find(request->resource_id);
    if (it == resources_.end()) {
      return;
    }
    scanout_.OnUpdateCursor(&it->second, request->hot_x, request->hot_y);
  }

  void MoveCursor(const virtio_gpu_update_cursor_t* request) {
    auto it = resources_.find(request->resource_id);
    if (it == resources_.end() || request->pos.scanout_id != 0) {
      return;
    }
    scanout_.OnMoveCursor(request->pos.x, request->pos.y);
  }
};

// Implementation of a virtio-gpu device.
class VirtioGpuImpl : public DeviceBase<VirtioGpuImpl>,
                      public fuchsia::virtualization::hardware::VirtioGpu {
 public:
  VirtioGpuImpl(sys::ComponentContext* context) : DeviceBase(context), context_(*context) {
    scanout_.SetConfigChangedHandler(fit::bind_member(this, &VirtioGpuImpl::OnConfigChanged));
  }

  // |fuchsia::virtualization::hardware::VirtioDevice|
  void NotifyQueue(uint16_t queue) override {
    switch (static_cast<Queue>(queue)) {
      case Queue::CONTROL:
        control_stream_.DoControl();
        break;
      case Queue::CURSOR:
        cursor_stream_.DoCursor();
        break;
      default:
        FX_CHECK(false) << "Queue index " << queue << " out of range";
        __UNREACHABLE;
    }
  }

 private:
  // |fuchsia::virtualization::hardware::VirtioGpu|
  void Start(fuchsia::virtualization::hardware::StartInfo start_info,
             fidl::InterfaceHandle<fuchsia::virtualization::hardware::ViewListener> view_listener,
             StartCallback callback) override {
    auto deferred = fit::defer(std::move(callback));
    PrepStart(std::move(start_info));

    if (view_listener) {
      auto [view_token, view_holder_token] = scenic::ViewTokenPair::New();

      // Create view.
      auto scenic = context_.svc()->Connect<fuchsia::ui::scenic::Scenic>();
      scenic::ViewContext view_context = {
          .session_and_listener_request =
              scenic::CreateScenicSessionPtrAndListenerRequest(scenic.get()),
          .view_token = std::move(view_token),
          .component_context = &context_,
      };
      view_ =
          std::make_unique<GuestView>(std::move(view_context), std::move(view_listener), &scanout_);
      view_->SetReleaseHandler([this](zx_status_t status) { view_.reset(); });

      // Present view.
      auto presenter = context_.svc()->Connect<fuchsia::ui::policy::Presenter>();
      presenter->PresentView(std::move(view_holder_token), nullptr);
    }

    // Initialize streams.
    control_stream_.Init(
        phys_mem_, fit::bind_member<zx_status_t, DeviceBase>(this, &VirtioGpuImpl::Interrupt));
    cursor_stream_.Init(phys_mem_,
                        fit::bind_member<zx_status_t, DeviceBase>(this, &VirtioGpuImpl::Interrupt));
  }

  // |fuchsia::virtualization::hardware::VirtioDevice|
  void ConfigureQueue(uint16_t queue, uint16_t size, zx_gpaddr_t desc, zx_gpaddr_t avail,
                      zx_gpaddr_t used, ConfigureQueueCallback callback) override {
    auto deferred = fit::defer(std::move(callback));
    switch (static_cast<Queue>(queue)) {
      case Queue::CONTROL:
        control_stream_.Configure(size, desc, avail, used);
        break;
      case Queue::CURSOR:
        cursor_stream_.Configure(size, desc, avail, used);
        break;
      default:
        FX_CHECK(false) << "Queue index " << queue << " out of range";
        __UNREACHABLE;
    }
  }

  // |fuchsia::virtualization::hardware::VirtioDevice|
  void Ready(uint32_t negotiated_features, ReadyCallback callback) override { callback(); }

  void OnConfigChanged() {
    for (auto& binding : bindings_.bindings()) {
      binding->events().OnConfigChanged();
    }
  }

  sys::ComponentContext& context_;
  std::unique_ptr<GuestView> view_;
  GpuScanout scanout_;
  GpuResourceMap resources_;
  ControlStream control_stream_{&scanout_, &resources_};
  CursorStream cursor_stream_{&scanout_, &resources_};
};

int main(int argc, char** argv) {
  syslog::InitLogger({"virtio_gpu"});

  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  trace::TraceProviderWithFdio trace_provider(loop.dispatcher());
  std::unique_ptr<sys::ComponentContext> context = sys::ComponentContext::Create();

  VirtioGpuImpl virtio_gpu(context.get());
  return loop.Run();
}
