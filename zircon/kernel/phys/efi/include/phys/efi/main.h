// Copyright 2022 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_PHYS_EFI_INCLUDE_PHYS_EFI_MAIN_H_
#define ZIRCON_KERNEL_PHYS_EFI_INCLUDE_PHYS_EFI_MAIN_H_

#include <lib/arch/ticks.h>

#include <efi/protocol/loaded-image.h>
#include <efi/system-table.h>
#include <efi/types.h>

// This is the entry point in the PE-COFF headers.
extern "C" efi_status EfiMain(efi_handle image, efi_system_table* systab);

void SetEfiStdout(efi_system_table* systab);

// Gives whether the current application was launched from the UEFI shell.
bool EfiLaunchedFromShell();

[[noreturn]] void EfiReboot(bool shutdown = false);

// The canonical entry point of the main program.
int main(int argc, char* argv[]);

// These are set by EfiMain before calling main.
extern arch::EarlyTicks gEfiEntryTicks;
extern efi_handle gEfiImageHandle;
extern efi_loaded_image_protocol* gEfiLoadedImage;
extern efi_system_table* gEfiSystemTable;

#endif  // ZIRCON_KERNEL_PHYS_EFI_INCLUDE_PHYS_EFI_MAIN_H_
