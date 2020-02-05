// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "gpu.h"

#include <fuchsia/sysmem/llcpp/fidl.h>
#include <inttypes.h>
#include <lib/image-format/image_format.h>
#include <string.h>
#include <sys/param.h>
#include <zircon/compiler.h>
#include <zircon/time.h>

#include <memory>
#include <utility>

#include <ddk/debug.h>
#include <fbl/alloc_checker.h>
#include <fbl/auto_call.h>
#include <fbl/auto_lock.h>

#include "trace.h"
#include "virtio_gpu.h"

#define LOCAL_TRACE 0

namespace sysmem = llcpp::fuchsia::sysmem;

namespace virtio {

namespace {

constexpr uint32_t kRefreshRateHz = 30;
constexpr uint64_t kDisplayId = 1;

zx_status_t to_zx_status(uint32_t type) {
  LTRACEF("response type %#x\n", type);
  if (type != VIRTIO_GPU_RESP_OK_NODATA) {
    return ZX_ERR_NO_MEMORY;
  }
  return ZX_OK;
}

}  // namespace

// DDK level ops

typedef struct imported_image {
  uint32_t resource_id;
  zx::pmt pmt;
} imported_image_t;

zx_status_t GpuDevice::DdkGetProtocol(uint32_t proto_id, void* out) {
  auto* proto = static_cast<ddk::AnyProtocol*>(out);
  proto->ctx = this;
  if (proto_id == ZX_PROTOCOL_DISPLAY_CONTROLLER_IMPL) {
    proto->ops = &display_controller_impl_protocol_ops_;
    return ZX_OK;
  }
  return ZX_ERR_NOT_SUPPORTED;
}

void GpuDevice::DisplayControllerImplSetDisplayControllerInterface(
    const display_controller_interface_protocol_t* intf) {
  {
    fbl::AutoLock al(&flush_lock_);
    dc_intf_ = *intf;
  }

  added_display_args_t args = {};
  args.display_id = kDisplayId, args.edid_present = false,
  args.panel.params =
      {
          .width = pmode_.r.width,
          .height = pmode_.r.height,
          .refresh_rate_e2 = kRefreshRateHz * 100,
      },
  args.pixel_format_list = &supported_formats_, args.pixel_format_count = 1,
  display_controller_interface_on_displays_changed(intf, &args, 1, nullptr, 0, nullptr, 0, nullptr);
}

zx_status_t GpuDevice::DisplayControllerImplImportVmoImage(image_t* image, zx::vmo vmo,
                                                           size_t offset) {
  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t GpuDevice::GetVmoAndStride(image_t* image, zx_unowned_handle_t handle, uint32_t index,
                                       zx::vmo* vmo_out, size_t* offset_out,
                                       uint32_t* pixel_size_out, uint32_t* row_bytes_out) {
  auto wait_result =
      sysmem::BufferCollection::Call::WaitForBuffersAllocated(zx::unowned_channel(handle));
  if (!wait_result.ok()) {
    zxlogf(ERROR, "%s: failed to WaitForBuffersAllocated %d\n", tag(), wait_result.status());
    return wait_result.status();
  }
  if (wait_result->status != ZX_OK) {
    zxlogf(ERROR, "%s: failed to WaitForBuffersAllocated call %d\n", tag(), wait_result->status);
    return wait_result->status;
  }

  sysmem::BufferCollectionInfo_2& collection_info = wait_result->buffer_collection_info;

  if (!collection_info.settings.has_image_format_constraints) {
    zxlogf(ERROR, "%s: bad image format constraints\n", tag());
    return ZX_ERR_INVALID_ARGS;
  }

  if (index >= collection_info.buffer_count) {
    return ZX_ERR_OUT_OF_RANGE;
  }

  ZX_DEBUG_ASSERT(collection_info.settings.image_format_constraints.pixel_format.type ==
                  sysmem::PixelFormatType::BGRA32);
  ZX_DEBUG_ASSERT(
      collection_info.settings.image_format_constraints.pixel_format.has_format_modifier);
  ZX_DEBUG_ASSERT(
      collection_info.settings.image_format_constraints.pixel_format.format_modifier.value ==
      sysmem::FORMAT_MODIFIER_LINEAR);

  fuchsia_sysmem_ImageFormatConstraints format_constraints;
  memcpy(&format_constraints, &collection_info.settings.image_format_constraints,
         sizeof(format_constraints));
  uint32_t minimum_row_bytes;
  if (!ImageFormatMinimumRowBytes(&format_constraints, image->width, &minimum_row_bytes)) {
    zxlogf(ERROR, "%s: Invalid image width %d for collection\n", tag(), image->width);
    return ZX_ERR_INVALID_ARGS;
  }

  *offset_out = collection_info.buffers[index].vmo_usable_start;
  *pixel_size_out = ImageFormatStrideBytesPerWidthPixel(&format_constraints.pixel_format);
  *row_bytes_out = minimum_row_bytes;
  *vmo_out = std::move(collection_info.buffers[index].vmo);
  return ZX_OK;
}

zx_status_t GpuDevice::DisplayControllerImplImportImage(image_t* image, zx_unowned_handle_t handle,
                                                        uint32_t index) {
  zx::vmo vmo;
  size_t offset;
  uint32_t pixel_size;
  uint32_t row_bytes;
  zx_status_t status =
      GetVmoAndStride(image, handle, index, &vmo, &offset, &pixel_size, &row_bytes);
  if (status != ZX_OK)
    return status;
  return Import(std::move(vmo), image, offset, pixel_size, row_bytes);
}

zx_status_t GpuDevice::Import(zx::vmo vmo, image_t* image, size_t offset, uint32_t pixel_size,
                              uint32_t row_bytes) {
  if (image->type != IMAGE_TYPE_SIMPLE) {
    return ZX_ERR_INVALID_ARGS;
  }

  fbl::AllocChecker ac;
  auto import_data = fbl::make_unique_checked<imported_image_t>(&ac);
  if (!ac.check()) {
    return ZX_ERR_NO_MEMORY;
  }

  unsigned size = ROUNDUP(row_bytes * image->height, PAGE_SIZE);
  zx_paddr_t paddr;
  zx_status_t status = bti_.pin(ZX_BTI_PERM_READ | ZX_BTI_CONTIGUOUS, vmo, offset, size, &paddr, 1,
                                &import_data->pmt);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: failed to pin vmo\n", tag());
    return status;
  }

  status = allocate_2d_resource(&import_data->resource_id, row_bytes / pixel_size, image->height);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: failed to allocate 2d resource\n", tag());
    return status;
  }

  status = attach_backing(import_data->resource_id, paddr, size);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: failed to attach backing store\n", tag());
    return status;
  }

  image->handle = reinterpret_cast<uint64_t>(import_data.release());

  return ZX_OK;
}

void GpuDevice::DisplayControllerImplReleaseImage(image_t* image) {
  delete reinterpret_cast<imported_image_t*>(image->handle);
}

uint32_t GpuDevice::DisplayControllerImplCheckConfiguration(
    const display_config_t** display_configs, size_t display_count, uint32_t** layer_cfg_results,
    size_t* layer_cfg_result_count) {
  if (display_count != 1) {
    ZX_DEBUG_ASSERT(display_count == 0);
    return CONFIG_DISPLAY_OK;
  }
  ZX_DEBUG_ASSERT(display_configs[0]->display_id == kDisplayId);
  bool success;
  if (display_configs[0]->layer_count != 1) {
    success = display_configs[0]->layer_count == 0;
  } else {
    primary_layer_t* layer = &display_configs[0]->layer_list[0]->cfg.primary;
    frame_t frame = {
        .x_pos = 0,
        .y_pos = 0,
        .width = pmode_.r.width,
        .height = pmode_.r.height,
    };
    success = display_configs[0]->layer_list[0]->type == LAYER_TYPE_PRIMARY &&
              layer->transform_mode == FRAME_TRANSFORM_IDENTITY &&
              layer->image.width == pmode_.r.width && layer->image.height == pmode_.r.height &&
              memcmp(&layer->dest_frame, &frame, sizeof(frame_t)) == 0 &&
              memcmp(&layer->src_frame, &frame, sizeof(frame_t)) == 0 &&
              display_configs[0]->cc_flags == 0 && layer->alpha_mode == ALPHA_DISABLE;
  }
  if (!success) {
    layer_cfg_results[0][0] = CLIENT_MERGE_BASE;
    for (unsigned i = 1; i < display_configs[0]->layer_count; i++) {
      layer_cfg_results[0][i] = CLIENT_MERGE_SRC;
    }
    layer_cfg_result_count[0] = display_configs[0]->layer_count;
  }
  return CONFIG_DISPLAY_OK;
}

void GpuDevice::DisplayControllerImplApplyConfiguration(const display_config_t** display_configs,
                                                        size_t display_count) {
  uint64_t handle = display_count == 0 || display_configs[0]->layer_count == 0
                        ? 0
                        : display_configs[0]->layer_list[0]->cfg.primary.image.handle;

  {
    fbl::AutoLock al(&flush_lock_);
    current_fb_ = reinterpret_cast<imported_image_t*>(handle);
  }

  Flush();
}

zx_status_t GpuDevice::DisplayControllerImplGetSysmemConnection(zx::channel sysmem_handle) {
  zx_status_t status = sysmem_connect(&sysmem_, sysmem_handle.release());
  if (status != ZX_OK) {
    return status;
  }

  return ZX_OK;
}

zx_status_t GpuDevice::DisplayControllerImplSetBufferCollectionConstraints(
    const image_t* config, zx_unowned_handle_t collection) {
  sysmem::BufferCollectionConstraints constraints;
  constraints.usage.display = sysmem::displayUsageLayer;
  constraints.has_buffer_memory_constraints = true;
  sysmem::BufferMemoryConstraints& buffer_constraints = constraints.buffer_memory_constraints;
  buffer_constraints.min_size_bytes = 0;
  buffer_constraints.max_size_bytes = 0xffffffff;
  buffer_constraints.physically_contiguous_required = true;
  buffer_constraints.secure_required = false;
  buffer_constraints.ram_domain_supported = true;
  buffer_constraints.cpu_domain_supported = true;
  constraints.image_format_constraints_count = 1;
  sysmem::ImageFormatConstraints& image_constraints = constraints.image_format_constraints[0];
  image_constraints.pixel_format.type = sysmem::PixelFormatType::BGRA32;
  image_constraints.pixel_format.has_format_modifier = true;
  image_constraints.pixel_format.format_modifier.value = sysmem::FORMAT_MODIFIER_LINEAR;
  image_constraints.color_spaces_count = 1;
  image_constraints.color_space[0].type = sysmem::ColorSpaceType::SRGB;
  image_constraints.min_coded_width = 0;
  image_constraints.max_coded_width = 0xffffffff;
  image_constraints.min_coded_height = 0;
  image_constraints.max_coded_height = 0xffffffff;
  image_constraints.min_bytes_per_row = 0;
  image_constraints.max_bytes_per_row = 0xffffffff;
  image_constraints.max_coded_width_times_coded_height = 0xffffffff;
  image_constraints.layers = 1;
  image_constraints.coded_width_divisor = 1;
  image_constraints.coded_height_divisor = 1;
  // Bytes per row needs to be a multiple of the pixel size.
  image_constraints.bytes_per_row_divisor = 4;
  image_constraints.start_offset_divisor = 1;
  image_constraints.display_width_divisor = 1;
  image_constraints.display_height_divisor = 1;

  zx_status_t status = sysmem::BufferCollection::Call::SetConstraints(
                           zx::unowned_channel(collection), true, constraints)
                           .status();

  if (status != ZX_OK) {
    zxlogf(ERROR, "virtio::GpuDevice: Failed to set constraints");
    return status;
  }

  return ZX_OK;
}

zx_status_t GpuDevice::DisplayControllerImplGetSingleBufferFramebuffer(zx::vmo* out_vmo,
                                                                       uint32_t* out_stride) {
  return ZX_ERR_NOT_SUPPORTED;
}

GpuDevice::GpuDevice(zx_device_t* bus_device, zx::bti bti, std::unique_ptr<Backend> backend)
    : virtio::Device(bus_device, std::move(bti), std::move(backend)), DeviceType(bus_device) {
  sem_init(&request_sem_, 0, 1);
  sem_init(&response_sem_, 0, 0);
  cnd_init(&flush_cond_);

  memset(&gpu_req_, 0, sizeof(gpu_req_));
}

GpuDevice::~GpuDevice() {
  io_buffer_release(&gpu_req_);

  // TODO: clean up allocated physical memory
  sem_destroy(&request_sem_);
  sem_destroy(&response_sem_);
  cnd_destroy(&flush_cond_);
}

template <typename RequestType, typename ResponseType>
void GpuDevice::send_command_response(const RequestType* cmd, ResponseType** res) {
  size_t cmd_len = sizeof(RequestType);
  size_t res_len = sizeof(ResponseType);
  LTRACEF("dev %p, cmd %p, cmd_len %zu, res %p, res_len %zu\n", this, cmd, cmd_len, res, res_len);

  // Keep this single message at a time
  sem_wait(&request_sem_);
  fbl::MakeAutoCall([this]() { sem_post(&request_sem_); });

  uint16_t i;
  struct vring_desc* desc = vring_.AllocDescChain(2, &i);
  ZX_ASSERT(desc);

  void* gpu_req_base = io_buffer_virt(&gpu_req_);
  zx_paddr_t gpu_req_pa = io_buffer_phys(&gpu_req_);

  memcpy(gpu_req_base, cmd, cmd_len);

  desc->addr = gpu_req_pa;
  desc->len = static_cast<uint32_t>(cmd_len);
  desc->flags = VRING_DESC_F_NEXT;

  // Set the second descriptor to the response with the write bit set
  desc = vring_.DescFromIndex(desc->next);
  ZX_ASSERT(desc);

  *res = reinterpret_cast<ResponseType*>(static_cast<uint8_t*>(gpu_req_base) + cmd_len);
  zx_paddr_t res_phys = gpu_req_pa + cmd_len;
  memset(*res, 0, res_len);

  desc->addr = res_phys;
  desc->len = static_cast<uint32_t>(res_len);
  desc->flags = VRING_DESC_F_WRITE;

  // Submit the transfer & wait for the response
  vring_.SubmitChain(i);
  vring_.Kick();
  sem_wait(&response_sem_);
}

zx_status_t GpuDevice::get_display_info() {
  LTRACEF("dev %p\n", this);

  // Construct the get display info message
  virtio_gpu_ctrl_hdr req;
  memset(&req, 0, sizeof(req));
  req.type = VIRTIO_GPU_CMD_GET_DISPLAY_INFO;

  // Send the message and get a response
  virtio_gpu_resp_display_info* info;
  send_command_response(&req, &info);
  if (info->hdr.type != VIRTIO_GPU_RESP_OK_DISPLAY_INFO) {
    return ZX_ERR_NOT_FOUND;
  }

  // We got a response
  LTRACEF("response:\n");
  for (int i = 0; i < VIRTIO_GPU_MAX_SCANOUTS; i++) {
    if (info->pmodes[i].enabled) {
      LTRACEF("%u: x %u y %u w %u h %u flags 0x%x\n", i, info->pmodes[i].r.x, info->pmodes[i].r.y,
              info->pmodes[i].r.width, info->pmodes[i].r.height, info->pmodes[i].flags);
      if (pmode_id_ < 0) {
        // Save the first valid pmode we see
        memcpy(&pmode_, &info->pmodes[i], sizeof(pmode_));
        pmode_id_ = i;
      }
    }
  }

  return ZX_OK;
}

zx_status_t GpuDevice::allocate_2d_resource(uint32_t* resource_id, uint32_t width,
                                            uint32_t height) {
  LTRACEF("dev %p\n", this);

  ZX_ASSERT(resource_id);

  // Construct the request
  virtio_gpu_resource_create_2d req;
  memset(&req, 0, sizeof(req));

  req.hdr.type = VIRTIO_GPU_CMD_RESOURCE_CREATE_2D;
  req.resource_id = next_resource_id_++;
  *resource_id = req.resource_id;
  req.format = VIRTIO_GPU_FORMAT_B8G8R8X8_UNORM;
  req.width = width;
  req.height = height;

  // Send the command and get a response
  struct virtio_gpu_ctrl_hdr* res;
  send_command_response(&req, &res);

  return to_zx_status(res->type);
}

zx_status_t GpuDevice::attach_backing(uint32_t resource_id, zx_paddr_t ptr, size_t buf_len) {
  LTRACEF("dev %p, resource_id %u, ptr %#" PRIxPTR ", buf_len %zu\n", this, resource_id, ptr,
          buf_len);

  ZX_ASSERT(ptr);

  // Construct the request
  struct {
    struct virtio_gpu_resource_attach_backing req;
    struct virtio_gpu_mem_entry mem;
  } req;
  memset(&req, 0, sizeof(req));

  req.req.hdr.type = VIRTIO_GPU_CMD_RESOURCE_ATTACH_BACKING;
  req.req.resource_id = resource_id;
  req.req.nr_entries = 1;

  req.mem.addr = ptr;
  req.mem.length = (uint32_t)buf_len;

  // Send the command and get a response
  struct virtio_gpu_ctrl_hdr* res;
  send_command_response(&req, &res);
  return to_zx_status(res->type);
}

zx_status_t GpuDevice::set_scanout(uint32_t scanout_id, uint32_t resource_id, uint32_t width,
                                   uint32_t height) {
  LTRACEF("dev %p, scanout_id %u, resource_id %u, width %u, height %u\n", this, scanout_id,
          resource_id, width, height);

  // Construct the request
  virtio_gpu_set_scanout req;
  memset(&req, 0, sizeof(req));

  req.hdr.type = VIRTIO_GPU_CMD_SET_SCANOUT;
  req.r.x = req.r.y = 0;
  req.r.width = width;
  req.r.height = height;
  req.scanout_id = scanout_id;
  req.resource_id = resource_id;

  // Send the command and get a response
  virtio_gpu_ctrl_hdr* res;
  send_command_response(&req, &res);
  return to_zx_status(res->type);
}

zx_status_t GpuDevice::flush_resource(uint32_t resource_id, uint32_t width, uint32_t height) {
  LTRACEF("dev %p, resource_id %u, width %u, height %u\n", this, resource_id, width, height);

  // Construct the request
  virtio_gpu_resource_flush req;
  memset(&req, 0, sizeof(req));

  req.hdr.type = VIRTIO_GPU_CMD_RESOURCE_FLUSH;
  req.r.x = req.r.y = 0;
  req.r.width = width;
  req.r.height = height;
  req.resource_id = resource_id;

  // Send the command and get a response
  virtio_gpu_ctrl_hdr* res;
  send_command_response(&req, &res);
  return to_zx_status(res->type);
}

zx_status_t GpuDevice::transfer_to_host_2d(uint32_t resource_id, uint32_t width, uint32_t height) {
  LTRACEF("dev %p, resource_id %u, width %u, height %u\n", this, resource_id, width, height);

  // Construct the request
  virtio_gpu_transfer_to_host_2d req;
  memset(&req, 0, sizeof(req));

  req.hdr.type = VIRTIO_GPU_CMD_TRANSFER_TO_HOST_2D;
  req.r.x = req.r.y = 0;
  req.r.width = width;
  req.r.height = height;
  req.offset = 0;
  req.resource_id = resource_id;

  // Send the command and get a response
  virtio_gpu_ctrl_hdr* res;
  send_command_response(&req, &res);
  return to_zx_status(res->type);
}

void GpuDevice::Flush() {
  fbl::AutoLock al(&flush_lock_);
  flush_pending_ = true;
  cnd_signal(&flush_cond_);
}

void GpuDevice::virtio_gpu_flusher() {
  LTRACE_ENTRY;
  zx_time_t next_deadline = zx_clock_get_monotonic();
  zx_time_t period = ZX_SEC(1) / kRefreshRateHz;
  for (;;) {
    zx_nanosleep(next_deadline);

    bool fb_change;
    {
      fbl::AutoLock al(&flush_lock_);
      fb_change = displayed_fb_ != current_fb_;
      displayed_fb_ = current_fb_;
    }

    LTRACEF("flushing\n");

    if (displayed_fb_) {
      zx_status_t status =
          transfer_to_host_2d(displayed_fb_->resource_id, pmode_.r.width, pmode_.r.height);
      if (status != ZX_OK) {
        LTRACEF("failed to flush resource\n");
        continue;
      }

      status = flush_resource(displayed_fb_->resource_id, pmode_.r.width, pmode_.r.height);
      if (status != ZX_OK) {
        LTRACEF("failed to flush resource\n");
        continue;
      }
    }

    if (fb_change) {
      uint32_t res_id = displayed_fb_ ? displayed_fb_->resource_id : 0;
      zx_status_t status = set_scanout(pmode_id_, res_id, pmode_.r.width, pmode_.r.height);
      if (status != ZX_OK) {
        zxlogf(ERROR, "%s: failed to set scanout: %d\n", tag(), status);
        continue;
      }
    }

    {
      fbl::AutoLock al(&flush_lock_);
      if (dc_intf_.ops) {
        uint64_t handles[] = {reinterpret_cast<uint64_t>(displayed_fb_)};
        display_controller_interface_on_display_vsync(&dc_intf_, kDisplayId, next_deadline, handles,
                                                      displayed_fb_ != nullptr);
      }
    }
    next_deadline = zx_time_add_duration(next_deadline, period);
  }
}

zx_status_t GpuDevice::virtio_gpu_start() {
  LTRACEF("dev %p\n", this);

  // Get the display info and see if we find a valid pmode
  zx_status_t status = get_display_info();
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: failed to get display info\n", tag());
    return status;
  }

  if (pmode_id_ < 0) {
    zxlogf(ERROR, "%s: failed to find a pmode, exiting\n", tag());
    return ZX_ERR_NOT_FOUND;
  }

  printf("virtio-gpu: found display x %u y %u w %u h %u flags 0x%x\n", pmode_.r.x, pmode_.r.y,
         pmode_.r.width, pmode_.r.height, pmode_.flags);

  // Run a worker thread to shove in flush events
  auto virtio_gpu_flusher_entry = [](void* arg) {
    static_cast<GpuDevice*>(arg)->virtio_gpu_flusher();
    return 0;
  };
  thrd_create_with_name(&flush_thread_, virtio_gpu_flusher_entry, this, "virtio-gpu-flusher");
  thrd_detach(flush_thread_);

  LTRACEF("publishing device\n");

  status = DdkAdd("virtio-gpu-display");
  device_ = zxdev();

  if (status != ZX_OK) {
    device_ = nullptr;
    return status;
  }

  LTRACE_EXIT;
  return ZX_OK;
}

zx_status_t GpuDevice::Init() {
  LTRACE_ENTRY;

  zx_status_t status = device_get_protocol(bus_device(), ZX_PROTOCOL_SYSMEM, &sysmem_);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: Could not get Display SYSMEM protocol\n", tag());
    return status;
  }

  DeviceReset();

  struct virtio_gpu_config config;
  CopyDeviceConfig(&config, sizeof(config));
  LTRACEF("events_read 0x%x\n", config.events_read);
  LTRACEF("events_clear 0x%x\n", config.events_clear);
  LTRACEF("num_scanouts 0x%x\n", config.num_scanouts);
  LTRACEF("reserved 0x%x\n", config.reserved);

  // Ack and set the driver status bit
  DriverStatusAck();

  // XXX check features bits and ack/nak them

  // Allocate the main vring
  status = vring_.Init(0, 16);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: failed to allocate vring\n", tag());
    return status;
  }

  // Allocate a GPU request
  status = io_buffer_init(&gpu_req_, bti_.get(), PAGE_SIZE, IO_BUFFER_RW | IO_BUFFER_CONTIG);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: cannot alloc gpu_req buffers %d\n", tag(), status);
    return status;
  }

  LTRACEF("allocated gpu request at %p, physical address %#" PRIxPTR "\n",
          io_buffer_virt(&gpu_req_), io_buffer_phys(&gpu_req_));

  StartIrqThread();
  DriverStatusOk();

  // Start a worker thread that runs through a sequence to finish initializing the GPU
  auto virtio_gpu_start_entry = [](void* arg) {
    return static_cast<GpuDevice*>(arg)->virtio_gpu_start();
  };
  thrd_create_with_name(&start_thread_, virtio_gpu_start_entry, this, "virtio-gpu-starter");
  thrd_detach(start_thread_);

  return ZX_OK;
}

void GpuDevice::IrqRingUpdate() {
  LTRACE_ENTRY;

  // Parse our descriptor chain, add back to the free queue
  auto free_chain = [this](vring_used_elem* used_elem) {
    uint16_t i = static_cast<uint16_t>(used_elem->id);
    struct vring_desc* desc = vring_.DescFromIndex(i);
    for (;;) {
      int next;

      if (desc->flags & VRING_DESC_F_NEXT) {
        next = desc->next;
      } else {
        // End of chain
        next = -1;
      }

      vring_.FreeDesc(i);

      if (next < 0) {
        break;
      }
      i = static_cast<uint16_t>(next);
      desc = vring_.DescFromIndex(i);
    }
    // Notify the request thread
    sem_post(&response_sem_);
  };

  // Tell the ring to find free chains and hand it back to our lambda
  vring_.IrqRingUpdate(free_chain);
}

void GpuDevice::IrqConfigChange() { LTRACE_ENTRY; }

}  // namespace virtio
