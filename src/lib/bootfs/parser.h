// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_BOOTFS_PARSER_H_
#define SRC_LIB_BOOTFS_PARSER_H_

#include <lib/zx/vmo.h>
#include <stdint.h>
#include <zircon/boot/bootfs.h>
#include <zircon/compiler.h>
#include <zircon/types.h>

#include <utility>

#include <fbl/function.h>

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
  using Callback = fbl::Function<zx_status_t(const zbi_bootfs_dirent_t* entry)>;
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

  size_t MappingSize() const { return dirsize_ + sizeof(zbi_bootfs_header_t); }

  uint32_t dirsize_ = 0;
  uint64_t vmo_size_ = 0;
  void* dir_ = nullptr;
};

}  // namespace bootfs

#endif  // SRC_LIB_BOOTFS_PARSER_H_
