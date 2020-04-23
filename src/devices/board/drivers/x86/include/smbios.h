// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_BOARD_DRIVERS_X86_INCLUDE_SMBIOS_H_
#define SRC_DEVICES_BOARD_DRIVERS_X86_INCLUDE_SMBIOS_H_

#include <zircon/compiler.h>
#include <zircon/types.h>

#include <string>

class SmbiosInfo {
 public:
  zx_status_t Load();

  const std::string& board_name() const { return board_name_; }
  const std::string& vendor() const { return vendor_; }

 private:
  std::string board_name_;
  std::string vendor_;
};

// Check if we consider the given product name to be valid.
bool smbios_product_name_is_valid(const char* product_name);

#endif  // SRC_DEVICES_BOARD_DRIVERS_X86_INCLUDE_SMBIOS_H_
