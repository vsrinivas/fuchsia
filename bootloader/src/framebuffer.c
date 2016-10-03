// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <framebuffer.h>
#include <xefi.h>

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

static efi_graphics_output_protocol* fb_get_gop() {
    static efi_graphics_output_protocol* gop = NULL;
    if (!gop) {
        gBS->LocateProtocol(&GraphicsOutputProtocol, NULL, (void**)&gop);
    }
    return gop;
}

uint32_t get_gfx_mode() {
    efi_graphics_output_protocol* gop = fb_get_gop();
    return gop->Mode->Mode;
}

uint32_t get_gfx_max_mode() {
    efi_graphics_output_protocol* gop = fb_get_gop();
    return gop->Mode->MaxMode;
}

uint32_t get_gfx_hres() {
    efi_graphics_output_protocol* gop = fb_get_gop();
    efi_graphics_output_mode_information* mode_info;
    size_t info_size = 0;
    efi_status status = gop->QueryMode(gop, gop->Mode->Mode, &info_size, &mode_info);
    if (EFI_ERROR(status)) {
        return 0;
    }
    return mode_info->HorizontalResolution;
}

uint32_t get_gfx_vres() {
    efi_graphics_output_protocol* gop = fb_get_gop();
    efi_graphics_output_mode_information* mode_info;
    size_t info_size = 0;
    efi_status status = gop->QueryMode(gop, gop->Mode->Mode, &info_size, &mode_info);
    if (EFI_ERROR(status)) {
        return 0;
    }
    return mode_info->VerticalResolution;
}

void set_gfx_mode(uint32_t mode) {
    efi_graphics_output_protocol* gop = fb_get_gop();
    if (mode >= gop->Mode->MaxMode) {
        printf("invalid framebuffer mode: %u\n", mode);
        return;
    }
    gop->SetMode(gop, mode);
    gBS->Stall(1000);
    gSys->ConOut->ClearScreen(gSys->ConOut);
}

void set_gfx_mode_from_cmdline(char* fbres) {
    if (!fbres) return;
    efi_graphics_output_protocol* gop = fb_get_gop();

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
            set_gfx_mode(i);
            return;
        }
    }
    printf("Could not find framebuffer mode %ux%u; using default mode = %ux%u\n",
            hres, vres, gop->Mode->Info->HorizontalResolution, gop->Mode->Info->VerticalResolution);
    gBS->Stall(5000000);
}

void print_fb_modes() {
    efi_graphics_output_protocol* gop = fb_get_gop();
    uint32_t max_mode = gop->Mode->MaxMode;
    uint32_t cur_mode = gop->Mode->Mode;
    for (int i = 0; i < max_mode; i++) {
        efi_graphics_output_mode_information* mode_info;
        size_t info_size = 0;
        efi_status status = gop->QueryMode(gop, i, &info_size, &mode_info);
        if (EFI_ERROR(status)) continue;
        printf(" (%u) %u x %u%s\n", i, mode_info->HorizontalResolution,
                mode_info->VerticalResolution, i == cur_mode ? " (current)" : "");
    }
}

void draw_logo() {
    efi_graphics_output_protocol* gop = fb_get_gop();

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
