// Copyright 2022 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/boot-options/boot-options.h>
#include <stdio.h>

#include <efi/protocol/loaded-image.h>
#include <efi/types.h>
#include <ktl/span.h>
#include <phys/efi/main.h>
#include <phys/main.h>
#include <phys/stdio.h>

#include <ktl/enforce.h>

arch::EarlyTicks gEfiEntryTicks;
efi_handle gEfiImageHandle;
efi_loaded_image_protocol* gEfiLoadedImage;
efi_system_table* gEfiSystemTable;

namespace {

efi_loaded_image_protocol* GetLoadedImage(efi_handle image_handle) {
  void* ptr;
  efi_status status =
      gEfiSystemTable->BootServices->HandleProtocol(image_handle, &LoadedImageProtocol, &ptr);
  if (status != EFI_SUCCESS) {
    printf("EFI: Failed to get EFI_LOADED_IMAGE_PROTOCOL: %#zx\n", status);
    return nullptr;
  }
  return static_cast<efi_loaded_image_protocol*>(ptr);
}

ktl::span<char*> GetImageArgs() {
  // TODO(mcgrathr): EFI_SHELL_PARAMETERS_PROTOCOL, convert to utf8

  if (!gEfiLoadedImage) {
    return {};
  }

  // TODO(mcgrathr): convert gEfiLoadedImage->LoadOptions into utf8 argv?
  return {};
}

efi_status CallMain() {
  ktl::span args = GetImageArgs();
  if (args.empty()) {
    return main(0, (char*[]){nullptr});
  }
  return main(static_cast<int>(args.size() - 1), args.data());
}

[[noreturn]] void EfiExit(efi_status status) {
  gEfiSystemTable->BootServices->Exit(gEfiImageHandle, status, 0, nullptr);
  __builtin_trap();
}

const BootOptions kDefaultBootOptions;

using InitFiniFnPtr = void (*)();

#pragma section(".CRT$XCA", read)
#pragma section(".CRT$XCZ", read)
[[gnu::section(".CRT$XCA")]] const InitFiniFnPtr InitBegin[1] = {};
[[gnu::section(".CRT$XCZ")]] const InitFiniFnPtr InitEnd[1] = {};

#pragma section(".CRT$XTA", read)
#pragma section(".CRT$XTZ", read)
[[gnu::section(".CRT$XTA")]] const InitFiniFnPtr FiniBegin[1] = {};
[[gnu::section(".CRT$XTZ")]] const InitFiniFnPtr FiniEnd[1] = {};

}  // namespace

efi_status EfiMain(efi_handle image_handle, efi_system_table* systab) {
  gEfiEntryTicks = arch::EarlyTicks::Get();

  gBootOptions = &kDefaultBootOptions;

  gEfiImageHandle = image_handle;
  gEfiSystemTable = systab;

  InitStdout();
  SetEfiStdout(systab);

  gEfiLoadedImage = GetLoadedImage(image_handle);

  ArchSetUp(nullptr);

  // Call static constructors in linked order.
  for (const InitFiniFnPtr* fn = InitBegin + 1; fn != InitEnd; ++fn) {
    (*fn)();
  }

  efi_status status = CallMain();

  // Call static destructors in reverse of linked order.
  const InitFiniFnPtr* fn = FiniEnd;
  while (fn-- != FiniBegin + 1) {
    (*fn)();
  }

  return status;
}

void ArchPanicReset() { EfiExit(EFI_ABORTED); }

void InitMemory(void* bootloader_data) {}
