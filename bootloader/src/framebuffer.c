// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <framebuffer.h>
#include <xefi.h>

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

void set_graphics_mode(efi_system_table* sys, efi_graphics_output_protocol* gop, char* fbres) {
    if (!gop || !fbres) return;

    uint32_t hres = 0;
    hres = atol(fbres);

    char* x = strchr(fbres, 'x');
    if (!x) return;
    x++;

    uint32_t vres = 0;
    vres = atol(x);
    if (!hres || !vres) return;

    uint32_t max_mode = gop->Mode->MaxMode;

    for (int i = 0; i < max_mode; i++) {
        efi_graphics_output_mode_information* mode_info;
        size_t info_size = 0;
        efi_status status = gop->QueryMode(gop, i, &info_size, &mode_info);
        if (EFI_ERROR(status)) {
            printf("Could not retrieve mode %d: %s\n", i, xefi_strerror(status));
            continue;
        }

        if (mode_info->HorizontalResolution == hres &&
            mode_info->VerticalResolution == vres) {
            gop->SetMode(gop, i);
            sys->BootServices->Stall(1000);
            sys->ConOut->ClearScreen(sys->ConOut);
            return;
        }
    }
    printf("Could not find framebuffer mode %ux%u; using default mode = %ux%u\n",
            hres, vres, gop->Mode->Info->HorizontalResolution, gop->Mode->Info->VerticalResolution);
    sys->BootServices->Stall(5000000);
}

void draw_logo(efi_graphics_output_protocol* gop) {
    if (!gop) return;

    const uint32_t h_res = gop->Mode->Info->HorizontalResolution;
    const uint32_t v_res = gop->Mode->Info->VerticalResolution;
    efi_graphics_output_blt_pixel fuchsia = {
        .Red = 0xFF,
        .Green = 0x0,
        .Blue = 0xFF,
    };
    efi_graphics_output_blt_pixel black = {
        .Red = 0x0,
        .Green = 0x0,
        .Blue = 0x0
    };

    // Blank the screen, removing vendor UEFI logos
    gop->Blt(gop, &black, EfiBltVideoFill, 0, 0, 0, 0, h_res, v_res, 0);
    // Draw the Fuchsia stripe on the top of the screen
    gop->Blt(gop, &fuchsia, EfiBltVideoFill, 0, 0, 0, 0, h_res, v_res/100, 0);
}
