// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <log.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <xefi.h>

#include <efi/boot-services.h>
#include <efi/protocol/loaded-image.h>

// Caller frees memory.
efi_status xefi_get_load_options(size_t *load_options_size, void **load_options) {
  efi_loaded_image_protocol *loaded;
  efi_status status;
  char16_t *load_options_ptr;

  DLOG("open loaded image");
  status = xefi_open_protocol(gImg, &LoadedImageProtocol, (void **)&loaded);
  if (status != EFI_SUCCESS) {
    ELOG_S(status, "%s: Cannot open LoadedImageProtocol", __func__);
    goto exit0;
  }

  LOG("image load options len = %u", loaded->LoadOptionsSize);
  // We request an additional char16_t, which we will set to \0, to ensure that
  // we call AllocatePool with Size > 0, since the spec is unclear about the
  // behavior of AllocatePool when requesting a size of 0.
  status =
      gBS->AllocatePool(EfiLoaderData, loaded->LoadOptionsSize + sizeof(char16_t), load_options);

  if (status != EFI_SUCCESS) {
    ELOG_S(status, "%s: failed to allocate memory", __func__);
    goto exit1;
  }

  // Ensure the extra UTF16 char at the end is zeroed.
  load_options_ptr = *load_options;
  load_options_ptr[loaded->LoadOptionsSize / sizeof(char16_t)] = 0;

  if (loaded->LoadOptionsSize) {
    DLOG("copy load options");
    gBS->CopyMem(*load_options, loaded->LoadOptions, loaded->LoadOptionsSize);
  }

  // Return the number of bytes of valid UTF-16 data (*not* including our padding
  // bytes at the end.)
  *load_options_size = loaded->LoadOptionsSize;

exit1:
  DLOG("close protocol");
  xefi_close_protocol(gImg, &LoadedImageProtocol);
exit0:
  return status;
}
