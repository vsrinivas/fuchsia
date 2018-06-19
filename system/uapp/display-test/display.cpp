// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "display.h"
#include <zircon/syscalls.h>

Display::Display(fuchsia_display_Info* info) {
    id_ = info->id;

    auto pixel_format = reinterpret_cast<int32_t*>(info->pixel_format.data);
    for (unsigned i = 0; i < info->pixel_format.count; i++) {
        pixel_formats_.push_back(pixel_format[i]);
    }

    auto mode = reinterpret_cast<fuchsia_display_Mode*>(info->modes.data);
    for (unsigned i = 0; i < info->modes.count; i++) {
        modes_.push_back(mode[i]);
    }

    auto cursors = reinterpret_cast<fuchsia_display_CursorInfo*>(info->cursor_configs.data);
    for (unsigned i = 0; i < info->cursor_configs.count; i++) {
        cursors_.push_back(cursors[i]);
    }
}

void Display::Dump() {
    printf("Display id = %ld\n", id_);

    printf("\tSupported pixel formats:\n");
    for (unsigned i = 0; i < pixel_formats_.size(); i++) {
        printf("\t\t%d\t: %08x\n", i, pixel_formats_[i]);
    }

    printf("\n\tSupported display modes:\n");
    for (unsigned i = 0; i < modes_.size(); i++) {
        printf("\t\t%d\t: %dx%d\t%d.%02d\n", i,
               modes_[i].horizontal_resolution, modes_[i].vertical_resolution,
               modes_[i].refresh_rate_e2 / 100, modes_[i].refresh_rate_e2 % 100);
    }

    printf("\n\tSupported cursor modes:\n");
    for (unsigned i = 0; i < cursors_.size(); i++) {
        printf("\t\t%d\t: %dx%d\t%08x\n",
               i, cursors_[i].width, cursors_[i].height, cursors_[i].pixel_format);
    }
    printf("\n");
}

void Display::Init(zx_handle_t dc_handle) {
    if (mode_idx_ == 0) {
        return;
    }

    fuchsia_display_ControllerSetDisplayModeRequest set_mode_msg;
    set_mode_msg.hdr.ordinal = fuchsia_display_ControllerSetDisplayModeOrdinal;
    set_mode_msg.display_id = id_;
    set_mode_msg.mode = modes_[mode_idx_];
    ZX_ASSERT(zx_channel_write(dc_handle, 0,
                               &set_mode_msg, sizeof(set_mode_msg), nullptr, 0) == ZX_OK);
}
