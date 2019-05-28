// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "display.h"

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/trace/event.h>
#include <fbl/auto_lock.h>
#include <fbl/unique_ptr.h>
#include <fuchsia/sysmem/c/fidl.h>
#include <zircon/pixelformat.h>
#include <zircon/threads.h>

#include <memory>

namespace goldfish {
namespace {

const char* kTag = "goldfish-display";

const char* kPipeName = "pipe:opengles";

constexpr uint32_t kRefreshRateHz = 60;

constexpr uint64_t kDisplayId = 1;

constexpr uint32_t kClientFlags = 0;

constexpr zx_pixel_format_t kPixelFormats[] = {
    ZX_PIXEL_FORMAT_RGB_x888,
    ZX_PIXEL_FORMAT_ARGB_8888,
};

constexpr uint32_t FB_WIDTH = 1;
constexpr uint32_t FB_HEIGHT = 2;

constexpr uint32_t GL_RGBA = 0x1908;
constexpr uint32_t GL_UNSIGNED_BYTE = 0x1401;

constexpr uint32_t IMAGE_TYPE_OPTIMAL = 1;

struct GetFbParamCmd {
    uint32_t op;
    uint32_t size;
    uint32_t param;
};
constexpr uint32_t kOP_rcGetFbParam = 10007;
constexpr uint32_t kSize_rcGetFbParam = 12;

struct CreateColorBufferCmd {
    uint32_t op;
    uint32_t size;
    uint32_t width;
    uint32_t height;
    uint32_t internalformat;
};
constexpr uint32_t kOP_rcCreateColorBuffer = 10012;
constexpr uint32_t kSize_rcCreateColorBuffer = 20;

struct OpenColorBufferCmd {
    uint32_t op;
    uint32_t size;
    uint32_t id;
};
constexpr uint32_t kOP_rcOpenColorBuffer = 10013;
constexpr uint32_t kSize_rcOpenColorBuffer = 12;

struct CloseColorBufferCmd {
    uint32_t op;
    uint32_t size;
    uint32_t id;
};
constexpr uint32_t kOP_rcCloseColorBuffer = 10014;
constexpr uint32_t kSize_rcCloseColorBuffer = 12;

struct UpdateColorBufferCmd {
    uint32_t op;
    uint32_t size;
    uint32_t id;
    uint32_t x;
    uint32_t y;
    uint32_t width;
    uint32_t height;
    uint32_t format;
    uint32_t type;
};
constexpr uint32_t kOP_rcUpdateColorBuffer = 10024;
constexpr uint32_t kSize_rcUpdateColorBuffer = 36;

struct FbPostCmd {
    uint32_t op;
    uint32_t size;
    uint32_t id;
};
constexpr uint32_t kOP_rcFbPost = 10018;
constexpr uint32_t kSize_rcFbPost = 12;

} // namespace

// static
zx_status_t Display::Create(void* ctx, zx_device_t* device) {
    auto display = std::make_unique<Display>(device);

    zx_status_t status = display->Bind();
    if (status == ZX_OK) {
        // devmgr now owns device.
        __UNUSED auto* dev = display.release();
    }
    return status;
}

Display::Display(zx_device_t* parent)
    : DisplayType(parent), control_(parent), pipe_(parent) {}

Display::~Display() {
    if (id_) {
        fbl::AutoLock lock(&lock_);
        if (cmd_buffer_.is_valid()) {
            auto buffer = static_cast<pipe_cmd_buffer_t*>(cmd_buffer_.virt());
            buffer->id = id_;
            buffer->cmd = PIPE_CMD_CODE_CLOSE;
            buffer->status = PIPE_ERROR_INVAL;

            pipe_.Exec(id_);
            ZX_DEBUG_ASSERT(!buffer->status);
        }
        pipe_.Destroy(id_);
    }

    {
        fbl::AutoLock lock(&flush_lock_);
        shutdown_ = true;
    }

    thrd_join(flush_thread_, NULL);
}

zx_status_t Display::Bind() {
    fbl::AutoLock lock(&lock_);

    if (!control_.is_valid()) {
        zxlogf(ERROR, "%s: no control protocol\n", kTag);
        return ZX_ERR_NOT_SUPPORTED;
    }

    if (!pipe_.is_valid()) {
        zxlogf(ERROR, "%s: no pipe protocol\n", kTag);
        return ZX_ERR_NOT_SUPPORTED;
    }

    zx_status_t status = pipe_.GetBti(&bti_);
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s: GetBti failed: %d\n", kTag, status);
        return status;
    }

    status =
        io_buffer_.Init(bti_.get(), PAGE_SIZE, IO_BUFFER_RW | IO_BUFFER_CONTIG);
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s: io_buffer_init failed: %d\n", kTag, status);
        return status;
    }

    zx::vmo vmo;
    goldfish_pipe_signal_value_t signal_cb = {Display::OnSignal, this};
    status = pipe_.Create(&signal_cb, &id_, &vmo);
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s: Create failed: %d\n", kTag, status);
        return status;
    }

    status = cmd_buffer_.InitVmo(bti_.get(), vmo.get(), 0, IO_BUFFER_RW);
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s: io_buffer_init_vmo failed: %d\n", kTag, status);
        return status;
    }

    auto buffer = static_cast<pipe_cmd_buffer_t*>(cmd_buffer_.virt());
    buffer->id = id_;
    buffer->cmd = PIPE_CMD_CODE_OPEN;
    buffer->status = PIPE_ERROR_INVAL;

    pipe_.Open(id_);
    if (buffer->status) {
        zxlogf(ERROR, "%s: Open failed: %d\n", kTag, buffer->status);
        cmd_buffer_.release();
        return ZX_ERR_INTERNAL;
    }

    size_t length = strlen(kPipeName) + 1;
    memcpy(io_buffer_.virt(), kPipeName, length);
    WriteLocked(static_cast<uint32_t>(length));

    memcpy(io_buffer_.virt(), &kClientFlags, sizeof(kClientFlags));
    WriteLocked(sizeof(kClientFlags));

    int rc = thrd_create_with_name(
        &flush_thread_,
        [](void* arg) { return static_cast<Display*>(arg)->FlushHandler(); },
        this, "goldfish_display_flush_thread");
    if (rc != thrd_success) {
        return thrd_status_to_zx_status(rc);
    }

    return DdkAdd("goldfish-display");
}

void Display::DdkUnbind() {
    DdkRemove();
}

void Display::DdkRelease() {
    delete this;
}

void Display::DisplayControllerImplSetDisplayControllerInterface(
    const display_controller_interface_protocol_t* interface) {
    fbl::AutoLock lock(&flush_lock_);
    dc_intf_ = ddk::DisplayControllerInterfaceProtocolClient(interface);
    {
        fbl::AutoLock lock(&lock_);
        width_ = GetFbParamLocked(FB_WIDTH, 1);
        height_ = GetFbParamLocked(FB_HEIGHT, 1);
    }

    added_display_args_t args = {};
    args.display_id = kDisplayId;
    args.edid_present = false;
    args.panel.params.height = height_;
    args.panel.params.width = width_;
    args.panel.params.refresh_rate_e2 = kRefreshRateHz * 100;
    args.pixel_format_list = kPixelFormats;
    args.pixel_format_count = sizeof(kPixelFormats) / sizeof(kPixelFormats[0]);

    dc_intf_.OnDisplaysChanged(&args, 1, nullptr, 0, nullptr, 0, nullptr);
}

zx_status_t Display::DisplayControllerImplImportVmoImage(image_t* image,
                                                         zx::vmo vmo,
                                                         size_t offset) {
    if (image->type != IMAGE_TYPE_SIMPLE) {
        zxlogf(ERROR, "%s: invalid image type\n", kTag);
        return ZX_ERR_INVALID_ARGS;
    }

    auto color_buffer = std::make_unique<ColorBuffer>();

    // Linear images must be pinned.
    unsigned pixel_size = ZX_PIXEL_FORMAT_BYTES(image->pixel_format);
    color_buffer->size =
        ROUNDUP(image->width * image->height * pixel_size, PAGE_SIZE);
    zx_status_t status = bti_.pin(ZX_BTI_PERM_READ | ZX_BTI_CONTIGUOUS, vmo,
                                  offset, color_buffer->size,
                                  &color_buffer->paddr, 1, &color_buffer->pmt);
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s: failed to pin VMO: %d\n", kTag, status);
        return status;
    }

    color_buffer->vmo = std::move(vmo);
    color_buffer->width = image->width;
    color_buffer->height = image->height;

    {
        fbl::AutoLock lock(&lock_);
        status = CreateColorBufferLocked(image->width, image->height,
                                         &color_buffer->id);
        if (status != ZX_OK) {
            zxlogf(ERROR, "%s: failed to create color buffer\n", kTag);
            return status;
        }
    }

    image->handle = reinterpret_cast<uint64_t>(color_buffer.release());
    return ZX_OK;
}

zx_status_t Display::DisplayControllerImplImportImage(
    image_t* image, zx_unowned_handle_t handle, uint32_t index) {
    if (image->type != IMAGE_TYPE_OPTIMAL) {
        zxlogf(ERROR, "%s: invalid image type\n", kTag);
        return ZX_ERR_INVALID_ARGS;
    }

    auto color_buffer = std::make_unique<ColorBuffer>();

    zx_status_t status, status2;
    fuchsia_sysmem_BufferCollectionInfo_2 collection_info;
    status = fuchsia_sysmem_BufferCollectionWaitForBuffersAllocated(
        handle, &status2, &collection_info);
    if (status != ZX_OK) {
        return status;
    }
    if (status2 != ZX_OK) {
        return status2;
    }

    if (index < collection_info.buffer_count) {
        color_buffer->vmo = zx::vmo(collection_info.buffers[index].vmo);
        collection_info.buffers[index].vmo = ZX_HANDLE_INVALID;
    }
    for (uint32_t i = 0; i < collection_info.buffer_count; ++i) {
        zx_handle_close(collection_info.buffers[i].vmo);
    }

    if (!collection_info.settings.has_image_format_constraints ||
        !color_buffer->vmo.is_valid()) {
        zxlogf(ERROR, "%s: invalid image format or index\n", kTag);
        return ZX_ERR_OUT_OF_RANGE;
    }

    uint64_t offset = collection_info.buffers[index].vmo_usable_start;
    if (offset) {
        zxlogf(ERROR, "%s: invalid offset\n", kTag);
        return ZX_ERR_INVALID_ARGS;
    }

    image->handle = reinterpret_cast<uint64_t>(color_buffer.release());
    return ZX_OK;
}

void Display::DisplayControllerImplReleaseImage(image_t* image) {
    auto color_buffer = reinterpret_cast<ColorBuffer*>(image->handle);

    // Color buffer is owned by image in the linear case.
    if (image->type == IMAGE_TYPE_SIMPLE) {
        fbl::AutoLock lock(&lock_);
        CloseColorBufferLocked(color_buffer->id);
    }

    delete color_buffer;
}

uint32_t Display::DisplayControllerImplCheckConfiguration(
    const display_config_t** display_configs, size_t display_count,
    uint32_t** layer_cfg_results, size_t* layer_cfg_result_count) {
    if (display_count != 1) {
        ZX_DEBUG_ASSERT(display_count == 0);
        return CONFIG_DISPLAY_OK;
    }
    ZX_DEBUG_ASSERT(display_configs[0]->display_id == kDisplayId);
    bool success;
    if (display_configs[0]->layer_count != 1) {
        success = false;
    } else {
        fbl::AutoLock lock(&flush_lock_);

        primary_layer_t* layer =
            &display_configs[0]->layer_list[0]->cfg.primary;
        frame_t frame = {
            .x_pos = 0,
            .y_pos = 0,
            .width = width_,
            .height = height_,
        };
        success =
            display_configs[0]->layer_list[0]->type == LAYER_TYPE_PRIMARY &&
            layer->transform_mode == FRAME_TRANSFORM_IDENTITY &&
            layer->image.width == width_ && layer->image.height == height_ &&
            memcmp(&layer->dest_frame, &frame, sizeof(frame_t)) == 0 &&
            memcmp(&layer->src_frame, &frame, sizeof(frame_t)) == 0 &&
            display_configs[0]->cc_flags == 0 &&
            layer->alpha_mode == ALPHA_DISABLE;
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

void Display::DisplayControllerImplApplyConfiguration(
    const display_config_t** display_configs, size_t display_count) {
    uint64_t handle = 0;
    if (display_count && display_configs[0]->layer_count) {
        handle = display_configs[0]->layer_list[0]->cfg.primary.image.handle;
    }

    auto color_buffer = reinterpret_cast<ColorBuffer*>(handle);
    if (color_buffer && !color_buffer->id) {
        zx::vmo vmo;

        zx_status_t status =
            color_buffer->vmo.duplicate(ZX_RIGHT_SAME_RIGHTS, &vmo);
        if (status != ZX_OK) {
            zxlogf(ERROR, "%s: failed to duplicate vmo: %d\n", kTag, status);
        } else {
            fbl::AutoLock lock(&lock_);

            status = control_.GetColorBuffer(std::move(vmo), &color_buffer->id);
            if (status != ZX_OK) {
                zxlogf(ERROR, "%s: failed to get color buffer: %d\n", kTag,
                       status);
            }
        }
    }

    {
        fbl::AutoLock lock(&flush_lock_);
        current_fb_ = color_buffer;
    }
}

uint32_t
Display::DisplayControllerImplComputeLinearStride(uint32_t width,
                                                  zx_pixel_format_t format) {
    return width;
}

zx_status_t Display::DisplayControllerImplAllocateVmo(uint64_t size,
                                                      zx::vmo* vmo_out) {
    return zx_vmo_create_contiguous(bti_.get(), size, 0,
                                    vmo_out->reset_and_get_address());
}

zx_status_t
Display::DisplayControllerImplGetSysmemConnection(zx::channel connection) {
    fbl::AutoLock lock(&lock_);

    zx_status_t status = pipe_.ConnectSysmem(std::move(connection));
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s: failed to connect to sysmem: %d\n", kTag, status);
        return status;
    }
    return ZX_OK;
}

zx_status_t Display::DisplayControllerImplSetBufferCollectionConstraints(
    const image_t* config, uint32_t collection) {
    fuchsia_sysmem_BufferCollectionConstraints constraints = {};
    constraints.usage.display = fuchsia_sysmem_displayUsageLayer;
    constraints.has_buffer_memory_constraints = true;
    fuchsia_sysmem_BufferMemoryConstraints& buffer_constraints =
        constraints.buffer_memory_constraints;
    buffer_constraints.min_size_bytes = 0;
    buffer_constraints.max_size_bytes = 0xffffffff;
    buffer_constraints.physically_contiguous_required = true;
    buffer_constraints.secure_required = false;
    buffer_constraints.ram_domain_supported = true;
    buffer_constraints.cpu_domain_supported = true;
    buffer_constraints.inaccessible_domain_supported = true;
    buffer_constraints.heap_permitted_count = 2;
    buffer_constraints.heap_permitted[0] = fuchsia_sysmem_HeapType_SYSTEM_RAM;
    buffer_constraints.heap_permitted[1] =
        fuchsia_sysmem_HeapType_GOLDFISH_DEVICE_LOCAL;
    constraints.image_format_constraints_count = 1;
    fuchsia_sysmem_ImageFormatConstraints& image_constraints =
        constraints.image_format_constraints[0];
    image_constraints.pixel_format.type = fuchsia_sysmem_PixelFormatType_BGRA32;
    image_constraints.color_spaces_count = 1;
    image_constraints.color_space[0].type = fuchsia_sysmem_ColorSpaceType_SRGB;
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
    image_constraints.bytes_per_row_divisor = 1;
    image_constraints.start_offset_divisor = 1;
    image_constraints.display_width_divisor = 1;
    image_constraints.display_height_divisor = 1;

    zx_status_t status = fuchsia_sysmem_BufferCollectionSetConstraints(
        collection, true, &constraints);
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s: failed to set constraints\n", kTag);
        return status;
    }
    return ZX_OK;
}

zx_status_t
Display::DisplayControllerImplGetSingleBufferFramebuffer(zx::vmo* out_vmo,
                                                         uint32_t* out_stride) {
    return ZX_ERR_NOT_SUPPORTED;
}

void Display::OnSignal(void* ctx, int32_t flags) {
    TRACE_DURATION("gfx", "Display::OnSignal", "flags", flags);

    if (flags & (PIPE_WAKE_FLAG_READ | PIPE_WAKE_FLAG_CLOSED)) {
        static_cast<Display*>(ctx)->OnReadable();
    }
}

void Display::OnReadable() {
    TRACE_DURATION("gfx", "Display::OnReadable");

    fbl::AutoLock lock(&lock_);
    readable_cvar_.Signal();
}

void Display::WriteLocked(uint32_t cmd_size) {
    TRACE_DURATION("gfx", "Display::Write", "cmd_size", cmd_size);

    auto buffer = static_cast<pipe_cmd_buffer_t*>(cmd_buffer_.virt());
    buffer->id = id_;
    buffer->cmd = PIPE_CMD_CODE_WRITE;
    buffer->status = PIPE_ERROR_INVAL;
    buffer->rw_params.ptrs[0] = io_buffer_.phys();
    buffer->rw_params.sizes[0] = cmd_size;
    buffer->rw_params.buffers_count = 1;
    buffer->rw_params.consumed_size = 0;
    pipe_.Exec(id_);
    ZX_DEBUG_ASSERT(buffer->rw_params.consumed_size ==
                    static_cast<int32_t>(cmd_size));
}

zx_status_t Display::ReadResultLocked(uint32_t* result) {
    TRACE_DURATION("gfx", "Display::ReadResult");

    while (1) {
        auto buffer = static_cast<pipe_cmd_buffer_t*>(cmd_buffer_.virt());
        buffer->id = id_;
        buffer->cmd = PIPE_CMD_CODE_READ;
        buffer->status = PIPE_ERROR_INVAL;
        buffer->rw_params.ptrs[0] = io_buffer_.phys();
        buffer->rw_params.sizes[0] = sizeof(*result);
        buffer->rw_params.buffers_count = 1;
        buffer->rw_params.consumed_size = 0;
        pipe_.Exec(id_);

        // Positive consumed size always indicate a successful transfer.
        if (buffer->rw_params.consumed_size) {
            ZX_DEBUG_ASSERT(buffer->rw_params.consumed_size == sizeof(*result));
            *result = *static_cast<uint32_t*>(io_buffer_.virt());
            return ZX_OK;
        }

        // Early out if error is not because of back-pressure.
        if (buffer->status != PIPE_ERROR_AGAIN) {
            zxlogf(ERROR, "%s: reading result failed: %d\n", kTag,
                   buffer->status);
            return ZX_ERR_INTERNAL;
        }

        buffer->id = id_;
        buffer->cmd = PIPE_CMD_CODE_WAKE_ON_READ;
        buffer->status = PIPE_ERROR_INVAL;
        pipe_.Exec(id_);
        ZX_DEBUG_ASSERT(!buffer->status);

        // Wait for pipe to become readable.
        readable_cvar_.Wait(&lock_);
    }
}

zx_status_t Display::ExecuteCommandLocked(uint32_t cmd_size, uint32_t* result) {
    TRACE_DURATION("gfx", "Display::ExecuteCommand", "cnd_size", cmd_size);

    WriteLocked(cmd_size);
    return ReadResultLocked(result);
}

int32_t Display::GetFbParamLocked(uint32_t param, int32_t default_value) {
    TRACE_DURATION("gfx", "Display::GetFbParam", "param", param);

    auto cmd = static_cast<GetFbParamCmd*>(io_buffer_.virt());
    cmd->op = kOP_rcGetFbParam;
    cmd->size = kSize_rcGetFbParam;
    cmd->param = param;

    uint32_t result;
    zx_status_t status = ExecuteCommandLocked(kSize_rcGetFbParam, &result);
    return status == ZX_OK ? result : default_value;
}

zx_status_t Display::CreateColorBufferLocked(uint32_t width, uint32_t height,
                                             uint32_t* id) {
    TRACE_DURATION("gfx", "Display::CreateColorBuffer", "width", width,
                   "height", height);

    auto cmd = static_cast<CreateColorBufferCmd*>(io_buffer_.virt());
    cmd->op = kOP_rcCreateColorBuffer;
    cmd->size = kSize_rcCreateColorBuffer;
    cmd->width = width;
    cmd->height = height;
    cmd->internalformat = GL_RGBA;

    return ExecuteCommandLocked(kSize_rcCreateColorBuffer, id);
}

void Display::OpenColorBufferLocked(uint32_t id) {
    TRACE_DURATION("gfx", "Display::OpenColorBuffer", "id", id);

    auto cmd = static_cast<OpenColorBufferCmd*>(io_buffer_.virt());
    cmd->op = kOP_rcOpenColorBuffer;
    cmd->size = kSize_rcOpenColorBuffer;
    cmd->id = id;

    WriteLocked(kSize_rcOpenColorBuffer);
}

void Display::CloseColorBufferLocked(uint32_t id) {
    TRACE_DURATION("gfx", "Display::CloseColorBuffer", "id", id);

    auto cmd = static_cast<CloseColorBufferCmd*>(io_buffer_.virt());
    cmd->op = kOP_rcCloseColorBuffer;
    cmd->size = kSize_rcCloseColorBuffer;
    cmd->id = id;

    WriteLocked(kSize_rcCloseColorBuffer);
}

zx_status_t Display::UpdateColorBufferLocked(uint32_t id, zx_paddr_t paddr,
                                             uint32_t width, uint32_t height,
                                             size_t size, uint32_t* result) {
    TRACE_DURATION("gfx", "Display::UpdateColorBuffer", "size", size);

    auto cmd = static_cast<UpdateColorBufferCmd*>(io_buffer_.virt());
    cmd->op = kOP_rcUpdateColorBuffer;
    cmd->size = kSize_rcUpdateColorBuffer + static_cast<uint32_t>(size);
    cmd->id = id;
    cmd->x = 0;
    cmd->y = 0;
    cmd->width = width;
    cmd->height = height;
    cmd->format = GL_RGBA;
    cmd->type = GL_UNSIGNED_BYTE;

    auto buffer = static_cast<pipe_cmd_buffer_t*>(cmd_buffer_.virt());
    buffer->id = id_;
    buffer->cmd = PIPE_CMD_CODE_WRITE;
    buffer->status = PIPE_ERROR_INVAL;
    buffer->rw_params.ptrs[0] = io_buffer_.phys();
    buffer->rw_params.ptrs[1] = paddr;
    buffer->rw_params.sizes[0] = kSize_rcUpdateColorBuffer;
    buffer->rw_params.sizes[1] = static_cast<uint32_t>(size);
    buffer->rw_params.buffers_count = 2;
    buffer->rw_params.consumed_size = 0;

    pipe_.Exec(id_);
    ZX_DEBUG_ASSERT(buffer->rw_params.consumed_size ==
                    static_cast<int32_t>(kSize_rcUpdateColorBuffer + size));

    return ReadResultLocked(result);
}

void Display::FbPostLocked(uint32_t id) {
    TRACE_DURATION("gfx", "Display::FbPost", "id", id);

    auto cmd = static_cast<FbPostCmd*>(io_buffer_.virt());
    cmd->op = kOP_rcFbPost;
    cmd->size = kSize_rcFbPost;
    cmd->id = id;

    WriteLocked(kSize_rcFbPost);
}

int Display::FlushHandler() {
    zx_time_t next_deadline = zx_clock_get_monotonic();
    zx_time_t period = ZX_SEC(1) / kRefreshRateHz;

    while (1) {
        zx_nanosleep(next_deadline);

        ColorBuffer* displayed_fb;
        {
            fbl::AutoLock lock(&flush_lock_);

            if (shutdown_)
                break;

            displayed_fb = current_fb_;
        }

        if (displayed_fb) {
            fbl::AutoLock lock(&lock_);

            if (displayed_fb->paddr) {
                uint32_t result;
                zx_status_t status = UpdateColorBufferLocked(
                    displayed_fb->id, displayed_fb->paddr,
                    displayed_fb->width, displayed_fb->height,
                    displayed_fb->size, &result);
                if (status != ZX_OK || result) {
                    zxlogf(ERROR, "%s: color buffer update failed\n", kTag);
                    continue;
                }
            }

            FbPostLocked(displayed_fb->id);
        }

        {
            fbl::AutoLock lock(&flush_lock_);

            if (dc_intf_.is_valid()) {
                uint64_t handles[] = {reinterpret_cast<uint64_t>(displayed_fb)};
                dc_intf_.OnDisplayVsync(kDisplayId, next_deadline, handles,
                                        displayed_fb ? 1 : 0);
            }
        }

        next_deadline = zx_time_add_duration(next_deadline, period);
    }

    return 0;
}

} // namespace goldfish

static constexpr zx_driver_ops_t goldfish_display_driver_ops = []() -> zx_driver_ops_t {
    zx_driver_ops_t ops = {};
    ops.version = DRIVER_OPS_VERSION;
    ops.bind = goldfish::Display::Create;
    return ops;
}();

// clang-format off
ZIRCON_DRIVER_BEGIN(goldfish_display, goldfish_display_driver_ops, "zircon",
                    "0.1", 1)
    BI_MATCH_IF(EQ, BIND_PROTOCOL, ZX_PROTOCOL_GOLDFISH_CONTROL),
ZIRCON_DRIVER_END(goldfish_display)
// clang-format on
