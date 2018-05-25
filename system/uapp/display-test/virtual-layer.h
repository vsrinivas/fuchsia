// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <zircon/device/display-controller.h>
#include <zircon/types.h>
#include <lib/zx/channel.h>

#include "display.h"
#include "fuchsia/display/c/fidl.h"
#include "image.h"

typedef struct frame {
    uint32_t width;
    uint32_t height;
    uint32_t x_pos;
    uint32_t y_pos;
} frame_t;

typedef struct layer {
    uint64_t id;
    bool active;

    frame_t src;
    frame_t dest;

    image_import_t import_info[2];
} layer_t;

// A layer whose output can appear on multiple displays.
class VirtualLayer {
public:
    VirtualLayer(Display* display);
    VirtualLayer(const fbl::Vector<Display>& displays);

    // Set* methods to configure the layer.
    void SetImageDimens(uint32_t width, uint32_t height) {
        image_width_ = width;
        image_height_ = height;

        src_frame_.width = width;
        src_frame_.height = height;
        dest_frame_.width = width;
        dest_frame_.height = height;
    }
    void SetSrcFrame(uint32_t width, uint32_t height) {
        src_frame_.width = width;
        src_frame_.height = height;
    }
    void SetDestFrame(uint32_t width, uint32_t height) {
        dest_frame_.width = width;
        dest_frame_.height = height;
    }
    void SetLayerFlipping(bool flip) { layer_flipping_ = flip; }
    void SetPanSrc(bool pan) { pan_src_ = pan; }
    void SetPanDest(bool pan) { pan_dest_ = pan; }
    void SetLayerToggle(bool toggle) { layer_toggle_ = toggle; }
    void SetRotates(bool rotates) { rotates_ = rotates; }

    // Finish initializing the layer. All Set* methods should be called before this.
    bool Init(zx_handle_t channel);

    // Steps the local layout state to frame_num.
    void StepLayout(int32_t frame_num);

    // Waits for the display controller to be done with the previous version of this frame.
    bool WaitForReady();

    // Sets the current layout to the display contorller.
    void SendLayout(zx_handle_t channel);

    // Renders the current frame (and signals the fence if necessary).
    void Render(int32_t frame_num);

    // Waits for the current layer configuration to be presented.
    bool WaitForPresent();

    // Gets the display controller layer ID for usage on the given display.
    uint64_t id(uint64_t display_id) const {
        for (unsigned i = 0; i < displays_.size(); i++) {
            if (displays_[i]->id() == display_id) {
                if (layers_[i].active) {
                    return layers_[i].id;
                }
            }
        }
        return INVALID_ID;
    }

private:
    void SetLayerImages(zx_handle_t handle);
    void SetLayerPositions(zx_handle_t handle);
    bool Wait(uint32_t idx);
    void InitImageDimens();

    fbl::Vector<Display*> displays_;
    fbl::Vector<layer_t> layers_;

    uint32_t width_;
    uint32_t height_;

    uint32_t image_width_ = 0;
    uint32_t image_height_ = 0;
    uint32_t image_format_ = 0;
    frame_t src_frame_ = {};
    frame_t dest_frame_ = {};
    uint8_t rotation_ = fuchsia_display_Transform_IDENTITY;
    bool layer_flipping_ = false;
    bool pan_src_ = false;
    bool pan_dest_ = false;
    bool layer_toggle_ = false;
    bool rotates_ = false;

    bool alt_image_ = false;
    Image* images_[2];
};
