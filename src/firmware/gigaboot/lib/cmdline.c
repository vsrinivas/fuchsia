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

  LOG("open loaded image");
  status = xefi_open_protocol(gImg, &LoadedImageProtocol, (void **)&loaded);
  if (status != EFI_SUCCESS) {
    ELOG_S(status, "xefi_cmdline: Cannot open LoadedImageProtocol");
    goto exit0;
  }

  LOG("allocate load options len %d", (int)loaded->LoadOptionsSize);
  status =
      gBS->AllocatePool(EfiLoaderData, loaded->LoadOptionsSize + sizeof(char16_t), load_options);

  if (status != EFI_SUCCESS) {
    ELOG_S(status, "xefi_cmdline: Cannot allocate memory");
    goto exit1;
  }
  LOG("copy load options");
  gBS->CopyMem(*load_options, loaded->LoadOptions, loaded->LoadOptionsSize);
  *load_options_size = loaded->LoadOptionsSize + sizeof(char16_t);

exit1:
  LOG("close protocol");
  xefi_close_protocol(gImg, &LoadedImageProtocol);
exit0:
  return status;
}
