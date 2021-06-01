// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/unwinder/memory.h"

#include <cstdint>

namespace unwinder {

Error Memory::ReadULEB128(uint64_t& addr, uint64_t& res) {
  res = 0;
  uint64_t shift = 0;
  uint8_t byte;
  do {
    if (auto err = Read(addr, byte); err.has_err()) {
      return err;
    }
    res |= (byte & 0x7F) << shift;
    shift += 7;
  } while (byte & 0x80);
  return Success();
}

Error Memory::ReadSLEB128(uint64_t& addr, int64_t& res) {
  // Use unsigned number for bit operations.
  uint64_t& res_u = reinterpret_cast<uint64_t&>(res);
  res_u = 0;
  uint64_t shift = 0;
  uint8_t byte;
  do {
    if (auto err = Read(addr, byte); err.has_err()) {
      return err;
    }
    res_u |= (byte & 0x7F) << shift;
    shift += 7;
  } while (byte & 0x80);
  if (byte & 0x40) {
    // sign extend
    res_u |= ~0ULL << shift;
  }
  return Success();
}

// https://refspecs.linuxfoundation.org/LSB_5.0.0/LSB-Core-generic/LSB-Core-generic/dwarfext.html#DWARFEHENCODING
Error Memory::ReadEncoded(uint64_t& addr, uint64_t& res, uint8_t enc, uint64_t data_rel_base) {
  if (enc == 0xFF) {  // DW_EH_PE_omit
    return Error("no value");
  }

  switch (enc & 0x70) {
    case 0x00:  // DW_EH_PE_absptr  Absolute value should only work for non-ptr types.
      res = 0;
      break;
    case 0x10:  // DW_EH_PE_pcrel  Value is relative to the current program counter (addr).
      res = addr;
      break;
    // case 0x20:  // DW_EH_PE_textrel  Value is relative to the beginning of the .text section.
    case 0x30:  // DW_EH_PE_datarel  Value is relative to the beginning of the .eh_frame_hdr
                // section. This is only valid when decoding .eh_frame_hdr section.
      if (!data_rel_base) {
        return Error("DW_EH_PE_datarel is invalid");
      }
      res = data_rel_base;
      break;
    // case 0x40:  // DW_EH_PE_funcrel  Value is relative to the beginning of the function.
    // case 0x50:  // DW_EH_PE_aligned  Value is aligned to an address unit sized boundary.
    default:
      return Error("unsupported encoding: %#x", enc);
  }

  switch (enc & 0x0F) {
    case 0x00: {  // DW_EH_PE_absptr  The Value is a literal pointer whose size is determined by the
                  // architecture.
      if (auto err = Read(addr, res); err.has_err()) {
        return err;
      }
      break;
    }
    case 0x01: {  // DW_EH_PE_uleb128  Unsigned value is encoded using the Little Endian Base 128
      uint64_t val;
      if (auto err = ReadULEB128(addr, val); err.has_err()) {
        return err;
      }
      res += val;
      break;
    }
    case 0x02: {  // DW_EH_PE_udata2  A 2 bytes unsigned value.
      uint16_t val;
      if (auto err = Read(addr, val); err.has_err()) {
        return err;
      }
      res += val;
      break;
    }
    case 0x03: {  // DW_EH_PE_udata4  A 4 bytes unsigned value.
      uint32_t val;
      if (auto err = Read(addr, val); err.has_err()) {
        return err;
      }
      res += val;
      break;
    }
    case 0x04: {  // DW_EH_PE_udata8  An 8 bytes unsigned value.
      uint64_t val;
      if (auto err = Read(addr, val); err.has_err()) {
        return err;
      }
      res += val;
      break;
    }
    case 0x09: {  // DW_EH_PE_sleb128  Signed value is encoded using the Little Endian Base 128
      int64_t val;
      if (auto err = ReadSLEB128(addr, val); err.has_err()) {
        return err;
      }
      res += val;
      break;
    }
    case 0x0A: {  // DW_EH_PE_sdata2  A 2 bytes signed value.
      int16_t val;
      if (auto err = Read(addr, val); err.has_err()) {
        return err;
      }
      res += val;
      break;
    }
    case 0x0B: {  // DW_EH_PE_sdata4  A 4 bytes signed value.
      int32_t val;
      if (auto err = Read(addr, val); err.has_err()) {
        return err;
      }
      res += val;
      break;
    }
    case 0x0C: {  // DW_EH_PE_sdata8  An 8 bytes signed value.
      int64_t val;
      if (auto err = Read(addr, val); err.has_err()) {
        return err;
      }
      res += val;
      break;
    }
    default:
      return Error("unsupported encoding: %#x", enc);
  }

  // It's an extension not documented in the spec.
  if (enc & 0x80) {  // DW_EH_PE_indirect  indirect read from the pointer.
    int64_t val;
    if (auto err = Read(res, val); err.has_err()) {
      return err;
    }
    res = val;
  }

  return Success();
}

}  // namespace unwinder
