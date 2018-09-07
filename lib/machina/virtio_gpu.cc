// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/machina/virtio_gpu.h"

#include <string.h>

#include <fbl/intrusive_hash_table.h>
#include <fbl/unique_ptr.h>
#include <trace/event.h>

#include "garnet/lib/machina/gpu_resource.h"
#include "garnet/lib/machina/gpu_scanout.h"
#include "lib/fxl/logging.h"

namespace machina {

bool Overlaps(virtio_gpu_rect_t a, virtio_gpu_rect_t b) {
  if (a.x > (b.x + b.width) || b.x > (a.x + a.width)) {
    return false;
  }
  if (a.y > (b.y + b.height) || b.y > (a.y + a.height)) {
    return false;
  }
  return true;
}

virtio_gpu_rect_t Clip(virtio_gpu_rect_t rect, virtio_gpu_rect_t clip) {
  if (rect.x < clip.x) {
    rect.width -= clip.x - rect.x;
    rect.x = clip.x;
  }
  if (rect.y < clip.y) {
    rect.height -= clip.y - rect.y;
    rect.y = clip.y;
  }
  if (rect.x + rect.width > clip.x + clip.width) {
    rect.width = clip.x + clip.width - rect.x;
  }
  if (rect.y + rect.height > clip.y + clip.height) {
    rect.height = clip.y + clip.height - rect.y;
  }
  return rect;
}

VirtioGpu::VirtioGpu(const PhysMem& phys_mem, async_dispatcher_t* dispatcher)
    : VirtioInprocessDevice(phys_mem, 0 /* device_features */,
                            noop_config_device,
                            fit::bind_member(this, &VirtioGpu::OnDeviceReady)),
      scanout_(this),
      dispatcher_(dispatcher) {}

VirtioGpu::~VirtioGpu() = default;

zx_status_t VirtioGpu::Init() {
  zx_status_t status = control_queue()->PollAsync(
      dispatcher_, &control_queue_wait_,
      fit::bind_member(this, &VirtioGpu::HandleGpuCommand));
  if (status == ZX_OK) {
    status = cursor_queue()->PollAsync(
        dispatcher_, &cursor_queue_wait_,
        fit::bind_member(this, &VirtioGpu::HandleGpuCommand));
  }
  std::lock_guard<std::mutex> lock(device_config_.mutex);
  config_.num_scanouts = 1;
  return status;
}

zx_status_t VirtioGpu::OnDeviceReady(uint32_t negotiated_features) {
  ready_ = true;
  return ZX_OK;
}

zx_status_t VirtioGpu::NotifyGuestScanoutsChanged() {
  std::lock_guard<std::mutex> lock(device_config_.mutex);
  if (ready_) {
    config_.events_read |= VIRTIO_GPU_EVENT_DISPLAY;
    return Interrupt(VirtioQueue::SET_CONFIG | VirtioQueue::TRY_INTERRUPT);
  }
  return ZX_OK;
}

namespace {

// Returns a string representation of the given virtio_gpu_ctrl_type command.
const char* command_to_string(uint32_t command) {
  switch (command) {
    case VIRTIO_GPU_CMD_GET_DISPLAY_INFO:
      return "get_display_info";
    case VIRTIO_GPU_CMD_RESOURCE_CREATE_2D:
      return "resource_create_2d";
    case VIRTIO_GPU_CMD_RESOURCE_UNREF:
      return "resource_unref";
    case VIRTIO_GPU_CMD_SET_SCANOUT:
      return "set_scanout";
    case VIRTIO_GPU_CMD_RESOURCE_FLUSH:
      return "resource_flush";
    case VIRTIO_GPU_CMD_TRANSFER_TO_HOST_2D:
      return "transfer_to_host_2d";
    case VIRTIO_GPU_CMD_RESOURCE_ATTACH_BACKING:
      return "resource_attach_backing";
    case VIRTIO_GPU_CMD_RESOURCE_DETACH_BACKING:
      return "resource_detach_backing";
    case VIRTIO_GPU_CMD_UPDATE_CURSOR:
      return "update_cursor";
    case VIRTIO_GPU_CMD_MOVE_CURSOR:
      return "move_cursor";
    default:
      return "[unknown]";
  }
}

}  // namespace

zx_status_t VirtioGpu::HandleGpuCommand(VirtioQueue* queue, uint16_t head,
                                        uint32_t* used) {
  VirtioDescriptor request_desc;
  zx_status_t status = queue->ReadDesc(head, &request_desc);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to read descriptor";
    return status;
  }
  const auto request_header =
      reinterpret_cast<virtio_gpu_ctrl_hdr_t*>(request_desc.addr);
  const uint32_t command_type = request_header->type;
  const char* command_label = command_to_string(command_type);

  // Attempt to correlate the processing of descriptors with a previous
  // notification. As noted in virtio_device.cc this should be considered
  // best-effort only.
  const uint16_t sel =
      queue == control_queue() ? VIRTIO_GPU_Q_CONTROLQ : VIRTIO_GPU_Q_CURSORQ;
  const char* queue_label = sel == VIRTIO_GPU_Q_CONTROLQ ? "control" : "cursor";
  const trace_async_id_t flow_id = trace_flow_id(sel)->exchange(0);
  TRACE_DURATION("machina", "virtio_gpu_command", "queue",
                 TA_STRING_LITERAL(queue_label), "command",
                 TA_STRING_LITERAL(command_label), "flow_id", flow_id);
  if (flow_id != 0) {
    TRACE_FLOW_END("machina", "queue_signal", flow_id);
  }

  // Cursor commands don't send a response (at least not in the linux driver).
  if (!request_desc.has_next && command_type != VIRTIO_GPU_CMD_MOVE_CURSOR &&
      command_type != VIRTIO_GPU_CMD_UPDATE_CURSOR) {
    FXL_LOG(ERROR) << "GPU command "
                   << "'" << command_label << "' (" << command_type << ") "
                   << "does not contain a response descriptor";
    return ZX_OK;
  }

  virtio_gpu_ctrl_hdr_t* response_header = nullptr;
  switch (command_type) {
    case VIRTIO_GPU_CMD_GET_DISPLAY_INFO: {
      VirtioDescriptor response_desc;
      status = queue->ReadDesc(request_desc.next, &response_desc);
      if (status != ZX_OK) {
        FXL_LOG(ERROR) << "Failed to read descriptor";
        return status;
      }
      auto request =
          reinterpret_cast<virtio_gpu_ctrl_hdr_t*>(request_desc.addr);
      auto response =
          reinterpret_cast<virtio_gpu_resp_display_info_t*>(response_desc.addr);
      response_header = &response->hdr;
      GetDisplayInfo(request, response);
      *used += sizeof(*response);
      break;
    }
    case VIRTIO_GPU_CMD_RESOURCE_CREATE_2D: {
      VirtioDescriptor response_desc;
      status = queue->ReadDesc(request_desc.next, &response_desc);
      if (status != ZX_OK) {
        FXL_LOG(ERROR) << "Failed to read descriptor";
        return status;
      }
      auto request =
          reinterpret_cast<virtio_gpu_resource_create_2d_t*>(request_desc.addr);
      auto response =
          reinterpret_cast<virtio_gpu_ctrl_hdr_t*>(response_desc.addr);
      response_header = response;
      ResourceCreate2D(request, response);
      *used += sizeof(*response);
      break;
    }
    case VIRTIO_GPU_CMD_SET_SCANOUT: {
      VirtioDescriptor response_desc;
      status = queue->ReadDesc(request_desc.next, &response_desc);
      if (status != ZX_OK) {
        FXL_LOG(ERROR) << "Failed to read descriptor";
        return status;
      }
      auto request =
          reinterpret_cast<virtio_gpu_set_scanout_t*>(request_desc.addr);
      auto response =
          reinterpret_cast<virtio_gpu_ctrl_hdr_t*>(response_desc.addr);
      response_header = response;
      SetScanout(request, response);
      *used += sizeof(*response);
      break;
    }
    case VIRTIO_GPU_CMD_RESOURCE_FLUSH: {
      VirtioDescriptor response_desc;
      status = queue->ReadDesc(request_desc.next, &response_desc);
      if (status != ZX_OK) {
        FXL_LOG(ERROR) << "Failed to read descriptor";
        return status;
      }
      auto request =
          reinterpret_cast<virtio_gpu_resource_flush_t*>(request_desc.addr);
      auto response =
          reinterpret_cast<virtio_gpu_ctrl_hdr_t*>(response_desc.addr);
      response_header = response;
      ResourceFlush(request, response);
      *used += sizeof(*response);
      break;
    }
    case VIRTIO_GPU_CMD_TRANSFER_TO_HOST_2D: {
      VirtioDescriptor response_desc;
      status = queue->ReadDesc(request_desc.next, &response_desc);
      if (status != ZX_OK) {
        FXL_LOG(ERROR) << "Failed to read descriptor";
        return status;
      }
      auto request = reinterpret_cast<virtio_gpu_transfer_to_host_2d_t*>(
          request_desc.addr);
      auto response =
          reinterpret_cast<virtio_gpu_ctrl_hdr_t*>(response_desc.addr);
      response_header = response;
      TransferToHost2D(request, response);
      *used += sizeof(*response);
      break;
    }
    case VIRTIO_GPU_CMD_RESOURCE_ATTACH_BACKING: {
      VirtioDescriptor response_desc;
      status = queue->ReadDesc(request_desc.next, &response_desc);
      if (status != ZX_OK) {
        FXL_LOG(ERROR) << "Failed to read descriptor";
        return status;
      }

      // This may or may not be on the same descriptor.
      virtio_gpu_mem_entry_t* mem_entries;
      if (response_desc.has_next) {
        mem_entries =
            reinterpret_cast<virtio_gpu_mem_entry_t*>(response_desc.addr);
        status = queue->ReadDesc(response_desc.next, &response_desc);
        if (status != ZX_OK) {
          FXL_LOG(ERROR) << "Failed to read descriptor";
          return status;
        }
      } else {
        uintptr_t addr = reinterpret_cast<uintptr_t>(request_desc.addr) +
                         sizeof(virtio_gpu_resource_attach_backing_t);
        mem_entries = reinterpret_cast<virtio_gpu_mem_entry_t*>(addr);
      }

      auto request = reinterpret_cast<virtio_gpu_resource_attach_backing_t*>(
          request_desc.addr);
      auto response =
          reinterpret_cast<virtio_gpu_ctrl_hdr_t*>(response_desc.addr);
      response_header = response;
      ResourceAttachBacking(request, mem_entries, response);
      *used += sizeof(*response);
      break;
    }
    case VIRTIO_GPU_CMD_RESOURCE_UNREF: {
      VirtioDescriptor response_desc;
      status = queue->ReadDesc(request_desc.next, &response_desc);
      if (status != ZX_OK) {
        FXL_LOG(ERROR) << "Failed to read descriptor";
        return status;
      }
      auto request =
          reinterpret_cast<virtio_gpu_resource_unref_t*>(request_desc.addr);
      auto response =
          reinterpret_cast<virtio_gpu_ctrl_hdr_t*>(response_desc.addr);
      response_header = response;
      ResourceUnref(request, response);
      *used += sizeof(*response);
      break;
    }
    case VIRTIO_GPU_CMD_RESOURCE_DETACH_BACKING: {
      VirtioDescriptor response_desc;
      status = queue->ReadDesc(request_desc.next, &response_desc);
      if (status != ZX_OK) {
        FXL_LOG(ERROR) << "Failed to read descriptor";
        return status;
      }
      auto request = reinterpret_cast<virtio_gpu_resource_detach_backing_t*>(
          request_desc.addr);
      auto response =
          reinterpret_cast<virtio_gpu_ctrl_hdr_t*>(response_desc.addr);
      response_header = response;
      ResourceDetachBacking(request, response);
      *used += sizeof(*response);
      break;
    }
    case VIRTIO_GPU_CMD_UPDATE_CURSOR: {
      auto request =
          reinterpret_cast<virtio_gpu_update_cursor_t*>(request_desc.addr);
      UpdateCursor(request);
      MoveCursor(request);
      *used = 0;
      break;
    }
    case VIRTIO_GPU_CMD_MOVE_CURSOR: {
      auto request =
          reinterpret_cast<virtio_gpu_update_cursor_t*>(request_desc.addr);
      MoveCursor(request);
      *used = 0;
      break;
    }
    default: {
      FXL_LOG(ERROR) << "Unsupported GPU command "
                     << "'" << command_label << "' (" << command_type << ")";
      // ACK.
      VirtioDescriptor response_desc;
      status = queue->ReadDesc(request_desc.next, &response_desc);
      if (status != ZX_OK) {
        FXL_LOG(ERROR) << "Failed to read descriptor";
        return status;
      }
      auto response =
          reinterpret_cast<virtio_gpu_ctrl_hdr_t*>(response_desc.addr);
      response_header = response;
      response->type = VIRTIO_GPU_RESP_ERR_UNSPEC;
      *used += sizeof(*response);
      break;
    }
  }
  if (response_header && request_header->flags & VIRTIO_GPU_FLAG_FENCE) {
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
    // Note: VirtioQueue::PollAsync runs sequentially so fences are naturally
    // enforced.
    response_header->flags |= VIRTIO_GPU_FLAG_FENCE;
    response_header->fence_id = request_header->fence_id;
  }
  return ZX_OK;
}

void VirtioGpu::GetDisplayInfo(const virtio_gpu_ctrl_hdr_t* request,
                               virtio_gpu_resp_display_info_t* response) {
  TRACE_DURATION("machina", "virtio_gpu_get_display_info");
  virtio_gpu_display_one_t* display = &response->pmodes[0];
  display->enabled = 1;
  display->r = scanout_.extents();
  response->hdr.type = VIRTIO_GPU_RESP_OK_DISPLAY_INFO;
}

void VirtioGpu::ResourceCreate2D(const virtio_gpu_resource_create_2d_t* request,
                                 virtio_gpu_ctrl_hdr_t* response) {
  TRACE_DURATION("machina", "virtio_gpu_resource_create_2d");
  std::unique_ptr<GpuResource> res;
  response->type = GpuResource::Create(&phys_mem_, request->format,
                                       request->width, request->height, &res);
  if (res) {
    resources_[request->resource_id] = std::move(res);
  }
}

void VirtioGpu::ResourceUnref(const virtio_gpu_resource_unref_t* request,
                              virtio_gpu_ctrl_hdr_t* response) {
  TRACE_DURATION("machina", "virtio_gpu_resource_unref");
  auto it = resources_.find(request->resource_id);
  if (it == resources_.end()) {
    response->type = VIRTIO_GPU_RESP_ERR_INVALID_RESOURCE_ID;
    return;
  }
  resources_.erase(it);
  response->type = VIRTIO_GPU_RESP_OK_NODATA;
}

void VirtioGpu::SetScanout(const virtio_gpu_set_scanout_t* request,
                           virtio_gpu_ctrl_hdr_t* response) {
  TRACE_DURATION("machina", "virtio_gpu_set_scanout");
  if (request->resource_id == 0) {
    // Resource ID 0 is a special case and means the provided scanout
    // should be disabled.
    scanout_.OnSetScanout(nullptr, {});
    response->type = VIRTIO_GPU_RESP_OK_NODATA;
    return;
  }
  if (request->scanout_id != 0) {
    // Only a single scanout is supported.
    response->type = VIRTIO_GPU_RESP_ERR_INVALID_SCANOUT_ID;
    return;
  }

  auto it = resources_.find(request->resource_id);
  if (it == resources_.end()) {
    response->type = VIRTIO_GPU_RESP_ERR_INVALID_RESOURCE_ID;
    return;
  }
  scanout_.OnSetScanout(it->second.get(), request->r);

  response->type = VIRTIO_GPU_RESP_OK_NODATA;
}

void VirtioGpu::ResourceAttachBacking(
    const virtio_gpu_resource_attach_backing_t* request,
    const virtio_gpu_mem_entry_t* mem_entries,
    virtio_gpu_ctrl_hdr_t* response) {
  TRACE_DURATION("machina", "virtio_gpu_resource_attach_backing");
  auto it = resources_.find(request->resource_id);
  if (it == resources_.end()) {
    response->type = VIRTIO_GPU_RESP_ERR_INVALID_RESOURCE_ID;
    return;
  }
  response->type = it->second->AttachBacking(mem_entries, request->nr_entries);
}

void VirtioGpu::ResourceDetachBacking(
    const virtio_gpu_resource_detach_backing_t* request,
    virtio_gpu_ctrl_hdr_t* response) {
  TRACE_DURATION("machina", "virtio_gpu_resource_detach_backing");
  auto it = resources_.find(request->resource_id);
  if (it == resources_.end()) {
    response->type = VIRTIO_GPU_RESP_ERR_INVALID_RESOURCE_ID;
    return;
  }
  response->type = it->second->DetachBacking();
}

void VirtioGpu::TransferToHost2D(
    const virtio_gpu_transfer_to_host_2d_t* request,
    virtio_gpu_ctrl_hdr_t* response) {
  TRACE_DURATION("machina", "virtio_gpu_transfer_to_host_2d");
  auto it = resources_.find(request->resource_id);
  if (it == resources_.end()) {
    response->type = VIRTIO_GPU_RESP_ERR_INVALID_RESOURCE_ID;
    return;
  }
  response->type = it->second->TransferToHost2D(request->r, request->offset);
}

void VirtioGpu::ResourceFlush(const virtio_gpu_resource_flush_t* request,
                              virtio_gpu_ctrl_hdr_t* response) {
  TRACE_DURATION("machina", "virtio_gpu_resource_flush");
  auto it = resources_.find(request->resource_id);
  if (it == resources_.end()) {
    response->type = VIRTIO_GPU_RESP_ERR_INVALID_RESOURCE_ID;
    return;
  }
  scanout_.OnResourceFlush(it->second.get(), request->r);
  response->type = VIRTIO_GPU_RESP_OK_NODATA;
}

void VirtioGpu::UpdateCursor(const virtio_gpu_update_cursor_t* request) {
  TRACE_DURATION("machina", "virtio_gpu_update_cursor");
  if (request->resource_id == 0) {
    scanout_.OnUpdateCursor(nullptr, 0, 0);
    return;
  }
  auto it = resources_.find(request->resource_id);
  if (it == resources_.end()) {
    return;
  }
  scanout_.OnUpdateCursor(it->second.get(), request->hot_x, request->hot_y);
}

void VirtioGpu::MoveCursor(const virtio_gpu_update_cursor_t* request) {
  TRACE_DURATION("machina", "virtio_gpu_move_cursor");
  auto it = resources_.find(request->resource_id);
  if (it == resources_.end()) {
    return;
  }
  if (request->pos.scanout_id != 0) {
    // Only a single scanout is supported.
    return;
  }
  scanout_.OnMoveCursor(request->pos.x, request->pos.y);
}

}  // namespace machina
