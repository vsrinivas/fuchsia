// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Stub implementations for files we're not yet ready to bring into the
// host-side test build.

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <efi/system-table.h>
#include <efi/types.h>

int puts16(char16_t* str) { return 0; }

size_t image_getsize(void* image, size_t sz) { return 0; }

bool image_is_valid(void* image, size_t sz) { return false; }

// Graphics stubs. If we need to actually test these functions we can mock
// out the underlying EFI protocols, but for now this is sufficient and much
// simpler.
uint32_t get_gfx_mode(void) { return 0; }
void set_gfx_mode(uint32_t mode) {}
uint32_t get_gfx_max_mode(void) { return 0; }
uint32_t get_gfx_hres(void) { return 0; }
uint32_t get_gfx_vres(void) { return 0; }
void print_fb_modes(void) {}
void set_gfx_mode_from_cmdline(const char* fbres) {}
void draw_logo(void) {}
void draw_version(const char* version) {}
void draw_nodename(const char* text) {}

int zbi_boot(efi_handle img, efi_system_table* sys, void* image, size_t sz) { return -1; }
int netboot_init(const char* nodename, uint32_t namegen) { return -1; }
const char* netboot_nodename(void) { return NULL; }
int netboot_poll(void) { return -1; }
void netboot_close(void) {}

// netifc.c stubs.
void netifc_poll(void) {}

// bootimg.c stubs.
uint32_t validate_bootimg(void* bootimg) { return -1; }
uint32_t get_kernel_size(void* bootimg, uint32_t hdr_version) { return 0; }
uint32_t get_page_size(void* bootimg, uint32_t hdr_version) { return 0; }

// zircon.c stubs.
int zircon_stage_zbi_file(const char* name, const uint8_t* data, size_t data_len) { return -1; }
