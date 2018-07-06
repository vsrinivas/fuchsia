// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/debug.h>
#include <fbl/auto_lock.h>
#include <fbl/limits.h>
#include <lib/edid/edid.h>
#include <lib/fidl/cpp/builder.h>
#include <lib/fidl/cpp/message.h>
#include <lib/async/cpp/task.h>
#include <math.h>
#include <zircon/device/display-controller.h>

#include "client.h"

#define DC_IMPL_CALL(fn, ...) controller_->ops()->fn(controller_->ops_ctx(), __VA_ARGS__)
#define SELECT_TABLE_CASE(NAME) case NAME ## Ordinal: table = &NAME ## RequestTable; break
#define HANDLE_REQUEST_CASE(NAME) \
    case fuchsia_display_Controller ## NAME ## Ordinal: { \
        auto req = reinterpret_cast<const fuchsia_display_Controller ## NAME ## Request*>(msg.bytes().data()); \
        Handle ## NAME (req, &builder, &out_type); \
        break; \
    }

namespace {

zx_status_t decode_message(fidl::Message* msg) {
    zx_status_t res;
    const fidl_type_t* table = nullptr;
    switch (msg->ordinal()) {
    SELECT_TABLE_CASE(fuchsia_display_ControllerImportVmoImage);
    SELECT_TABLE_CASE(fuchsia_display_ControllerReleaseImage);
    SELECT_TABLE_CASE(fuchsia_display_ControllerImportEvent);
    SELECT_TABLE_CASE(fuchsia_display_ControllerReleaseEvent);
    SELECT_TABLE_CASE(fuchsia_display_ControllerCreateLayer);
    SELECT_TABLE_CASE(fuchsia_display_ControllerDestroyLayer);
    SELECT_TABLE_CASE(fuchsia_display_ControllerSetDisplayMode);
    SELECT_TABLE_CASE(fuchsia_display_ControllerSetDisplayColorConversion);
    SELECT_TABLE_CASE(fuchsia_display_ControllerSetDisplayLayers);
    SELECT_TABLE_CASE(fuchsia_display_ControllerSetLayerPrimaryConfig);
    SELECT_TABLE_CASE(fuchsia_display_ControllerSetLayerPrimaryPosition);
    SELECT_TABLE_CASE(fuchsia_display_ControllerSetLayerPrimaryAlpha);
    SELECT_TABLE_CASE(fuchsia_display_ControllerSetLayerCursorConfig);
    SELECT_TABLE_CASE(fuchsia_display_ControllerSetLayerCursorPosition);
    SELECT_TABLE_CASE(fuchsia_display_ControllerSetLayerColorConfig);
    SELECT_TABLE_CASE(fuchsia_display_ControllerSetLayerImage);
    SELECT_TABLE_CASE(fuchsia_display_ControllerCheckConfig);
    SELECT_TABLE_CASE(fuchsia_display_ControllerApplyConfig);
    SELECT_TABLE_CASE(fuchsia_display_ControllerEnableVsync);
    SELECT_TABLE_CASE(fuchsia_display_ControllerSetVirtconMode);
    SELECT_TABLE_CASE(fuchsia_display_ControllerComputeLinearImageStride);
    SELECT_TABLE_CASE(fuchsia_display_ControllerAllocateVmo);
    }
    if (table != nullptr) {
        const char* err;
        if ((res = msg->Decode(table, &err)) != ZX_OK) {
            zxlogf(INFO, "Error decoding message %d: %s\n", msg->ordinal(), err);
        }
    } else {
        zxlogf(INFO, "Unknown fidl ordinal %d\n", msg->ordinal());
        res = ZX_ERR_NOT_SUPPORTED;
    }
    return res;
}

bool frame_contains(const frame_t& a, const frame_t& b) {
    return b.x_pos < a.width
            && b.y_pos < a.height
            && b.x_pos + b.width <= a.width
            && b.y_pos + b.height <= a.height;
}

// We allocate some variable sized stack allocations based on the number of
// layers, so we limit the total number of layers to prevent blowing the stack.
static constexpr uint64_t kMaxLayers = 65536;

static constexpr uint32_t kInvalidLayerType = UINT32_MAX;

uint32_t calculate_refresh_rate_e2(const edid::timing_params_t params) {
    double total_pxls =
            (params.horizontal_addressable + params.horizontal_blanking) *
            (params.vertical_addressable + params.vertical_blanking);
    double pixel_clock_hz = params.pixel_freq_10khz * 1000 * 10;
    return static_cast<uint32_t>(round(100 * pixel_clock_hz / total_pxls));
}

void populate_display_mode(const edid::timing_params_t params, display_mode_t* mode) {
    mode->pixel_clock_10khz = params.pixel_freq_10khz;
    mode->h_addressable = params.horizontal_addressable;
    mode->h_front_porch = params.horizontal_front_porch;
    mode->h_sync_pulse = params.horizontal_sync_pulse;
    mode->h_blanking = params.horizontal_blanking;
    mode->v_addressable = params.vertical_addressable;
    mode->v_front_porch = params.vertical_front_porch;
    mode->v_sync_pulse = params.vertical_sync_pulse;
    mode->v_blanking = params.vertical_blanking;
    mode->pixel_clock_10khz = params.pixel_freq_10khz;
    mode->mode_flags = (params.vertical_sync_polarity ? MODE_FLAG_VSYNC_POSITIVE : 0)
            | (params.horizontal_sync_polarity ? MODE_FLAG_HSYNC_POSITIVE : 0);
}

// Removes and invokes EarlyRetire on all entries before end.
static void do_early_retire(list_node_t* list, display::image_node_t* end = nullptr) {
    display::image_node_t* node;
    while ((node = list_peek_head_type(list, display::image_node_t, link)) != end) {
        node->self->EarlyRetire();
        node->self.reset();
        list_remove_head(list);
    }
}

static void populate_image(const fuchsia_display_ImageConfig& image, image_t* image_out) {
    static_assert(offsetof(image_t, width) ==
            offsetof(fuchsia_display_ImageConfig, width), "Struct mismatch");
    static_assert(offsetof(image_t, height) ==
            offsetof(fuchsia_display_ImageConfig, height), "Struct mismatch");
    static_assert(offsetof(image_t, pixel_format) ==
            offsetof(fuchsia_display_ImageConfig, pixel_format), "Struct mismatch");
    static_assert(offsetof(image_t, type) ==
            offsetof(fuchsia_display_ImageConfig, type), "Struct mismatch");
    memcpy(image_out, &image, sizeof(fuchsia_display_ImageConfig));
}

} // namespace

namespace display {

void Client::HandleControllerApi(async_t* async, async::WaitBase* self,
                                 zx_status_t status, const zx_packet_signal_t* signal) {
    if (status != ZX_OK) {
        zxlogf(INFO, "Unexpected status async status %d\n", status);
        ZX_DEBUG_ASSERT(false);
        return;
    } else if (signal->observed & ZX_CHANNEL_PEER_CLOSED) {
        zxlogf(TRACE, "Client closed\n");
        TearDown();
        return;
    }

    ZX_DEBUG_ASSERT(signal->observed & ZX_CHANNEL_READABLE);

    zx_handle_t in_handle;
    uint8_t in_byte_buffer[ZX_CHANNEL_MAX_MSG_BYTES];
    fidl::Message msg(fidl::BytePart(in_byte_buffer, ZX_CHANNEL_MAX_MSG_BYTES),
                      fidl::HandlePart(&in_handle, 1));
    status = msg.Read(server_handle_, 0);
    api_wait_.Begin(controller_->loop().async());

    if (status != ZX_OK) {
        zxlogf(TRACE, "Channel read failed %d\n", status);
        return;
    } else if ((status = decode_message(&msg)) != ZX_OK) {
        return;
    }

    uint8_t out_byte_buffer[ZX_CHANNEL_MAX_MSG_BYTES];
    fidl::Builder builder(out_byte_buffer, ZX_CHANNEL_MAX_MSG_BYTES);
    zx_handle_t out_handle = ZX_HANDLE_INVALID;
    bool has_out_handle = false;
    const fidl_type_t* out_type = nullptr;

    switch (msg.ordinal()) {
    HANDLE_REQUEST_CASE(ImportVmoImage);
    HANDLE_REQUEST_CASE(ReleaseImage);
    HANDLE_REQUEST_CASE(ImportEvent);
    HANDLE_REQUEST_CASE(ReleaseEvent);
    HANDLE_REQUEST_CASE(CreateLayer);
    HANDLE_REQUEST_CASE(DestroyLayer);
    HANDLE_REQUEST_CASE(SetDisplayMode);
    HANDLE_REQUEST_CASE(SetDisplayColorConversion);
    HANDLE_REQUEST_CASE(SetDisplayLayers);
    HANDLE_REQUEST_CASE(SetLayerPrimaryConfig);
    HANDLE_REQUEST_CASE(SetLayerPrimaryPosition);
    HANDLE_REQUEST_CASE(SetLayerPrimaryAlpha);
    HANDLE_REQUEST_CASE(SetLayerCursorConfig);
    HANDLE_REQUEST_CASE(SetLayerCursorPosition);
    HANDLE_REQUEST_CASE(SetLayerColorConfig);
    HANDLE_REQUEST_CASE(SetLayerImage);
    HANDLE_REQUEST_CASE(CheckConfig);
    HANDLE_REQUEST_CASE(ApplyConfig);
    HANDLE_REQUEST_CASE(EnableVsync);
    HANDLE_REQUEST_CASE(SetVirtconMode);
    HANDLE_REQUEST_CASE(ComputeLinearImageStride);
    case fuchsia_display_ControllerAllocateVmoOrdinal: {
        auto r = reinterpret_cast<const fuchsia_display_ControllerAllocateVmoRequest*>(msg.bytes().data());
        HandleAllocateVmo(r, &builder, &out_handle, &has_out_handle, &out_type);
        break;
    }
    default:
        zxlogf(INFO, "Unknown ordinal %d\n", msg.ordinal());
    }

    fidl::BytePart resp_bytes = builder.Finalize();
    if (resp_bytes.actual() != 0) {
        ZX_DEBUG_ASSERT(out_type != nullptr);

        fidl::Message resp(fbl::move(resp_bytes),
                           fidl::HandlePart(&out_handle, 1, has_out_handle ? 1 : 0));
        resp.header() = msg.header();

        const char* err_msg;
        ZX_DEBUG_ASSERT_MSG(resp.Validate(out_type, &err_msg) == ZX_OK,
                            "Error validating fidl response \"%s\"\n", err_msg);
        if ((status = resp.Write(server_handle_, 0)) != ZX_OK) {
            zxlogf(ERROR, "Error writing response message %d\n", status);
        }
    }
}

void Client::HandleImportVmoImage(const fuchsia_display_ControllerImportVmoImageRequest* req,
                                  fidl::Builder* resp_builder, const fidl_type_t** resp_table) {
    auto resp = resp_builder->New<fuchsia_display_ControllerImportVmoImageResponse>();
    *resp_table = &fuchsia_display_ControllerImportVmoImageResponseTable;

    zx::vmo vmo(req->vmo);

    image_t dc_image;
    dc_image.height = req->image_config.height;
    dc_image.width = req->image_config.width;
    dc_image.pixel_format = req->image_config.pixel_format;
    dc_image.type = req->image_config.type;
    resp->res = DC_IMPL_CALL(import_vmo_image, &dc_image, vmo.get(), req->offset);

    if (resp->res == ZX_OK) {
        fbl::AllocChecker ac;
        auto image = fbl::AdoptRef(new (&ac) Image(controller_, dc_image, fbl::move(vmo)));
        if (!ac.check()) {
            DC_IMPL_CALL(release_image, &dc_image);

            resp->res = ZX_ERR_NO_MEMORY;
            return;
        }

        image->id = next_image_id_++;
        resp->image_id = image->id;
        images_.insert(fbl::move(image));
    }
}

void Client::HandleReleaseImage(const fuchsia_display_ControllerReleaseImageRequest* req,
                                fidl::Builder* resp_builder, const fidl_type_t** resp_table) {
    auto image = images_.find(req->image_id);
    if (!image.IsValid()) {
        return;
    }

    if (CleanUpImage(&(*image))) {
        ApplyConfig();
    }
}

void Client::HandleImportEvent(const fuchsia_display_ControllerImportEventRequest* req,
                               fidl::Builder* resp_builder, const fidl_type_t** resp_table) {
    zx::event event(req->event);
    zx_status_t status = ZX_ERR_INVALID_ARGS;
    bool success = false;

    fbl::AutoLock lock(&fence_mtx_);

    // TODO(stevensd): it would be good for this not to be able to fail due to allocation failures
    if (req->id != INVALID_ID) {
        auto fence = fences_.find(req->id);
        if (!fence.IsValid()) {
            fbl::AllocChecker ac;
            auto new_fence = fbl::AdoptRef(new (&ac) Fence(this, controller_->loop().async(),
                                                           req->id, fbl::move(event)));
            if (ac.check() && new_fence->CreateRef()) {
                fences_.insert_or_find(fbl::move(new_fence));
                success = true;
            }
        } else {
            success = fence->CreateRef();
        }
    }

    if (!success) {
        zxlogf(ERROR, "Failed to import event#%ld (%d)\n", req->id, status);
        TearDown();
    }
}

void Client::HandleReleaseEvent(const fuchsia_display_ControllerReleaseEventRequest* req,
                                fidl::Builder* resp_builder, const fidl_type_t** resp_table) {
    // Hold a ref to prevent double locking if this destroys the fence.
    auto fence_ref = GetFence(req->id);
    if (fence_ref) {
        fbl::AutoLock lock(&fence_mtx_);
        fences_.find(req->id)->ClearRef();
    }
}

void Client::HandleCreateLayer(const fuchsia_display_ControllerCreateLayerRequest* req,
                               fidl::Builder* resp_builder, const fidl_type_t** resp_table) {
    auto resp = resp_builder->New<fuchsia_display_ControllerCreateLayerResponse>();
    *resp_table = &fuchsia_display_ControllerCreateLayerResponseTable;

    if (layers_.size() == kMaxLayers) {
        resp->res = ZX_ERR_NO_RESOURCES;
        return;
    }

    fbl::AllocChecker ac;
    auto new_layer = fbl::make_unique_checked<Layer>(&ac);
    if (!ac.check()) {
        resp->res = ZX_ERR_NO_MEMORY;
        return;
    }
    resp->layer_id = next_layer_id++;

    memset(&new_layer->pending_layer_, 0, sizeof(layer_t));
    memset(&new_layer->current_layer_, 0, sizeof(layer_t));
    new_layer->config_change_ = false;
    new_layer->pending_node_.layer = new_layer.get();
    new_layer->current_node_.layer = new_layer.get();
    new_layer->current_display_id_ = INVALID_DISPLAY_ID;
    new_layer->id = resp->layer_id;
    new_layer->current_layer_.type = kInvalidLayerType;
    new_layer->pending_layer_.type = kInvalidLayerType;

    layers_.insert(fbl::move(new_layer));

    resp->res = ZX_OK;
}

void Client::HandleDestroyLayer(const fuchsia_display_ControllerDestroyLayerRequest* req,
                                fidl::Builder* resp_builder, const fidl_type_t** resp_table) {
    auto layer = layers_.find(req->layer_id);
    if (!layer.IsValid()) {
        zxlogf(ERROR, "Tried to destroy invalid layer %ld\n", req->layer_id);
        TearDown();
        return;
    }
    if (layer->current_node_.InContainer() || layer->pending_node_.InContainer()) {
        zxlogf(ERROR, "Destroyed layer %ld which was in use\n", req->layer_id);
        TearDown();
        return;
    }

    auto destroyed = layers_.erase(req->layer_id);
    if (destroyed->pending_image_) {
        destroyed->pending_image_->DiscardAcquire();
    }
    do_early_retire(&destroyed->waiting_images_);
    if (destroyed->displayed_image_) {
        destroyed->displayed_image_->StartRetire();
    }
}

void Client::HandleSetDisplayMode(const fuchsia_display_ControllerSetDisplayModeRequest* req,
                                  fidl::Builder* resp_builder, const fidl_type_t** resp_table) {
    auto config = configs_.find(req->display_id);
    if (!config.IsValid()) {
        return;
    }

    fbl::AutoLock lock(controller_->mtx());
    const edid::Edid* edid;
    const display_params_t* params;
    controller_->GetPanelConfig(req->display_id, &edid, &params);

    if (edid) {
        for (auto timings = edid->begin(); timings != edid->end(); ++timings) {
            if ((*timings).horizontal_addressable == req->mode.horizontal_resolution
                    && (*timings).vertical_addressable == req->mode.vertical_resolution
                    && calculate_refresh_rate_e2(*timings) == req->mode.refresh_rate_e2) {
                populate_display_mode(*timings, &config->pending_.mode);
                pending_config_valid_ = false;
                config->display_config_change_ = true;
                return;
            }
        }
        zxlogf(ERROR, "Invalid display mode\n");
    } else {
        zxlogf(ERROR, "Failed to find edid when setting display mode\n");
    }

    TearDown();
}

void Client::HandleSetDisplayColorConversion(
        const fuchsia_display_ControllerSetDisplayColorConversionRequest* req,
        fidl::Builder* resp_builder, const fidl_type_t** resp_table) {
    auto config = configs_.find(req->display_id);
    if (!config.IsValid()) {
        return;
    }

    config->pending_.cc_flags = 0;
    if (!isnan(req->preoffsets[0])) {
        config->pending_.cc_flags |= COLOR_CONVERSION_PREOFFSET;
        memcpy(config->pending_.cc_preoffsets, req->preoffsets, sizeof(req->preoffsets));
        static_assert(sizeof(req->preoffsets) == sizeof(config->pending_.cc_preoffsets), "");
    }

    if (!isnan(req->coefficients[0])) {
        config->pending_.cc_flags |= COLOR_CONVERSION_COEFFICIENTS;
        memcpy(config->pending_.cc_coefficients, req->coefficients, sizeof(req->coefficients));
        static_assert(sizeof(req->coefficients) == sizeof(config->pending_.cc_coefficients), "");
    }

    if (!isnan(req->postoffsets[0])) {
        config->pending_.cc_flags |= COLOR_CONVERSION_POSTOFFSET;
        memcpy(config->pending_.cc_postoffsets, req->postoffsets, sizeof(req->postoffsets));
        static_assert(sizeof(req->postoffsets) == sizeof(config->pending_.cc_postoffsets), "");
    }

    config->display_config_change_ = true;
    pending_config_valid_ = false;
}

void Client::HandleSetDisplayLayers(const fuchsia_display_ControllerSetDisplayLayersRequest* req,
                                    fidl::Builder* resp_builder, const fidl_type_t** resp_table) {
    auto config = configs_.find(req->display_id);
    if (!config.IsValid()) {
        return;
    }

    config->pending_layer_change_ = true;
    config->pending_layers_.clear();
    uint64_t* layer_ids = static_cast<uint64_t*>(req->layer_ids.data);
    for (uint64_t i = req->layer_ids.count - 1; i != UINT64_MAX; i--) {
        auto layer = layers_.find(layer_ids[i]);
        if (!layer.IsValid() || layer->pending_node_.InContainer()) {
            zxlogf(ERROR, "Tried to reuse an in-use layer\n");
            TearDown();
            return;
        }
        layer->pending_layer_.z_index = static_cast<uint32_t>(i);
        config->pending_layers_.push_front(&layer->pending_node_);
    }
    config->pending_.layer_count = static_cast<int32_t>(req->layer_ids.count);
    pending_config_valid_ = false;
}

void Client::HandleSetLayerPrimaryConfig(
        const fuchsia_display_ControllerSetLayerPrimaryConfigRequest* req,
        fidl::Builder* resp_builder, const fidl_type_t** resp_table) {
    auto layer = layers_.find(req->layer_id);
    if (!layer.IsValid()) {
        zxlogf(ERROR, "SetLayerPrimaryConfig on invalid layer\n");
        TearDown();
        return;
    }

    layer->pending_layer_.type = LAYER_PRIMARY;
    primary_layer_t* primary_layer = &layer->pending_layer_.cfg.primary;

    populate_image(req->image_config, &primary_layer->image);

    // Initialize the src_frame and dest_frame with the default, full-image frame.
    frame_t new_frame = {
        .x_pos = 0, .y_pos = 0,
        .width = req->image_config.width, .height = req->image_config.height,
    };
    memcpy(&primary_layer->src_frame, &new_frame, sizeof(frame_t));
    memcpy(&primary_layer->dest_frame, &new_frame, sizeof(frame_t));

    primary_layer->transform_mode = FRAME_TRANSFORM_IDENTITY;

    layer->pending_image_ = nullptr;
    layer->config_change_ = true;
    pending_config_valid_ = false;
}

void Client::HandleSetLayerPrimaryPosition(
        const fuchsia_display_ControllerSetLayerPrimaryPositionRequest* req,
        fidl::Builder* resp_builder, const fidl_type_t** resp_table) {
    auto layer = layers_.find(req->layer_id);
    if (!layer.IsValid() || layer->pending_layer_.type != LAYER_PRIMARY) {
        zxlogf(ERROR, "SetLayerPrimaryPosition on invalid layer\n");
        TearDown();
        return;
    }
    if (req->transform > fuchsia_display_Transform_ROT_90_REFLECT_Y) {
        zxlogf(ERROR, "Invalid transform %d\n", req->transform);
        TearDown();
        return;
    }
    primary_layer_t* primary_layer = &layer->pending_layer_.cfg.primary;

    static_assert(sizeof(fuchsia_display_Frame) == sizeof(frame_t), "Struct mismatch");
    static_assert(offsetof(fuchsia_display_Frame, x_pos) ==
            offsetof(frame_t, x_pos), "Struct mismatch");
    static_assert(offsetof(fuchsia_display_Frame, y_pos) ==
            offsetof(frame_t, y_pos), "Struct mismatch");
    static_assert(offsetof(fuchsia_display_Frame, width) ==
            offsetof(frame_t, width), "Struct mismatch");
    static_assert(offsetof(fuchsia_display_Frame, height) ==
            offsetof(frame_t, height), "Struct mismatch");

    memcpy(&primary_layer->src_frame, &req->src_frame, sizeof(frame_t));
    memcpy(&primary_layer->dest_frame, &req->dest_frame, sizeof(frame_t));
    primary_layer->transform_mode = req->transform;

    layer->config_change_ = true;
    pending_config_valid_ = false;
}

void Client::HandleSetLayerPrimaryAlpha(
        const fuchsia_display_ControllerSetLayerPrimaryAlphaRequest* req,
        fidl::Builder* resp_builder, const fidl_type_t** resp_table) {
    auto layer = layers_.find(req->layer_id);
    if (!layer.IsValid() || layer->pending_layer_.type != LAYER_PRIMARY) {
        zxlogf(ERROR, "SetLayerPrimaryAlpha on invalid layer\n");
        TearDown();
        return;
    }

    if (req->mode > fuchsia_display_AlphaMode_HW_MULTIPLY ||
            (!isnan(req->val) && (req->val < 0 || req->val > 1))) {
        zxlogf(ERROR, "Invalid args %d %f\n", req->mode, req->val);
        TearDown();
        return;
    }

    primary_layer_t* primary_layer = &layer->pending_layer_.cfg.primary;

    static_assert(fuchsia_display_AlphaMode_DISABLE == ALPHA_DISABLE, "Bad constant");
    static_assert(fuchsia_display_AlphaMode_PREMULTIPLIED == ALPHA_PREMULTIPLIED, "Bad constant");
    static_assert(fuchsia_display_AlphaMode_HW_MULTIPLY== ALPHA_HW_MULTIPLY, "Bad constant");

    primary_layer->alpha_mode = req->mode;
    primary_layer->alpha_layer_val = req->val;

    layer->config_change_ = true;
    pending_config_valid_ = false;
}

void Client::HandleSetLayerCursorConfig(
        const fuchsia_display_ControllerSetLayerCursorConfigRequest* req,
        fidl::Builder* resp_builder, const fidl_type_t** resp_table) {
    auto layer = layers_.find(req->layer_id);
    if (!layer.IsValid()) {
        zxlogf(ERROR, "SetLayerCursorConfig on invalid layer\n");
        TearDown();
        return;
    }

    layer->pending_layer_.type = LAYER_CURSOR;
    layer->pending_cursor_x_ = layer->pending_cursor_y_ = 0;


    cursor_layer_t* cursor_layer = &layer->pending_layer_.cfg.cursor;
    populate_image(req->image_config, &cursor_layer->image);

    layer->pending_image_ = nullptr;
    layer->config_change_ = true;
    pending_config_valid_ = false;
}

void Client::HandleSetLayerCursorPosition(
        const fuchsia_display_ControllerSetLayerCursorPositionRequest* req,
        fidl::Builder* resp_builder, const fidl_type_t** resp_table) {
    auto layer = layers_.find(req->layer_id);
    if (!layer.IsValid() || layer->pending_layer_.type != LAYER_CURSOR) {
        zxlogf(ERROR, "SetLayerCursorPosition on invalid layer\n");
        TearDown();
        return;
    }

    layer->pending_cursor_x_ = req->x;
    layer->pending_cursor_y_ = req->y;

    layer->config_change_ = true;
}

void Client::HandleSetLayerColorConfig(
        const fuchsia_display_ControllerSetLayerColorConfigRequest* req,
        fidl::Builder* resp_builder, const fidl_type_t** resp_table) {
    auto layer = layers_.find(req->layer_id);
    if (!layer.IsValid()) {
        zxlogf(ERROR, "SetLayerColorConfig on invalid layer\n");
        return;
    }

    if (req->color_bytes.count != ZX_PIXEL_FORMAT_BYTES(req->pixel_format)) {
        zxlogf(ERROR, "SetLayerColorConfig with invalid color bytes\n");
        TearDown();
        return;
    }
    // Increase the size of the static array when large color formats are introduced
    ZX_ASSERT(req->color_bytes.count <= sizeof(layer->pending_color_bytes_));

    layer->pending_layer_.type = LAYER_COLOR;
    color_layer_t* color_layer = &layer->pending_layer_.cfg.color;

    color_layer->format = req->pixel_format;
    memcpy(layer->pending_color_bytes_, req->color_bytes.data, sizeof(layer->pending_color_bytes_));

    layer->pending_image_ = nullptr;
    layer->config_change_ = true;
    pending_config_valid_ = false;
}

void Client::HandleSetLayerImage(const fuchsia_display_ControllerSetLayerImageRequest* req,
                                 fidl::Builder* resp_builder, const fidl_type_t** resp_table) {
    auto layer = layers_.find(req->layer_id);
    if (!layer.IsValid() || layer->pending_layer_.type == LAYER_COLOR) {
        zxlogf(ERROR, "SetLayerImage ordinal with invalid layer\n");
        TearDown();
        return;
    }
    auto image = images_.find(req->image_id);
    if (!image.IsValid() || !image->Acquire()) {
        zxlogf(ERROR, "SetLayerImage ordinal with %s image\n", image.IsValid() ? "invl" : "busy");
        TearDown();
        return;
    }
    // Only primary or cursor layers can have images
    ZX_ASSERT(layer->pending_layer_.type == LAYER_PRIMARY
            || layer->pending_layer_.type == LAYER_CURSOR);
    image_t* cur_image = layer->pending_layer_.type == LAYER_PRIMARY ?
            &layer->pending_layer_.cfg.primary.image : &layer->pending_layer_.cfg.cursor.image;
    if (!image->HasSameConfig(*cur_image)) {
        zxlogf(ERROR, "SetLayerImage with mismatch layer config\n");
        if (image.IsValid()) {
            image->DiscardAcquire();
        }
        TearDown();
        return;
    }

    layer->pending_image_ = image.CopyPointer();
    layer->pending_wait_event_id_ = req->wait_event_id;
    layer->pending_signal_event_id_ = req->signal_event_id;
}

void Client::HandleCheckConfig(const fuchsia_display_ControllerCheckConfigRequest* req,
                               fidl::Builder* resp_builder, const fidl_type_t** resp_table) {
    *resp_table = &fuchsia_display_ControllerCheckConfigResponseTable;

    pending_config_valid_ = CheckConfig(resp_builder);

    if (req->discard) {
        // Go through layers and release any pending resources they claimed
        for (auto& layer : layers_) {
            if (layer.pending_image_) {
                layer.pending_image_->DiscardAcquire();
                layer.pending_image_ = nullptr;
            }
            if (layer.config_change_) {
                layer.pending_layer_ = layer.current_layer_;
                layer.config_change_ = false;

                layer.pending_cursor_x_ = layer.current_cursor_x_;
                layer.pending_cursor_y_ = layer.current_cursor_y_;
            }

            memcpy(layer.pending_color_bytes_,layer.current_color_bytes_,
                   sizeof(layer.pending_color_bytes_));
        }
        // Reset each config's pending layers to their current layers. Clear
        // all displays first in case layers were moved between displays.
        for (auto& config : configs_) {
            config.pending_layers_.clear();
        }
        for (auto& config : configs_) {
            fbl::SinglyLinkedList<layer_node_t*> current_layers;
            for (auto& layer_node : config.current_layers_) {
                current_layers.push_front(&layer_node.layer->pending_node_);
            }
            while (!current_layers.is_empty()) {
                auto layer = current_layers.pop_front();
                config.pending_layers_.push_front(layer);
            }
            config.pending_layer_change_ = false;

            config.pending_ = config.current_;
            config.display_config_change_ = false;
        }
        pending_config_valid_ = true;
    }
}

void Client::HandleApplyConfig(const fuchsia_display_ControllerApplyConfigRequest* req,
                               fidl::Builder* resp_builder, const fidl_type_t** resp_table) {
    if (!pending_config_valid_) {
        pending_config_valid_ = CheckConfig(nullptr);
        if (!pending_config_valid_) {
            zxlogf(INFO, "Tried to apply invalid config\n");
            return;
        }
    }

    // First go through and reset any current layer lists that are changing, so
    // we don't end up trying to put an image into two lists.
    for (auto& display_config : configs_) {
        if (display_config.pending_layer_change_) {
            while (!display_config.current_layers_.is_empty()) {
                display_config.current_layers_.pop_front();
            }
        }
    }

    for (auto& display_config : configs_) {
        if (display_config.display_config_change_) {
            display_config.current_ = display_config.pending_;
            display_config.display_config_change_ = false;
        }

        // Put the pending image in the wait queue (the case where it's already ready
        // will be handled later). This needs to be done before migrating layers, as
        // that needs to know if there are any waiting images.
        for (auto layer_node : display_config.pending_layers_) {
            Layer* layer = layer_node.layer;
            if (layer->pending_image_) {
                layer_node.layer->pending_image_->PrepareFences(
                        GetFence(layer->pending_wait_event_id_),
                        GetFence(layer->pending_signal_event_id_));
                list_add_tail(&layer->waiting_images_, &layer->pending_image_->node.link);
                layer->pending_image_->node.self = fbl::move(layer->pending_image_);
            }
        }

        // If there was a layer change, update the current layers list.
        if (display_config.pending_layer_change_) {
            fbl::SinglyLinkedList<layer_node_t*> new_current;
            for (auto layer_node : display_config.pending_layers_) {
                new_current.push_front(&layer_node.layer->current_node_);
            }

            while (!new_current.is_empty()) {
                // Don't migrate images between displays if there are pending images. See
                // Controller::ApplyConfig for more details.
                auto* layer = new_current.pop_front();
                if (layer->layer->current_display_id_ != display_config.id
                        && layer->layer->displayed_image_
                        && !list_is_empty(&layer->layer->waiting_images_)) {
                    {
                        fbl::AutoLock lock(controller_->mtx());
                        layer->layer->displayed_image_->StartRetire();
                    }
                    layer->layer->displayed_image_ = nullptr;

                    // This doesn't need to be reset anywhere, since we really care about the last
                    // display this layer was shown on. Ignoring the 'null' display could cause
                    // unusual layer changes to trigger this unnecessary, but that's not wrong.
                    layer->layer->current_display_id_ = display_config.id;
                }
                layer->layer->current_layer_.z_index = layer->layer->pending_layer_.z_index;

                display_config.current_layers_.push_front(layer);
            }
            display_config.pending_layer_change_ = false;
            display_config.pending_apply_layer_change_ = true;
        }

        // Apply any pending configuration changes to active layers.
        for (auto layer_node : display_config.current_layers_) {
            Layer* layer = layer_node.layer;
            if (layer->config_change_) {
                layer->current_layer_ = layer->pending_layer_;
                layer->config_change_ = false;

                image_t* new_image_config = nullptr;
                if (layer->current_layer_.type == LAYER_PRIMARY) {
                    new_image_config = &layer->current_layer_.cfg.primary.image;
                } else if (layer->current_layer_.type == LAYER_CURSOR) {
                    new_image_config = &layer->current_layer_.cfg.cursor.image;

                    layer->current_cursor_x_ = layer->pending_cursor_x_;
                    layer->current_cursor_y_ = layer->pending_cursor_y_;

                    display_mode_t* mode = &display_config.current_.mode;
                    layer->current_layer_.cfg.cursor.x_pos =
                            fbl::clamp(layer->current_cursor_x_,
                                       -static_cast<int32_t>(new_image_config->width) + 1,
                                       static_cast<int32_t>(mode->h_addressable) - 1);
                    layer->current_layer_.cfg.cursor.y_pos =
                            fbl::clamp(layer->current_cursor_y_,
                                       -static_cast<int32_t>(new_image_config->height) + 1,
                                       static_cast<int32_t>(mode->v_addressable) - 1);
                    new_image_config = &layer->current_layer_.cfg.cursor.image;
                } else if (layer->current_layer_.type == LAYER_COLOR) {
                    memcpy(layer->current_color_bytes_, layer->pending_color_bytes_,
                           sizeof(layer->current_color_bytes_));
                    layer->current_layer_.cfg.color.color = layer->current_color_bytes_;
                } else {
                    // type is validated in ::CheckConfig, so something must be very wrong.
                    ZX_ASSERT(false);
                }

                if (new_image_config) {
                    // If the layer's image configuration changed, drop any waiting images
                    if (!list_is_empty(&layer->waiting_images_)
                            && !list_peek_head_type(&layer->waiting_images_, image_node_t, link)
                                    ->self->HasSameConfig(*new_image_config)) {
                        do_early_retire(&layer->waiting_images_);
                    }

                    // Either retire the displayed image if the configuration changed or
                    // put it back into the new layer_t configuration
                    if (layer->displayed_image_ != nullptr) {
                        if (!layer->displayed_image_->HasSameConfig(*new_image_config)) {
                            {
                                fbl::AutoLock lock(controller_->mtx());
                                layer->displayed_image_->StartRetire();
                            }
                            layer->displayed_image_ = nullptr;
                        } else {
                            new_image_config->handle = layer->displayed_image_->info().handle;
                        }
                    }
                }
            }
        }
    }
    // Overflow doesn't matter, since stamps only need to be unique until
    // the configuration is applied with vsync.
    client_apply_count_++;

    ApplyConfig();
}

void Client::HandleEnableVsync(const fuchsia_display_ControllerEnableVsyncRequest* req,
                               fidl::Builder* resp_builder, const fidl_type_t** resp_table) {
    fbl::AutoLock lock(controller_->mtx());
    proxy_->EnableVsync(req->enable);
}

void Client::HandleSetVirtconMode(const fuchsia_display_ControllerSetVirtconModeRequest* req,
                                fidl::Builder* resp_builder, const fidl_type_t** resp_table) {
    if (!is_vc_) {
        zxlogf(ERROR, "Illegal non-virtcon ownership\n");
        TearDown();
        return;
    }
    controller_->SetVcMode(req->mode);
}

void Client::HandleComputeLinearImageStride(
        const fuchsia_display_ControllerComputeLinearImageStrideRequest* req,
        fidl::Builder* resp_builder, const fidl_type_t** resp_table) {
    auto resp = resp_builder->New<fuchsia_display_ControllerComputeLinearImageStrideResponse>();
    *resp_table = &fuchsia_display_ControllerComputeLinearImageStrideResponseTable;
    resp->stride = DC_IMPL_CALL(compute_linear_stride, req->width, req->pixel_format);
}

void Client::HandleAllocateVmo(const fuchsia_display_ControllerAllocateVmoRequest* req,
                               fidl::Builder* resp_builder,
                               zx_handle_t* handle_out, bool* has_handle_out,
                               const fidl_type_t** resp_table) {
    auto resp = resp_builder->New<fuchsia_display_ControllerAllocateVmoResponse>();
    *resp_table = &fuchsia_display_ControllerAllocateVmoResponseTable;

    resp->res = DC_IMPL_CALL(allocate_vmo, req->size, handle_out);
    *has_handle_out = resp->res == ZX_OK;
    resp->vmo = *has_handle_out ? FIDL_HANDLE_PRESENT : FIDL_HANDLE_ABSENT;
}

bool Client::CheckConfig(fidl::Builder* resp_builder) {
    const display_config_t* configs[configs_.size()];
    layer_t* layers[layers_.size()];
    uint32_t layer_cfg_results[layers_.size()];
    uint32_t* display_cfg_results[configs_.size()];
    memset(layer_cfg_results, 0, layers_.size() * sizeof(uint32_t));

    fuchsia_display_ControllerCheckConfigResponse* resp = nullptr;
    if (resp_builder) {
        resp = resp_builder->New<fuchsia_display_ControllerCheckConfigResponse>();
        resp->res.count = 0;
        resp->res.data = reinterpret_cast<void*>(FIDL_ALLOC_PRESENT);
    }

    bool config_fail = false;
    int config_idx = 0;
    int layer_idx = 0;
    for (auto& display_config : configs_) {
        if (display_config.pending_layers_.is_empty()) {
            continue;
        }

        // Put this display's display_config_t* into the compact array
        configs[config_idx] = &display_config.pending_;

        // Set the index in the primary result array with this display's layer result array
        display_cfg_results[config_idx++] = layer_cfg_results + layer_idx;

        // Create this display's compact layer_t* array
        display_config.pending_.layers = layers + layer_idx;

        // Frame used for checking that each layer's dest_frame lies entirely
        // within the composed output.
        frame_t display_frame = {
            .x_pos = 0, .y_pos = 0,
            .width = display_config.pending_.mode.h_addressable,
            .height = display_config.pending_.mode.v_addressable,
        };

        // Do any work that needs to be done to make sure that the pending layer_t structs
        // are up to date, and validate that the configuration doesn't violate any API
        // constraints.
        for (auto& layer_node : display_config.pending_layers_) {
            layers[layer_idx++] = &layer_node.layer->pending_layer_;

            bool invalid = false;
            if (layer_node.layer->pending_layer_.type == LAYER_PRIMARY) {
                primary_layer_t* layer = &layer_node.layer->pending_layer_.cfg.primary;
                // Frame for checking that the layer's src_frame lies entirely
                // within the source image.
                frame_t image_frame = {
                    .x_pos = 0, .y_pos = 0,
                    .width = layer->image.width, .height = layer->image.height,
                };
                invalid = (!frame_contains(image_frame, layer->src_frame)
                        || !frame_contains(display_frame, layer->dest_frame));
            } else if (layer_node.layer->pending_layer_.type == LAYER_CURSOR) {
                // The image is already set, so nothing to do here, and there's
                // nothing that could make this invald.
            } else if (layer_node.layer->pending_layer_.type == LAYER_COLOR) {
                // There aren't any API constraints on valid colors.
                layer_node.layer->pending_layer_.cfg.color.color =
                        layer_node.layer->pending_color_bytes_;
            } else {
                invalid = true;
            }

            if (invalid) {
                // Populate the response and continue to the next display, since
                // there's nothing more to check for this one.
                if (resp) {
                    resp->res.count++;
                    auto disp_res = resp_builder->New<fuchsia_display_ConfigResult>();
                    disp_res->display_id = display_config.id;
                    disp_res->error = fuchsia_display_ConfigError_INVALID_CONFIG;
                    disp_res->layers.count = 0;
                    disp_res->layers.data = (void*) FIDL_ALLOC_PRESENT;
                    disp_res->client_ops = disp_res->layers;
                }
                config_fail = true;
                break;
            }
        }
    }

    if (config_fail) {
        // If the config is invalid, there's no point in sending it to the impl driver.
        return false;
    }

    DC_IMPL_CALL(check_configuration, configs, display_cfg_results, config_idx);

    // Count the number of displays that had an error
    int display_fail_count = 0;
    for (int i = 0; i < config_idx; i++) {
        for (unsigned j = 0; j < configs[i]->layer_count; j++) {
            if (display_cfg_results[i][j]) {
                display_fail_count++;
                break;
            }
        }
    }

    // If there is a response builder, allocate the response
    fuchsia_display_ConfigResult* display_failures = nullptr;
    if (resp_builder && display_fail_count) {
        resp->res.count = display_fail_count;
        display_failures = resp_builder->NewArray<fuchsia_display_ConfigResult>(display_fail_count);
    }

    // Return unless we need to finish constructing the response
    if (display_fail_count == 0) {
        return true;
    } else if (!resp_builder) {
        return false;
    }

    static_assert((1 << fuchsia_display_ClientCompositionOp_CLIENT_USE_PRIMARY)
            == CLIENT_USE_PRIMARY, "Const mismatch");
    static_assert((1 << fuchsia_display_ClientCompositionOp_CLIENT_MERGE_BASE)
            == CLIENT_MERGE_BASE, "Const mismatch");
    static_assert((1 << fuchsia_display_ClientCompositionOp_CLIENT_MERGE_SRC)
            == CLIENT_MERGE_SRC, "Const mismatch");
    static_assert((1 << fuchsia_display_ClientCompositionOp_CLIENT_FRAME_SCALE)
            == CLIENT_FRAME_SCALE, "Const mismatch");
    static_assert((1 << fuchsia_display_ClientCompositionOp_CLIENT_SRC_FRAME)
            == CLIENT_SRC_FRAME, "Const mismatch");
    static_assert((1 << fuchsia_display_ClientCompositionOp_CLIENT_TRANSFORM)
            == CLIENT_TRANSFORM, "Const mismatch");
    static_assert((1 << fuchsia_display_ClientCompositionOp_CLIENT_COLOR_CONVERSION)
            == CLIENT_COLOR_CONVERSION, "Const mismatch");
    static_assert((1 << fuchsia_display_ClientCompositionOp_CLIENT_ALPHA)
            == CLIENT_ALPHA, "Const mismatch");
    constexpr uint32_t kAllErrors = (CLIENT_ALPHA << 1) - 1;

    config_idx = 0;
    layer_idx = 0;
    for (auto& display_config : configs_) {
        if (display_config.pending_layers_.is_empty()) {
            continue;
        }

        // Count how many layer errors were on this display
        int fail_count = 0;
        int32_t start_layer_idx = layer_idx;
        bool seen_base = false;
        for (__UNUSED auto& layer_node : display_config.pending_layers_) {
            uint32_t err = kAllErrors & layer_cfg_results[layer_idx];
            // Fixup the error flags if the driver impl incorrectly set multiple MERGE_BASEs
            if (err & CLIENT_MERGE_BASE) {
                if (seen_base) {
                    err &= ~CLIENT_MERGE_BASE;
                    err |= CLIENT_MERGE_SRC;
                } else {
                    seen_base = true;
                    err &= ~CLIENT_MERGE_SRC;
                }
            }
            layer_cfg_results[layer_idx++] = err;

            while (err) {
                fail_count += (err & 1);
                err >>= 1;
            }
        }

        if (fail_count == 0) {
            continue;
        }
        layer_idx = start_layer_idx;

        // Populate this display's layer errors
        display_failures->display_id = display_config.id;
        display_failures->layers.data = (void*) FIDL_ALLOC_PRESENT;
        display_failures->layers.count = fail_count;
        display_failures->client_ops.data = (void*) FIDL_ALLOC_PRESENT;
        display_failures->client_ops.count = fail_count;
        display_failures++;

        uint64_t* fail_layers = resp_builder->NewArray<uint64_t>(fail_count);
        fuchsia_display_ClientCompositionOp* fail_ops =
                resp_builder->NewArray<fuchsia_display_ClientCompositionOp>(fail_count);

        for (auto& layer_node : display_config.pending_layers_) {
            uint32_t err = layer_cfg_results[layer_idx];
            for (uint8_t i = 0; i < 32; i++) {
                if (err & (1 << i)) {
                    *(fail_layers++) = layer_node.layer->id;
                    *(fail_ops++) = i;
                }
            }
            layer_idx++;
        }
        config_idx++;
    }
    return false;
}

void Client::ApplyConfig() {
    ZX_DEBUG_ASSERT(controller_->current_thread_is_loop());

    layer_t* layers[layers_.size()];
    int layer_idx = 0;
    for (auto& display_config : configs_) {
        display_config.current_.layer_count = 0;
        display_config.current_.layers = layers + layer_idx;
        display_config.vsync_layer_count_ = 0;

        // Displays with no current layers are filtered out in Controller::ApplyConfig,
        // after it updates its own image tracking logic.

        for (auto layer_node : display_config.current_layers_) {
            // Find the newest image which has become ready
            Layer* layer = layer_node.layer;
            image_node_t* node = list_peek_tail_type(&layer->waiting_images_, image_node_t, link);
            while (node != nullptr && !node->self->IsReady()) {
                node = list_prev_type(&layer->waiting_images_, &node->link, image_node_t, link);
            }
            if (node != nullptr) {
                if (layer->displayed_image_ != nullptr) {
                    // Start retiring the image which had been displayed
                    fbl::AutoLock lock(controller_->mtx());
                    layer->displayed_image_->StartRetire();
                } else {
                    // Turning on a new layer is a (pseudo) layer change
                    display_config.pending_apply_layer_change_ = true;
                }

                // Drop any images older than node.
                do_early_retire(&layer->waiting_images_, node);

                layer->displayed_image_ = fbl::move(node->self);
                list_remove_head(&layer->waiting_images_);

                void* handle = layer->displayed_image_->info().handle;
                if (layer->current_layer_.type == LAYER_PRIMARY) {
                    layer->current_layer_.cfg.primary.image.handle = handle;
                } else if (layer->current_layer_.type == LAYER_CURSOR) {
                    layer->current_layer_.cfg.cursor.image.handle = handle;
                } else {
                    // type is validated in ::CheckConfig, so something must be very wrong.
                    ZX_ASSERT(false);
                }
            }

            if (is_vc_) {
                if (layer->displayed_image_) {
                    // If the virtcon is displaying an image, set it as the kernel's framebuffer
                    // vmo. If the the virtcon is displaying images on multiple displays, this ends
                    // executing multiple times, but the extra work is okay since the virtcon
                    // shouldn't be flipping images.
                    console_fb_display_id_ = display_config.id;

                    auto fb = layer->displayed_image_;
                    uint32_t stride = DC_IMPL_CALL(compute_linear_stride,
                                                   fb->info().width, fb->info().pixel_format);
                    uint32_t size = fb->info().height *
                            ZX_PIXEL_FORMAT_BYTES(fb->info().pixel_format) * stride;
                    zx_framebuffer_set_range(get_root_resource(),
                                       fb->vmo().get(), size, fb->info().pixel_format,
                                       fb->info().width, fb->info().height, stride);
                } else if (console_fb_display_id_ == display_config.id) {
                    // If this display doesnt' have an image but it was the display which had the
                    // kernel's framebuffer, make the kernel drop the reference. Note that this
                    // executes when tearing down the the virtcon client.
                    zx_framebuffer_set_range(get_root_resource(), ZX_HANDLE_INVALID, 0, 0, 0, 0, 0);
                    console_fb_display_id_ = -1;
                }
            }

            // If the layer has no image, skip it
            layer->is_skipped_ =
                    layer->displayed_image_ == nullptr && layer->current_layer_.type != LAYER_COLOR;
            if (!layer->is_skipped_) {
                display_config.current_.layer_count++;
                layers[layer_idx++] = &layer->current_layer_;

                if (layer->displayed_image_) {
                    display_config.vsync_layer_count_++;
                }
            }
        }
    }

    if (is_owner_) {
        DisplayConfig* dc_configs[configs_.size()];
        int dc_idx = 0;
        for (auto& c : configs_) {
            dc_configs[dc_idx++] = &c;
        }
        controller_->ApplyConfig(dc_configs, dc_idx, is_vc_, client_apply_count_);
    }
}

void Client::SetOwnership(bool is_owner) {
    ZX_DEBUG_ASSERT(controller_->current_thread_is_loop());

    is_owner_ = is_owner;

    fuchsia_display_ControllerClientOwnershipChangeEvent msg;
    msg.hdr.ordinal = fuchsia_display_ControllerClientOwnershipChangeOrdinal;
    msg.has_ownership = is_owner;

    zx_status_t status = zx_channel_write(server_handle_, 0, &msg, sizeof(msg), nullptr, 0);
    if (status != ZX_OK) {
        zxlogf(ERROR, "Error writing remove message %d\n", status);
    }

    ApplyConfig();
}

void Client::OnDisplaysChanged(uint64_t* displays_added,
                               uint32_t added_count,
                               uint64_t* displays_removed, uint32_t removed_count) {
    ZX_DEBUG_ASSERT(controller_->current_thread_is_loop());

    uint8_t bytes[ZX_CHANNEL_MAX_MSG_BYTES];
    fidl::Builder builder(bytes, ZX_CHANNEL_MAX_MSG_BYTES);
    auto req = builder.New<fuchsia_display_ControllerDisplaysChangedEvent>();
    zx_status_t status;
    req->hdr.ordinal = fuchsia_display_ControllerDisplaysChangedOrdinal;
    req->added.count = 0;
    req->added.data = reinterpret_cast<void*>(FIDL_ALLOC_PRESENT);
    req->removed.count = removed_count;
    req->removed.data = reinterpret_cast<void*>(FIDL_ALLOC_PRESENT);

    for (unsigned i = 0; i < removed_count; i++) {
        auto display = configs_.erase(displays_removed[i]);
        if (display) {
            display->pending_layers_.clear();
            display->current_layers_.clear();
        }
    }

    {
        fbl::AutoLock lock(controller_->mtx());
        for (unsigned i = 0; i < added_count; i++) {
            fbl::AllocChecker ac;
            auto config = fbl::make_unique_checked<DisplayConfig>(&ac);
            if (!ac.check()) {
                zxlogf(WARN, "Out of memory when processing hotplug\n");
                continue;
            }

            config->id = displays_added[i];

            const edid::Edid* edid;
            const display_params_t* params;
            if (!controller_->GetPanelConfig(config->id, &edid, &params)) {
                // This can only happen if the display was already disconnected.
                zxlogf(WARN, "No config when adding display\n");
                continue;
            }
            req->added.count++;

            config->current_.display_id = config->id;
            config->current_.layers = nullptr;
            config->current_.layer_count = 0;

            if (edid) {
                auto timings = edid->begin();
                populate_display_mode(*timings, &config->current_.mode);
            } else {
                config->current_.mode = {};
                config->current_.mode.h_addressable = params->width;
                config->current_.mode.v_addressable = params->height;
            }

            config->current_.cc_flags = 0;

            config->pending_ = config->current_;

            if (!controller_->GetSupportedPixelFormats(config->id,
                                                       &config->pixel_format_count_,
                                                       &config->pixel_formats_)) {
                zxlogf(WARN, "Failed to get pixel formats when processing hotplug\n");
                continue;
            }

            if (!controller_->GetCursorInfo(config->id,
                                            &config->cursor_info_count_, &config->cursor_infos_)) {
                zxlogf(WARN, "Failed to get cursor info when processing hotplug\n");
                continue;
            }
            configs_.insert(fbl::move(config));
        }

        // We need 2 loops, since we need to make sure we allocate the
        // correct size array in the fidl response.
        fuchsia_display_Info* coded_configs = nullptr;
        if (req->added.count > 0) {
            coded_configs =
                    builder.NewArray<fuchsia_display_Info>(static_cast<uint32_t>(req->added.count));
        }

        for (unsigned i = 0; i < added_count; i++) {
            auto config = configs_.find(displays_added[i]);
            if (!config.IsValid()) {
                continue;
            }

            const edid::Edid* edid;
            const display_params_t* params;
            controller_->GetPanelConfig(config->id, &edid, &params);

            coded_configs[i].id = config->id;
            coded_configs[i].pixel_format.data = reinterpret_cast<void*>(FIDL_ALLOC_PRESENT);
            coded_configs[i].modes.data = reinterpret_cast<void*>(FIDL_ALLOC_PRESENT);
            coded_configs[i].cursor_configs.data = reinterpret_cast<void*>(FIDL_ALLOC_PRESENT);

            if (edid) {
                auto timings = edid->begin();

                coded_configs[i].modes.count = 0;
                while (timings != edid->end()) {
                    coded_configs[i].modes.count++;
                    auto mode = builder.New<fuchsia_display_Mode>();

                    mode->horizontal_resolution = (*timings).horizontal_addressable;
                    mode->vertical_resolution = (*timings).vertical_addressable;
                    mode->refresh_rate_e2 = calculate_refresh_rate_e2(*timings);

                    ++timings;
                }
            } else {
                coded_configs[i].modes.count = 1;
                auto mode = builder.New<fuchsia_display_Mode>();
                mode->horizontal_resolution = params->width;
                mode->vertical_resolution = params->height;
                mode->refresh_rate_e2 = params->refresh_rate_e2;
            }

            static_assert(sizeof(zx_pixel_format_t) == sizeof(int32_t), "Bad pixel format size");
            coded_configs[i].pixel_format.count = config->pixel_format_count_;
            memcpy(builder.NewArray<zx_pixel_format_t>(config->pixel_format_count_),
                   config->pixel_formats_.get(),
                   sizeof(zx_pixel_format_t) * config->pixel_format_count_);

            static_assert(offsetof(cursor_info_t, width) ==
                    offsetof(fuchsia_display_CursorInfo, width), "Bad struct");
            static_assert(offsetof(cursor_info_t, height) ==
                    offsetof(fuchsia_display_CursorInfo, height), "Bad struct");
            static_assert(offsetof(cursor_info_t, format) ==
                    offsetof(fuchsia_display_CursorInfo, pixel_format), "Bad struct");
            static_assert(sizeof(cursor_info_t) <= sizeof(fuchsia_display_CursorInfo), "Bad size");
            coded_configs[i].cursor_configs.count = config->cursor_info_count_;
            auto coded_cursor_configs =
                    builder.NewArray<fuchsia_display_CursorInfo>(config->cursor_info_count_);
            for (unsigned i = 0; i < config->cursor_info_count_; i++) {
                memcpy(&coded_cursor_configs[i], &config->cursor_infos_[i], sizeof(cursor_info_t));
            }
        }
    }

    if (removed_count > 0) {
        auto removed_ids = builder.NewArray<int32_t>(removed_count);
        memcpy(removed_ids, displays_removed, sizeof(int32_t) * removed_count);
    }

    fidl::Message msg(builder.Finalize(), fidl::HandlePart());
    const char* err;
    ZX_DEBUG_ASSERT_MSG(
            msg.Validate(&fuchsia_display_ControllerDisplaysChangedEventTable, &err) == ZX_OK,
            "Failed to validate \"%s\"", err);

    if ((status = msg.Write(server_handle_, 0)) != ZX_OK) {
        zxlogf(ERROR, "Error writing remove message %d\n", status);
    }
}

fbl::RefPtr<FenceReference> Client::GetFence(uint64_t id) {
    if (id == INVALID_ID) {
        return nullptr;
    }
    fbl::AutoLock lock(&fence_mtx_);
    auto fence = fences_.find(id);
    return fence.IsValid() ? fence->GetReference() : nullptr;
}

void Client::OnFenceFired(FenceReference* fence) {
    for (auto& layer: layers_) {
        image_node_t* waiting;
        list_for_every_entry(&layer.waiting_images_, waiting, image_node_t, link) {
            waiting->self->OnFenceReady(fence);
        }
    }
    ApplyConfig();
}

void Client::OnRefForFenceDead(Fence* fence) {
    fbl::AutoLock lock(&fence_mtx_);
    if (fence->OnRefDead()) {
        fences_.erase(fence->id);
    }
}

void Client::TearDown() {
    ZX_DEBUG_ASSERT(controller_->loop().GetState() == ASYNC_LOOP_SHUTDOWN
            || controller_->current_thread_is_loop());
    pending_config_valid_ = false;

    if (api_wait_.object() != ZX_HANDLE_INVALID) {
        api_wait_.Cancel();
        api_wait_.set_object(ZX_HANDLE_INVALID);
    }
    server_handle_ = ZX_HANDLE_INVALID;

    CleanUpImage(nullptr);

    // Use a temporary list to prevent double locking when resetting
    fbl::SinglyLinkedList<fbl::RefPtr<Fence>> fences;
    {
        fbl::AutoLock lock(&fence_mtx_);
        while (!fences_.is_empty()) {
            fences.push_front(fences_.erase(fences_.begin()));
        }
    }
    while (!fences.is_empty()) {
        fences.pop_front()->ClearRef();
    }

    for (auto& config : configs_) {
        config.pending_layers_.clear();
        config.current_layers_.clear();
    }

    // The layer's images have already been handled in CleanUpImageLayerState
    layers_.clear();

    ApplyConfig();

    proxy_->OnClientDead();
}

bool Client::CleanUpImage(Image* image) {
    // Clean up any fences associated with the image
    {
        fbl::AutoLock lock(controller_->mtx());
        if (image) {
            image->ResetFences();
        } else {
            for (auto& image : images_) {
                image.ResetFences();
            }
        }
    }

    // Clean up any layer state associated with the images
    bool current_config_change = false;
    for (auto& layer : layers_) {
        if (layer.pending_image_ && (image == nullptr || layer.pending_image_.get() == image)) {
            layer.pending_image_->DiscardAcquire();
            layer.pending_image_ = nullptr;
        }
        if (image == nullptr) {
            do_early_retire(&layer.waiting_images_, nullptr);
        } else {
            image_node_t* waiting;
            list_for_every_entry(&layer.waiting_images_, waiting, image_node_t, link) {
                if (waiting->self.get() == image) {
                    list_delete(&waiting->link);
                    waiting->self->EarlyRetire();
                    waiting->self.reset();
                    break;
                }
            }
        }
        if (layer.displayed_image_ && (image == nullptr || layer.displayed_image_.get() == image)) {
            {
                fbl::AutoLock lock(controller_->mtx());
                layer.displayed_image_->StartRetire();
            }
            layer.displayed_image_ = nullptr;

            if (layer.current_node_.InContainer()) {
                current_config_change = true;
            }
        }
    }

    // Clean up the image id map
    if (image) {
        images_.erase(*image);
    } else {
        images_.clear();
    }

    return current_config_change;
}

zx_status_t Client::Init(zx_handle_t server_handle) {
    zx_status_t status;

    api_wait_.set_object(server_handle);
    api_wait_.set_trigger(ZX_CHANNEL_READABLE | ZX_CHANNEL_PEER_CLOSED);
    if ((status = api_wait_.Begin(controller_->loop().async())) != ZX_OK) {
        // Clear the object, since that's used to detect whether or not api_wait_ is inited.
        api_wait_.set_object(ZX_HANDLE_INVALID);
        zxlogf(ERROR, "Failed to start waiting %d\n", status);
        return status;
    }

    server_handle_ = server_handle;
    mtx_init(&fence_mtx_, mtx_plain);

    return ZX_OK;
}

Client::Client(Controller* controller, ClientProxy* proxy, bool is_vc)
        : controller_(controller), proxy_(proxy), is_vc_(is_vc) { }

Client::~Client() {
    ZX_DEBUG_ASSERT(server_handle_ == ZX_HANDLE_INVALID);
}

void ClientProxy::SetOwnership(bool is_owner) {
    auto task = new async::Task();
    task->set_handler([client_handler = &handler_, is_owner]
                       (async_t* async, async::Task* task, zx_status_t status) {
            if (status == ZX_OK && client_handler->IsValid()) {
                client_handler->SetOwnership(is_owner);
            }

            delete task;

    });
    task->Post(controller_->loop().async());
}

zx_status_t ClientProxy::OnDisplaysChanged(const uint64_t* displays_added,
                                           uint32_t added_count, const uint64_t* displays_removed,
                                           uint32_t removed_count) {
    fbl::unique_ptr<uint64_t[]> added;
    fbl::unique_ptr<uint64_t[]> removed;

    fbl::AllocChecker ac;
    if (added_count) {
        added = fbl::unique_ptr<uint64_t[]>(new (&ac) uint64_t[added_count]);
        if (!ac.check()) {
            return ZX_ERR_NO_MEMORY;
        }
    }
    if (removed_count) {
        removed = fbl::unique_ptr<uint64_t[]>(new (&ac) uint64_t[removed_count]);
        if (!ac.check()) {
            return ZX_ERR_NO_MEMORY;
        }
    }

    memcpy(removed.get(), displays_removed, sizeof(*displays_removed) * removed_count);
    memcpy(added.get(), displays_added, sizeof(*displays_added) * added_count);

    auto task = new async::Task();
    task->set_handler([client_handler = &handler_,
                       added_ptr = added.release(), removed_ptr = removed.release(),
                       added_count, removed_count]
                       (async_t* async, async::Task* task, zx_status_t status) {
            if (status == ZX_OK && client_handler->IsValid()) {
                client_handler->OnDisplaysChanged(added_ptr, added_count,
                                                  removed_ptr, removed_count);
            }

            delete[] added_ptr;
            delete[] removed_ptr;
            delete task;
    });
    return task->Post(controller_->loop().async());
}

void ClientProxy::ReapplyConfig() {
    fbl::AllocChecker ac;
    auto task = new (&ac) async::Task();
    if (!ac.check()) {
        zxlogf(WARN, "Failed to reapply config\n");
        return;
    }

    task->set_handler([client_handler = &handler_]
                       (async_t* async, async::Task* task, zx_status_t status) {
            if (status == ZX_OK && client_handler->IsValid()) {
                client_handler->ApplyConfig();
            }

            delete task;

    });
    task->Post(controller_->loop().async());
}

void ClientProxy::OnDisplayVsync(uint64_t display_id, zx_time_t timestamp,
                                 uint64_t* image_ids, uint32_t count) {
    ZX_DEBUG_ASSERT(mtx_trylock(controller_->mtx()) == thrd_busy);

    if (!enable_vsync_) {
        return;
    }
    uint32_t size = static_cast<uint32_t>(
            sizeof(fuchsia_display_ControllerVsyncEvent) + sizeof(uint64_t) * count);
    uint8_t data[size];

    fuchsia_display_ControllerVsyncEvent* msg =
            reinterpret_cast<fuchsia_display_ControllerVsyncEvent*>(data);
    msg->hdr.ordinal = fuchsia_display_ControllerVsyncOrdinal;
    msg->display_id = display_id;
    msg->timestamp = timestamp;
    msg->images.count = count;
    msg->images.data = reinterpret_cast<void*>(FIDL_ALLOC_PRESENT);

    memcpy(msg + 1, image_ids, sizeof(uint64_t) * count);

    zx_status_t status = server_handle_.write(0, data, size, nullptr, 0);
    if (status != ZX_OK) {
        zxlogf(WARN, "Failed to send vsync event %d\n", status);
    }
}

void ClientProxy::OnClientDead() {
    controller_->OnClientDead(this);
}

void ClientProxy::Close() {
    if (controller_->current_thread_is_loop()) {
        handler_.TearDown();
    } else {
        mtx_t mtx;
        mtx_init(&mtx, mtx_plain);
        cnd_t cnd;
        cnd_init(&cnd);
        bool done = false;

        mtx_lock(&mtx);
        auto task = new async::Task();
        task->set_handler([client_handler = &handler_,
                           cnd_ptr = &cnd, mtx_ptr = &mtx, done_ptr = &done]
                           (async_t* async, async::Task* task, zx_status_t status) {
                mtx_lock(mtx_ptr);

                client_handler->TearDown();

                *done_ptr = true;
                cnd_signal(cnd_ptr);
                mtx_unlock(mtx_ptr);

                delete task;
        });
        if (task->Post(controller_->loop().async()) != ZX_OK) {
            // Tasks only fail to post if the looper is dead. That shouldn't actually
            // happen, but if it does then it's safe to call Reset on this thread anyway.
            delete task;
            handler_.TearDown();
        } else {
            while (!done) {
                cnd_wait(&cnd, &mtx);
            }
        }
        mtx_unlock(&mtx);
    }
}

zx_status_t ClientProxy::DdkIoctl(uint32_t op, const void* in_buf, size_t in_len, void* out_buf,
                             size_t out_len, size_t* actual) {
    switch (op) {
    case IOCTL_DISPLAY_CONTROLLER_GET_HANDLE: {
        if (out_len != sizeof(zx_handle_t)) {
            return ZX_ERR_INVALID_ARGS;
        }

        if (client_handle_.get() == ZX_HANDLE_INVALID) {
            return ZX_ERR_ALREADY_BOUND;
        }

        *reinterpret_cast<zx_handle_t*>(out_buf) = client_handle_.release();
        *actual = sizeof(zx_handle_t);
        return ZX_OK;
    }
    default:
        return ZX_ERR_NOT_SUPPORTED;
    }
}

zx_status_t ClientProxy::DdkClose(uint32_t flags) {
    Close();
    return ZX_OK;
}

void ClientProxy::DdkRelease() {
    delete this;
}

zx_status_t ClientProxy::Init() {
    zx_status_t status;
    if ((status = zx_channel_create(0, server_handle_.reset_and_get_address(),
                                    client_handle_.reset_and_get_address())) != ZX_OK) {
        zxlogf(ERROR, "Failed to create channels %d\n", status);
        return status;
    }

    return handler_.Init(server_handle_.get());
}

ClientProxy::ClientProxy(Controller* controller, bool is_vc)
        : ClientParent(controller->zxdev()),
          controller_(controller), is_vc_(is_vc), handler_(controller_, this, is_vc_) {}

ClientProxy::~ClientProxy() { }

} // namespace display
