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

#include <zircon/listnode.h>
#include <zircon/process.h>

#include <zircon/device/display.h>

typedef struct fbi fbi_t;
typedef struct fb fb_t;

#define GROUP_VIRTCON 0
#define GROUP_FULLSCREEN 1

struct fb {
    zx_device_t* zxdev;
    display_protocol_t dpy;
    zx_display_info_t info;
    size_t bufsz;
    void* buffer;
    mtx_t lock;

    // which group is active
    uint32_t active;

    // event to notify which group is active
    zx_handle_t event;

    // only one fullscreen client may exist at a time
    // and we keep track of it here
    fbi_t* fullscreen;
};

#define FB_HAS_GPU(fb) (fb->dpy.ops->acquire_or_release_display != NULL)
#define FB_ACQUIRE(fb) (fb->dpy.ops->acquire_or_release_display(fb->dpy.ctx, true))
#define FB_RELEASE(fb) (fb->dpy.ops->acquire_or_release_display(fb->dpy.ctx, false))
static inline void FB_FLUSH(fb_t* fb) {
    if (fb->dpy.ops->flush) {
        fb->dpy.ops->flush(fb->dpy.ctx);
    }
}

struct fbi {
    fb_t* fb;
    void* buffer;
    zx_handle_t vmo;
    uint32_t group;
};

void fb_callback(bool acquired, void* cookie) {
    fb_t* fb = cookie;
    mtx_lock(&fb->lock);
    if (acquired) {
        fb->active = GROUP_VIRTCON;
        zx_object_signal(fb->event, ZX_USER_SIGNAL_1, ZX_USER_SIGNAL_0);
    } else {
        fb->active = GROUP_FULLSCREEN;
        zx_object_signal(fb->event, ZX_USER_SIGNAL_0, ZX_USER_SIGNAL_1);
    }
    mtx_unlock(&fb->lock);
}

static zx_status_t fbi_get_vmo(fbi_t* fbi, zx_handle_t* vmo) {
    mtx_lock(&fbi->fb->lock);
    zx_status_t r;
    if (fbi->vmo != ZX_HANDLE_INVALID) {
        r = ZX_OK;
        goto done;
    }
    if ((r = zx_vmo_create(fbi->fb->bufsz, 0, &fbi->vmo)) < 0) {
        printf("fb: cannot create vmo (%zu bytes): %d\n", fbi->fb->bufsz, r);
        goto done;
    }
    if ((r = zx_vmar_map(zx_vmar_root_self(), 0, fbi->vmo, 0, fbi->fb->bufsz,
                ZX_VM_FLAG_PERM_READ | ZX_VM_FLAG_PERM_WRITE,
                (uintptr_t*) &fbi->buffer)) < 0) {
        printf("fb: cannot map buffer: %d\n", r);
        zx_handle_close(fbi->vmo);
        fbi->vmo = ZX_HANDLE_INVALID;
        goto done;
    }
done:
    *vmo = fbi->vmo;
    mtx_unlock(&fbi->fb->lock);
    return r;
}

static zx_status_t fbi_ioctl(void* ctx, uint32_t op,
                             const void* in_buf, size_t in_len,
                             void* out_buf, size_t out_len, size_t* out_actual) {
    fbi_t* fbi = ctx;
    fb_t* fb = fbi->fb;
    zx_status_t r;
    if (!fb->zxdev) {
        return ZX_ERR_PEER_CLOSED;
    }

    switch (op) {
    case IOCTL_DISPLAY_FLUSH_FB_REGION: {
        if (in_len != sizeof(ioctl_display_region_t)) {
            return ZX_ERR_INVALID_ARGS;
        }
        const ioctl_display_region_t* r = in_buf;
        uint32_t y = r->y;
        uint32_t h = r->height;
        if ((y >= fb->info.height) ||
            (h > (fb->info.height - y))) {
            return ZX_ERR_OUT_OF_RANGE;
        }
        uint32_t linesize = fb->info.stride * fb->info.pixelsize;
        mtx_lock(&fb->lock);
        if (!fb->zxdev) {
            mtx_unlock(&fb->lock);
            return ZX_ERR_PEER_CLOSED;
        }
        if ((fb->active == fbi->group) && (fbi->buffer != NULL)) {
            memcpy(fb->buffer + y * linesize, fbi->buffer + y * linesize, h * linesize);
            FB_FLUSH(fb);
        }
        mtx_unlock(&fb->lock);
        return ZX_OK;
    }
    case IOCTL_DISPLAY_FLUSH_FB:
        mtx_lock(&fb->lock);
        if (!fb->zxdev) {
            mtx_unlock(&fb->lock);
            return ZX_ERR_PEER_CLOSED;
        }
        if ((fb->active == fbi->group) && (fbi->buffer != NULL)) {
            memcpy(fb->buffer, fbi->buffer, fb->bufsz);
            FB_FLUSH(fb);
        }
        mtx_unlock(&fb->lock);
        return ZX_OK;

    case IOCTL_DISPLAY_GET_FB: {
        if ((fbi->group == GROUP_FULLSCREEN) && FB_HAS_GPU(fb)) {
            printf("fb: fullscreen soft framebuffer not supported (GPU)\n");
            return ZX_ERR_NOT_SUPPORTED;
        }

        if (out_len < sizeof(ioctl_display_get_fb_t)) {
            return ZX_ERR_BUFFER_TOO_SMALL;
        }
        ioctl_display_get_fb_t* out = out_buf;
        memcpy(&out->info, &fb->info, sizeof(zx_display_info_t));
        out->info.flags = 0;

        zx_handle_t vmo;
        if ((r = fbi_get_vmo(fbi, &vmo)) < 0) {
            return r;
        }
        if ((r = zx_handle_duplicate(vmo, ZX_RIGHT_SAME_RIGHTS, &out->vmo)) < 0) {
            return r;
        } else {
            *out_actual = sizeof(ioctl_display_get_fb_t);
            return ZX_OK;
        }
    }
    case IOCTL_DISPLAY_SET_OWNER: {
        if (in_len != sizeof(uint32_t)) {
            return ZX_ERR_INVALID_ARGS;
        }
        if (fbi->group != GROUP_VIRTCON) {
            return ZX_ERR_ACCESS_DENIED;
        }
        const uint32_t* n = (const uint32_t*) in_buf;
        if (FB_HAS_GPU(fb)) {
            if (*n != fb->active) {
                if (*n == GROUP_VIRTCON) {
                    FB_ACQUIRE(fb);
                } else {
                    FB_RELEASE(fb);
                }
            }
            return ZX_OK;
        }
        mtx_lock(&fb->lock);
        if (!fb->zxdev) {
            mtx_unlock(&fb->lock);
            return ZX_ERR_PEER_CLOSED;
        }
        if ((*n == GROUP_VIRTCON) || (fb->fullscreen == NULL)) {
            fb->active = GROUP_VIRTCON;
            zx_object_signal(fb->event, ZX_USER_SIGNAL_1, ZX_USER_SIGNAL_0);
        } else {
            fb->active = GROUP_FULLSCREEN;
            zx_object_signal(fb->event, ZX_USER_SIGNAL_0, ZX_USER_SIGNAL_1);
            if (fb->fullscreen->buffer) {
                memcpy(fb->buffer, fb->fullscreen->buffer, fb->bufsz);
            } else {
                memset(fb->buffer, 0, fb->bufsz);
            }
            FB_FLUSH(fb);
        }
        mtx_unlock(&fb->lock);
        return ZX_OK;
    }
    case IOCTL_DISPLAY_GET_OWNERSHIP_CHANGE_EVENT: {
        if (out_len != sizeof(zx_handle_t)) {
            return ZX_ERR_INVALID_ARGS;
        }
        zx_handle_t* out = (zx_handle_t*) out_buf;
        if ((r = zx_handle_duplicate(fb->event, ZX_RIGHTS_BASIC | ZX_RIGHT_READ, out)) < 0) {
            return r;
        } else {
            *out_actual = sizeof(zx_handle_t);
            return ZX_OK;
        }
    }

    default:
        return ZX_ERR_NOT_SUPPORTED;
    }
}

static void fbi_release(void* ctx) {
    fbi_t* fbi = ctx;

    // if we were the group 1 client
    // make group 1 available for future clients
    // and if group 1 was active, make group 0 active
    fb_t* fb = fbi->fb;
    mtx_lock(&fb->lock);
    if (fb->fullscreen == fbi) {
        fb->fullscreen = NULL;
        if (fb->active == GROUP_FULLSCREEN) {
            fb->active = GROUP_VIRTCON;
            zx_object_signal(fb->event, ZX_USER_SIGNAL_1, ZX_USER_SIGNAL_0);
        }
    }
    mtx_unlock(&fb->lock);

    if (fbi->buffer) {
        zx_vmar_unmap(zx_vmar_root_self(), (uintptr_t) fbi->buffer, fbi->fb->bufsz);
    }
    if (fbi->vmo != ZX_HANDLE_INVALID) {
        zx_handle_close(fbi->vmo);
    }
    free(fbi);
}

zx_protocol_device_t fbi_ops = {
    .version = DEVICE_OPS_VERSION,
    .ioctl = fbi_ioctl,
    .release = fbi_release,
};

static zx_status_t fb_open_at(void* ctx, zx_device_t** out, const char* path, uint32_t flags) {
    fb_t* fb = ctx;
    if (!fb->zxdev) {
        return ZX_ERR_PEER_CLOSED;
    }

    uint32_t group;
    if (!strcmp(path, "virtcon")) {
        group = GROUP_VIRTCON;
    } else {
        group = GROUP_FULLSCREEN;
    }

    fbi_t* fbi;
    if ((fbi = calloc(1, sizeof(fbi_t))) == NULL) {
        return ZX_ERR_NO_MEMORY;
    }
    fbi->fb = fb;
    fbi->group = group;

    device_add_args_t args = {
        .version = DEVICE_ADD_ARGS_VERSION,
        .name = "framebuffer",
        .ctx = fbi,
        .ops = &fbi_ops,
        .proto_id = ZX_PROTOCOL_DISPLAY,
        .flags = DEVICE_ADD_INSTANCE,
    };

    // if we are a new fullscreen client (against a non-GPU display),
    // fail if there's already a fullscreen client
    // otherwise the fullscreen client becomes active
    if (!FB_HAS_GPU(fb)) {
        mtx_lock(&fb->lock);
        if (!fb->zxdev) {
            mtx_unlock(&fb->lock);
            return ZX_ERR_PEER_CLOSED;
        }
        if (fbi->group == GROUP_FULLSCREEN) {
            if (fb->fullscreen != NULL) {
                mtx_unlock(&fb->lock);
                free(fbi);
                return ZX_ERR_ALREADY_BOUND;
            }
            fb->fullscreen = fbi;
            fb->active = GROUP_FULLSCREEN;
            zx_object_signal(fb->event, ZX_USER_SIGNAL_0, ZX_USER_SIGNAL_1);
        }
        mtx_unlock(&fb->lock);
    }

    zx_status_t r;
    if ((r = device_add(fb->zxdev, &args, out)) < 0) {
        fbi_release(fbi);
        return r;
    }

    return ZX_OK;
}

static zx_status_t fb_open(void* ctx, zx_device_t** out, uint32_t flags) {
    return fb_open_at(ctx, out, "", flags);
}

static void fb_unbind(void* ctx) {
    fb_t* fb = ctx;
    zx_device_t* dev = fb->zxdev;
    mtx_lock(&fb->lock);
    fb->zxdev = NULL;
    mtx_unlock(&fb->lock);
    device_remove(dev);
}

static void fb_release(void* ctx) {
    fb_t* fb = ctx;
    zx_handle_close(fb->event);
    free(fb);
}

static zx_protocol_device_t fb_ops = {
    .version = DEVICE_OPS_VERSION,
    .open = fb_open,
    .open_at = fb_open_at,
    .unbind = fb_unbind,
    .release = fb_release,
};

static zx_status_t fb_bind(void* ctx, zx_device_t* dev) {
    fb_t* fb;
    if ((fb = calloc(1, sizeof(fb_t))) == NULL) {
        return ZX_ERR_NO_MEMORY;
    }

    zx_status_t r;
    if ((r = device_get_protocol(dev, ZX_PROTOCOL_DISPLAY, &fb->dpy)) < 0) {
        printf("fb: display does not support display protocol: %d\n", r);
        goto fail;
    }
    if ((r = fb->dpy.ops->get_mode(fb->dpy.ctx, &fb->info)) < 0) {
        printf("fb: display get mode failed: %d\n", r);
        goto fail;
    }
    if ((r = fb->dpy.ops->get_framebuffer(fb->dpy.ctx, &fb->buffer)) < 0) {
        printf("fb: display get framebuffer failed: %d\n", r);
        goto fail;
    }

    if ((r = zx_event_create(0, &fb->event)) < 0) {
        goto fail;
    }
    zx_object_signal(fb->event, 0, ZX_USER_SIGNAL_0);

    if ((fb->info.pixelsize = ZX_PIXEL_FORMAT_BYTES(fb->info.format)) == 0) {
        printf("fb: unknown format %u\n", fb->info.format);
        r = ZX_ERR_NOT_SUPPORTED;
        goto fail;
    }

    fb->bufsz = fb->info.pixelsize * fb->info.stride * fb->info.height;

    printf("fb: %u x %u (stride=%u pxlsz=%u format=%u): %zu bytes @ %p%s\n",
           fb->info.width, fb->info.height,
           fb->info.stride, fb->info.pixelsize, fb->info.format, fb->bufsz,
           fb->buffer,
           FB_HAS_GPU(fb) ? " GPU" : " SW");

    mtx_init(&fb->lock, mtx_plain);

    device_add_args_t args = {
        .version = DEVICE_ADD_ARGS_VERSION,
        .name = "framebuffer",
        .ctx = fb,
        .ops = &fb_ops,
        .proto_id = ZX_PROTOCOL_FRAMEBUFFER,
    };

    if ((r = device_add(dev, &args, &fb->zxdev)) < 0) {
        goto fail;
    }

    if (FB_HAS_GPU(fb)) {
        fb->dpy.ops->set_ownership_change_callback(fb->dpy.ctx, fb_callback, fb);
        FB_ACQUIRE(fb);
    }
    return ZX_OK;

fail:
    zx_handle_close(fb->event);
    free(fb);
    return r;
}

static zx_driver_ops_t fb_driver_ops = {
    .version = DRIVER_OPS_VERSION,
    .bind = fb_bind,
};

ZIRCON_DRIVER_BEGIN(framebuffer, fb_driver_ops, "zircon", "0.1", 1)
    BI_MATCH_IF(EQ, BIND_PROTOCOL, ZX_PROTOCOL_DISPLAY),
ZIRCON_DRIVER_END(framebuffer)
