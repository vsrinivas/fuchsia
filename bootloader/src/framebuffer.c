// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <framebuffer.h>
#include <xefi.h>

#include <stdio.h>
#include <stdlib.h>
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
    if (!gop)
        return;
    if (mode >= gop->Mode->MaxMode) {
        printf("invalid framebuffer mode: %u\n", mode);
        return;
    }
    efi_status s = gop->SetMode(gop, mode);
    if (EFI_ERROR(s)) {
        printf("could not set mode: %s\n", xefi_strerror(s));
    }
    gBS->Stall(1000);
    gSys->ConOut->SetCursorPosition(gSys->ConOut, 0, 0);
}

void set_gfx_mode_from_cmdline(const char* fbres) {
    if (!fbres)
        return;
    efi_graphics_output_protocol* gop = fb_get_gop();
    if (!gop)
        return;

    uint32_t hres = 0;
    hres = atol(fbres);

    char* x = strchr(fbres, 'x');
    if (!x)
        return;
    x++;

    uint32_t vres = 0;
    vres = atol(x);
    if (!hres || !vres)
        return;

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
        if (EFI_ERROR(status))
            continue;
        printf(" (%u) %u x %u%s\n", i, mode_info->HorizontalResolution,
               mode_info->VerticalResolution, i == cur_mode ? " (current)" : "");
    }
}

#include "logo.h"

static efi_graphics_output_blt_pixel font_white = {
    .Red = 0xFF,
    .Green = 0xFF,
    .Blue = 0xFF,
};

static efi_graphics_output_blt_pixel font_black = {
    .Red = 0x0,
    .Green = 0x0,
    .Blue = 0x0,
};

static efi_graphics_output_blt_pixel font_fuchsia = {
    .Red = 0xFF,
    .Green = 0x0,
    .Blue = 0x80,
};

void draw_logo() {
    efi_graphics_output_protocol* gop = fb_get_gop();
    if (!gop)
        return;

    const uint32_t h_res = gop->Mode->Info->HorizontalResolution;
    const uint32_t v_res = gop->Mode->Info->VerticalResolution;

    // Blank the screen, removing vendor UEFI logos
    gop->Blt(gop, &font_black, EfiBltVideoFill, 0, 0, 0, 0, h_res, v_res, 0);

    efi_graphics_output_blt_pixel* tmp;
    unsigned sz = sizeof(efi_graphics_output_blt_pixel) * logo_width * logo_height;
    if (EFI_ERROR(gBS->AllocatePool(EfiLoaderData, sz, (void*)&tmp))) {
        // Draw the Fuchsia stripe on the top of the screen
        gop->Blt(gop, &font_fuchsia, EfiBltVideoFill, 0, 0, 0, 0, h_res, v_res / 100, 0);
        return;
    }

    // Un-RLE the logo
    unsigned char* iptr = logo_rle;
    efi_graphics_output_blt_pixel* optr = tmp;
    unsigned entries = sizeof(logo_rle) / 2;
    while (entries-- > 0) {
        unsigned count = *iptr++;
        unsigned alpha = *iptr++;
        efi_graphics_output_blt_pixel px = {
            .Red = (alpha * 0xFF) / 255,
            .Green = 0,
            .Blue = (alpha * 0x80) / 255,
        };
        while (count-- > 0) {
            *optr++ = px;
        }
    }

    gop->Blt(gop, tmp, EfiBltBufferToVideo, 0, 0,
             h_res - logo_width - (h_res / 75), v_res - logo_height - (v_res / 75),
             logo_width, logo_height, 0);
}

#include <magenta/font/font-9x16.h>
#include <magenta/font/font-18x32.h>

static void putchar(efi_graphics_output_protocol* gop, fb_font* font, unsigned ch, unsigned x, unsigned y, unsigned scale_x, unsigned scale_y, efi_graphics_output_blt_pixel* fg, efi_graphics_output_blt_pixel* bg) {
    const uint16_t* cdata = font->data + ch * font->height;
    unsigned fw = font->width;
    for (unsigned i = 0; i <= font->height; i++) {
        uint16_t xdata = *cdata++;
        for (unsigned j = 0; j < fw; j++) {
            gop->Blt(gop, (xdata & 1) ? fg : bg, EfiBltVideoFill, 0, 0, x + scale_x * j, y + scale_y * i, scale_x, scale_y, 0);
            xdata >>= 1;
        }
    }
}

void draw_text(const char* text, size_t length, fb_font* font, int x, int y) {
    efi_graphics_output_protocol* gop = fb_get_gop();
    efi_graphics_output_blt_pixel* fg_color = &font_white;
    if (!gop)
        return;

    if (font->color != NULL) {
        fg_color = font->color;
    }

    size_t offset = 0;
    size_t scale = 1;
    for (size_t i = 0; i < length; ++i) {
        char c = text[i];
        if (c > 127)
            continue;
        putchar(gop, font, c, x + offset, y, scale, scale, fg_color, &font_black);
        offset += font->width * scale;
    }
}

void draw_nodename(const char* nodename) {
    efi_graphics_output_protocol* gop = fb_get_gop();
    if (!gop)
        return;

    fb_font font = {
        .data = FONT18X32,
        .width = FONT18X32_WIDTH,
        .height = FONT18X32_HEIGHT,
        .color = &font_white,
    };

    const uint32_t h_res = gop->Mode->Info->HorizontalResolution;
    const uint32_t v_res = gop->Mode->Info->VerticalResolution;
    size_t length = strlen(nodename);
    draw_text(nodename, length, &font, h_res - (length + 1) * font.width, v_res / 100 + font.height);
}

void draw_version(const char* version) {
    efi_graphics_output_protocol* gop = fb_get_gop();
    if (!gop)
        return;

    const char* prefix = "GigaBoot 20X6 - Version";
    size_t prefix_len = strlen(prefix);
    size_t version_len = strlen(version);

    fb_font font = {
        .data = FONT9X16,
        .width = FONT9X16_WIDTH,
        .height = FONT9X16_HEIGHT,
        .color = &font_fuchsia,
    };

    draw_text(prefix, prefix_len, &font, 0, 0);
    draw_text(version, version_len, &font, (prefix_len + 1) * font.width, 0);
}
