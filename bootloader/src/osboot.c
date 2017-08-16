// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include <efi/boot-services.h>
#include <efi/protocol/device-path.h>
#include <efi/protocol/graphics-output.h>
#include <efi/protocol/simple-text-input.h>
#include <efi/system-table.h>

#include <cmdline.h>
#include <device_id.h>
#include <framebuffer.h>
#include <inet6.h>
#include <xefi.h>

#include "osboot.h"

#include <magenta/boot/netboot.h>

#define DEFAULT_TIMEOUT 3

#define KBUFSIZE (32*1024*1024)
#define RBUFSIZE (512 * 1024 * 1024)


static nbfile nbkernel;
static nbfile nbramdisk;
static nbfile nbcmdline;

nbfile* netboot_get_buffer(const char* name, size_t size) {
    if (!strcmp(name, NB_KERNEL_FILENAME)) {
        return &nbkernel;
    }
    if (!strcmp(name, NB_RAMDISK_FILENAME)) {
        efi_physical_addr mem = 0xFFFFFFFF;
        size_t buf_size = size > 0 ? (size + PAGE_MASK) & ~PAGE_MASK : RBUFSIZE;

        if (nbramdisk.size > 0) {
            if (nbramdisk.size < buf_size) {
                mem = (efi_physical_addr)nbramdisk.data;
                nbramdisk.data = 0;
                if (gBS->FreePages(mem - FRONT_BYTES, (nbramdisk.size / PAGE_SIZE) + FRONT_PAGES)) {
                    printf("Could not free previous ramdisk allocation\n");
                    nbramdisk.size = 0;
                    return NULL;
                }
                nbramdisk.size = 0;
            } else {
                return &nbramdisk;
            }
        }

        printf("netboot: allocating %zu for ramdisk (requested %zu)\n", buf_size, size);
        if (gBS->AllocatePages(AllocateMaxAddress, EfiLoaderData,
                               (buf_size / PAGE_SIZE) + FRONT_PAGES, &mem)) {
            printf("Failed to allocate network io buffer\n");
            return NULL;
        }
        nbramdisk.data = (void*) (mem + FRONT_BYTES);
        nbramdisk.size = buf_size;

        return &nbramdisk;
    }
    if (!strcmp(name, NB_CMDLINE_FILENAME)) {
        return &nbcmdline;
    }
    return NULL;
}

// Wait for a keypress from a set of valid keys. If 0 < timeout_s < INT_MAX, the
// first key in the set of valid keys will be returned after timeout_s seconds
// if no other valid key is pressed.
char key_prompt(char* valid_keys, int timeout_s) {
    if (strlen(valid_keys) < 1) return 0;
    if (timeout_s <= 0) return valid_keys[0];

    efi_event TimerEvent;
    efi_event WaitList[2];

    efi_status status;
    size_t Index;
    efi_input_key key;
    memset(&key, 0, sizeof(key));

    status = gBS->CreateEvent(EVT_TIMER, 0, NULL, NULL, &TimerEvent);
    if (status != EFI_SUCCESS) {
        printf("could not create event timer: %s\n", xefi_strerror(status));
        return 0;
    }

    status = gBS->SetTimer(TimerEvent, TimerPeriodic, 10000000);
    if (status != EFI_SUCCESS) {
        printf("could not set timer: %s\n", xefi_strerror(status));
        return 0;
    }

    int wait_idx = 0;
    int key_idx = wait_idx;
    WaitList[wait_idx++] = gSys->ConIn->WaitForKey;
    int timer_idx = wait_idx;  // timer should always be last
    WaitList[wait_idx++] = TimerEvent;

    bool cur_vis = gConOut->Mode->CursorVisible;
    int32_t col = gConOut->Mode->CursorColumn;
    int32_t row = gConOut->Mode->CursorRow;
    gConOut->EnableCursor(gConOut, false);

    // TODO: better event loop
    char pressed = 0;
    if (timeout_s < INT_MAX) {
        printf("%-10d", timeout_s);
    }
    do {
        status = gBS->WaitForEvent(wait_idx, WaitList, &Index);

        // Check the timer
        if (!EFI_ERROR(status)) {
            if (Index == timer_idx) {
                if (timeout_s < INT_MAX) {
                    timeout_s--;
                    gConOut->SetCursorPosition(gConOut, col, row);
                    printf("%-10d", timeout_s);
                }
                continue;
            } else if (Index == key_idx) {
                status = gSys->ConIn->ReadKeyStroke(gSys->ConIn, &key);
                if (EFI_ERROR(status)) {
                    // clear the key and wait for another event
                    memset(&key, 0, sizeof(key));
                } else {
                    char* which_key = strchr(valid_keys, key.UnicodeChar);
                    if (which_key) {
                        pressed = *which_key;
                        break;
                    }
                }
            }
        } else {
            printf("Error waiting for event: %s\n", xefi_strerror(status));
            gConOut->EnableCursor(gConOut, cur_vis);
            return 0;
        }
    } while (timeout_s);

    gBS->CloseEvent(TimerEvent);
    gConOut->EnableCursor(gConOut, cur_vis);
    if (timeout_s > 0 && pressed) {
        return pressed;
    }

    // Default to first key in list
    return valid_keys[0];
}

void do_select_fb() {
    uint32_t cur_mode = get_gfx_mode();
    uint32_t max_mode = get_gfx_max_mode();
    while (true) {
        printf("\n");
        print_fb_modes();
        printf("Choose a framebuffer mode or press (b) to return to the menu\n");
        char key = key_prompt("b0123456789", INT_MAX);
        if (key == 'b') break;
        if (key - '0' >= max_mode) {
            printf("invalid mode: %c\n", key);
            continue;
        }
        set_gfx_mode(key - '0');
        printf("Use \"bootloader.fbres=%ux%u\" to use this resolution by default\n",
                get_gfx_hres(), get_gfx_vres());
        printf("Press space to accept or (r) to choose again ...");
        key = key_prompt("r ", 5);
        if (key == ' ') {
            return;
        }
        set_gfx_mode(cur_mode);
    }
}

void do_bootmenu(bool have_fb) {
    char* menukeys;
    if (have_fb)
        menukeys = "rfx";
    else
        menukeys = "rx";
    printf("  BOOT MENU  \n");
    printf("  ---------  \n");
    if (have_fb)
        printf("  (f) list framebuffer modes\n");
    printf("  (r) reset\n");
    printf("  (x) exit menu\n");
    printf("\n");
    char key = key_prompt(menukeys, INT_MAX);
    switch (key) {
    case 'f': {
        do_select_fb();
        break;
    }
    case 'r':
        gSys->RuntimeServices->ResetSystem(EfiResetCold, EFI_SUCCESS, 0, NULL);
        break;
    case 'x':
    default:
        break;
    }
}

static char cmdbuf[CMDLINE_MAX];
void print_cmdline(void) {
    cmdline_to_string(cmdbuf, sizeof(cmdbuf));
    printf("cmdline: %s\n", cmdbuf);
}

static char netboot_cmdline[CMDLINE_MAX];
void do_netboot() {
    efi_physical_addr mem = 0xFFFFFFFF;
    if (gBS->AllocatePages(AllocateMaxAddress, EfiLoaderData, KBUFSIZE / 4096, &mem)) {
        printf("Failed to allocate network io buffer\n");
        return;
    }
    nbkernel.data = (void*) mem;
    nbkernel.size = KBUFSIZE;

    // ramdisk is dynamically allocated now
    nbramdisk.data = 0;
    nbramdisk.size = 0;

    nbcmdline.data = (void*) netboot_cmdline;
    nbcmdline.size = sizeof(netboot_cmdline);
    nbcmdline.offset = 0;

    printf("\nNetBoot Server Started...\n\n");
    efi_tpl prev_tpl = gBS->RaiseTPL(TPL_NOTIFY);
    while (true) {
        int n = netboot_poll();
        if (n < 1) {
            continue;
        }
        if (nbkernel.offset < 32768) {
            // too small to be a kernel
            continue;
        }
        uint8_t* x = nbkernel.data;
        if ((x[0] == 'M') && (x[1] == 'Z') && (x[0x80] == 'P') && (x[0x81] == 'E')) {
            size_t exitdatasize;
            efi_status r;
            efi_handle h;

            efi_device_path_hw_memmap mempath[2] = {
                {
                    .Header = {
                        .Type = DEVICE_PATH_HARDWARE,
                        .SubType = DEVICE_PATH_HW_MEMMAP,
                        .Length = { (uint8_t)(sizeof(efi_device_path_hw_memmap) & 0xff),
                            (uint8_t)((sizeof(efi_device_path_hw_memmap) >> 8) & 0xff), },
                    },
                    .MemoryType = EfiLoaderData,
                    .StartAddress = (efi_physical_addr)nbkernel.data,
                    .EndAddress = (efi_physical_addr)(nbkernel.data + nbkernel.offset),
                },
                {
                    .Header = {
                        .Type = DEVICE_PATH_END,
                        .SubType = DEVICE_PATH_ENTIRE_END,
                        .Length = { (uint8_t)(sizeof(efi_device_path_protocol) & 0xff),
                            (uint8_t)((sizeof(efi_device_path_protocol) >> 8) & 0xff), },
                    },
                },
            };

            printf("Attempting to run EFI binary...\n");
            r = gBS->LoadImage(false, gImg, (efi_device_path_protocol*)mempath, (void*)nbkernel.data, nbkernel.offset, &h);
            if (EFI_ERROR(r)) {
                printf("LoadImage Failed (%s)\n", xefi_strerror(r));
                continue;
            }
            r = gBS->StartImage(h, &exitdatasize, NULL);
            if (EFI_ERROR(r)) {
                printf("StartImage Failed %zu\n", r);
                continue;
            }
            printf("\nNetBoot Server Resuming...\n");
            continue;
        }

        // make sure network traffic is not in flight, etc
        netboot_close();

        // Restore the TPL before booting the kernel, or failing to netboot
        gBS->RestoreTPL(prev_tpl);

        cmdline_append((void*) nbcmdline.data, nbcmdline.offset);
        print_cmdline();

        const char* fbres = cmdline_get("bootloader.fbres", NULL);
        if (fbres) {
            set_gfx_mode_from_cmdline(fbres);
        }

        // maybe it's a kernel image?
        boot_kernel(gImg, gSys, (void*) nbkernel.data, nbkernel.offset,
                    (void*) nbramdisk.data, nbramdisk.offset);
        break;
    }
}

EFIAPI efi_status efi_main(efi_handle img, efi_system_table* sys) {
    xefi_init(img, sys);
    gConOut->ClearScreen(gConOut);

    uint64_t mmio;
    if (xefi_find_pci_mmio(gBS, 0x0C, 0x03, 0x30, &mmio) == EFI_SUCCESS) {
        char tmp[32];
        sprintf(tmp, "%#" PRIx64 , mmio);
        cmdline_set("xdc.mmio", tmp);
    }

    // Load the cmdline
    size_t csz = 0;
    char* cmdline_file = xefi_load_file(L"cmdline", &csz, 0);
    if (cmdline_file) {
        cmdline_append(cmdline_file, csz);
    }

    efi_graphics_output_protocol* gop;
    efi_status status = gBS->LocateProtocol(&GraphicsOutputProtocol, NULL,
                                            (void**)&gop);
    bool have_fb = !EFI_ERROR(status);

    if (have_fb) {
        const char* fbres = cmdline_get("bootloader.fbres", NULL);
        if (fbres) {
            set_gfx_mode_from_cmdline(fbres);
        }
        draw_logo();
    }

    int32_t prev_attr = gConOut->Mode->Attribute;
    gConOut->SetAttribute(gConOut, EFI_LIGHTMAGENTA | EFI_BACKGROUND_BLACK);
    draw_version(BOOTLOADER_VERSION);
    gConOut->SetAttribute(gConOut, prev_attr);

    if (have_fb) {
        printf("Framebuffer base is at %" PRIx64 "\n\n",
               gop->Mode->FrameBufferBase);
    }

    // Default boot defaults to network
    const char* defboot = cmdline_get("bootloader.default", "network");
    const char* nodename = cmdline_get("magenta.nodename", "");

    // See if there's a network interface
    bool have_network = netboot_init(nodename) == 0;
    if (have_network) {
        if (have_fb) {
            draw_nodename(netboot_nodename());
        } else {
            printf("\nNodename: %s\n", netboot_nodename());
        }
        // If nodename was set through cmdline earlier in the code path then
        // netboot_nodename will return that same value, otherwise it will
        // return the generated value in which case it needs to be added to
        // the command line arguments.
        if (nodename[0] == 0) {
            cmdline_set("magenta.nodename", netboot_nodename());
        }
    }

    printf("\n\n");
    print_cmdline();

    // First look for a self-contained magentaboot image
    size_t ksz = 0;
    void* kernel = xefi_load_file(L"mxboot.bin", &ksz, 0);

    if (kernel) {
        mxboot(img, sys, kernel, ksz);
    }

    // Look for a kernel image on disk
    ksz = 0;
    kernel = xefi_load_file(L"magenta.bin", &ksz, 0);
    if (!have_network && kernel == NULL) {
        goto fail;
    }

    char valid_keys[4];
    memset(valid_keys, 0, sizeof(valid_keys));
    int key_idx = 0;

    // The first entry in valid_keys will be the default after the timeout.
    // Use the value of bootloader.default to determine the first entry. If
    // bootloader.default is not set, use "network".
    if (!memcmp(defboot, "local", 5)) {
        if (kernel != NULL) {
            valid_keys[key_idx++] = 'm';
        }
        if (have_network) {
            valid_keys[key_idx++] = 'n';
        }
    } else {
        if (have_network) {
            valid_keys[key_idx++] = 'n';
        }
        if (kernel != NULL) {
            valid_keys[key_idx++] = 'm';
        }
    }
    valid_keys[key_idx++] = 'b';

    // make sure we update valid_keys if we ever add new options
    if (key_idx >= sizeof(valid_keys)) goto fail;

    // Disable WDT
    // The second parameter can be any value outside of the range [0,0xffff]
    gBS->SetWatchdogTimer(0, 0x10000, 0, NULL);

    int timeout_s = cmdline_get_uint32("bootloader.timeout", DEFAULT_TIMEOUT);
    while (true) {
        printf("\nPress (b) for the boot menu");
        if (have_network) {
            printf(", ");
            if (!kernel) printf("or ");
            printf("(n) for network boot");
        }
        if (kernel) {
            printf(", ");
            printf("or (m) to boot the magenta.bin on the device");
        }
        printf(" ...");

        char key = key_prompt(valid_keys, timeout_s);
        printf("\n\n");

        switch (key) {
        case 'b':
            do_bootmenu(have_fb);
            break;
        case 'n':
            do_netboot();
            break;
        case 'm': {
            size_t rsz = 0;
            void* ramdisk = NULL;
            efi_file_protocol* ramdisk_file = xefi_open_file(L"bootdata.bin");
            const char* ramdisk_name = "bootdata.bin";
            if (ramdisk_file == NULL) {
                ramdisk_file = xefi_open_file(L"ramdisk.bin");
                ramdisk_name = "ramdisk.bin";
            }
            if (ramdisk_file) {
                printf("Loading %s...\n", ramdisk_name);
                ramdisk = xefi_read_file(ramdisk_file, &rsz, FRONT_BYTES);
                ramdisk_file->Close(ramdisk_file);
            }
            boot_kernel(gImg, gSys, kernel, ksz, ramdisk, rsz);
            break;
        }
        default:
            goto fail;
        }
    }

fail:
    printf("\nBoot Failure\n");
    xefi_wait_any_key();
    return EFI_SUCCESS;
}
