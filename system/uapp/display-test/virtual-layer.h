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

    bool done;

    frame_t src;
    frame_t dest;

    image_import_t import_info[2];
} layer_t;

// A layer whose output can appear on multiple displays.
class VirtualLayer {
public:
    explicit VirtualLayer(Display* display);
    explicit VirtualLayer(const fbl::Vector<Display>& displays);

    // Finish initializing the layer. All Set* methods should be called before this.
    virtual bool Init(zx_handle_t channel) = 0;

    // Steps the local layout state to frame_num.
    virtual void StepLayout(int32_t frame_num) = 0;

    // Waits for the display controller to be done with the previous version of this frame.
    virtual bool WaitForReady() = 0;

    // Sets the current layout to the display contorller.
    virtual void SendLayout(zx_handle_t channel) = 0;

    // Renders the current frame (and signals the fence if necessary).
    virtual void Render(int32_t frame_num) = 0;

    // Gets the display controller layer ID for usage on the given display.
    uint64_t id(uint64_t display_id) const {
        for (unsigned i = 0; i < displays_.size(); i++) {
            if (displays_[i]->id() == display_id && layers_[i].active) {
                return layers_[i].id;
            }
        }
        return INVALID_ID;
    }

    // Gets the ID of the image on the given display.
    virtual uint64_t image_id(uint64_t display_id) const = 0;

    void set_frame_done(uint64_t display_id) {
        for (unsigned i = 0; i < displays_.size(); i++) {
            if (displays_[i]->id() == display_id) {
                layers_[i].done = true;
            }
        }
    }

    virtual bool is_done() const {
        bool done = true;
        for (unsigned i = 0; i < displays_.size(); i++) {
            done &= !layers_[i].active || layers_[i].done;
        }
        return done;
    }

    void clear_done() {
        for (unsigned i = 0; i < displays_.size(); i++) {
            layers_[i].done = false;
        }
    }

protected:
    layer_t* CreateLayer(zx_handle_t dc_handle);
    void SetLayerImages(zx_handle_t handle, bool alt_image);

    fbl::Vector<Display*> displays_;
    fbl::Vector<layer_t> layers_;

    uint32_t width_;
    uint32_t height_;
};

class PrimaryLayer : public VirtualLayer {
public:
    explicit PrimaryLayer(Display* display);
    explicit PrimaryLayer(const fbl::Vector<Display>& displays);

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
    void SetAlpha(bool enable, float val) {
        alpha_enable_ = enable;
        alpha_val_ = val;
    }
    void SetScaling(bool enable) {
        scaling_ = enable;
    }

    bool Init(zx_handle_t channel) override;
    void StepLayout(int32_t frame_num) override;
    bool WaitForReady() override;
    void SendLayout(zx_handle_t channel) override;
    void Render(int32_t frame_num) override;

    uint64_t image_id(uint64_t display_id) const override {
        for (unsigned i = 0; i < displays_.size(); i++) {
            if (displays_[i]->id() == display_id && layers_[i].active) {
                return layers_[i].import_info[alt_image_].id;
            }
        }
        return INVALID_ID;
    }

private:
    void SetLayerPositions(zx_handle_t handle);
    bool Wait(uint32_t idx);
    void InitImageDimens();

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
    bool alpha_enable_ = false;
    float alpha_val_ = 0.f;
    bool scaling_ = false;

    bool alt_image_ = false;
    Image* images_[2];
};

class CursorLayer : public VirtualLayer {
public:
    explicit CursorLayer(Display* display);
    explicit CursorLayer(const fbl::Vector<Display>& displays);

    bool Init(zx_handle_t channel) override;
    void StepLayout(int32_t frame_num) override;
    void SendLayout(zx_handle_t channel) override;

    bool WaitForReady() override { return true; }
    void Render(int32_t frame_num) override {}

    uint64_t image_id(uint64_t display_id) const override {
        for (unsigned i = 0; i < displays_.size(); i++) {
            if (displays_[i]->id() == display_id && layers_[i].active) {
                return layers_[i].import_info[0].id;
            }
        }
        return INVALID_ID;
    }


private:
    uint32_t x_pos_ = 0;
    uint32_t y_pos_ = 0;

    Image* image_;
};

class ColorLayer : public VirtualLayer {
public:
    explicit ColorLayer(Display* display);
    explicit ColorLayer(const fbl::Vector<Display>& displays);

    bool Init(zx_handle_t channel) override;

    void SendLayout(zx_handle_t channel) override {};
    void StepLayout(int32_t frame_num) override {};
    bool WaitForReady() override { return true; }
    void Render(int32_t frame_num) override {}
    uint64_t image_id(uint64_t display_id) const override { return INVALID_ID; }
    virtual bool is_done() const override { return true; }
};
