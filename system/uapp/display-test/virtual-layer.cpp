// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fbl/algorithm.h>
#include <math.h>
#include <zircon/device/display-controller.h>

#include "utils.h"
#include "virtual-layer.h"

static constexpr uint32_t kSrcFrameBouncePeriod = 90;
static constexpr uint32_t kDestFrameBouncePeriod = 60;
static constexpr uint32_t kRotationPeriod = 24;


static uint32_t get_fg_color() {
    static uint32_t layer_count = 0;
    static uint32_t colors[] = {
        0xffff0000, 0xff00ff00, 0xff0000ff,
    };
    return colors[layer_count++ % fbl::count_of(colors)];
}

// Checks if two rectangles intersect, and if so, returns their intersection.
static bool compute_intersection(const frame_t& a, const frame_t& b, frame_t* intersection) {
    uint32_t left = fbl::max(a.x_pos, b.x_pos);
    uint32_t right = fbl::min(a.x_pos + a.width, b.x_pos + b.width);
    uint32_t top = fbl::max(a.y_pos, b.y_pos);
    uint32_t bottom = fbl::min(a.y_pos + a.height, b.y_pos + b.height);

    if (left >= right || top >= bottom) {
        return false;
    }

    intersection->x_pos = left;
    intersection->y_pos = top;
    intersection->width = right - left;
    intersection->height = bottom - top;

    return true;
}

VirtualLayer::VirtualLayer(Display* display) {
    displays_.push_back(display);
    width_ = display->mode().horizontal_resolution;
    height_ = display->mode().vertical_resolution;
}

VirtualLayer::VirtualLayer(const fbl::Vector<Display>& displays) {
    for (auto& d : displays) {
        displays_.push_back(&d);
    }

    width_ = 0;
    height_ = 0;
    for (auto* d : displays_) {
        width_ += d->mode().horizontal_resolution;
        height_ = fbl::max(height_, d->mode().vertical_resolution);
    }
}

layer_t* VirtualLayer::CreateLayer(zx_handle_t dc_handle) {
    layers_.push_back(layer_t());
    layers_[layers_.size() - 1].active = false;

    fuchsia_display_ControllerCreateLayerRequest create_layer_msg;
    create_layer_msg.hdr.ordinal = fuchsia_display_ControllerCreateLayerOrdinal;

    fuchsia_display_ControllerCreateLayerResponse create_layer_rsp;
    zx_channel_call_args_t call_args = {};
    call_args.wr_bytes = &create_layer_msg;
    call_args.rd_bytes = &create_layer_rsp;
    call_args.wr_num_bytes = sizeof(create_layer_msg);
    call_args.rd_num_bytes = sizeof(create_layer_rsp);
    uint32_t actual_bytes, actual_handles;
    if (zx_channel_call(dc_handle, 0, ZX_TIME_INFINITE, &call_args,
                        &actual_bytes, &actual_handles) != ZX_OK) {
        printf("Creating layer failed\n");
        return nullptr;
    }
    if (create_layer_rsp.res != ZX_OK) {
        printf("Creating layer failed\n");
        return nullptr;
    }
    layers_[layers_.size() - 1].id = create_layer_rsp.layer_id;

    return &layers_[layers_.size() - 1];
}

PrimaryLayer::PrimaryLayer(Display* display) : VirtualLayer(display) {
    image_format_ = display->format();
}

PrimaryLayer::PrimaryLayer(const fbl::Vector<Display>& displays) : VirtualLayer(displays) {
    image_format_ = displays_[0]->format();
    SetImageDimens(width_, height_);
}

bool PrimaryLayer::Init(zx_handle_t dc_handle) {
    uint32_t fg_color = get_fg_color();
    uint32_t bg_color = alpha_enable_ ? 0x3fffffff : 0xffffffff;

    images_[0] = Image::Create(
            dc_handle, image_width_, image_height_, image_format_, fg_color, bg_color, false);
    if (layer_flipping_) {
        images_[1] = Image::Create(
                dc_handle, image_width_, image_height_, image_format_, fg_color, bg_color, false);
    } else {
        images_[0]->Render(-1, -1);
    }

    if (!images_[0] || (layer_flipping_ && !images_[1])) {
        return false;
    }

    for (unsigned i = 0; i < displays_.size(); i++) {
        layer_t* layer = CreateLayer(dc_handle);
        if (layer == nullptr) {
            return false;
        }

        images_[0]->Import(dc_handle, &layer->import_info[0]);
        if (layer_flipping_) {
            images_[1]->Import(dc_handle, &layer->import_info[1]);
        } else {
            zx_object_signal(layer->import_info[alt_image_].events[WAIT_EVENT],
                             0, ZX_EVENT_SIGNALED);
        }

        fuchsia_display_ControllerSetLayerPrimaryConfigRequest config;
        config.hdr.ordinal = fuchsia_display_ControllerSetLayerPrimaryConfigOrdinal;
        config.layer_id = layer->id;
        config.image_config.height = image_height_;
        config.image_config.width = image_width_;
        config.image_config.pixel_format = image_format_;
#if !USE_INTEL_Y_TILING
        config.image_config.type = IMAGE_TYPE_SIMPLE;
#else
        config.image_config.type = 2; // IMAGE_TYPE_Y_LEGACY
#endif

        if (zx_channel_write(dc_handle, 0, &config, sizeof(config), nullptr, 0) != ZX_OK) {
            printf("Setting layer config failed\n");
            return false;
        }

        fuchsia_display_ControllerSetLayerPrimaryAlphaRequest alpha_config;
        alpha_config.hdr.ordinal = fuchsia_display_ControllerSetLayerPrimaryAlphaOrdinal;
        alpha_config.layer_id = layer->id;
        alpha_config.mode = alpha_enable_ ?
                fuchsia_display_AlphaMode_HW_MULTIPLY : fuchsia_display_AlphaMode_DISABLE;
        alpha_config.val = alpha_val_;

        if (zx_channel_write(dc_handle, 0,
                             &alpha_config, sizeof(alpha_config), nullptr, 0) != ZX_OK) {
            printf("Setting layer alpha config failed\n");
            return false;
        }
    }

    StepLayout(0);
    if (!layer_flipping_) {
        SetLayerImages(dc_handle, false);
    }
    if (!(pan_src_ || pan_dest_)) {
        SetLayerPositions(dc_handle);
    }

    return true;
}

void PrimaryLayer::StepLayout(int32_t frame_num) {
    if (layer_flipping_) {
        alt_image_ = frame_num % 2;
    }
    if (pan_src_) {
        src_frame_.x_pos =
                interpolate(image_width_ - src_frame_.width, frame_num, kSrcFrameBouncePeriod);

    }
    if (pan_dest_) {
        dest_frame_.x_pos =
                interpolate(width_ - dest_frame_.width, frame_num, kDestFrameBouncePeriod);
    }
    if (rotates_) {
        switch ((frame_num / kRotationPeriod) % 4) {
            case 0: rotation_ = fuchsia_display_Transform_IDENTITY; break;
            case 1: rotation_ = fuchsia_display_Transform_ROT_90; break;
            case 2: rotation_ = fuchsia_display_Transform_ROT_180; break;
            case 3: rotation_ = fuchsia_display_Transform_ROT_270; break;
        }

        if (frame_num % kRotationPeriod == 0 && frame_num != 0) {
            uint32_t tmp = dest_frame_.width;
            dest_frame_.width = dest_frame_.height;
            dest_frame_.height = tmp;
        }
    }

    frame_t display = {};
    for (unsigned i = 0; i < displays_.size(); i++) {
        display.height = displays_[i]->mode().vertical_resolution;
        display.width = displays_[i]->mode().horizontal_resolution;

        // Calculate the portion of the dest frame which shows up on this display
        if (compute_intersection(display, dest_frame_, &layers_[i].dest)) {
            // Find the subset of the src region which shows up on this display
            if (rotation_ == fuchsia_display_Transform_IDENTITY
                    || rotation_ == fuchsia_display_Transform_ROT_180) {
                layers_[i].src.x_pos =
                        src_frame_.x_pos + (layers_[i].dest.x_pos - dest_frame_.x_pos);
                layers_[i].src.y_pos = src_frame_.y_pos;
                layers_[i].src.width = layers_[i].dest.width;
                layers_[i].src.height = layers_[i].dest.height;
            } else {
                layers_[i].src.x_pos = src_frame_.x_pos;
                layers_[i].src.y_pos =
                        src_frame_.y_pos + (layers_[i].dest.y_pos - dest_frame_.y_pos);
                layers_[i].src.height = layers_[i].dest.width;
                layers_[i].src.width = layers_[i].dest.height;
            }

            // Put the dest frame coordinates in the display's coord space
            layers_[i].dest.x_pos -= display.x_pos;
            layers_[i].active = true;
        } else {
            layers_[i].active = false;
        }

        display.x_pos += display.width;
    }

    if (layer_toggle_) {
        for (auto& layer : layers_) {
            layer.active = !(frame_num % 2);
        }
    }
}

void PrimaryLayer::SendLayout(zx_handle_t channel) {
    if (layer_flipping_) {
        SetLayerImages(channel, alt_image_);
    }
    if (pan_src_ || pan_dest_) {
        SetLayerPositions(channel);
    }
}

bool PrimaryLayer::WaitForReady() {
    return Wait(SIGNAL_EVENT);
}

void PrimaryLayer::Render(int32_t frame_num) {
    if (!layer_flipping_) {
        return;
    }
    images_[alt_image_]->Render(frame_num < 2 ? 0 : frame_num - 2, frame_num);
    for (auto& layer : layers_) {
        zx_object_signal(layer.import_info[alt_image_].events[WAIT_EVENT], 0, ZX_EVENT_SIGNALED);
    }
}

void PrimaryLayer::SetLayerPositions(zx_handle_t dc_handle) {
    fuchsia_display_ControllerSetLayerPrimaryPositionRequest msg;
    msg.hdr.ordinal = fuchsia_display_ControllerSetLayerPrimaryPositionOrdinal;

    for (auto& layer : layers_) {
        msg.layer_id = layer.id;
        msg.transform = rotation_;

        msg.src_frame.width = layer.src.width;
        msg.src_frame.height = layer.src.height;
        msg.src_frame.x_pos = layer.src.x_pos;
        msg.src_frame.y_pos = layer.src.y_pos;

        msg.dest_frame.width = layer.dest.width;
        msg.dest_frame.height = layer.dest.height;
        msg.dest_frame.x_pos = layer.dest.x_pos;
        msg.dest_frame.y_pos = layer.dest.y_pos;

        if (zx_channel_write(dc_handle, 0, &msg, sizeof(msg), nullptr, 0) != ZX_OK) {
            ZX_ASSERT(false);
        }
    }
}

void VirtualLayer::SetLayerImages(zx_handle_t dc_handle, bool alt_image) {
    fuchsia_display_ControllerSetLayerImageRequest msg;
    msg.hdr.ordinal = fuchsia_display_ControllerSetLayerImageOrdinal;

    for (auto& layer : layers_) {
        msg.layer_id = layer.id;
        msg.image_id = layer.import_info[alt_image].id;
        msg.wait_event_id = layer.import_info[alt_image].event_ids[WAIT_EVENT];
        msg.signal_event_id = layer.import_info[alt_image].event_ids[SIGNAL_EVENT];

        if (zx_channel_write(dc_handle, 0, &msg, sizeof(msg), nullptr, 0) != ZX_OK) {
            ZX_ASSERT(false);
        }
    }
}

bool PrimaryLayer::Wait(uint32_t idx) {
    zx_time_t deadline = zx_deadline_after(ZX_MSEC(100));
    for (auto& layer : layers_) {
        uint32_t observed;
        if (!layer.active) {
            continue;
        }
        zx_handle_t event = layer.import_info[alt_image_].events[idx];
        zx_status_t res;
        if ((res = zx_object_wait_one(event, ZX_EVENT_SIGNALED, deadline, &observed)) == ZX_OK) {
            if (layer_flipping_) {
                zx_object_signal(event, ZX_EVENT_SIGNALED, 0);
            }
        } else {
            return false;
        }
    }
    return true;
}

CursorLayer::CursorLayer(Display* display) : VirtualLayer(display) { }

CursorLayer::CursorLayer(const fbl::Vector<Display>& displays) : VirtualLayer(displays) { }

bool CursorLayer::Init(zx_handle_t dc_handle) {
    fuchsia_display_CursorInfo info = displays_[0]->cursor();
    uint32_t bg_color = 0xffffffff;
    image_ = Image::Create(
            dc_handle, info.width, info.height, info.pixel_format, get_fg_color(), bg_color, true);
    if (!image_) {
        return false;
    }
    image_->Render(-1, -1);

    for (unsigned i = 0; i < displays_.size(); i++) {
        layer_t* layer = CreateLayer(dc_handle);
        if (layer == nullptr) {
            return false;
        }

        layer->active = true;
        if (!image_->Import(dc_handle, &layer->import_info[0])) {
            return false;
        }
        zx_object_signal(layer->import_info[0].events[WAIT_EVENT], 0, ZX_EVENT_SIGNALED);

        fuchsia_display_ControllerSetLayerCursorConfigRequest config;
        config.hdr.ordinal = fuchsia_display_ControllerSetLayerCursorConfigOrdinal;
        config.layer_id = layer->id;
        config.image_config.height = info.height;
        config.image_config.width = info.width;
        config.image_config.pixel_format = info.pixel_format;
        config.image_config.type = IMAGE_TYPE_SIMPLE;

        if (zx_channel_write(dc_handle, 0, &config, sizeof(config), nullptr, 0) != ZX_OK) {
            printf("Setting layer config failed\n");
            return false;
        }
    }

    SetLayerImages(dc_handle, false);

    return true;
}

void CursorLayer::StepLayout(int32_t frame_num) {
    fuchsia_display_CursorInfo info = displays_[0]->cursor();

    x_pos_ = interpolate(width_ + info.width, frame_num, kDestFrameBouncePeriod) - info.width;
    y_pos_ = interpolate(height_ + info.height, frame_num, kDestFrameBouncePeriod) - info.height;
}

void CursorLayer::SendLayout(zx_handle_t dc_handle) {
    fuchsia_display_ControllerSetLayerCursorPositionRequest msg;
    msg.hdr.ordinal = fuchsia_display_ControllerSetLayerCursorPositionOrdinal;

    uint32_t display_start = 0;
    for (unsigned i = 0; i < displays_.size(); i++) {
        msg.layer_id = layers_[i].id;
        msg.x = x_pos_ - display_start;
        msg.y = y_pos_;

        if (zx_channel_write(dc_handle, 0, &msg, sizeof(msg), nullptr, 0) != ZX_OK) {
            ZX_ASSERT(false);
        }

        display_start += displays_[i]->mode().horizontal_resolution;
    }
}

ColorLayer::ColorLayer(Display* display) : VirtualLayer(display) { }

ColorLayer::ColorLayer(const fbl::Vector<Display>& displays) : VirtualLayer(displays) { }

bool ColorLayer::Init(zx_handle_t dc_handle) {
    for (unsigned i = 0; i < displays_.size(); i++) {
        layer_t* layer = CreateLayer(dc_handle);
        if (layer == nullptr) {
            return false;
        }

        layer->active = true;

        constexpr uint32_t kColorLayerFormat = ZX_PIXEL_FORMAT_ARGB_8888;
        uint32_t kColorLayerColor = get_fg_color();

        uint32_t size = sizeof(fuchsia_display_ControllerSetLayerColorConfigRequest)
                + FIDL_ALIGN(ZX_PIXEL_FORMAT_BYTES(kColorLayerFormat));
        uint8_t data[size];

        auto config = reinterpret_cast<fuchsia_display_ControllerSetLayerColorConfigRequest*>(data);
        config->hdr.ordinal = fuchsia_display_ControllerSetLayerColorConfigOrdinal;
        config->layer_id = layer->id;
        config->pixel_format = kColorLayerFormat;
        config->color_bytes.count = ZX_PIXEL_FORMAT_BYTES(kColorLayerFormat);
        config->color_bytes.data = reinterpret_cast<void*>(FIDL_ALLOC_PRESENT);

        *reinterpret_cast<uint32_t*>(config + 1) = kColorLayerColor;

        if (zx_channel_write(dc_handle, 0, data, size, nullptr, 0) != ZX_OK) {
            printf("Setting layer config failed\n");
            return false;
        }
    }

    return true;
}
