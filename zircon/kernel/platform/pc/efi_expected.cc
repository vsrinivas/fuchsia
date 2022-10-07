// Copyright 2022 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <platform/efi.h>
#include <platform/pc.h>

namespace {

// Is the string `needle` in the string `haysack`?
bool StringContains(ktl::string_view haysack, ktl::string_view needle) {
  return haysack.find(needle) != ktl::string_view::npos;
}

}  // namespace

// Return true if the the platform/manufacturer provided by SMBios is expected to have
// functioning EFI support.
//
// Return false if no EFI is expected or unknown.
//
// While most x86_64 platforms _will_ have EFI support, but some platforms (in
// particular, QEMU and Chromebooks) don't have EFI support, and this is fine.
bool IsEfiExpected() {
  // All Intel NUCs are expected to have functining EFI support.
  return StringContains(manufacturer, "Intel") && StringContains(product, "NUC");
}
