// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stdint.h>
#include <utility>

#include <fbl/function.h>
#include <lib/zx/vmo.h>
#include <zircon/boot/bootdata.h>
#include <zircon/compiler.h>
#include <zircon/types.h>

namespace bootfs {

// A parser for the bootfs format
class Parser {
 public:
  // Create an empty Parser.
  Parser() = default;
  ~Parser();

  // Initializes a bootfs file system from |vmo|.
  zx_status_t Init(zx::unowned_vmo vmo);

  // Parses the bootfs file system and calls |callback| for each |bootfs_entry_t|.
  // If a callback returns something besides ZX_OK, the iteration stops.
  using Callback = fbl::Function<zx_status_t(const bootfs_entry_t* entry)>;
  zx_status_t Parse(Callback callback);

 private:
  Parser(const Parser&) = delete;
  Parser& operator=(const Parser&) = delete;

  Parser(Parser&& other) { *this = std::move(other); }
  Parser& operator=(Parser&& other) {
    dirsize_ = other.dirsize_;
    dir_ = other.dir_;
    other.dir_ = nullptr;
    return *this;
  }

  size_t MappingSize() const { return dirsize_ + sizeof(bootfs_header_t); }

  uint32_t dirsize_ = 0;
  void* dir_ = nullptr;
};

}  // namespace bootfs
