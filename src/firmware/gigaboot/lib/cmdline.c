// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <printf.h>
#include <stdlib.h>
#include <string.h>
#include <xefi.h>

#include <efi/protocol/loaded-image.h>
#include <efi/boot-services.h>

#define VERBOSE 1

#ifndef VERBOSE
#define xprintf(...) \
  do {               \
  } while (0);
#else
#define xprintf(fmt...) printf(fmt)
#endif

// Caller frees memory.
efi_status xefi_get_load_options(size_t *load_options_size, void **load_options) {
  efi_loaded_image_protocol* loaded;
  efi_status status;

  printf("open loaded image\n");
  status = xefi_open_protocol(gImg, &LoadedImageProtocol, (void**)&loaded);
  if (status != EFI_SUCCESS) {
    xprintf("xefi_cmdline: Cannot open LoadedImageProtocol (%s)\n", xefi_strerror(status));
    goto exit0;
  }

  printf("allocate load options len %d\n", (int)loaded->LoadOptionsSize);
  status = gBS->AllocatePool(EfiLoaderData,
    loaded->LoadOptionsSize + sizeof(char16_t), load_options);

  if (status != EFI_SUCCESS) {
    xprintf("xefi_cmdline: Cannot allocate memory (%s)\n", xefi_strerror(status));
    goto exit1;
  }
  printf("copy load options\n");
  gBS->CopyMem(*load_options, loaded->LoadOptions, loaded->LoadOptionsSize);

  printf("return value\n");
  *load_options_size = loaded->LoadOptionsSize + sizeof(char16_t);

exit1:
  printf("close protocol\n");
  xefi_close_protocol(gImg, &LoadedImageProtocol);
exit0:
  return status;
}
