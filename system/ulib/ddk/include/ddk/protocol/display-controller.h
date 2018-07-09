// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <zircon/compiler.h>
#include <zircon/types.h>
#include <zircon/pixelformat.h>

__BEGIN_CDECLS;

/**
 * protocol/display-controller.h - display controller protocol definitions
 */

#define INVALID_DISPLAY_ID 0

// a fallback structure to convey display information without an edid
typedef struct display_params {
    uint32_t width;
    uint32_t height;
    uint32_t refresh_rate_e2;
} display_params_t;

// Info about valid cursor configuratoins.
typedef struct cursor_info {
    // The width and height of the cursor configuration, in pixels.
    uint32_t width;
    uint32_t height;
    zx_pixel_format_t format;
} cursor_info_t;

// a structure containing information a connected display
typedef struct display_info {
    // A flag indicating whether or not the display has a valid edid. If no edid is
    // present, then the meaning of display_config's mode structure is undefined, and
    // drivers should ignore it.
    bool edid_present;
    union {
        // the display's edid
        struct {
            const uint8_t* data;
            uint16_t length;
        } edid;
        // the display's parameters if an edid is not present
        display_params_t params;
    } panel;

    // A list of pixel formats supported by the display. The first entry is the
    // preferred pixel format.
    const zx_pixel_format_t* pixel_formats;
    uint32_t pixel_format_count;

    // A list of cursor configurations most likely to be accepted by the driver. Can
    // be null if cursor_count is 0.
    //
    // The driver may reject some of these configurations in some circumstances, and
    // it may accept other configurations, but at least one of these configurations
    // should be valid at most times.
    const cursor_info_t* cursor_infos;
    uint32_t cursor_info_count;
} display_info_t;

// The image is linear and VMO backed.
#define IMAGE_TYPE_SIMPLE 0

// a structure containing information about an image
typedef struct image {
    // the width and height of the image in pixels
    uint32_t width;
    uint32_t height;

    // the pixel format of the image
    zx_pixel_format_t pixel_format;

    // The type conveys information about what is providing the pixel data. If this is not
    // IMAGE_FORMAT_SIMPLE, it is up to the driver and buffer producer to agree on the meaning
    // of the value through some mechanism outside the scope of this API.
    uint32_t type;

    // A driver-defined handle to the image. Each handle must be unique.
    void* handle;
} image_t;

typedef struct display_controller_cb {
    // Callbacks which are invoked when displays are added or removed. |displays_added| and
    // |displays_removed| point to arrays of the display ids which were added and removed. If
    // |added_count| or |removed_count| is 0, the corresponding array can be NULL.
    //
    // The driver must be done accessing any images which were on the removed displays.
    //
    // The driver should call this function when the callback is registered if any displays
    // are present.
    void (*on_displays_changed)(void* ctx,
                                uint64_t* displays_added, uint32_t added_count,
                                uint64_t* displays_removed, uint32_t removed_count);

    // |timestamp| is the ZX_CLOCK_MONOTONIC timestamp at which the vsync occurred.
    // |handles| points to an array of image handles of each framebuffer being
    // displayed, in increasing z-order.
    void (*on_display_vsync)(void* ctx, uint64_t display_id, zx_time_t timestamp,
                             void** handles, uint32_t handle_count);
} display_controller_cb_t;

#define ALPHA_DISABLE 0
#define ALPHA_PREMULTIPLIED 1
#define ALPHA_HW_MULTIPLY 2

// Rotations are applied counter-clockwise, and are applied before reflections.
#define FRAME_TRANSFORM_IDENTITY 0
#define FRAME_TRANSFORM_REFLECT_X 1
#define FRAME_TRANSFORM_REFLECT_Y 2
#define FRAME_TRANSFORM_ROT_90 3
#define FRAME_TRANSFORM_ROT_180 4
#define FRAME_TRANSFORM_ROT_270 5
#define FRAME_TRANSFORM_ROT_90_REFLECT_X 6
#define FRAME_TRANSFORM_ROT_90_REFLECT_Y 7

typedef struct frame {
    // (x_pos, y_pos) specifies the position of the upper-left corner
    // of the frame.
    uint32_t x_pos;
    uint32_t y_pos;
    uint32_t width;
    uint32_t height;
} frame_t;

typedef struct primary_layer {
    image_t image;

    // An ALPHA_* constant.
    //
    // If alpha_mode == ALPHA_DISABLED, the layer is opaque and alpha_layer_val is ignored.
    //
    // If alpha_mode == PREMULTIPLIED or HW_MULTIPLY and alpha_layer_val is NaN, the alpha
    // used when blending is determined by the per-pixel alpha channel.
    //
    // If alpha_mode == PREMULTIPLIED or HW_MULTIPLY and alpha_layer_val is not NaN, the
    // alpha used when blending is the product of alpha_layer_val and any per-pixel alpha.
    // Additionally, if alpha_mode == PREMULTIPLIED, then the hardware must premultiply the color
    // channel with alpha_layer_val before blending.
    //
    // If alpha_layer_val is not NaN, it will be in the range [0, 1].
    uint32_t alpha_mode;
    float alpha_layer_val;

    uint32_t transform_mode;

    // The source frame, where (0,0) is the top-left corner of the image. The
    // client guarantees that src_frame lies entirely within the image.
    frame_t src_frame;

    // The destination frame, where (0,0) is the top-left corner of the
    // composed output. The client guarantees that dest_frame lies entirely
    // within the composed output.
    frame_t dest_frame;
} primary_layer_t;

typedef struct cursor_layer {
    image_t image;

    // The position of the top-left corner of the cursor's image. When being
    // applied to a display, the cursor is guaranteed to have at least one
    // pixel of overlap with the display.
    int32_t x_pos;
    int32_t y_pos;
} cursor_layer_t;

typedef struct color_layer {
    zx_pixel_format_t format;
    // The color to use for the layer. The color is little-endian, and is
    // guaranteed to be of the appropriate size.
    uint8_t* color;
} color_layer_t;

// Types of layers.

#define LAYER_PRIMARY 0
#define LAYER_CURSOR 1
#define LAYER_COLOR 2

typedef struct layer {
    // One of the LAYER_* flags.
    uint32_t type;
    // z_index of the layer. See |check_configuration| and |apply_configuration|.
    uint32_t z_index;
    union {
        primary_layer_t primary;
        cursor_layer_t cursor;
        color_layer_t color;
    } cfg;
} layer_t;

// constants for display_config's mode_flags field
#define MODE_FLAG_VSYNC_POSITIVE (1 << 0)
#define MODE_FLAG_HSYNC_POSITIVE (1 << 1)
#define MODE_FLAG_INTERLACED (1 << 2)

// The video parameters which specify the display mode.
typedef struct display_mode {
    uint32_t pixel_clock_10khz;
    uint32_t h_addressable;
    uint32_t h_front_porch;
    uint32_t h_sync_pulse;
    uint32_t h_blanking;
    uint32_t v_addressable;
    uint32_t v_front_porch;
    uint32_t v_sync_pulse;
    uint32_t v_blanking;
    uint32_t mode_flags; // A bitmask of MODE_FLAG_* values
} display_mode_t;

// If set, use the 0 vector for the color conversion preoffset
#define COLOR_CONVERSION_PREOFFSET (1 << 0)
// If set, use the identity matrix for the color conversion coefficients
#define COLOR_CONVERSION_COEFFICIENTS (1 << 1)
// If set, use the 0 vector for the color conversion postoffset
#define COLOR_CONVERSION_POSTOFFSET (1 << 2)

typedef struct display_config {
    // the display id to which the configuration applies
    uint64_t display_id;

    display_mode_t mode;

    // Bitmask of COLOR_CONVERSION_* flags
    uint32_t cc_flags;
    // Color conversion is applied to each pixel according to the formula:
    //
    // (cc_coefficients * (pixel + cc_preoffsets)) + cc_postoffsets
    //
    // where pixel is a column vector consiting of the pixel's 3 components.
    float cc_preoffsets[3];
    float cc_coefficients[3][3];
    float cc_postoffsets[3];

    uint32_t layer_count;
    layer_t** layers;
} display_config_t;

// The display mode configuration is valid. Note that this is distinct from
// whether or not the layer configuration is valid.
#define CONFIG_DISPLAY_OK 0
// Error indicating that the hardware cannot simultaniously support the
// requested number of displays.
#define CONFIG_DISPLAY_TOO_MANY 1
// Error indicating that the hardware cannot simultaniously support the given
// set of display modes. To support a mode, the display must be able to display
// a single layer with width and height equal to the requested mode and the
// preferred pixel format.
#define CONFIG_DISPLAY_UNSUPPORTED_MODES 2

// The client should convert the corresponding layer to a primary layer.
#define CLIENT_USE_PRIMARY (1 << 0)
// The client should compose all layers with MERGE_BASE and MERGE_SRC into a new,
// single primary layer at the MERGE_BASE layer's z-order. The driver must accept
// a fullscreen layer with the default pixel format, but may accept other layer
// parameters.
//
// MERGE_BASE should only be set on one layer per display. If it is set on multiple
// layers, the client will arbitrarily pick one and change the rest to MERGE_SRC.
#define CLIENT_MERGE_BASE (1 << 1)
#define CLIENT_MERGE_SRC (1 << 2)
// The client should pre-scale the image so that src_frame's dimensions are equal
// to dest_frame's dimensions.
#define CLIENT_FRAME_SCALE (1 << 3)
// The client should pre-clip the image so that src_frame's dimensions are equal to
// the image's dimensions.
#define CLIENT_SRC_FRAME (1 << 4)
// The client should pre-apply the transformation so TRANSFORM_IDENTITY can be used.
#define CLIENT_TRANSFORM (1 << 5)
// The client should apply the color conversion.
#define CLIENT_COLOR_CONVERSION (1 << 6)
// The client should apply the alpha transformation itself.
#define CLIENT_ALPHA (1 << 7)

// The client guarantees that check_configuration and apply_configuration are always
// made from a single thread. The client makes no other threading guarantees.
typedef struct display_controller_protocol_ops {
    void (*set_display_controller_cb)(void* ctx, void* cb_ctx, display_controller_cb_t* cb);

    // Gets all information about the display. Pointers returned in |info| must remain
    // valid until the the display is removed with on_displays_changed or the device's
    // release device-op is invoked.
    zx_status_t (*get_display_info)(void* ctx, uint64_t display_id, display_info_t* info);

    // Imports a VMO backed image into the driver. The driver should set image->handle. The
    // driver does not own the vmo handle passed to this function.
    zx_status_t (*import_vmo_image)(void* ctx, image_t* image,
                                    zx_handle_t vmo, size_t offset);

    // Releases any driver state associated with the given image. The client guarantees that
    // any images passed to apply_config will not be released until a vsync occurs with a
    // more recent image.
    void (*release_image)(void* ctx, image_t* image);

    // Validates the given configuration.
    //
    // The configuration may not include all displays. Omiteed displays should be treated as
    // whichever of off or displaying a blank screen results in a more premissive validation.
    //
    // All displays in a configuration will have at least one layer. The layers will be
    // arranged in increasing z-order, and their z_index fields will be set consecutively.
    //
    // Whether or not the driver can accept the configuration cannot depend on the
    // particular image handles, as it must always be possible to present a new image in
    // place of another image with a matching configuration. It also cannot depend on the
    // cursor position, as that can be updated without another call to check_configuration.
    //
    // display_cfg_result should be set to a CONFIG_DISPLAY_* error if the combination of
    // display modes is not supported.
    //
    // layer_cfg_result points to an array of arrays. The primary length is display_count, the
    // secondary lengths are the corresponding display_cfg's layer_count. If display_cfg_result
    // is CONFIG_DISPLAY_OK, any errors in layer configuration should be returned as a CLIENT*
    // flag in the corresponding layer_cfg_result entry.
    //
    // The driver must not retain references to the configuration after this function returns.
    void (*check_configuration)(void* ctx, const display_config_t** display_config,
                                uint32_t* display_cfg_result, uint32_t** layer_cfg_result,
                                uint32_t display_count);

    // Applies the configuration.
    //
    // All configurations passed to this function will be derived from configurations which
    // have been succesfully validated, with the only differences either being omitted layers
    // or different image handles. To account for any layers which are not present, the driver
    // must use the z_index values of the present layers to configure them as if the whole
    // configuration was present.
    //
    // Unlike with check_configuration, displays included in the configuration are not
    // guaranteed to include any layers. Both omitted displays and displays with no layers
    // can either be turned off or set to display a blank screen, but for displays with no
    // layers there is a strong preference to display a blank screen instead of turn them off.
    // In either case, the driver must drop all references to old images and invoke the vsync
    // callback after doing so.
    //
    // The driver must not retain references to the configuration after this function returns.
    void (*apply_configuration)(void* ctx,
                                const display_config_t** display_configs, uint32_t display_count);

    // Computes the stride (in pixels) necessary for a linear image with the given width
    // and pixel format. Returns 0 on error.
    uint32_t (*compute_linear_stride)(void* ctx, uint32_t width, zx_pixel_format_t pixel_format);

    // Allocates a VMO of the requested size which can be used for images.
    // TODO: move this functionallity into a seperate video buffer management system.
    zx_status_t (*allocate_vmo)(void* ctx, uint64_t size, zx_handle_t* vmo_out);
} display_controller_protocol_ops_t;

typedef struct zx_display_controller_protocol {
    display_controller_protocol_ops_t* ops;
    void* ctx;
} display_controller_protocol_t;
__END_CDECLS;
