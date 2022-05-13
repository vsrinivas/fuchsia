// Copyright 2022 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_LIB_EFI_INCLUDE_EFI_PROTOCOL_SHELL_PARAMETERS_H_
#define ZIRCON_KERNEL_LIB_EFI_INCLUDE_EFI_PROTOCOL_SHELL_PARAMETERS_H_

#include <zircon/compiler.h>

#include <efi/types.h>

__BEGIN_CDECLS

#define EFI_SHELL_PARAMETERS_PROTOCOL_GUID                                         \
  {                                                                                \
    0x752f3136, 0x4e16, 0x4fdc, { 0xa2, 0x2a, 0xe5, 0xf4, 0x68, 0x12, 0xf4, 0xca } \
  }

typedef struct {
  char16_t** Argv;
  size_t Argc;
  efi_handle StdIn;
  efi_handle StdOut;
  efi_handle StdErr;
} efi_shell_parameters_protocol;

extern const efi_guid ShellParametersProtocol;

__END_CDECLS

#endif  // ZIRCON_KERNEL_LIB_EFI_INCLUDE_EFI_PROTOCOL_SHELL_PARAMETERS_H_
