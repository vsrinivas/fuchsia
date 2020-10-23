// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/arch/x86/cpuid.h>

namespace arch {

uint8_t CpuidVersionInfo::family() const {
  if (base_family() == 0xf) {
    return (static_cast<uint8_t>(base_family() + extended_family()));
  }
  return static_cast<uint8_t>(base_family());
}

uint8_t CpuidVersionInfo::model() const {
  if (base_family() == 0x6 || base_family() == 0xf) {
    return (static_cast<uint8_t>(extended_model() << 4)) | static_cast<uint8_t>(base_model());
  }
  return static_cast<uint8_t>(base_model());
}

}  // namespace arch
