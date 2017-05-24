// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <threads.h>

#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/binding.h>
#include <ddk/protocol/display.h>

#include <magenta/listnode.h>
#include <magenta/process.h>

#include <magenta/device/console.h>
#include <magenta/device/display.h>

typedef struct {
    mx_device_t* mxdev;
    mx_display_protocol_t* dpy;
    mx_display_info_t info;
    size_t bufsz;
    void* buffer;
    mtx_t lock;
    int refcount;
    list_node_t list;
} fb_t;

typedef struct {
    mx_device_t* mxdev;
    fb_t* fb;
    void* buffer;
    mx_handle_t vmo;
    list_node_t node;
} fbi_t;

static mx_status_t fbi_get_vmo(fbi_t* fbi, mx_handle_t* vmo) {
    mtx_lock(&fbi->fb->lock);
    mx_status_t r;
    if (fbi->vmo != MX_HANDLE_INVALID) {
        r = NO_ERROR;
        goto done;
    }
    if ((r = mx_vmo_create(fbi->fb->bufsz, 0, &fbi->vmo)) < 0) {
        printf("fb: cannot create vmo (%zu bytes): %d\n", fbi->fb->bufsz, r);
        goto done;
    }
    if ((r = mx_vmar_map(mx_vmar_root_self(), 0, fbi->vmo, 0, fbi->fb->bufsz,
                MX_VM_FLAG_PERM_READ | MX_VM_FLAG_PERM_WRITE,
                (uintptr_t*) &fbi->buffer)) < 0) {
        printf("fb: cannot map buffer: %d\n", r);
        mx_handle_close(fbi->vmo);
        fbi->vmo = MX_HANDLE_INVALID;
        goto done;
    }
done:
    *vmo = fbi->vmo;
    mtx_unlock(&fbi->fb->lock);
    return r;
}

static mx_status_t fbi_ioctl(void* ctx, uint32_t op,
                             const void* in_buf, size_t in_len,
                             void* out_buf, size_t out_len, size_t* out_actual) {
    fbi_t* fbi = ctx;
    fb_t* fb = fbi->fb;
    mx_status_t r;

    switch (op) {
    case IOCTL_DISPLAY_SET_FULLSCREEN:
        //TODO: remove compat stub once no longer needed
        return NO_ERROR;

    case IOCTL_CONSOLE_SET_ACTIVE_VC:
        //TODO: remove compat stub once no longer needed
        return NO_ERROR;

    case IOCTL_DISPLAY_FLUSH_FB_REGION: {
        if (in_len != sizeof(ioctl_display_region_t)) {
            return ERR_INVALID_ARGS;
        }
        const ioctl_display_region_t* r = in_buf;
        uint32_t y = r->y;
        uint32_t h = r->height;
        if ((y >= fb->info.height) ||
            (h > (fb->info.height - y))) {
            return ERR_OUT_OF_RANGE;
        }
        uint32_t linesize = fb->info.stride * fb->info.pixelsize;
        memcpy(fb->buffer + y * linesize, fbi->buffer + y * linesize, h * linesize);
        return NO_ERROR;
    }
    case IOCTL_DISPLAY_FLUSH_FB:
        memcpy(fb->buffer, fbi->buffer, fb->bufsz);
        return NO_ERROR;

    case IOCTL_DISPLAY_GET_FB:
        if (out_len < sizeof(ioctl_display_get_fb_t)) {
            return ERR_BUFFER_TOO_SMALL;
        }
        ioctl_display_get_fb_t* out = out_buf;
        memcpy(&out->info, &fb->info, sizeof(mx_display_info_t));
        out->info.flags = 0;

        mx_handle_t vmo;
        if ((r = fbi_get_vmo(fbi, &vmo)) < 0) {
            return r;
        }
        if ((r = mx_handle_duplicate(vmo, MX_RIGHT_SAME_RIGHTS, &out->vmo)) < 0) {
            return r;
        } else {
            *out_actual = sizeof(ioctl_display_get_fb_t);
            return NO_ERROR;
        }
    default:
        return ERR_NOT_SUPPORTED;
    }
}

static void fb_release(void* ctx) {
    fb_t* fb = ctx;
    mtx_lock(&fb->lock);
    if (--fb->refcount == 0) {
        mtx_unlock(&fb->lock);
        free(fb);
    } else {
        mtx_unlock(&fb->lock);
    }
}

static void fbi_release(void* ctx) {
    fbi_t* fbi = ctx;

    // detach instance from device
    if (fbi->fb) {
        mtx_lock(&fbi->fb->lock);
        list_delete(&fbi->node);
        mtx_unlock(&fbi->fb->lock);
        fb_release(fbi->fb);
    }

    if (fbi->buffer) {
        printf("fb: unmap buffer %p (%zu bytes)\n", fbi->buffer, fbi->fb->bufsz);
        mx_vmar_unmap(mx_vmar_root_self(), (uintptr_t) fbi->buffer, fbi->fb->bufsz);
    }
    if (fbi->vmo != MX_HANDLE_INVALID) {
        mx_handle_close(fbi->vmo);
    }
    free(fbi);
}

static mx_status_t fb_open(void* ctx, mx_device_t** out, uint32_t flags);

// Allow use of openat() to obtain another offscreen framebuffer from
// an existing framebuffer instance
static mx_status_t fbi_open_at(void* ctx, mx_device_t** out, const char* path, uint32_t flags) {
    fbi_t* fbi = ctx;
    return fb_open(fbi->fb, out, flags);
}

mx_protocol_device_t fbi_ops = {
    .version = DEVICE_OPS_VERSION,
    .open_at = fbi_open_at,
    .ioctl = fbi_ioctl,
    .release = fbi_release,
};

static mx_status_t fb_open(void* ctx, mx_device_t** out, uint32_t flags) {
    fb_t* fb = ctx;

    fbi_t* fbi;
    if ((fbi = calloc(1, sizeof(fbi_t))) == NULL) {
        return ERR_NO_MEMORY;
    }

    mtx_lock(&fb->lock);
    fb->refcount++;
    fbi->fb = fb;
    list_add_tail(&fb->list, &fbi->node);
    mtx_unlock(&fb->lock);

    device_add_args_t args = {
        .version = DEVICE_ADD_ARGS_VERSION,
        .name = "framebuffer",
        .ctx = fbi,
        .driver = &_driver_framebuffer,
        .ops = &fbi_ops,
        .proto_id = MX_PROTOCOL_DISPLAY,
        .flags = DEVICE_ADD_INSTANCE,
    };

    mx_status_t r;
    if ((r = device_add(fb->mxdev, &args, &fbi->mxdev)) < 0) {
        fbi_release(fbi);
        return r;
    }
    *out = fbi->mxdev;
    return NO_ERROR;
}

static void fb_unbind(void* ctx) {
}

static mx_protocol_device_t fb_ops = {
    .version = DEVICE_OPS_VERSION,
    .open = fb_open,
    .unbind = fb_unbind,
    .release = fb_release,
};

static mx_status_t fb_bind(void* ctx, mx_device_t* dev, void** cookie) {
    fb_t* fb;
    if ((fb = calloc(1, sizeof(fb_t))) == NULL) {
        return ERR_NO_MEMORY;
    }

    mx_status_t r;
    if ((r = device_op_get_protocol(dev, MX_PROTOCOL_DISPLAY, (void**)&fb->dpy)) < 0) {
        printf("fb: display does not support display protocol: %d\n", r);
        goto fail;
    }
    if ((r = fb->dpy->get_mode(dev, &fb->info)) < 0) {
        printf("fb: display get mode failed: %d\n", r);
        goto fail;
    }
    if ((r = fb->dpy->get_framebuffer(dev, &fb->buffer)) < 0) {
        printf("fb: display get framebuffer failed: %d\n", r);
        goto fail;
    }

    // Our display drivers do not initialize pixelsize
    // Determine it based on pixel format
    switch (fb->info.format) {
    case MX_PIXEL_FORMAT_RGB_565:
        fb->info.pixelsize = 2;
        break;
    case MX_PIXEL_FORMAT_RGB_x888:
    case MX_PIXEL_FORMAT_ARGB_8888:
        fb->info.pixelsize = 4;
        break;
    case MX_PIXEL_FORMAT_RGB_332:
        fb->info.pixelsize = 1;
        break;
    case MX_PIXEL_FORMAT_RGB_2220:
        fb->info.pixelsize = 1;
        break;
    default:
        printf("fb: unknown format %u\n", fb->info.format);
        r = ERR_NOT_SUPPORTED;
        goto fail;
    }

    fb->bufsz = fb->info.pixelsize * fb->info.stride * fb->info.width;

    printf("fb: %u x %u (stride=%u pxlsz=%u format=%u): %zu bytes\n",
           fb->info.width, fb->info.height,
           fb->info.stride, fb->info.pixelsize, fb->info.format, fb->bufsz);

    // initial reference for ourself, later ones for children
    fb->refcount = 1;
    list_initialize(&fb->list);
    mtx_init(&fb->lock, mtx_plain);

    device_add_args_t args = {
        .version = DEVICE_ADD_ARGS_VERSION,
        .name = "framebuffer",
        .ctx = fb,
        .ops = &fb_ops,
        .proto_id = MX_PROTOCOL_FRAMEBUFFER,
    };

    if ((r = device_add(dev, &args, &fb->mxdev)) < 0) {
        goto fail;
    }
    return NO_ERROR;

fail:
    free(fb);
    return r;
}

static mx_driver_ops_t fb_driver_ops = {
    .version = DRIVER_OPS_VERSION,
    .bind = fb_bind,
};

MAGENTA_DRIVER_BEGIN(framebuffer, fb_driver_ops, "magenta", "0.1", 1)
    BI_MATCH_IF(EQ, BIND_PROTOCOL, MX_PROTOCOL_DISPLAY),
MAGENTA_DRIVER_END(framebuffer)