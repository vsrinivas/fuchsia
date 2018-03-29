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

// a structure containing information a connected display
typedef struct display_info {
    // the display's edid
    const uint8_t* edid;
    uint16_t edid_length;

    // A list of pixel formats supported by the display. The first entry is the
    // preferred pixel format.
    const zx_pixel_format_t* pixel_formats;
    uint32_t pixel_format_count;
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
                                int32_t* displays_added, uint32_t added_count,
                                int32_t* displays_removed, uint32_t removed_count);

    void (*on_display_vsync)(void* ctx, int32_t display_id, void* handle);
} display_controller_cb_t;

// constants for display_config's mode_flags field
#define MODE_FLAG_VSYNC_POSITIVE (1 << 0)
#define MODE_FLAG_HSYNC_POSITIVE (1 << 1)
#define MODE_FLAG_INTERLACED (1 << 2)

typedef struct display_config {
    // the display id to which the configuration applies
    int32_t display_id;

    // The video parameters which specify the display mode. The client guarantees that
    // these values match some timing advertised by the display's EDID.
    uint32_t pixel_clock_khz;
    uint32_t h_active;
    uint32_t h_sync_start;
    uint32_t h_sync_end;
    uint32_t h_total;
    uint32_t v_active;
    uint32_t v_sync_start;
    uint32_t v_sync_end;
    uint32_t v_total;
    uint32_t mode_flags; // A bitmask of MODE_FLAG_* values

    image_t image;
} display_config_t;

// The client guarantees that check_configuration and apply_configuration are always
// made from a single thread. The client makes no other threading guarantees.
typedef struct display_controller_protocol_ops {
    void (*set_display_controller_cb)(void* ctx, void* cb_ctx, display_controller_cb_t* cb);

    // Gets all information about the display. Pointers returned in |info| must remain
    // valid until the the display is removed with on_displays_changed or the device's
    // release device-op is invoked.
    zx_status_t (*get_display_info)(void* ctx, int32_t display_id, display_info_t* info);

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
    // Whether or not the driver can accept the configuration cannot depend on the
    // particular image handles, as it must always be possible to present a new image in
    // place of another image with a matching configuration.
    //
    // The driver must not retain references to the configuration after this function returns.
    bool (*check_configuration)(void* ctx,
                                display_config_t** display_config, uint32_t display_count);

    // Applies the configuration.
    //
    // |display_config| will contain configurations for all displays which the controller
    // has advertised. The client guarantees that the configuration has been successfully
    // validated with check_configuration.
    //
    // The driver must not retain references to the configuration after this function returns.
    void (*apply_configuration)(void* ctx,
                                display_config_t** display_configs, uint32_t display_count);

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
