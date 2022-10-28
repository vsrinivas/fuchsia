// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "osboot.h"

#include <bootbyte.h>
#include <cmdline.h>
#include <device_id.h>
#include <fastboot.h>
#include <framebuffer.h>
#include <inet6.h>
#include <inttypes.h>
#include <lib/netboot/netboot.h>
#include <limits.h>
#include <log.h>
#include <netifc.h>
#include <stdio.h>
#include <string.h>
#include <utf_conversion.h>
#include <xefi.h>
#include <zircon/compiler.h>
#include <zircon/hw/gpt.h>

#include <efi/boot-services.h>
#include <efi/protocol/device-path.h>
#include <efi/protocol/graphics-output.h>
#include <efi/protocol/simple-text-input.h>
#include <efi/system-table.h>

#include "abr.h"
#include "avb.h"
#include "diskio.h"
#include "mdns.h"
#include "netboot.h"
#include "variable.h"

#define DEFAULT_TIMEOUT 10

#define KBUFSIZE (32 * 1024 * 1024)
#define RBUFSIZE (512 * 1024 * 1024)

static const char16_t kDfv2VariableName[] = L"use_dfv2";

static nbfile nbzbi;
static nbfile nbcmdline;

static char cmdbuf[CMDLINE_MAX];
void print_cmdline(void) {
  cmdline_to_string(cmdbuf, sizeof(cmdbuf));
  LOG("cmdline: %s", cmdbuf);
}

nbfile* netboot_get_buffer(const char* name, size_t size) {
  if (!strcmp(name, NB_KERNEL_FILENAME)) {
    return &nbzbi;
  }
  if (!strcmp(name, NB_CMDLINE_FILENAME)) {
    return &nbcmdline;
  }
  return NULL;
}

// Wait for a keypress from a set of valid keys. If 0 < timeout_s < INT_MAX, the
// first key in the set of valid keys will be returned after timeout_s seconds
// if no other valid key is pressed.
char key_prompt(const char* valid_keys, int timeout_s) {
  if (strlen(valid_keys) < 1)
    return 0;
  if (timeout_s <= 0)
    return valid_keys[0];

  efi_status status;
  efi_event timer_event = NULL;
  if (timeout_s < INT_MAX) {
    status = gBS->CreateEvent(EVT_TIMER, 0, NULL, NULL, &timer_event);
    if (status != EFI_SUCCESS) {
      ELOG_S(status, "could not create event timer");
      return 0;
    }

    status = gBS->SetTimer(timer_event, TimerPeriodic, 10000000);
    if (status != EFI_SUCCESS) {
      ELOG_S(status, "could not set timer");
      return 0;
    }
  }

  bool cur_vis = gConOut->Mode->CursorVisible;
  int32_t col = gConOut->Mode->CursorColumn;
  int32_t row = gConOut->Mode->CursorRow;
  gConOut->EnableCursor(gConOut, false);

  char pressed = 0;
  if (timeout_s < INT_MAX) {
    LOG("Auto-boot in %ds", timeout_s);
  }
  do {
    int key;
    if (timeout_s == INT_MAX) {
      key = xefi_getc(-1);
    } else {
      key = xefi_getc(0);
    }

    if (key > 0) {
      char* which_key = strchr(valid_keys, key);
      if (which_key) {
        pressed = *which_key;
        break;
      }
    }

    if (timer_event != NULL && gBS->CheckEvent(timer_event) == EFI_SUCCESS) {
      timeout_s--;
      gConOut->SetCursorPosition(gConOut, col, row);
      LOG("Auto-boot in %ds", timeout_s);
    }
  } while (timeout_s);

  if (timer_event != NULL) {
    gBS->CloseEvent(timer_event);
  }
  gConOut->EnableCursor(gConOut, cur_vis);
  if (timeout_s > 0 && pressed) {
    return pressed;
  }

  // Default to first key in list
  return valid_keys[0];
}

// Wait for the user to type "yes". Returns true if they did, false if they didn't.
static bool confirm(void) {
  const char* expected = "yes";
  for (size_t i = 0; i < strlen(expected); i++) {
    int key = xefi_getc(15000);
    if (key < 0) {
      printf("xefi_getc failed, aborting\n");
      return false;
    }

    printf("%c", key);
    if (key != expected[i]) {
      return false;
    }
  }

  return true;
}

void list_abr_info(void) {
  for (uint32_t i = 0; i <= kAbrSlotIndexR; i++) {
    AbrSlotInfo info;
    AbrResult result;
    result = zircon_abr_get_slot_info(i, &info);
    if (result != kAbrResultOk) {
      ELOG("Failed to get zircon%s slot info: %d", AbrGetSlotSuffix(i), result);
      return;
    }
    LOG("Slot zircon%s : Bootable? %d, Successful boot? %d, Active? %d, Retry# %d",
        AbrGetSlotSuffix(i), info.is_bootable, info.is_marked_successful, info.is_active,
        info.num_tries_remaining);
  }
}

void do_select_fb(void) {
  uint32_t cur_mode = get_gfx_mode();
  uint32_t max_mode = get_gfx_max_mode();
  while (true) {
    printf("\n");
    print_fb_modes();
    printf("Choose a framebuffer mode or press (b) to return to the menu\n");
    char key = key_prompt("b0123456789", INT_MAX);
    if (key == 'b')
      break;
    if ((uint32_t)(key - '0') >= max_mode) {
      printf("invalid mode: %c\n", key);
      continue;
    }
    set_gfx_mode(key - '0');
    printf("Use \"bootloader.fbres=%ux%u\" to use this resolution by default\n", get_gfx_hres(),
           get_gfx_vres());
    printf("Press space to accept or (r) to choose again ...");
    key = key_prompt("r ", 5);
    if (key == ' ') {
      return;
    }
    set_gfx_mode(cur_mode);
  }
}

void do_fastboot(efi_handle img, efi_system_table* sys, uint32_t namegen) {
  LOG("entering fastboot mode");
  fb_bootimg_t bootimg;
  mdns_start(namegen);
  fb_poll_next_action action = POLL;
  while (action == POLL) {
    mdns_poll();
    action = fb_poll(&bootimg);
  }
  switch (action) {
    case BOOT_FROM_RAM:
      mdns_stop();
      zbi_boot(img, sys, bootimg.kernel_start, bootimg.kernel_size);
      break;
    case CONTINUE_BOOT:
    case POLL:
      break;
    case REBOOT: {
      efi_status status = gSys->RuntimeServices->ResetSystem(EfiResetCold, EFI_SUCCESS, 0, NULL);
      if (status != EFI_SUCCESS) {
        ELOG_S(status, "Failed to reboot");
      }
    }
  }
  mdns_stop();
}

void do_bootmenu(bool have_fb) {
  const char* menukeys;
  if (have_fb) {
    menukeys = "rfax";
  } else {
    menukeys = "rax";
  }

  while (true) {
    printf("  BOOT MENU  \n");
    printf("  ---------  \n");
    if (have_fb)
      printf("  (f) list framebuffer modes\n");
    printf("  (a) List abr info\n");
    printf("  (r) reset\n");
    printf("  (x) exit menu\n");
    printf("\n");
    char key = key_prompt(menukeys, INT_MAX);
    switch (key) {
      case 'f': {
        do_select_fb();
        break;
      }
      case 'a':
        list_abr_info();
        break;
      case 'r':
        gSys->RuntimeServices->ResetSystem(EfiResetCold, EFI_SUCCESS, 0, NULL);
        break;
      case 'x':
      default:
        return;
    }
  }
}

static char netboot_cmdline[CMDLINE_MAX];
void do_netboot(void) {
  efi_physical_addr mem = 0xFFFFFFFF;
  if (gBS->AllocatePages(AllocateMaxAddress, EfiLoaderData, KBUFSIZE / 4096, &mem)) {
    ELOG("Failed to allocate network io buffer");
    return;
  }
  nbzbi.data = (void*)mem;
  nbzbi.size = KBUFSIZE;

  nbcmdline.data = (void*)netboot_cmdline;
  nbcmdline.size = sizeof(netboot_cmdline);
  nbcmdline.offset = 0;

  LOG("NetBoot server started");
  efi_tpl prev_tpl = gBS->RaiseTPL(TPL_NOTIFY);
  while (true) {
    int n = netboot_poll();
    if (n < 1) {
      continue;
    }
    if (nbzbi.offset < 32768) {
      // too small to be a kernel
      continue;
    }
    uint8_t* x = nbzbi.data;
    if ((x[0] == 'M') && (x[1] == 'Z') && (x[0x80] == 'P') && (x[0x81] == 'E')) {
      size_t exitdatasize;
      efi_status r;
      efi_handle h;

      efi_device_path_hw_memmap mempath[2] = {
          {
              .Header =
                  {
                      .Type = DEVICE_PATH_HARDWARE,
                      .SubType = DEVICE_PATH_HW_MEMMAP,
                      .Length =
                          {
                              (uint8_t)(sizeof(efi_device_path_hw_memmap) & 0xff),
                              (uint8_t)((sizeof(efi_device_path_hw_memmap) >> 8) & 0xff),
                          },
                  },
              .MemoryType = EfiLoaderData,
              .StartAddress = (efi_physical_addr)nbzbi.data,
              .EndAddress = (efi_physical_addr)(nbzbi.data + nbzbi.offset),
          },
          {
              .Header =
                  {
                      .Type = DEVICE_PATH_END,
                      .SubType = DEVICE_PATH_ENTIRE_END,
                      .Length =
                          {
                              (uint8_t)(sizeof(efi_device_path_protocol) & 0xff),
                              (uint8_t)((sizeof(efi_device_path_protocol) >> 8) & 0xff),
                          },
                  },
          },
      };

      LOG("Attempting to run EFI binary");
      r = gBS->LoadImage(false, gImg, (efi_device_path_protocol*)mempath, (void*)nbzbi.data,
                         nbzbi.offset, &h);
      if (EFI_ERROR(r)) {
        ELOG_S(r, "LoadImage failed");
        continue;
      }
      r = gBS->StartImage(h, &exitdatasize, NULL);
      if (EFI_ERROR(r)) {
        ELOG_S(r, "StartImage failed");
        continue;
      }
      LOG("NetBoot server resuming");
      continue;
    }

    // make sure network traffic is not in flight, etc
    netboot_close();

    // Restore the TPL before booting the kernel, or failing to netboot
    gBS->RestoreTPL(prev_tpl);

    cmdline_append((void*)nbcmdline.data, nbcmdline.offset);
    print_cmdline();

    const char* fbres = cmdline_get("bootloader.fbres", NULL);
    if (fbres) {
      set_gfx_mode_from_cmdline(fbres);
    }

    zbi_boot(gImg, gSys, (void*)nbzbi.data, nbzbi.offset);
    break;
  }
}

// Runs the top-level boot menu.
//
// Args:
//   have_network: true if we have a working network interface.
//   have_fb: true if we have a framebuffer.
//   use_dfv2: updated if user toggles DFv2.
//
// Returns the user's selection.
static BootAction main_boot_menu(bool have_network, bool have_fb, bool* use_dfv2_p) {
  int timeout_s = cmdline_get_uint32("bootloader.timeout", DEFAULT_TIMEOUT);
  bool use_dfv2 = false;
  if (use_dfv2_p != NULL) {
    use_dfv2 = *use_dfv2_p;
  }

  while (1) {
#define VALID_KEYS_COMMON "\r\nbf1m2rzd"
    const char* valid_keys = VALID_KEYS_COMMON;
    printf(
        "\n"
        "Boot options:\n"
        "  <enter> to continue default boot\n"
        "  b) boot menu\n"
        "  f) fastboot\n"
        "  1) set A slot active and boot (alternate: m)\n"
        "  2) set B slot active and boot\n"
        "  r) one-time boot R slot (alternate: z)\n"
        "  d) %sable DFv2\n",
        (use_dfv2 ? "dis" : "en"));
    if (have_network) {
      valid_keys = VALID_KEYS_COMMON "n";
      printf("  n) network boot\n");
    }
#undef VALID_KEYS_COMMON

    char key = key_prompt(valid_keys, timeout_s);
    printf("\n\n");

    switch (key) {
      case '\r':
      case '\n':
        // <enter> or timeout, use the default boot behavior.
        return kBootActionDefault;
      case 'b':
        // Run the sub-menu then repeat this top-level menu.
        do_bootmenu(have_fb);
        break;
      case 'n':
        return kBootActionNetboot;
      case 'f':
        return kBootActionFastboot;
      case '1':
      case 'm':
        return kBootActionSlotA;
      case '2':
        return kBootActionSlotB;
      case 'r':
      case 'z':
        return kBootActionSlotR;
      case 'd':
        printf("Type yes to confirm %sabling DFv2: ", (use_dfv2 ? "dis" : "en"));
        bool confirmed = confirm();
        printf("\n");
        if (!confirmed) {
          printf("Aborting, DFv2 still %sabled.\n", (use_dfv2 ? "en" : "dis"));
          continue;
        }
        use_dfv2 = !use_dfv2;
        if (use_dfv2_p) {
          *use_dfv2_p = use_dfv2;
        }
        break;
    }
  }
}

BootAction get_boot_action(efi_runtime_services* runtime, bool have_network, bool have_fb,
                           bool* is_dfv2) {
  // 1. Bootbyte.
  uint8_t bootbyte = EFI_BOOT_DEFAULT;
  efi_status status = get_bootbyte(runtime, &bootbyte);
  if (status != EFI_SUCCESS) {
    // We only log an error if we get something other than EFI_NOT_FOUND,
    // as the call could return not found if the variable hasn't been set.
    if (status != EFI_NOT_FOUND) {
      ELOG("failed to retrieve bootbyte: %zu. Assuming normal boot", status);
    }
  }
  // Set the reboot reason to default so future boots proceed normally.
  status = set_bootbyte(runtime, EFI_BOOT_DEFAULT);
  if (status != EFI_SUCCESS) {
    WLOG("failed to clear bootbyte: %zu", status);
  }
  if (bootbyte == EFI_BOOT_RECOVERY) {
    return kBootActionSlotR;
  } else if (bootbyte == EFI_BOOT_BOOTLOADER) {
    return kBootActionFastboot;
  }

  // 2. Boot menu.
  BootAction boot_action = main_boot_menu(have_network, have_fb, is_dfv2);

  // 3. Commandline, options are "local", "zedboot", "fastboot", or "network".
  if (boot_action == kBootActionDefault) {
    // If no commandline, default to network (TODO: Is this still needed?).
    const char* defboot = cmdline_get("bootloader.default", "network");
    if (strcmp(defboot, "local") == 0) {
      return boot_action;
    } else if (strcmp(defboot, "zedboot") == 0) {
      return kBootActionSlotR;
    } else if (strcmp(defboot, "fastboot") == 0) {
      return kBootActionFastboot;
    } else if (strcmp(defboot, "network") == 0) {
      if (have_network) {
        return kBootActionNetboot;
      } else {
        LOG("No network, skipping netboot and booting from disk");
        return boot_action;
      }
    } else {
      WLOG("Ignoring unknown bootloader.default: '%s'", defboot);
    }
  }

  return boot_action;
}

size_t kernel_zone_size;
efi_physical_addr kernel_zone_base;

efi_status efi_main(efi_handle img, efi_system_table* sys) {
  xefi_init(img, sys);
  gConOut->ClearScreen(gConOut);

  printf("Welcome to GigaBoot 20X6!\n");
  printf("gSys %p gImg %p gBS %p gConOut %p\n", gSys, gImg, gBS, gConOut);

  uint64_t mmio;
  if (xefi_find_pci_mmio(gBS, 0x0C, 0x03, 0x30, &mmio) == EFI_SUCCESS) {
    char tmp[32];
    sprintf(tmp, "%#" PRIx64, mmio);
    cmdline_set("xdc.mmio", tmp);
  }

  // Prepend any EFI app command line arguments
  cmdline_append_load_options();

  // Load the cmdline
  size_t csz = 0;
  char* cmdline_file = xefi_load_file(L"cmdline", &csz, 0);
  if (cmdline_file) {
    cmdline_append(cmdline_file, csz);
  }

  uint32_t enable_serial = cmdline_get_uint32("bootloader.serial", 0);
  if (!enable_serial) {
    // TODO(https://fxbug.dev/72512): Remove when GCE handles serial i/o.
    gSerial = NULL;
  }

  efi_graphics_output_protocol* gop;
  efi_status status = gBS->LocateProtocol(&GraphicsOutputProtocol, NULL, (void**)&gop);
  bool have_fb = !EFI_ERROR(status);

  if (have_fb) {
    const char* fbres = cmdline_get("bootloader.fbres", NULL);
    if (fbres) {
      set_gfx_mode_from_cmdline(fbres);
    }
    draw_logo();
  }

  int32_t prev_attr = gConOut->Mode->Attribute;
  gConOut->SetAttribute(gConOut, EFI_LIGHTZIRCON | EFI_BACKGROUND_BLACK);
  draw_version(BOOTLOADER_VERSION);
  gConOut->SetAttribute(gConOut, prev_attr);

  if (have_fb) {
    LOG("Framebuffer base is at %" PRIx64, gop->Mode->FrameBufferBase);
  }

#if __x86_64__
  // Set aside space for the kernel down at the 1MB mark up front
  // to avoid other allocations getting in the way.
  // The kernel itself is about 1MB, but we leave generous space
  // for its BSS afterwards.
  //
  // Previously we requested 32MB but that caused issues. When the kernel
  // becomes relocatable this won't be an problem. See fxbug.dev/32223.
  kernel_zone_base = 0x100000;
  kernel_zone_size = 8 * 1024 * 1024;

  efi_allocate_type alloc_type = AllocateAddress;
#else
  // arm can allocate anywhere in physical memory
  kernel_zone_base = 0;
  kernel_zone_size = 16 * 1024 * 1024;
  efi_allocate_type alloc_type = AllocateAnyPages;
#endif  // !__x86_64__

  if (gBS->AllocatePages(alloc_type, EfiLoaderData, BYTES_TO_PAGES(kernel_zone_size),
                         &kernel_zone_base)) {
    ELOG("boot: cannot obtain %zu bytes for kernel @ %p", kernel_zone_size,
         (void*)kernel_zone_base);
    kernel_zone_size = 0;
  }
  // HACK: Try again with a smaller size - certain platforms (ex: GCE) are unable
  // to support a large fixed allocation at 0x100000.
  if (kernel_zone_size == 0) {
    kernel_zone_size = 3 * 1024 * 1024;
    efi_status status = gBS->AllocatePages(alloc_type, EfiLoaderData,
                                           BYTES_TO_PAGES(kernel_zone_size), &kernel_zone_base);
    if (status) {
      ELOG("boot: cannot obtain %zu bytes for kernel @ %p", kernel_zone_size,
           (void*)kernel_zone_base);
      kernel_zone_size = 0;
    }
  }

#if __aarch64__
  // align the buffer on at least a 64k boundary
  efi_physical_addr prev_base = kernel_zone_base;
  kernel_zone_base = ROUNDUP(kernel_zone_base, 64 * 1024);
  kernel_zone_size -= kernel_zone_base - prev_base;
#endif
  LOG("Kernel space reserved at %#" PRIx64 ", length %#zx\n", kernel_zone_base, kernel_zone_size);

  const char* nodename = cmdline_get("zircon.nodename", "");
  uint32_t namegen = cmdline_get_uint32("zircon.namegen", 1);

  // See if there's a network interface
  bool have_network = netboot_init(nodename, namegen) == 0;
  if (have_network) {
    if (have_fb) {
      draw_nodename(netboot_nodename());
    } else {
      LOG("Nodename: %s", netboot_nodename());
    }
    // If nodename was set through cmdline earlier in the code path then
    // netboot_nodename will return that same value, otherwise it will
    // return the generated value in which case it needs to be added to
    // the command line arguments.
    if (nodename[0] == 0) {
      cmdline_set("zircon.nodename", netboot_nodename());
    }
  }

  printf("\n\n");
  print_cmdline();

  // TODO(jonmayo): loading these images before making a decision is very wasteful.

  size_t zedboot_size = 0;
  void* zedboot_kernel = NULL;
  size_t ksz = 0;
  void* kernel = NULL;
  size_t ksz_b = 0;
  void* kernel_b = NULL;

  struct {
    const char16_t* wfilename;
    const char* filename;
    uint8_t guid_value[GPT_GUID_LEN];
    const char* guid_name;
    void** kernel;
    size_t* size;
  } boot_list[] = {
      // ZIRCON-A with legacy fallback filename on EFI partition
      {L"zircon.bin", "zircon.bin", GUID_ZIRCON_A_VALUE, GUID_ZIRCON_A_NAME, &kernel, &ksz},
      // no filename fallback for ZIRCON-B
      {NULL, NULL, GUID_ZIRCON_B_VALUE, GUID_ZIRCON_B_NAME, &kernel_b, &ksz_b},
      // Recovery / ZIRCON-R
      {L"zedboot.bin", "zedboot.bin", GUID_ZIRCON_R_VALUE, GUID_ZIRCON_R_NAME, &zedboot_kernel,
       &zedboot_size},
  };
  unsigned i;

  // Check for command-line overrides for files
  const char* zircon_a_filename = cmdline_get("bootloader.zircon-a", NULL);
  if (zircon_a_filename != NULL) {
    static uint16_t zircon_a_wfilename[128];
    size_t wfilename_converted_size = sizeof(zircon_a_wfilename);
    if (utf8_to_utf16((const uint8_t*)zircon_a_filename, strlen(zircon_a_filename) + 1,
                      zircon_a_wfilename, &wfilename_converted_size) == ZX_OK) {
      if (wfilename_converted_size >= sizeof(zircon_a_wfilename)) {
        WLOG("bootloader.zircon-a string truncated");
        wfilename_converted_size = sizeof(zircon_a_wfilename) - sizeof(uint16_t);
      }
      zircon_a_wfilename[wfilename_converted_size / sizeof(uint16_t)] = 0;
      boot_list[0].wfilename = zircon_a_wfilename;
      boot_list[0].filename = zircon_a_filename;
      LOG("Using zircon-a=%s", zircon_a_filename);
    }
  }
  const char* zircon_b_filename = cmdline_get("bootloader.zircon-b", NULL);
  if (zircon_b_filename != NULL) {
    static uint16_t zircon_b_wfilename[128];
    size_t wfilename_converted_size = sizeof(zircon_b_wfilename);
    if (utf8_to_utf16((const uint8_t*)zircon_b_filename, strlen(zircon_b_filename) + 1,
                      zircon_b_wfilename, &wfilename_converted_size) == ZX_OK) {
      if (wfilename_converted_size >= sizeof(zircon_b_wfilename)) {
        WLOG("bootloader.zircon-b string truncated");
        wfilename_converted_size = sizeof(zircon_b_wfilename) - sizeof(uint16_t);
      }
      zircon_b_wfilename[wfilename_converted_size / sizeof(uint16_t)] = 0;
      boot_list[1].wfilename = zircon_b_wfilename;
      boot_list[1].filename = zircon_b_filename;
      LOG("Using zircon-b=%s", zircon_b_filename);
    }
  }
  const char* zircon_r_filename = cmdline_get("bootloader.zircon-r", NULL);
  if (zircon_r_filename != NULL) {
    static uint16_t zircon_r_wfilename[128];
    size_t wfilename_converted_size = sizeof(zircon_r_wfilename);
    if (utf8_to_utf16((const uint8_t*)zircon_r_filename, strlen(zircon_r_filename) + 1,
                      zircon_r_wfilename, &wfilename_converted_size) == ZX_OK) {
      if (wfilename_converted_size >= sizeof(zircon_r_wfilename)) {
        WLOG("bootloader.zircon-r string truncated");
        wfilename_converted_size = sizeof(zircon_r_wfilename) - sizeof(uint16_t);
      }
      zircon_r_wfilename[wfilename_converted_size / sizeof(uint16_t)] = 0;
      boot_list[2].wfilename = zircon_r_wfilename;
      boot_list[2].filename = zircon_r_filename;
      LOG("Using zircon-r=%s", zircon_r_filename);
    }
  }

  // Look for ZIRCON-A/B/R partitions
  for (i = 0; i < sizeof(boot_list) / sizeof(*boot_list); i++) {
    *boot_list[i].kernel = image_load_from_disk(img, sys, EXTRA_ZBI_ITEM_SPACE, boot_list[i].size,
                                                boot_list[i].guid_value, boot_list[i].guid_name);

    if (*boot_list[i].kernel != NULL) {
      LOG("zircon image loaded from zircon partition %s", boot_list[i].guid_name);
    } else if (boot_list[i].wfilename != NULL) {
      *boot_list[i].kernel = xefi_load_file(boot_list[i].wfilename, boot_list[i].size, 0);
      if (image_is_valid(*boot_list[i].kernel, *boot_list[i].size)) {
        LOG("%s is a valid image", boot_list[i].filename);
      } else {
        *boot_list[i].kernel = NULL;
        *boot_list[i].size = 0;
        LOG("%s is not a valid image", boot_list[i].filename);
      }
    }
  }

  if (!have_network && zedboot_kernel == NULL && kernel == NULL && kernel_b == NULL) {
    ELOG("No valid kernel image found to load. Abort.");
    xefi_getc(-1);
    return EFI_SUCCESS;
  }

  // Disable WDT
  // The second parameter can be any value outside of the range [0,0xffff]
  gBS->SetWatchdogTimer(0, 0x10000, 0, NULL);

  bool force_recovery = false;
  bool use_dfv2 = false;

  status = get_bool(sys->RuntimeServices, (char16_t*)kDfv2VariableName, &use_dfv2);
  if (status != EFI_SUCCESS) {
    LOG("Failed to get use_dfv2: %zu. Assuming use_dfv2=false", status);
  }
  bool new_dfv2_state = use_dfv2;

  BootAction boot_action =
      get_boot_action(sys->RuntimeServices, have_network, have_fb, &new_dfv2_state);
  if (new_dfv2_state != use_dfv2) {
    status = set_bool(sys->RuntimeServices, (char16_t*)kDfv2VariableName, new_dfv2_state);
    if (status != EFI_SUCCESS) {
      LOG("Failed to store use_dfv2: %zu. It will not be persisted.", status);
    }
  }

  if (new_dfv2_state) {
    LOG("Booting with DFv2 enabled.");
    cmdline_append(DFV2_CMDLINE, sizeof(DFV2_CMDLINE));
  }
  switch (boot_action) {
    case kBootActionDefault:
      break;
    case kBootActionFastboot:
      // do_fastboot() only returns on `fastboot continue`, in which case we
      // continue to boot from disk.
      do_fastboot(img, sys, namegen);
      break;
    case kBootActionNetboot:
      // do_netboot() only returns on error.
      do_netboot();
      ELOG("netboot failure");
      xefi_getc(-1);
      return EFI_SUCCESS;
    case kBootActionSlotA:
      zircon_abr_set_slot_active(kAbrSlotIndexA);
      break;
    case kBootActionSlotB:
      zircon_abr_set_slot_active(kAbrSlotIndexB);
      break;
    case kBootActionSlotR:
      // We could use zircon_abr_set_oneshot_recovery() here but there's no
      // need to write to disk when we can just track it locally.
      force_recovery = true;
      break;
  }

  // If we got here, boot from disk according to A/B/R metadata.
  // Consider switching over to using the zircon_boot library which has a lot
  // of this logic built-in.
  void* zbi = NULL;
  size_t zbi_size = 0;
  const char* slot_string = NULL;
  AbrSlotIndex slot;
  while (1) {
    slot = force_recovery ? kAbrSlotIndexR : zircon_abr_get_boot_slot(true);
    switch (slot) {
      case kAbrSlotIndexA:
        zbi = kernel;
        zbi_size = ksz;
        slot_string = "-a";
        break;
      case kAbrSlotIndexB:
        zbi = kernel_b;
        zbi_size = ksz_b;
        slot_string = "-b";
        break;
      case kAbrSlotIndexR:
        zbi = zedboot_kernel;
        zbi_size = zedboot_size;
        slot_string = "-r";
        break;
    }

    // No verified boot yet, if we have a non-NULL ZBI we assume it's good.
    if (zbi != NULL) {
      LOG("Booting slot %d", slot);
      break;
    }
    LOG("Failed to find a kernel in slot %d", slot);

    // R is always the last slot to try, if we got here there's nothing else
    // we can do.
    if (slot == kAbrSlotIndexR) {
      ELOG("no valid kernel was found");
      break;
    }

    // Move to the next slot since we don't have a kernel in this one.
    AbrResult result = zircon_abr_mark_slot_unbootable(slot);
    if (result != kAbrResultOk) {
      ELOG("failed to mark slot %d unbootable (%d)", slot, result);
      break;
    }
  }

  if (zbi != NULL) {
    // Only set these flags when not booting zedboot. See http://fxbug.dev/72713
    // for more information.
    if (slot != kAbrSlotIndexR && is_booting_from_usb(img, sys)) {
      LOG("booting from usb");
      // TODO(fxbug.dev/44586): remove devmgr.bind-eager once better driver
      // prioritisation exists.
      static const char* usb_boot_args =
          "boot.usb=true devmgr.bind-eager=fuchsia-boot:///#meta/usb-composite.cm";
      cmdline_append(usb_boot_args, strlen(usb_boot_args));
    }

    zircon_abr_update_boot_slot_metadata();
    append_avb_zbi_items(img, sys, zbi, zbi_size, slot_string);
    zbi_boot(img, sys, zbi, zbi_size);
  }

  // We only get here if we ran out of slots to try or zbi_boot() failed.
  ELOG("failed to boot from disk");
  xefi_getc(-1);
  return EFI_SUCCESS;
}
