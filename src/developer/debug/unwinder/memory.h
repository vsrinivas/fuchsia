// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_UNWINDER_MEMORY_H_
#define SRC_DEVELOPER_DEBUG_UNWINDER_MEMORY_H_

#include <cstdint>
#include <cstring>

#include "src/developer/debug/unwinder/error.h"

namespace unwinder {

class Memory {
 public:
  virtual Error ReadBytes(uint64_t addr, uint64_t size, void* dst) = 0;

  template <class Type>
  [[nodiscard]] Error Read(uint64_t& addr, Type& res) {
    if (auto err = ReadBytes(addr, sizeof(res), &res); err.has_err()) {
      return err;
    }
    addr += sizeof(res);
    return Success();
  }

  template <class Type>
  [[nodiscard]] Error Read(const uint64_t& addr, Type& res) {
    uint64_t mut = addr;
    return Read(mut, res);
  }

  [[nodiscard]] Error ReadSLEB128(uint64_t& addr, int64_t& res);
  [[nodiscard]] Error ReadULEB128(uint64_t& addr, uint64_t& res);

  // Read the data in DWARF encoding. data_rel_base is only used in .eh_frame_hdr.
  [[nodiscard]] Error ReadEncoded(uint64_t& addr, uint64_t& res, uint8_t enc,
                                  uint64_t data_rel_base = 0);
};

class LocalMemory : public Memory {
 public:
  Error ReadBytes(uint64_t addr, uint64_t size, void* dst) override {
    memcpy(dst, reinterpret_cast<void*>(addr), size);  // NOLINT(performance-no-int-to-ptr)
    return Success();
  }
};

}  // namespace unwinder

#endif  // SRC_DEVELOPER_DEBUG_UNWINDER_MEMORY_H_
