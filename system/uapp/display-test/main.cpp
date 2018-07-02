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

#include <fbl/vector.h>
#include <fbl/algorithm.h>
#include <lib/fidl/cpp/message.h>
#include <lib/fidl/cpp/string_view.h>
#include <lib/fidl/cpp/vector_view.h>

#include <zircon/pixelformat.h>
#include <zircon/status.h>
#include <zircon/syscalls.h>

#include "display.h"
#include "fuchsia/display/c/fidl.h"
#include "virtual-layer.h"

static zx_handle_t dc_handle;

static bool wait_for_driver_event() {
    zx_handle_t observed;
    uint32_t signals = ZX_CHANNEL_READABLE | ZX_CHANNEL_PEER_CLOSED;
    if (zx_object_wait_one(dc_handle, signals, ZX_TIME_INFINITE, &observed) != ZX_OK) {
        printf("Wait failed\n");
        return false;
    }
    if (observed & ZX_CHANNEL_PEER_CLOSED) {
        printf("Display controller died\n");
        return false;
    }
    return true;
}

static bool bind_display(fbl::Vector<Display>* displays) {
    printf("Opening controller\n");
    int vfd = open("/dev/class/display-controller/000", O_RDWR);
    if (vfd < 0) {
        printf("Failed to open display controller (%d)\n", errno);
        return false;
    }

    if (ioctl_display_controller_get_handle(vfd, &dc_handle) != sizeof(zx_handle_t)) {
        printf("Failed to get display controller handle\n");
        return false;
    }

    fuchsia_display_ControllerEnableVsyncRequest enable_vsync;
    enable_vsync.hdr.ordinal = fuchsia_display_ControllerEnableVsyncOrdinal;
    enable_vsync.enable = true;
    if (zx_channel_write(dc_handle, 0, &enable_vsync, sizeof(enable_vsync), nullptr, 0) != ZX_OK) {
        printf("Failed to enable vsync\n");
        return false;
    }

    printf("Wating for display\n");
    if (!wait_for_driver_event()) {
        return false;
    }

    printf("Querying display\n");
    uint8_t byte_buffer[ZX_CHANNEL_MAX_MSG_BYTES];
    fidl::Message msg(fidl::BytePart(byte_buffer, ZX_CHANNEL_MAX_MSG_BYTES), fidl::HandlePart());
    if (msg.Read(dc_handle, 0) != ZX_OK) {
        printf("Read failed\n");
        return false;
    }

    const char* err_msg;
    if (msg.Decode(&fuchsia_display_ControllerDisplaysChangedEventTable, &err_msg) != ZX_OK) {
        printf("Fidl decode error %s\n", err_msg);
        return false;
    }

    auto changes = reinterpret_cast<fuchsia_display_ControllerDisplaysChangedEvent*>(
            msg.bytes().data());
    auto display_info = reinterpret_cast<fuchsia_display_Info*>(changes->added.data);

    for (unsigned i = 0; i < changes->added.count; i++) {
        displays->push_back(Display(display_info + i));
    }

    return true;
}

Display* find_display(fbl::Vector<Display>& displays, const char* id_str) {
    uint64_t id = strtoul(id_str, nullptr, 10);
    if (id != 0) { // 0 is the invalid id, and luckily what strtoul returns on failure
        for (auto& d : displays) {
            if (d.id() == id) {
                return &d;
            }
        }
    }
    return nullptr;
}

bool update_display_layers(const fbl::Vector<VirtualLayer*>& layers,
                           const Display& display, fbl::Vector<uint64_t>* current_layers) {
    fbl::Vector<uint64_t> new_layers;

    for (auto& layer : layers) {
        uint64_t id = layer->id(display.id());
        if (id != INVALID_ID) {
            new_layers.push_back(id);
        }
    }

    bool layer_change = new_layers.size() != current_layers->size();
    if (!layer_change) {
        for (unsigned i = 0; i < new_layers.size(); i++) {
            if (new_layers[i] != (*current_layers)[i]) {
                layer_change = true;
                break;
            }
        }
    }

    if (layer_change) {
        current_layers->swap(new_layers);

        uint32_t size = static_cast<int32_t>(
                sizeof(fuchsia_display_ControllerSetDisplayLayersRequest) +
                FIDL_ALIGN(sizeof(uint64_t) * current_layers->size()));
        uint8_t fidl_bytes[size];

        auto set_layers_msg =
                reinterpret_cast<fuchsia_display_ControllerSetDisplayLayersRequest*>(fidl_bytes);
        set_layers_msg->hdr.ordinal = fuchsia_display_ControllerSetDisplayLayersOrdinal;
        set_layers_msg->layer_ids.count = current_layers->size();
        set_layers_msg->layer_ids.data = reinterpret_cast<void*>(FIDL_ALLOC_PRESENT);
        set_layers_msg->display_id = display.id();

        auto layer_list = reinterpret_cast<uint64_t*>(set_layers_msg + 1);
        for (auto layer_id : *current_layers) {
            *(layer_list++) = layer_id;
        }

        if (zx_channel_write(dc_handle, 0, fidl_bytes, size, nullptr, 0) != ZX_OK) {
            printf("Failed to set layers\n");
            return false;
        }
    }
    return true;
}

bool apply_config() {
    fuchsia_display_ControllerCheckConfigRequest check_msg;
    uint8_t check_resp_bytes[ZX_CHANNEL_MAX_MSG_BYTES];
    check_msg.discard = false;
    check_msg.hdr.ordinal = fuchsia_display_ControllerCheckConfigOrdinal;
    zx_channel_call_args_t check_call = {};
    check_call.wr_bytes = &check_msg;
    check_call.rd_bytes = check_resp_bytes;
    check_call.wr_num_bytes = sizeof(check_msg);
    check_call.rd_num_bytes = sizeof(check_resp_bytes);
    uint32_t actual_bytes, actual_handles;
    zx_status_t status;
    if ((status = zx_channel_call(dc_handle, 0, ZX_TIME_INFINITE, &check_call,
                        &actual_bytes, &actual_handles)) != ZX_OK) {
        printf("Failed to make check call: %d (%s)\n", status, zx_status_get_string(status));
        return false;
    }

    fidl::Message msg(fidl::BytePart(check_resp_bytes, ZX_CHANNEL_MAX_MSG_BYTES, actual_bytes),
                      fidl::HandlePart());
    const char* err_msg;
    if (msg.Decode(&fuchsia_display_ControllerCheckConfigResponseTable, &err_msg) != ZX_OK) {
        return false;
    }
    auto check_rsp =
            reinterpret_cast<fuchsia_display_ControllerCheckConfigResponse*>(msg.bytes().data());

    if (check_rsp->res.count) {
        printf("Config not valid\n");
        fuchsia_display_ConfigResult* arr =
                static_cast<fuchsia_display_ConfigResult*>(check_rsp->res.data);
        for (unsigned i = 0; i < check_rsp->res.count; i++) {
            printf("Display %ld\n", arr[i].display_id);
            if (arr[i].error) {
                printf("  Display error: %d\n", arr[i].error);
            }

            uint64_t* layers = static_cast<uint64_t*>(arr[i].layers.data);
            fuchsia_display_ClientCompositionOp* ops =
                    static_cast<fuchsia_display_ClientCompositionOp*>(arr[i].client_ops.data);
            for (unsigned j = 0; j < arr[i].layers.count; j++) {
                printf("  Layer %ld: %d\n", layers[j], ops[j]);
            }
        }
        return false;
    }

    fuchsia_display_ControllerApplyConfigRequest apply_msg;
    apply_msg.hdr.ordinal = fuchsia_display_ControllerApplyConfigOrdinal;
    if (zx_channel_write(dc_handle, 0, &apply_msg, sizeof(apply_msg), nullptr, 0) != ZX_OK) {
        printf("Apply failed\n");
        return false;
    }
    return true;
}

zx_status_t wait_for_vsync(const fbl::Vector<VirtualLayer*>& layers, zx_time_t* timestamp) {
    *timestamp = 0;

    if (!wait_for_driver_event()) {
        return ZX_ERR_STOP;
    }

    uint8_t byte_buffer[ZX_CHANNEL_MAX_MSG_BYTES];
    fidl::Message msg(fidl::BytePart(byte_buffer, ZX_CHANNEL_MAX_MSG_BYTES), fidl::HandlePart());
    if (msg.Read(dc_handle, 0) != ZX_OK) {
        printf("Read failed\n");
        return ZX_ERR_STOP;
    }

    switch (msg.ordinal()) {
    case fuchsia_display_ControllerDisplaysChangedOrdinal:
        printf("Display disconnected\n");
        return ZX_ERR_STOP;
    case fuchsia_display_ControllerClientOwnershipChangeOrdinal:
        printf("Ownership change\n");
        return ZX_ERR_NEXT;
    case fuchsia_display_ControllerVsyncOrdinal:
        break;
    default:
        printf("Unknown ordinal %d\n", msg.ordinal());
        return ZX_ERR_STOP;
    }

    const char* err_msg;
    if (msg.Decode(&fuchsia_display_ControllerVsyncEventTable, &err_msg) != ZX_OK) {
        printf("Fidl decode error %s\n", err_msg);
        return ZX_ERR_STOP;
    }

    auto vsync = reinterpret_cast<fuchsia_display_ControllerVsyncEvent*>(msg.bytes().data());
    *timestamp = vsync->timestamp;
    uint64_t* image_ids = reinterpret_cast<uint64_t*>(vsync->images.data);

    for (auto& layer : layers) {
        uint64_t id = layer->image_id(vsync->display_id);
        if (id == 0) {
            continue;
        }
        for (unsigned i = 0; i < vsync->images.count; i++) {
            if (image_ids[i] == layer->image_id(vsync->display_id)) {
                layer->set_frame_done(vsync->display_id);
            }
        }
    }

    for (auto& layer : layers) {
        if (!layer->is_done()) {
            return ZX_ERR_NEXT;
        }
    }
    return ZX_OK;
}

int main(int argc, const char* argv[]) {
    printf("Running display test\n");

    fbl::Vector<Display> displays;
    fbl::Vector<fbl::Vector<uint64_t>> display_layers;
    fbl::Vector<VirtualLayer*> layers;
    int32_t num_frames = 120; // default to 120 frames

    if (!bind_display(&displays)) {
        return -1;
    }

    if (displays.is_empty()) {
        printf("No displays available\n");
        return 0;
    }

    for (unsigned i = 0; i < displays.size(); i++) {
        display_layers.push_back(fbl::Vector<uint64_t>());
    }

    argc--;
    argv++;

    while (argc) {
        if (strcmp(argv[0], "--dump") == 0) {
            for (auto& display : displays) {
                display.Dump();
            }
            return 0;
        } else if (strcmp(argv[0], "--mode-set") == 0
                || strcmp(argv[0], "--format-set") == 0) {
            Display* display = find_display(displays, argv[1]);
            if (!display) {
                printf("Invalid display \"%s\" for %s\n", argv[1], argv[0]);
                return -1;
            }
            if (strcmp(argv[0], "--mode-set") == 0) {
                if (!display->set_mode_idx(atoi(argv[2]))) {
                    printf("Invalid mode id\n");
                    return -1;
                }
            } else {
                if (!display->set_format_idx(atoi(argv[2]))) {
                    printf("Invalid format id\n");
                    return -1;
                }
            }
            argv += 3;
            argc -= 3;
        } else if (strcmp(argv[0], "--grayscale") == 0) {
            for (auto& d : displays) {
                d.set_grayscale(true);
            }
            argv++;
            argc--;
        } else if (strcmp(argv[0], "--num-frames") == 0) {
            num_frames = atoi(argv[1]);
            argv += 2;
            argc -= 2;
        } else {
            printf("Unrecognized argument \"%s\"\n", argv[0]);
            return -1;
        }
    }

    // Color layer which covers all displays
    ColorLayer layer0(displays);
    layers.push_back(&layer0);

    // Layer which covers all displays and uses page flipping.
    PrimaryLayer layer1(displays);
    layer1.SetLayerFlipping(true);
    layer1.SetAlpha(true, .75);
    layers.push_back(&layer1);

    // Layer which covers the left half of the of the first display
    // and toggles on and off every frame.
    PrimaryLayer layer2(&displays[0]);
    layer2.SetImageDimens(displays[0].mode().horizontal_resolution / 2,
                          displays[0].mode().vertical_resolution);
    layer2.SetLayerToggle(true);
    layers.push_back(&layer2);

// Intel only supports 3 layers, so add ifdef for quick toggling of the 3rd layer
#if 1
    // Layer which is smaller than the display and bigger than its image
    // and which animates back and forth across all displays and also
    // its src image and also rotates.
    PrimaryLayer layer3(displays);
    // Width is the larger of disp_width/2, display_height/2, but we also need
    // to make sure that it's less than the smaller display dimension.
    uint32_t width = fbl::min(fbl::max(displays[0].mode().vertical_resolution / 2,
                                       displays[0].mode().horizontal_resolution / 2),
                              fbl::min(displays[0].mode().vertical_resolution,
                                       displays[0].mode().horizontal_resolution));
    uint32_t height = fbl::min(displays[0].mode().vertical_resolution / 2,
                               displays[0].mode().horizontal_resolution / 2);
    layer3.SetImageDimens(width * 2, height);
    layer3.SetDestFrame(width, height);
    layer3.SetSrcFrame(width, height);
    layer3.SetPanDest(true);
    layer3.SetPanSrc(true);
    layer3.SetRotates(true);
    layers.push_back(&layer3);
#else
    CursorLayer layer4(displays);
    layers.push_back(&layer4);
#endif

    printf("Initializing layers\n");
    for (auto& layer : layers) {
        if (!layer->Init(dc_handle)) {
            printf("Layer init failed\n");
            return -1;
        }
    }

    for (auto& display : displays) {
        display.Init(dc_handle);
    }

    printf("Starting rendering\n");
    for (int i = 0; i < num_frames; i++) {
        zx_time_t now = zx_clock_get(ZX_CLOCK_MONOTONIC);
        for (auto& layer : layers) {
            // Step before waiting, since not every layer is used every frame
            // so we won't necessarily need to wait.
            layer->StepLayout(i);

            if (!layer->WaitForReady()) {
                printf("Buffer failed to become free\n");
                return -1;
            }

            layer->clear_done();
            layer->SendLayout(dc_handle);
        }

        for (unsigned i = 0; i < displays.size(); i++) {
            if (!update_display_layers(layers, displays[i], &display_layers[i])) {
                return -1;
            }
        }

        if (!apply_config()) {
            return -1;
        }

        for (auto& layer : layers) {
            layer->Render(i);
        }

        zx_status_t status;
        zx_time_t deadline = now + ZX_MSEC(100);
        zx_time_t timestamp;
        while (((status = wait_for_vsync(layers, &timestamp)) == ZX_ERR_NEXT)
                && timestamp < deadline) {
            // wait again
        }
        ZX_ASSERT(status == ZX_OK);
    }

    printf("Done rendering\n");
    zx_nanosleep(zx_deadline_after(ZX_MSEC(500)));

    return 0;
}
