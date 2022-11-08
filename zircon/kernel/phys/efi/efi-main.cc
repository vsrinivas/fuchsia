// Copyright 2022 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/boot-options/boot-options.h>
#include <stdio.h>

#include <efi/protocol/loaded-image.h>
#include <efi/protocol/shell-parameters.h>
#include <efi/runtime-services.h>
#include <efi/types.h>
#include <fbl/alloc_checker.h>
#include <ktl/move.h>
#include <ktl/span.h>
#include <ktl/string_view.h>
#include <ktl/unique_ptr.h>
#include <phys/efi/main.h>
#include <phys/efi/protocol.h>
#include <phys/main.h>
#include <phys/stdio.h>
#include <phys/symbolize.h>

#include "src/lib/utf_conversion/utf_conversion.h"

#include <ktl/enforce.h>

arch::EarlyTicks gEfiEntryTicks;
efi_handle gEfiImageHandle;
efi_loaded_image_protocol* gEfiLoadedImage;
efi_system_table* gEfiSystemTable;

template <>
constexpr const efi_guid& kEfiProtocolGuid<efi_loaded_image_protocol> = LoadedImageProtocol;

template <>
constexpr const efi_guid& kEfiProtocolGuid<efi_shell_parameters_protocol> = ShellParametersProtocol;

namespace {

efi_status CallMain(ktl::span<char*> args) {
  if (args.empty()) {
    return main(0, (char*[]){nullptr});
  }
  return main(static_cast<int>(args.size()), args.data());
}

[[noreturn]] void EfiExit(efi_status status) {
  gEfiSystemTable->BootServices->Exit(gEfiImageHandle, status, 0, nullptr);
  __builtin_trap();
}

ktl::span<char*> ConvertUtf16Args(ktl::span<char16_t*> in) {
  if (in.empty()) {
    return {};
  }

  ktl::unique_ptr<char*[]> argv;
  {
    fbl::AllocChecker ac;
    argv.reset(new (&ac) char*[in.size() + 1]);
    if (!ac.check()) {
      printf("%s: Cannot allocate memory for %#zx argument pointers!\n", ProgramName(),
             in.size() + 1);
      return {};
    }
  }

  for (size_t i = 0; i < in.size(); ++i) {
    ktl::basic_string_view<char16_t> utf16 = in[i];
    const size_t max_utf8 = utf16.size() * 2;

    fbl::AllocChecker ac;
    ktl::unique_ptr<char[]> utf8(new (&ac) char[max_utf8 + 1]);
    if (!ac.check()) {
      printf("%s: Cannot allocate %#zx bytes for argv[%#zx]!\n", ProgramName(), max_utf8 + 1, i);
      while (i-- > 0) {
        delete argv[i];
        return {};
      }
    }

    size_t utf8_len = max_utf8;
    zx_status_t status =
        utf16_to_utf8(reinterpret_cast<const uint16_t*>(utf16.data()), utf16.size(),
                      reinterpret_cast<uint8_t*>(utf8.get()), &utf8_len);
    if (status != ZX_OK) {
      printf("%s: Error %d converting UTF16 argv[%#zx] to UTF8!\n", ProgramName(), status, i);
      argv[i] = const_cast<char*>("<invalid-UTF16>");
    } else {
      utf8[utf8_len] = '\0';
      argv[i] = utf8.release();
    }
  }

  argv[in.size()] = nullptr;
  return {argv.release(), in.size()};
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

  ktl::span<char16_t*> args_utf16;
  if (auto shell_params = EfiOpenProtocol<efi_shell_parameters_protocol>(image_handle);
      shell_params.is_ok()) {
    args_utf16 = {shell_params->Argv, shell_params->Argc};
  } else if (shell_params.error_value() != EFI_UNSUPPORTED) {
    printf("%s: EFI error %#zx getting EFI_SHELL_PARAMETERS_PROTOCOL\n", ProgramName(),
           shell_params.error_value());
  }

  if (auto image = EfiOpenProtocol<efi_loaded_image_protocol>(image_handle); image.is_ok()) {
    gEfiLoadedImage = image.value().release();
  } else {
    printf("%s: Cannot open EFI_LOADED_IMAGE_PROTOCOL: %#zx\n", ProgramName(), image.error_value());
  }

  ArchSetUp(nullptr);

  // Allocate heap copies of the argument strings converted to UTF8.
  // These will never be freed.
  ktl::span<char*> args_utf8 = ConvertUtf16Args(args_utf16);

  // Call static constructors in linked order.
  for (const InitFiniFnPtr* fn = InitBegin + 1; fn != InitEnd; ++fn) {
    (*fn)();
  }

  efi_status status = CallMain(args_utf8);

  // Call static destructors in reverse of linked order.
  const InitFiniFnPtr* fn = FiniEnd;
  while (fn-- != FiniBegin + 1) {
    (*fn)();
  }

  return status;
}

void ArchPanicReset() { EfiExit(EFI_ABORTED); }

void InitMemory(void* bootloader_data) {}

bool EfiLaunchedFromShell() {
  // A shell-launched application is spec'd to have the parameters protocol
  // present on its image handle.
  return EfiHasProtocol<efi_shell_parameters_protocol>(gEfiImageHandle);
}

void EfiReboot(bool shutdown) {
  gEfiSystemTable->RuntimeServices->ResetSystem(shutdown ? EfiResetShutdown : EfiResetCold,
                                                EFI_SUCCESS, 0, nullptr);
  __builtin_trap();
}
