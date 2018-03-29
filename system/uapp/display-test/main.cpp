// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>

#include <zircon/device/display-controller.h>
#include <zircon/process.h>
#include <zircon/syscalls.h>
#include <zircon/types.h>

#include <lib/fidl/cpp/message.h>
#include <lib/fidl/cpp/string_view.h>
#include <lib/fidl/cpp/vector_view.h>

#include <zircon/pixelformat.h>
#include <zircon/syscalls.h>

#include "display/c/fidl.h"

#ifdef DEBUG
#define dprintf(...) printf(__VA_ARGS__)
#else
#define dprintf(...)
#endif

static int32_t display_id;
static zx_handle_t dc_handle;

static int32_t width;
static int32_t height;
static int32_t stride;
static zx_pixel_format_t format;

typedef struct image {
    int32_t id;
    void* buf;
    zx_handle_t events[3];
    int32_t event_ids[3];
} image_t;

// Indicies into image_t.event and image_t.event_ids
#define WAIT_EVENT 0
#define PRESENT_EVENT 1
#define SIGNAL_EVENT 2

static bool bind_display() {
    zx_status_t status;
    printf("Opening controller\n");
    int vfd = open("/dev/class/display-controller/000", O_RDWR);
    if (vfd < 0) {
        printf("Failed to open display controller (%d)\n", errno);
        return false;
    }

    printf("Getting handle\n");
    if (ioctl_display_controller_get_handle(vfd, &dc_handle) != sizeof(zx_handle_t)) {
        printf("Failed to get display controller handle\n");
        return false;
    }

    printf("Setting callback\n");
    zx_handle_t dc_cb, dc_cb_prime;
    if (zx_channel_create(0, &dc_cb, &dc_cb_prime) != ZX_OK) {
        printf("Failed to create display controller callback channel\n");
        return false;
    }

    display_ControllerSetControllerCallbackRequest set_cb_msg;
    set_cb_msg.hdr.ordinal = display_ControllerSetControllerCallbackOrdinal;
    set_cb_msg.callback = FIDL_HANDLE_PRESENT;
    if ((status = zx_channel_write(dc_handle, 0, &set_cb_msg,
                                   sizeof(set_cb_msg), &dc_cb_prime, 1)) != ZX_OK) {
        printf("Failed to set callback %d\n", status);
        return false;
    }

    printf("Wating for display\n");
    zx_handle_t observed;
    uint32_t signals = ZX_CHANNEL_READABLE | ZX_CHANNEL_PEER_CLOSED;
    if (zx_object_wait_one(dc_cb, signals, ZX_TIME_INFINITE, &observed) != ZX_OK) {
        printf("Wait failed\n");
        return false;
    }
    if (observed & ZX_CHANNEL_PEER_CLOSED) {
        printf("Display controller died\n");
        return false;
    }

    printf("Querying display\n");
    uint8_t byte_buffer[ZX_CHANNEL_MAX_MSG_BYTES];
    fidl::Message msg(fidl::BytePart(byte_buffer, ZX_CHANNEL_MAX_MSG_BYTES), fidl::HandlePart());
    if (msg.Read(dc_cb, 0) != ZX_OK) {
        printf("Read failed\n");
        return false;
    }

    const char* err_msg;
    if (msg.Decode(&display_ControllerCallbackOnDisplaysChangedRequestTable, &err_msg) != ZX_OK) {
        printf("Fidl decode error %s\n", err_msg);
        return false;
    }

    auto changes = reinterpret_cast<display_ControllerCallbackOnDisplaysChangedRequest*>(
            msg.bytes().data());
    auto display = reinterpret_cast<display_Info*>(changes->added.data);
    auto mode = reinterpret_cast<display_Mode*>(display->modes.data);
    auto pixel_format = reinterpret_cast<int32_t*>(display->pixel_format.data)[0];

    printf("Getting stride\n");
    display_ControllerComputeLinearImageStrideRequest stride_msg;
    stride_msg.hdr.ordinal = display_ControllerComputeLinearImageStrideOrdinal;
    stride_msg.width = mode->horizontal_resolution;
    stride_msg.pixel_format = pixel_format;

    display_ControllerComputeLinearImageStrideResponse stride_rsp;
    zx_channel_call_args_t stride_call = {};
    stride_call.wr_bytes = &stride_msg;
    stride_call.rd_bytes = &stride_rsp;
    stride_call.wr_num_bytes = sizeof(stride_msg);
    stride_call.rd_num_bytes = sizeof(stride_rsp);
    uint32_t actual_bytes, actual_handles;
    zx_status_t read_status;
    if (zx_channel_call(dc_handle, 0, ZX_TIME_INFINITE,
            &stride_call, &actual_bytes, &actual_handles, &read_status) != ZX_OK) {
        printf("Failed to make stride call\n");
        return false;
    }

    display_id = display->id;

    width = mode->horizontal_resolution;
    height = mode->vertical_resolution;
    stride = stride_rsp.stride;
    format = pixel_format;

    printf("Bound to display %dx%d (stride = %d, format=%d)\n", width, height, stride, format);

    return true;
}

static bool create_image(image_t* img) {
    printf("Creating image\n");
    for (int i = 0; i < 3; i++) {
        static int event_id = 1;
        printf("Creating event %d (%d)\n", i, event_id);

        zx_handle_t e1, e2;
        if (zx_event_create(0, &e1) != ZX_OK
                || zx_handle_duplicate(e1, ZX_RIGHT_SAME_RIGHTS, &e2) != ZX_OK) {
            printf("Failed to create event\n");
            return false;
        }

        display_ControllerImportEventRequest import_evt_msg;
        import_evt_msg.hdr.ordinal = display_ControllerImportEventOrdinal;
        import_evt_msg.id = event_id++;
        import_evt_msg.event = FIDL_HANDLE_PRESENT;

        if (zx_channel_write(dc_handle, 0, &import_evt_msg,
                             sizeof(import_evt_msg), &e2, 1) != ZX_OK) {
            printf("Failed to send import message\n");
            return false;
        }

        img->events[i] = e1;
        img->event_ids[i] = import_evt_msg.id;
    }

    printf("Creating and mapping vmo\n");
    zx_handle_t vmo;
    display_ControllerAllocateVmoRequest alloc_msg;
    alloc_msg.hdr.ordinal = display_ControllerAllocateVmoOrdinal;
    alloc_msg.size = stride * height * ZX_PIXEL_FORMAT_BYTES(format);

    display_ControllerAllocateVmoResponse alloc_rsp;
    zx_channel_call_args_t call_args = {};
    call_args.wr_bytes = &alloc_msg;
    call_args.rd_bytes = &alloc_rsp;
    call_args.rd_handles = &vmo;
    call_args.wr_num_bytes = sizeof(alloc_msg);
    call_args.rd_num_bytes = sizeof(alloc_rsp);
    call_args.rd_num_handles = 1;
    uint32_t actual_bytes, actual_handles;
    zx_status_t read_status;
    zx_status_t status;
    if ((status = zx_channel_call(dc_handle, 0, ZX_TIME_INFINITE, &call_args,
                        &actual_bytes, &actual_handles, &read_status)) != ZX_OK) {
        printf("Vmo alloc call failed %d %d\n", status, read_status);
        return false;
    }
    if (alloc_rsp.res != ZX_OK) {
        printf("Failed to alloc vmo %d\n", alloc_rsp.res);
        return false;
    }

    uintptr_t addr;
    uint32_t len = stride * height *  ZX_PIXEL_FORMAT_BYTES(format);
    uint32_t perms = ZX_VM_FLAG_PERM_READ | ZX_VM_FLAG_PERM_WRITE;
    if (zx_vmar_map(zx_vmar_root_self(), 0, vmo, 0, len, perms, &addr) != ZX_OK) {
        printf("Failed to map vmar\n");
        return false;
    }

    printf("Importing image\n");
    display_ControllerImportVmoImageRequest import_msg;
    import_msg.hdr.ordinal = display_ControllerImportVmoImageOrdinal;
    import_msg.image_config.height = height;
    import_msg.image_config.width = width;
    import_msg.image_config.pixel_format = format;
    import_msg.image_config.type = IMAGE_TYPE_SIMPLE;
    import_msg.vmo = FIDL_HANDLE_PRESENT;
    import_msg.offset = 0;
    zx_handle_t vmo_dup;
    if (zx_handle_duplicate(vmo, ZX_RIGHT_SAME_RIGHTS, &vmo_dup) != ZX_OK) {
        printf("Failed to dup handle\n");
        return false;
    }

    display_ControllerImportVmoImageResponse import_rsp;
    zx_channel_call_args_t import_call = {};
    import_call.wr_bytes = &import_msg;
    import_call.wr_handles = &vmo_dup;
    import_call.rd_bytes = &import_rsp;
    import_call.wr_num_bytes = sizeof(import_msg);
    import_call.wr_num_handles = 1;
    import_call.rd_num_bytes = sizeof(import_rsp);
    if (zx_channel_call(dc_handle, 0, ZX_TIME_INFINITE, &import_call,
                        &actual_bytes, &actual_handles, &read_status) != ZX_OK) {
        printf("Failed to make import call\n");
        return false;
    }

    if (import_rsp.res != ZX_OK) {
        printf("Failed to import vmo\n");
        return false;
    }

    img->id = import_rsp.image_id;
    img->buf = reinterpret_cast<void*>(addr);

    printf("Created display\n");

    return true;
}

#define NUM_FRAMES 120

int main(int argc, char* argv[]) {
    printf("Running display test\n");

    if (!bind_display()) {
        return -1;
    }

    image_t img1, img2;

    if (!create_image(&img1) || !create_image(&img2)) {
        return -1;
    }

    for (int i = 0; i < NUM_FRAMES; i++) {
        printf("Rendering frame %d\n", i);
        image_t* img = i % 2 == 0 ? &img1 : &img2;

        // No signals the first iteration
        if (i / 2 >= 1) {
            zx_signals_t observed;
            if (zx_object_wait_one(img->events[SIGNAL_EVENT], ZX_EVENT_SIGNALED,
                                   zx_deadline_after(ZX_SEC(1)), &observed) != ZX_OK) {
                dprintf("Buffer failed to become free\n");
                return -1;
            }
        }

        zx_object_signal(img->events[SIGNAL_EVENT], ZX_EVENT_SIGNALED, 0);
        zx_object_signal(img->events[PRESENT_EVENT], ZX_EVENT_SIGNALED, 0);

        display_ControllerSetDisplayImageRequest set_msg;
        set_msg.hdr.ordinal = display_ControllerSetDisplayImageOrdinal;
        set_msg.display = display_id;
        set_msg.image_id = img->id;
        set_msg.wait_event_id = img->event_ids[WAIT_EVENT];
        set_msg.present_event_id = img->event_ids[PRESENT_EVENT];
        set_msg.signal_event_id = img->event_ids[SIGNAL_EVENT];
        if (zx_channel_write(dc_handle, 0, &set_msg, sizeof(set_msg), nullptr, 0) != ZX_OK) {
            dprintf("Failed to set image\n");
            return -1;
        }

        display_ControllerCheckConfigRequest check_msg;
        display_ControllerCheckConfigResponse check_rsp;
        check_msg.discard = false;
        check_msg.hdr.ordinal = display_ControllerCheckConfigOrdinal;
        zx_channel_call_args_t check_call = {};
        check_call.wr_bytes = &check_msg;
        check_call.rd_bytes = &check_rsp;
        check_call.wr_num_bytes = sizeof(check_msg);
        check_call.rd_num_bytes = sizeof(check_rsp);
        uint32_t actual_bytes, actual_handles;
        zx_status_t read_status;
        if (zx_channel_call(dc_handle, 0, ZX_TIME_INFINITE, &check_call,
                            &actual_bytes, &actual_handles, &read_status) != ZX_OK) {
            dprintf("Failed to make check call\n");
            return -1;
        }

        if (!check_rsp.valid) {
            dprintf("Config not valid\n");
            return -1;
        }

        display_ControllerApplyConfigRequest apply_msg;
        apply_msg.hdr.ordinal = display_ControllerApplyConfigOrdinal;
        if (zx_channel_write(dc_handle, 0, &apply_msg, sizeof(apply_msg), nullptr, 0) != ZX_OK) {
            dprintf("Apply failed\n");
            return -1;
        }

        for (int y = 0; y < height; y++) {
            int32_t color =
                    y < ((((double) height) / NUM_FRAMES) * (i + 1)) ? 0xffff0000 : 0xff00ff00;
            for (int x = 0; x < width; x++) {
                *(static_cast<int32_t*>(img->buf) + (y * stride) + x) = color;
            }
        }
        zx_cache_flush(img->buf,
                       stride * height *  ZX_PIXEL_FORMAT_BYTES(format), ZX_CACHE_FLUSH_DATA);

        dprintf("Signaling wait sem\n");
        zx_object_signal(img->events[WAIT_EVENT], 0, ZX_EVENT_SIGNALED);

        dprintf("Waiting on present sem %d\n", img->event_ids[PRESENT_EVENT]);
        zx_signals_t observed;
        if (zx_object_wait_one(img->events[PRESENT_EVENT], ZX_EVENT_SIGNALED,
                               zx_deadline_after(ZX_SEC(1)), &observed) != ZX_OK) {
            dprintf("Buffer failed to become visible\n");
            return -1;
        }
    }

    printf("Done rendering\n");
    zx_nanosleep(zx_deadline_after(ZX_MSEC(500)));
    printf("Return!\n");

    return 0;
}
