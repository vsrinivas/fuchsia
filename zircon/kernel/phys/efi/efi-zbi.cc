// Copyright 2022 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/zbitl/efi.h>
#include <lib/zbitl/error-stdio.h>
#include <lib/zbitl/item.h>
#include <lib/zbitl/view.h>
#include <stdio.h>

#include <ktl/move.h>
#include <ktl/string_view.h>
#include <phys/efi/file.h>
#include <phys/symbolize.h>

#include <ktl/enforce.h>

using EfiZbiView = zbitl::View<EfiFilePtr>;

int main(int argc, char** argv) {
  MainSymbolize symbolize("efi-zbi");

  ktl::string_view filename;
  if (argc == 0) {
    // When not launched from the UEFI Shell, there are no arguments so
    // a default file name must be used.  This lets the test be run by
    // putting it in \efi\boot\boot$cpu.efi and the data file in \test.zbi
    // on a bootable VFAT filesystem rather than by using the shell.
    filename = "test.zbi";
  } else if (argc == 1) {
    printf("Usage: %s PATH.zbi\n", argv[0]);
    return 1;
  } else {
    filename = argv[1];
  }

  printf("Looking for ZBI file \"%.*s\"...\n", static_cast<int>(filename.size()), filename.data());

  auto result = EfiOpenFile(filename);
  if (result.is_error()) {
    printf("Cannot open ZBI file: EFI error %#zx\n", result.error_value());
    return 1;
  }

  EfiZbiView zbi{ktl::move(result).value()};
  for (const auto& [header, payload] : zbi) {
    ktl::string_view type = zbitl::TypeName(*header);
    printf("%-15.*s extra=%#x length=%#x (%#x uncompressed)\n", static_cast<int>(type.size()),
           type.data(), header->extra, header->length, zbitl::UncompressedLength(*header));
  }
  if (auto zbi_result = zbi.take_error(); zbi_result.is_error()) {
    zbitl::PrintViewError(zbi_result.error_value());
    return 1;
  }

  return 0;
}
